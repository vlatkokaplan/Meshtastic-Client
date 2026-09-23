#include "MeshtasticProtocol.h"
#include "DeviceConfig.h"
#include <QDateTime>
#include <QRandomGenerator>
#include <QDebug>
#include <QStringDecoder>
#include <openssl/evp.h>

// Include generated protobuf headers
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/admin.pb.h"

// The firmware still reports several fields upstream has deprecated
// (serial_enabled, is_managed, gps_enabled, ...). They are read and written
// back unchanged so a config save does not reset them.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

// Constants
static constexpr double SNR_SCALE_FACTOR = 4.0;  // SNR is stored as int * 4 in protocol

// Helper function to wrap serialized protobuf in frame with sync bytes and length
static QByteArray wrapInFrame(const std::string &serialized)
{
    QByteArray frame;
    frame.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_1));
    frame.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));
    return frame;
}

static void mapSetFields(const google::protobuf::Message &msg, QVariantMap &fields);

// Helper functions to map config fields to QVariantMap (avoids duplication)
static void mapDeviceConfig(const meshtastic::Config_DeviceConfig &dev, QVariantMap &fields)
{
    fields["configType"] = "device";
    fields["raw"] = QByteArray::fromStdString(dev.SerializeAsString());
    fields["role"] = static_cast<int>(dev.role());
    fields["buttonGpio"] = dev.button_gpio();
    fields["buzzerGpio"] = dev.buzzer_gpio();
    fields["rebroadcastMode"] = static_cast<int>(dev.rebroadcast_mode());
    fields["nodeInfoBroadcastSecs"] = dev.node_info_broadcast_secs();
    fields["doubleTapAsButtonPress"] = dev.double_tap_as_button_press();
    fields["disableTripleClick"] = dev.disable_triple_click();
    fields["tzdef"] = QString::fromStdString(dev.tzdef());
    fields["ledHeartbeatDisabled"] = dev.led_heartbeat_disabled();
}

static void mapPositionConfig(const meshtastic::Config_PositionConfig &pos, QVariantMap &fields)
{
    fields["configType"] = "position";
    fields["raw"] = QByteArray::fromStdString(pos.SerializeAsString());
    fields["positionBroadcastSecs"] = pos.position_broadcast_secs();
    fields["smartPositionEnabled"] = pos.position_broadcast_smart_enabled();
    fields["fixedPosition"] = pos.fixed_position();
    fields["gpsEnabled"] = pos.gps_enabled();
    fields["gpsUpdateInterval"] = pos.gps_update_interval();
    fields["gpsAttemptTime"] = pos.gps_attempt_time();
    fields["positionFlags"] = pos.position_flags();
    fields["broadcastSmartMinDistance"] = pos.broadcast_smart_minimum_distance();
    fields["broadcastSmartMinIntervalSecs"] = pos.broadcast_smart_minimum_interval_secs();
    fields["gpsMode"] = static_cast<int>(pos.gps_mode());
}

// Security holds the node's keypair and admin keys. Only the two booleans the
// UI edits are mapped; everything else only ever travels inside `raw`.
static void mapSecurityConfig(const meshtastic::Config_SecurityConfig &sec, QVariantMap &fields)
{
    fields["configType"] = "security";
    fields["raw"] = QByteArray::fromStdString(sec.SerializeAsString());
    fields["serialEnabled"] = sec.serial_enabled();
    fields["debugLogApiEnabled"] = sec.debug_log_api_enabled();
}

static void mapLoraConfig(const meshtastic::Config_LoRaConfig &lora, QVariantMap &fields)
{
    fields["configType"] = "lora";
    fields["raw"] = QByteArray::fromStdString(lora.SerializeAsString());
    fields["usePreset"] = lora.use_preset();
    fields["modemPreset"] = static_cast<int>(lora.modem_preset());
    fields["bandwidth"] = lora.bandwidth();
    fields["spreadFactor"] = lora.spread_factor();
    fields["codingRate"] = lora.coding_rate();
    fields["frequencyOffset"] = lora.frequency_offset();
    fields["region"] = static_cast<int>(lora.region());
    fields["hopLimit"] = lora.hop_limit();
    fields["txEnabled"] = lora.tx_enabled();
    fields["txPower"] = lora.tx_power();
    fields["channelNum"] = lora.channel_num();
    fields["overrideDutyCycle"] = lora.override_duty_cycle();
}

MeshtasticProtocol::MeshtasticProtocol(QObject *parent)
    : QObject(parent), m_parseState(ParseState::WaitingForSync1), m_expectedLength(0)
{
}

MeshtasticProtocol::~MeshtasticProtocol() = default;

void MeshtasticProtocol::resetParser()
{
    m_parseState = ParseState::WaitingForSync1;
    m_frameBuffer.clear();
    m_expectedLength = 0;
}

void MeshtasticProtocol::processIncomingData(const QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i)
    {
        uint8_t byte = static_cast<uint8_t>(data[i]);

        switch (m_parseState)
        {
        case ParseState::WaitingForSync1:
            if (byte == SYNC_BYTE_1)
            {
                m_parseState = ParseState::WaitingForSync2;
            }
            break;

        case ParseState::WaitingForSync2:
            if (byte == SYNC_BYTE_2)
            {
                m_parseState = ParseState::WaitingForMSB;
            }
            else if (byte == SYNC_BYTE_1)
            {
                // Stay in sync2 state
            }
            else
            {
                m_parseState = ParseState::WaitingForSync1;
            }
            break;

        case ParseState::WaitingForMSB:
            m_expectedLength = byte << 8;
            m_parseState = ParseState::WaitingForLSB;
            break;

        case ParseState::WaitingForLSB:
            m_expectedLength |= byte;
            if (m_expectedLength > 0 && m_expectedLength <= MAX_PACKET_SIZE)
            {
                m_frameBuffer.clear();
                m_frameBuffer.reserve(m_expectedLength);
                m_parseState = ParseState::ReadingPayload;
            }
            else
            {
                qWarning() << "Invalid packet length:" << m_expectedLength;
                m_parseState = ParseState::WaitingForSync1;
            }
            break;

        case ParseState::ReadingPayload:
            m_frameBuffer.append(static_cast<char>(byte));
            if (m_frameBuffer.size() >= m_expectedLength)
            {
                processFrame(m_frameBuffer);
                m_frameBuffer.clear();
                m_parseState = ParseState::WaitingForSync1;
            }
            break;
        }
    }
}

void MeshtasticProtocol::processFrame(const QByteArray &frame)
{
    try
    {
        qDebug() << "[Protocol] Processing frame, size:" << frame.size();
        DecodedPacket decoded = decodeFromRadio(frame);
        qDebug() << "[Protocol] Packet decoded - type:" << static_cast<int>(decoded.type)
                 << "from:" << QString::number(decoded.from, 16)
                 << "to:" << QString::number(decoded.to, 16)
                 << "portNum:" << static_cast<int>(decoded.portNum);
        emit packetReceived(decoded);
    }
    catch (const std::exception &e)
    {
        qWarning() << "[Protocol] Parse error:" << e.what();
        emit parseError(QString("Failed to decode packet: %1").arg(e.what()));
    }
}

MeshtasticProtocol::DecodedPacket MeshtasticProtocol::decodeFromRadio(const QByteArray &data)
{
    DecodedPacket result;
    result.timestamp = QDateTime::currentMSecsSinceEpoch();
    result.rawData = data;
    result.type = PacketType::Unknown;
    result.from = 0;
    result.to = 0;
    result.portNum = PortNum::Unknown;

    meshtastic::FromRadio fromRadio;
    if (!fromRadio.ParseFromArray(data.constData(), data.size()))
    {
        throw std::runtime_error("Failed to parse FromRadio message");
    }

    result.fields["id"] = fromRadio.id();

    switch (fromRadio.payload_variant_case())
    {
    case meshtastic::FromRadio::kPacket:
    {
        result.type = PacketType::PacketReceived;
        const auto &packet = fromRadio.packet();
        result.from = packet.from();
        result.to = packet.to();
        result.channelIndex = packet.channel();
        result.fields = decodeMeshPacket(packet, result.portNum);
        // For encrypted packets the wire field is a channel hash, not an index;
        // decodeMeshPacket resolves it when the hash matches a known channel.
        if (result.fields.contains("resolvedChannel"))
            result.channelIndex = result.fields["resolvedChannel"].toInt();
        result.fields["hopLimit"] = packet.hop_limit();
        result.fields["hopStart"] = packet.hop_start();
        if (packet.via_mqtt())
            result.fields["viaMqtt"] = true;
        if (packet.rx_time() > 0)
        {
            result.fields["rxTime"] = QDateTime::fromSecsSinceEpoch(packet.rx_time()).toString(Qt::ISODate);
        }
        if (packet.rx_snr() != 0)
        {
            result.fields["rxSnr"] = packet.rx_snr();
        }
        if (packet.rx_rssi() != 0)
        {
            result.fields["rxRssi"] = packet.rx_rssi();
        }
        break;
    }

    case meshtastic::FromRadio::kMyInfo:
    {
        result.type = PacketType::MyInfo;
        const auto &myInfo = fromRadio.my_info();
        result.fields["myNodeNum"] = myInfo.my_node_num();
        result.fields["rebootCount"] = myInfo.reboot_count();
        result.fields["minAppVersion"] = myInfo.min_app_version();
        break;
    }

    case meshtastic::FromRadio::kNodeInfo:
    {
        result.type = PacketType::NodeInfo;
        const auto &nodeInfo = fromRadio.node_info();
        result.fields["nodeNum"] = nodeInfo.num();
        result.fields["lastHeard"] = nodeInfo.last_heard();
        result.fields["snr"] = nodeInfo.snr();
        if (nodeInfo.has_user())
        {
            const auto &user = nodeInfo.user();
            result.fields["userId"] = QString::fromUtf8(user.id().c_str(), static_cast<int>(user.id().size()));
            result.fields["longName"] = QString::fromUtf8(user.long_name().c_str(), static_cast<int>(user.long_name().size()));
            result.fields["shortName"] = QString::fromUtf8(user.short_name().c_str(), static_cast<int>(user.short_name().size()));
            result.fields["hwModel"] = static_cast<int>(user.hw_model());
            result.fields["role"] = static_cast<int>(user.role());
        }
        if (nodeInfo.has_position())
        {
            const auto &pos = nodeInfo.position();
            if (pos.latitude_i() != 0 || pos.longitude_i() != 0)
            {
                result.fields["latitude"] = pos.latitude_i() / 1e7;
                result.fields["longitude"] = pos.longitude_i() / 1e7;
                if (pos.altitude() != 0)
                {
                    result.fields["altitude"] = pos.altitude();
                }
            }
        }
        result.fields["isFavorite"] = nodeInfo.is_favorite();
        if (nodeInfo.has_hops_away())
            result.fields["hopsAway"] = nodeInfo.hops_away();
        result.fields["viaMqtt"] = nodeInfo.via_mqtt();
        if (nodeInfo.has_device_metrics())
        {
            QVariantMap metrics;
            mapSetFields(nodeInfo.device_metrics(), metrics);
            result.fields["deviceMetrics"] = metrics;
        }
        break;
    }

    case meshtastic::FromRadio::kChannel:
    {
        result.type = PacketType::Channel;
        const auto &channel = fromRadio.channel();
        result.fields["index"] = channel.index();
        result.fields["role"] = static_cast<int>(channel.role());
        if (channel.has_settings())
        {
            const auto &settings = channel.settings();
            result.fields["channelName"] = QString::fromStdString(settings.name());
            result.fields["name"] = QString::fromStdString(settings.name());
            const std::string &psk = settings.psk();
            result.fields["psk"] = QByteArray(psk.data(), psk.size());
            result.fields["uplinkEnabled"] = settings.uplink_enabled();
            result.fields["downlinkEnabled"] = settings.downlink_enabled();
            result.fields["channelId"] = settings.id();
            result.fields["positionPrecision"] = settings.module_settings().position_precision();
            result.fields["isMuted"] = settings.module_settings().is_muted();
        }
        else
        {
            // A channel with no settings carries no name or key. Report them as
            // empty rather than omitting them, otherwise DeviceConfig keeps
            // whatever was there before and a channel disabled on the device
            // still shows its old name and PSK.
            result.fields["channelName"] = QString();
            result.fields["name"] = QString();
            result.fields["psk"] = QByteArray();
            result.fields["uplinkEnabled"] = false;
            result.fields["downlinkEnabled"] = false;
            result.fields["channelId"] = 0u;
            result.fields["positionPrecision"] = 0u;
            result.fields["isMuted"] = false;
        }
        break;
    }

    case meshtastic::FromRadio::kConfig:
    {
        result.type = PacketType::Config;
        const auto &config = fromRadio.config();

        switch (config.payload_variant_case())
        {
        case meshtastic::Config::kDevice:
            mapDeviceConfig(config.device(), result.fields);
            break;
        case meshtastic::Config::kPosition:
            mapPositionConfig(config.position(), result.fields);
            break;
        case meshtastic::Config::kLora:
            mapLoraConfig(config.lora(), result.fields);
            break;
        case meshtastic::Config::kPower:
        {
            result.fields["configType"] = "power";
            const auto &pwr = config.power();
            result.fields["isPowerSaving"] = pwr.is_power_saving();
            result.fields["onBatteryShutdownAfterSecs"] = pwr.on_battery_shutdown_after_secs();
            result.fields["adcMultiplierOverride"] = pwr.adc_multiplier_override();
            result.fields["waitBluetoothSecs"] = pwr.wait_bluetooth_secs();
            result.fields["sdsSecs"] = pwr.sds_secs();
            result.fields["lsSecs"] = pwr.ls_secs();
            result.fields["minWakeSecs"] = pwr.min_wake_secs();
            break;
        }
        case meshtastic::Config::kNetwork:
        {
            result.fields["configType"] = "network";
            const auto &net = config.network();
            result.fields["wifiEnabled"] = net.wifi_enabled();
            result.fields["wifiSsid"] = QString::fromStdString(net.wifi_ssid());
            result.fields["ntpServer"] = QString::fromStdString(net.ntp_server());
            result.fields["ethEnabled"] = net.eth_enabled();
            break;
        }
        case meshtastic::Config::kDisplay:
        {
            result.fields["configType"] = "display";
            const auto &disp = config.display();
            result.fields["screenOnSecs"] = disp.screen_on_secs();
            result.fields["gpsFormat"] = static_cast<int>(disp.gps_format());
            result.fields["autoScreenCarouselSecs"] = disp.auto_screen_carousel_secs();
            result.fields["compassNorthTop"] = disp.compass_north_top();
            result.fields["flipScreen"] = disp.flip_screen();
            result.fields["units"] = static_cast<int>(disp.units());
            break;
        }
        case meshtastic::Config::kSecurity:
            mapSecurityConfig(config.security(), result.fields);
            break;
        case meshtastic::Config::kBluetooth:
        {
            result.fields["configType"] = "bluetooth";
            const auto &bt = config.bluetooth();
            result.fields["enabled"] = bt.enabled();
            result.fields["mode"] = static_cast<int>(bt.mode());
            result.fields["fixedPin"] = bt.fixed_pin();
            break;
        }
        default:
            result.fields["configType"] = "unknown";
            break;
        }
        break;
    }

    case meshtastic::FromRadio::kModuleConfig:
    {
        result.type = PacketType::ModuleConfig;
        result.fields["configType"] = "moduleConfig";
        break;
    }

    case meshtastic::FromRadio::kQueueStatus:
    {
        result.type = PacketType::QueueStatus;
        const auto &status = fromRadio.queuestatus();
        result.fields["free"] = status.free();
        result.fields["maxLen"] = status.maxlen();
        result.fields["meshPacketId"] = status.mesh_packet_id();
        break;
    }

    case meshtastic::FromRadio::kMetadata:
    {
        result.type = PacketType::Metadata;
        const auto &meta = fromRadio.metadata();
        result.fields["firmwareVersion"] = QString::fromStdString(meta.firmware_version());
        result.fields["deviceStateVersion"] = meta.device_state_version();
        result.fields["hwModel"] = static_cast<int>(meta.hw_model());
        break;
    }

    case meshtastic::FromRadio::kConfigCompleteId:
    {
        result.type = PacketType::ConfigCompleteId;
        result.fields["configId"] = fromRadio.config_complete_id();
        break;
    }

    case meshtastic::FromRadio::kLogRecord:
    {
        result.type = PacketType::LogRecord;
        const auto &log = fromRadio.log_record();
        result.fields["message"] = QString::fromStdString(log.message());
        result.fields["level"] = log.level();
        result.fields["source"] = QString::fromStdString(log.source());
        break;
    }

    case meshtastic::FromRadio::kClientNotification:
    {
        // How the firmware tells the app about problems it can't put in a
        // routing error: rejected settings, duty-cycle warnings, key issues.
        result.type = PacketType::ClientNotification;
        const auto &note = fromRadio.clientnotification();
        result.fields["message"] = QString::fromStdString(note.message());
        result.fields["level"] = static_cast<int>(note.level());
        if (note.has_reply_id())
            result.fields["replyId"] = note.reply_id();
        break;
    }

    case meshtastic::FromRadio::kRebooted:
    {
        result.type = PacketType::Rebooted;
        result.fields["rebooted"] = fromRadio.rebooted();
        break;
    }

    default:
        result.type = PacketType::Unknown;
        break;
    }

    result.typeName = packetTypeToString(result.type);
    return result;
}

QVariantMap MeshtasticProtocol::decodeMeshPacket(const meshtastic::MeshPacket &packet, PortNum &portNum)
{
    QVariantMap fields;

    // Add packet ID to fields so we can track routing responses
    fields["packetId"] = packet.id();

    if (packet.has_decoded())
    {
        const auto &decoded = packet.decoded();
        portNum = static_cast<PortNum>(decoded.portnum());
        fields["portnum"] = portNumToString(portNum);

        const std::string &payload = decoded.payload();
        QByteArray payloadData(payload.data(), payload.size());

        switch (portNum)
        {
        case PortNum::TextMessage:
            fields["text"] = QString::fromUtf8(payloadData);
            // A tapback is a text message carrying the id of what it responds
            // to, with emoji set. Without these it arrives as an ordinary
            // message and the thread loses the link.
            if (decoded.reply_id() != 0)
                fields["replyId"] = static_cast<uint32_t>(decoded.reply_id());
            if (decoded.emoji() != 0)
                fields["isReaction"] = true;
            break;

        case PortNum::Position:
            fields.insert(decodePosition(payloadData));
            break;

        case PortNum::NodeInfo:
            fields.insert(decodeUser(payloadData));
            break;

        case PortNum::Telemetry:
            fields.insert(decodeTelemetry(payloadData));
            break;

        case PortNum::Routing:
        {
            meshtastic::Routing routing;
            if (!routing.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                qWarning() << "[Protocol] Failed to parse Routing message";
                break;
            }
            if (routing.has_error_reason())
            {
                fields["errorReason"] = static_cast<int>(routing.error_reason());
            }

            // For routing packets, the request_id in the Data message tells us which packet this is responding to
            if (decoded.request_id() != 0)
            {
                fields["packetId"] = decoded.request_id();
            }
            break;
        }

        case PortNum::Traceroute:
        {
            meshtastic::RouteDiscovery routeData;
            if (!routeData.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                qWarning() << "[Protocol] Failed to parse RouteDiscovery message";
                break;
            }

            // Route towards destination
            QVariantList routeList;
            for (const auto &node : routeData.route())
            {
                routeList.append(nodeIdToString(node));
            }
            fields["route"] = routeList;

            // SNR values towards destination (from RouteDiscovery only)
            // These are int32 values scaled by SNR_SCALE_FACTOR in the protobuf
            // A raw value of -128 (resulting in -32.0) means "unknown/no data"
            QVariantList snrTowardsList;
            for (const auto &snr : routeData.snr_towards())
            {
                if (snr == -128)
                    snrTowardsList.append(QVariant()); // null = unknown
                else
                    snrTowardsList.append(snr / SNR_SCALE_FACTOR);
            }
            fields["snrTowards"] = snrTowardsList;

            // Route back
            QVariantList routeBackList;
            for (const auto &node : routeData.route_back())
            {
                routeBackList.append(nodeIdToString(node));
            }
            fields["routeBack"] = routeBackList;

            // SNR values back. The firmware appends the final hop's SNR (the
            // one we received the response on) before handing the packet to
            // us - TraceRouteModule::appendMyIDandSNR with SNRonly - so a
            // current response has one more entry than route_back.
            // A raw value of -128 (resulting in -32.0) means "unknown/no data"
            QVariantList snrBackList;
            for (const auto &snr : routeData.snr_back())
            {
                if (snr == -128)
                    snrBackList.append(QVariant()); // null = unknown
                else
                    snrBackList.append(snr / SNR_SCALE_FACTOR);
            }
            // Older firmware did not add that last entry; fill it from rx_snr
            // (a float in dB, not scaled) only when it is actually missing,
            // otherwise the last hop is counted twice.
            if (decoded.request_id() != 0
                && routeData.snr_back_size() == routeData.route_back_size()
                && packet.rx_snr() != 0)
            {
                snrBackList.append(static_cast<double>(packet.rx_snr()));
            }
            fields["snrBack"] = snrBackList;
            break;
        }

        case PortNum::Neighborinfo:
        {
            meshtastic::NeighborInfo neighborInfo;
            if (!neighborInfo.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                qWarning() << "[Protocol] Failed to parse NeighborInfo message";
                break;
            }

            fields["nodeId"] = neighborInfo.node_id();
            fields["lastSentById"] = neighborInfo.last_sent_by_id();
            fields["nodeBroadcastIntervalSecs"] = neighborInfo.node_broadcast_interval_secs();

            QVariantList neighborsList;
            for (const auto &neighbor : neighborInfo.neighbors())
            {
                QVariantMap nm;
                nm["nodeId"] = neighbor.node_id();
                nm["snr"] = neighbor.snr();
                nm["lastRxTime"] = neighbor.last_rx_time();
                nm["nodeBroadcastIntervalSecs"] = neighbor.node_broadcast_interval_secs();
                neighborsList.append(nm);
            }
            fields["neighbors"] = neighborsList;

            qDebug() << "[Protocol] NeighborInfo from node" << neighborInfo.node_id()
                     << "with" << neighborInfo.neighbors_size() << "neighbors";
            break;
        }

        case PortNum::Admin:
        {
            meshtastic::AdminMessage admin;
            if (!admin.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                qWarning() << "[Protocol] Failed to parse AdminMessage";
                break;
            }

            qDebug() << "[Protocol] Admin message received, payload_variant_case:"
                     << admin.payload_variant_case();

            fields["adminType"] = "response";

            // Check for session passkey in the response
            if (admin.session_passkey().size() > 0) {
                QByteArray sessionKey(admin.session_passkey().data(), admin.session_passkey().size());
                fields["sessionPasskey"] = sessionKey;
                qDebug() << "[Protocol] Received session passkey, size:" << sessionKey.size();
            }

            // Handle get_channel_response
            if (admin.has_get_channel_response())
            {
                const auto &channel = admin.get_channel_response();
                qDebug() << "[Protocol] Got channel response - index:" << channel.index()
                         << "role:" << channel.role()
                         << "name:" << QString::fromStdString(channel.settings().name());
            }

            // Handle get_config_response
            if (admin.has_get_config_response())
            {
                const auto &config = admin.get_config_response();
                switch (config.payload_variant_case())
                {
                case meshtastic::Config::kDevice:
                    mapDeviceConfig(config.device(), fields);
                    break;
                case meshtastic::Config::kPosition:
                    mapPositionConfig(config.position(), fields);
                    break;
                case meshtastic::Config::kLora:
                    mapLoraConfig(config.lora(), fields);
                    break;
                case meshtastic::Config::kSecurity:
                    mapSecurityConfig(config.security(), fields);
                    break;
                default:
                    fields["configType"] = "unknown";
                    break;
                }
            }
            break;
        }

        default:
            fields["payloadHex"] = payloadData.toHex();
            break;
        }

        if (decoded.request_id() != 0)
        {
            fields["requestId"] = decoded.request_id();
        }
    }
    else if (packet.has_encrypted() && packet.pki_encrypted())
    {
        // A direct message encrypted to the recipient's public key. No channel
        // key can open it, so don't spend the brute force on it.
        portNum = PortNum::Unknown;
        fields["encrypted"] = true;
        fields["pkiEncrypted"] = true;
        fields["encryptedData"] = QByteArray(packet.encrypted().data(),
                                             static_cast<int>(packet.encrypted().size())).toHex();
    }
    else if (packet.has_encrypted())
    {
        QByteArray encryptedData(packet.encrypted().data(), packet.encrypted().size());

        // Try to decrypt (will brute force simple keys if needed)
        int foundKeyByte = -1;
        int matchedChannel = -1;
        QByteArray decrypted = decryptPayload(encryptedData, packet.id(), packet.from(), packet.channel(),
                                              &foundKeyByte, &matchedChannel);

        // packet.channel() is a hash for encrypted packets; only a hash match
        // against a configured channel gives us the real index.
        if (matchedChannel >= 0)
            fields["resolvedChannel"] = matchedChannel;

        if (!decrypted.isEmpty()) {
            // Try to parse as Data message
            meshtastic::Data data;
            if (data.ParseFromArray(decrypted.constData(), decrypted.size())) {
                portNum = static_cast<PortNum>(data.portnum());
                fields["portnum"] = portNumToString(portNum);
                fields["decrypted"] = true;

                // Report the key if found via brute force
                if (foundKeyByte >= 0) {
                    fields["foundKey"] = QByteArray(1, static_cast<char>(foundKeyByte)).toBase64();
                    fields["foundKeyByte"] = foundKeyByte;
                }

                const std::string &payload = data.payload();
                QByteArray payloadData(payload.data(), payload.size());

                // Decode based on port type (same as decoded packets)
                switch (portNum)
                {
                case PortNum::TextMessage:
                    fields["text"] = QString::fromUtf8(payloadData);
                    if (data.reply_id() != 0)
                        fields["replyId"] = static_cast<uint32_t>(data.reply_id());
                    if (data.emoji() != 0)
                        fields["isReaction"] = true;
                    break;
                case PortNum::Position:
                    fields.insert(decodePosition(payloadData));
                    break;
                case PortNum::NodeInfo:
                    fields.insert(decodeUser(payloadData));
                    break;
                case PortNum::Telemetry:
                    fields.insert(decodeTelemetry(payloadData));
                    break;
                case PortNum::Neighborinfo:
                {
                    meshtastic::NeighborInfo neighborInfo;
                    if (neighborInfo.ParseFromArray(payloadData.constData(), payloadData.size()))
                    {
                        fields["nodeId"] = neighborInfo.node_id();
                        fields["lastSentById"] = neighborInfo.last_sent_by_id();
                        fields["nodeBroadcastIntervalSecs"] = neighborInfo.node_broadcast_interval_secs();

                        QVariantList neighborsList;
                        for (const auto &neighbor : neighborInfo.neighbors())
                        {
                            QVariantMap nm;
                            nm["nodeId"] = neighbor.node_id();
                            nm["snr"] = neighbor.snr();
                            nm["lastRxTime"] = neighbor.last_rx_time();
                            nm["nodeBroadcastIntervalSecs"] = neighbor.node_broadcast_interval_secs();
                            neighborsList.append(nm);
                        }
                        fields["neighbors"] = neighborsList;
                    }
                    break;
                }
                default:
                    fields["payloadHex"] = payloadData.toHex();
                    break;
                }

                if (data.request_id() != 0) {
                    fields["requestId"] = data.request_id();
                }
            } else {
                // Decryption succeeded but parse failed - wrong key?
                portNum = PortNum::Unknown;
                fields["encrypted"] = true;
                fields["decryptFailed"] = true;
                fields["encryptedData"] = encryptedData.toHex();
            }
        } else {
            // No key available or decryption failed
            portNum = PortNum::Unknown;
            fields["encrypted"] = true;
            fields["encryptedData"] = encryptedData.toHex();
        }
    }

    return fields;
}

QVariantMap MeshtasticProtocol::decodePosition(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::Position pos;

    if (pos.ParseFromArray(data.constData(), data.size()))
    {
        if (pos.latitude_i() != 0 || pos.longitude_i() != 0)
        {
            fields["latitude"] = pos.latitude_i() / 1e7;
            fields["longitude"] = pos.longitude_i() / 1e7;
        }
        if (pos.altitude() != 0)
        {
            fields["altitude"] = pos.altitude();
        }
        if (pos.time() != 0)
        {
            fields["positionTime"] = QDateTime::fromSecsSinceEpoch(pos.time()).toString(Qt::ISODate);
        }
        if (pos.ground_speed() != 0)
        {
            fields["groundSpeed"] = pos.ground_speed();
        }
        if (pos.ground_track() != 0)
        {
            fields["groundTrack"] = pos.ground_track();
        }
        if (pos.sats_in_view() != 0)
        {
            fields["satsInView"] = pos.sats_in_view();
        }
        if (pos.precision_bits() != 0)
        {
            fields["precisionBits"] = pos.precision_bits();
        }
    }

    return fields;
}

QVariantMap MeshtasticProtocol::decodeUser(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::User user;

    if (user.ParseFromArray(data.constData(), data.size()))
    {
        fields["userId"] = QString::fromUtf8(user.id().c_str(), static_cast<int>(user.id().size()));
        fields["longName"] = QString::fromUtf8(user.long_name().c_str(), static_cast<int>(user.long_name().size()));
        fields["shortName"] = QString::fromUtf8(user.short_name().c_str(), static_cast<int>(user.short_name().size()));
        fields["hwModel"] = static_cast<int>(user.hw_model());
        fields["role"] = static_cast<int>(user.role());
        if (user.is_licensed())
        {
            fields["isLicensed"] = true;
        }
    }

    return fields;
}

// Every scalar field the sender set, keyed by the field's camelCase name
// (battery_level -> batteryLevel). Presence decides, not value: telemetry
// fields are proto3 `optional`, so a real 0 °C or 0 % is kept, and an unset
// field is left out rather than reported as 0.
static void mapSetFields(const google::protobuf::Message &msg, QVariantMap &fields)
{
    using google::protobuf::FieldDescriptor;
    const auto *refl = msg.GetReflection();
    std::vector<const FieldDescriptor *> set;
    refl->ListFields(msg, &set);

    for (const FieldDescriptor *f : set)
    {
        const QString key = QString::fromStdString(std::string(f->camelcase_name()));
        if (f->is_repeated())
        {
            if (f->cpp_type() != FieldDescriptor::CPPTYPE_FLOAT)
                continue;
            QVariantList list;
            for (int i = 0; i < refl->FieldSize(msg, f); ++i)
                list.append(refl->GetRepeatedFloat(msg, f, i));
            fields[key] = list;
            continue;
        }
        switch (f->cpp_type())
        {
        case FieldDescriptor::CPPTYPE_FLOAT:  fields[key] = refl->GetFloat(msg, f); break;
        case FieldDescriptor::CPPTYPE_DOUBLE: fields[key] = refl->GetDouble(msg, f); break;
        case FieldDescriptor::CPPTYPE_INT32:  fields[key] = refl->GetInt32(msg, f); break;
        case FieldDescriptor::CPPTYPE_UINT32: fields[key] = refl->GetUInt32(msg, f); break;
        case FieldDescriptor::CPPTYPE_INT64:  fields[key] = static_cast<qint64>(refl->GetInt64(msg, f)); break;
        case FieldDescriptor::CPPTYPE_UINT64: fields[key] = static_cast<quint64>(refl->GetUInt64(msg, f)); break;
        case FieldDescriptor::CPPTYPE_BOOL:   fields[key] = refl->GetBool(msg, f); break;
        case FieldDescriptor::CPPTYPE_ENUM:   fields[key] = refl->GetEnumValue(msg, f); break;
        case FieldDescriptor::CPPTYPE_STRING: fields[key] = QString::fromStdString(refl->GetString(msg, f)); break;
        default: break;  // nested messages: not used by telemetry variants we display
        }
    }
}

QVariantMap MeshtasticProtocol::decodeTelemetry(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::Telemetry telemetry;

    if (!telemetry.ParseFromArray(data.constData(), data.size()))
        return fields;

    fields["telemetryTime"] = telemetry.time();

    // telemetryType tells consumers which variant the keys belong to: several
    // share names (voltage in device and environment, temperature in
    // environment and health), and they mean different things.
    switch (telemetry.variant_case())
    {
    case meshtastic::Telemetry::kDeviceMetrics:
        fields["telemetryType"] = "device";
        mapSetFields(telemetry.device_metrics(), fields);
        break;
    case meshtastic::Telemetry::kEnvironmentMetrics:
        fields["telemetryType"] = "environment";
        mapSetFields(telemetry.environment_metrics(), fields);
        break;
    case meshtastic::Telemetry::kAirQualityMetrics:
        fields["telemetryType"] = "airQuality";
        mapSetFields(telemetry.air_quality_metrics(), fields);
        break;
    case meshtastic::Telemetry::kPowerMetrics:
        fields["telemetryType"] = "power";
        mapSetFields(telemetry.power_metrics(), fields);
        break;
    case meshtastic::Telemetry::kLocalStats:
        fields["telemetryType"] = "localStats";
        mapSetFields(telemetry.local_stats(), fields);
        break;
    case meshtastic::Telemetry::kHealthMetrics:
        fields["telemetryType"] = "health";
        mapSetFields(telemetry.health_metrics(), fields);
        break;
    case meshtastic::Telemetry::kHostMetrics:
        fields["telemetryType"] = "host";
        mapSetFields(telemetry.host_metrics(), fields);
        break;
    default:
        fields["telemetryType"] = "unknown";
        break;
    }

    return fields;
}

QByteArray MeshtasticProtocol::createWantConfigPacket(uint32_t configId)
{
    meshtastic::ToRadio toRadio;
    toRadio.set_want_config_id(configId);

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

// Same scheme as the Python client and firmware generatePacketId: a random
// upper part with a 10-bit counter below it. Random so ids don't repeat across
// restarts or collide with other clients; the counter so two packets built in
// the same millisecond still differ (the mesh drops a repeated from+id pair).
uint32_t MeshtasticProtocol::nextPacketId()
{
    m_packetCounter = (m_packetCounter + 1) & 0x3FF;
    uint32_t id = 0;
    while (id == 0)
        id = (QRandomGenerator::global()->bounded(0x400000u) << 10) | m_packetCounter;
    return id;
}

QString MeshtasticProtocol::nodeIdToString(uint32_t nodeId)
{
    return QString("!%1").arg(nodeId, 8, 16, QChar('0'));
}

uint32_t MeshtasticProtocol::nodeIdFromString(const QString &nodeId)
{
    QString hex = nodeId;
    if (hex.startsWith('!'))
    {
        hex = hex.mid(1);
    }
    bool ok;
    uint32_t result = hex.toUInt(&ok, 16);
    return ok ? result : 0;
}

QString MeshtasticProtocol::portNumToString(PortNum portNum)
{
    switch (portNum)
    {
    case PortNum::TextMessage:
        return "TEXT_MESSAGE";
    case PortNum::RemoteHardware:
        return "REMOTE_HARDWARE";
    case PortNum::Position:
        return "POSITION";
    case PortNum::NodeInfo:
        return "NODEINFO";
    case PortNum::Routing:
        return "ROUTING";
    case PortNum::Admin:
        return "ADMIN";
    case PortNum::TextMessageCompressed:
        return "TEXT_MESSAGE_COMPRESSED";
    case PortNum::Waypoint:
        return "WAYPOINT";
    case PortNum::Audio:
        return "AUDIO";
    case PortNum::Detection:
        return "DETECTION";
    case PortNum::Reply:
        return "REPLY";
    case PortNum::IpTunnel:
        return "IP_TUNNEL";
    case PortNum::Paxcounter:
        return "PAXCOUNTER";
    case PortNum::Serial:
        return "SERIAL";
    case PortNum::StoreForward:
        return "STORE_FORWARD";
    case PortNum::RangeTest:
        return "RANGE_TEST";
    case PortNum::Telemetry:
        return "TELEMETRY";
    case PortNum::ZPS:
        return "ZPS";
    case PortNum::Simulator:
        return "SIMULATOR";
    case PortNum::Traceroute:
        return "TRACEROUTE";
    case PortNum::Neighborinfo:
        return "NEIGHBORINFO";
    case PortNum::Atak:
        return "ATAK";
    case PortNum::Map:
        return "MAP";
    case PortNum::PowerStress:
        return "POWERSTRESS";
    case PortNum::Private:
        return "PRIVATE";
    default:
        return QString("UNKNOWN(%1)").arg(static_cast<int>(portNum));
    }
}

QString MeshtasticProtocol::packetTypeToString(PacketType type)
{
    switch (type)
    {
    case PacketType::PacketReceived:
        return "Packet";
    case PacketType::MyInfo:
        return "MyInfo";
    case PacketType::NodeInfo:
        return "NodeInfo";
    case PacketType::Channel:
        return "Channel";
    case PacketType::Config:
        return "Config";
    case PacketType::ModuleConfig:
        return "ModuleConfig";
    case PacketType::QueueStatus:
        return "QueueStatus";
    case PacketType::XModemPacket:
        return "XModem";
    case PacketType::Metadata:
        return "Metadata";
    case PacketType::MqttClientProxyMessage:
        return "MqttProxy";
    case PacketType::FileInfo:
        return "FileInfo";
    case PacketType::ClientNotification:
        return "Notification";
    case PacketType::ConfigCompleteId:
        return "ConfigCompleteId";
    case PacketType::LogRecord:
        return "LogRecord";
    case PacketType::Rebooted:
        return "Rebooted";
    default:
        return "Unknown";
    }
}

QByteArray MeshtasticProtocol::createTraceroutePacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(nextPacketId());

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::TRACEROUTE_APP);

    // Empty RouteDiscovery as payload
    meshtastic::RouteDiscovery route;
    decoded->set_payload(route.SerializeAsString());
    decoded->set_want_response(true);

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

QByteArray MeshtasticProtocol::createPositionRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(nextPacketId());

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::POSITION_APP);
    decoded->set_want_response(true);

    // Empty position as request
    meshtastic::Position pos;
    decoded->set_payload(pos.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

QByteArray MeshtasticProtocol::createTelemetryRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(nextPacketId());

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::TELEMETRY_APP);
    decoded->set_want_response(true);

    // Empty telemetry as request
    meshtastic::Telemetry telem;
    decoded->set_payload(telem.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

QByteArray MeshtasticProtocol::createNodeInfoRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(nextPacketId());

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::NODEINFO_APP);
    decoded->set_want_response(true);

    // Empty user as request
    meshtastic::User user;
    decoded->set_payload(user.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

QByteArray MeshtasticProtocol::createTextMessagePacket(const QString &text, uint32_t destNode, uint32_t myNode, int channel, uint32_t replyId, uint32_t *outPacketId, bool isReaction)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_channel(channel);
    packet->set_want_ack(true);
    const uint32_t packetId = nextPacketId();
    packet->set_id(packetId);

    if (outPacketId)
    {
        *outPacketId = packetId;
    }

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::TEXT_MESSAGE_APP);

    // Text message is just the UTF-8 bytes
    std::string textBytes = text.toUtf8().toStdString();
    decoded->set_payload(textBytes);

    // Set reply_id for reactions/replies
    if (replyId != 0)
    {
        decoded->set_reply_id(replyId);
    }
    if (isReaction)
    {
        decoded->set_emoji(1);
    }

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

// Helper to create admin message frame for LOCAL device (connected via serial)
static QByteArray createAdminFrame(uint32_t packetId, uint32_t destNode, uint32_t myNode, const std::string &adminPayload, const QByteArray &sessionKey = QByteArray())
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    // For LOCAL admin: don't set 'to' or 'from' (both default to 0)
    // This tells the device this is a local admin command, not to be routed
    packet->set_id(packetId);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::ADMIN_APP);

    // If we have a session key, we need to include it in the AdminMessage
    if (!sessionKey.isEmpty()) {
        // Parse the admin payload, add session key, and re-serialize
        meshtastic::AdminMessage admin;
        if (admin.ParseFromArray(adminPayload.data(), adminPayload.size())) {
            admin.set_session_passkey(sessionKey.constData(), sessionKey.size());
            std::string newPayload = admin.SerializeAsString();
            decoded->set_payload(newPayload);
            qDebug() << "createAdminFrame (local) - payload size:" << newPayload.size() << "with session key";
        } else {
            decoded->set_payload(adminPayload);
            qDebug() << "createAdminFrame (local) - payload size:" << adminPayload.size() << "(failed to add session key)";
        }
    } else {
        decoded->set_payload(adminPayload);
        qDebug() << "createAdminFrame (local) - payload size:" << adminPayload.size() << "(no session key)";
    }

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    qDebug() << "Full ToRadio packet hex:" << QByteArray(serialized.data(), serialized.size()).toHex();

    return wrapInFrame(serialized);
}

QByteArray MeshtasticProtocol::createGetConfigRequestPacket(uint32_t destNode, uint32_t myNode, int configType)
{
    meshtastic::AdminMessage admin;
    // configType is an AdminMessage::ConfigType: 0=Device, 1=Position, 2=Power,
    // 3=Network, 4=Display, 5=LoRa, 6=Bluetooth, 7=Security, 8=SessionKey
    admin.set_get_config_request(static_cast<meshtastic::AdminMessage_ConfigType>(configType));

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createSessionKeyRequestPacket()
{
    meshtastic::AdminMessage admin;
    admin.set_get_config_request(meshtastic::AdminMessage::SESSIONKEY_CONFIG);

    qDebug() << "Requesting session key from device";

    return createAdminFrame(nextPacketId(), 0, 0, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createLoRaConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *lora = setConfig->mutable_lora();
    // set_config replaces the whole section: start from what the device sent
    // so fields we don't edit (ignore_mqtt, config_ok_to_mqtt, rx boosted gain,
    // override_frequency, ignore_incoming, custom BW/SF/CR, ...) are kept.
    const QByteArray base = config.value("raw").toByteArray();
    if (!base.isEmpty() && !lora->ParseFromArray(base.constData(), base.size()))
        qWarning() << "[Protocol] Stored LoRa config did not parse; unedited fields will reset";

    lora->set_use_preset(config.value("usePreset", true).toBool());
    lora->set_modem_preset(static_cast<meshtastic::Config_LoRaConfig_ModemPreset>(config.value("modemPreset", 0).toInt()));
    lora->set_region(static_cast<meshtastic::Config_LoRaConfig_RegionCode>(config.value("region", 0).toInt()));
    lora->set_hop_limit(config.value("hopLimit", 3).toUInt());
    lora->set_tx_enabled(config.value("txEnabled", true).toBool());
    lora->set_tx_power(config.value("txPower", 0).toInt());
    lora->set_channel_num(config.value("channelNum", 0).toUInt());
    lora->set_override_duty_cycle(config.value("overrideDutyCycle", false).toBool());
    lora->set_frequency_offset(config.value("frequencyOffset", 0.0).toFloat());
    // Custom modem settings; the firmware ignores them while use_preset is on
    if (config.contains("bandwidth"))
        lora->set_bandwidth(config.value("bandwidth").toUInt());
    if (config.contains("spreadFactor"))
        lora->set_spread_factor(config.value("spreadFactor").toUInt());
    if (config.contains("codingRate"))
        lora->set_coding_rate(config.value("codingRate").toUInt());

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createDeviceConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *device = setConfig->mutable_device();
    // Start from the device's config; see createLoRaConfigPacket
    const QByteArray base = config.value("raw").toByteArray();
    if (!base.isEmpty() && !device->ParseFromArray(base.constData(), base.size()))
        qWarning() << "[Protocol] Stored device config did not parse; unedited fields will reset";

    device->set_role(static_cast<meshtastic::Config_DeviceConfig_Role>(config.value("role", 0).toInt()));
    device->set_button_gpio(config.value("buttonGpio", 0).toUInt());
    device->set_buzzer_gpio(config.value("buzzerGpio", 0).toUInt());
    device->set_rebroadcast_mode(static_cast<meshtastic::Config_DeviceConfig_RebroadcastMode>(config.value("rebroadcastMode", 0).toInt()));
    device->set_node_info_broadcast_secs(config.value("nodeInfoBroadcastSecs", 900).toUInt());
    device->set_double_tap_as_button_press(config.value("doubleTapAsButtonPress", false).toBool());
    device->set_disable_triple_click(config.value("disableTripleClick", false).toBool());
    device->set_tzdef(config.value("tzdef").toString().toStdString());
    device->set_led_heartbeat_disabled(config.value("ledHeartbeatDisabled", false).toBool());

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createPositionConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *position = setConfig->mutable_position();
    // Start from the device's config; see createLoRaConfigPacket. Keeps the
    // GPS pins and anything else this client doesn't edit.
    const QByteArray base = config.value("raw").toByteArray();
    if (!base.isEmpty() && !position->ParseFromArray(base.constData(), base.size()))
        qWarning() << "[Protocol] Stored position config did not parse; unedited fields will reset";

    position->set_position_broadcast_secs(config.value("positionBroadcastSecs", 900).toUInt());
    position->set_position_broadcast_smart_enabled(config.value("smartPositionEnabled", true).toBool());
    position->set_fixed_position(config.value("fixedPosition", false).toBool());
    position->set_gps_enabled(config.value("gpsEnabled", true).toBool());
    position->set_gps_update_interval(config.value("gpsUpdateInterval", 120).toUInt());
    position->set_gps_attempt_time(config.value("gpsAttemptTime", 120).toUInt());
    position->set_position_flags(config.value("positionFlags", 0).toUInt());
    position->set_broadcast_smart_minimum_distance(config.value("broadcastSmartMinDistance", 100).toUInt());
    position->set_broadcast_smart_minimum_interval_secs(config.value("broadcastSmartMinIntervalSecs", 30).toUInt());
    position->set_gps_mode(static_cast<meshtastic::Config_PositionConfig_GpsMode>(config.value("gpsMode", 0).toInt()));

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createSecurityConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    // Never build a security config from scratch: it carries the private key
    // and admin keys, and a partial one would rotate or drop them.
    const QByteArray base = config.value("raw").toByteArray();
    meshtastic::AdminMessage admin;
    auto *security = admin.mutable_set_config()->mutable_security();
    if (base.isEmpty() || !security->ParseFromArray(base.constData(), base.size()))
    {
        qWarning() << "[Protocol] No security config from the device to edit; not sending";
        return QByteArray();
    }
    security->set_serial_enabled(config.value("serialEnabled", security->serial_enabled()).toBool());
    security->set_debug_log_api_enabled(config.value("debugLogApiEnabled", security->debug_log_api_enabled()).toBool());

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createBeginEditSettingsPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::AdminMessage admin;
    admin.set_begin_edit_settings(true);
    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createCommitEditSettingsPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::AdminMessage admin;
    admin.set_commit_edit_settings(true);
    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createChannelConfigPacket(uint32_t destNode, uint32_t myNode, int channelIndex, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setChannel = admin.mutable_set_channel();

    // IMPORTANT: For channel index 0, protobuf3 won't serialize it (default value).
    setChannel->set_index(channelIndex);

    int role = config.value("role", 0).toInt();
    setChannel->set_role(static_cast<::meshtastic::Channel_Role>(role));

    auto *settings = setChannel->mutable_settings();
    settings->set_name(config.value("name").toString().toStdString());

    QByteArray psk = config.value("psk").toByteArray();
    if (!psk.isEmpty())
    {
        settings->set_psk(psk.constData(), psk.size());
    }

    // set_channel replaces the whole channel on the device, so send back the
    // id and module settings it reported rather than letting them reset.
    const uint32_t channelId = config.value("channelId", 0u).toUInt();
    settings->set_id(channelId);

    settings->set_uplink_enabled(config.value("uplinkEnabled", false).toBool());
    settings->set_downlink_enabled(config.value("downlinkEnabled", false).toBool());

    auto *moduleSettings = settings->mutable_module_settings();
    moduleSettings->set_position_precision(config.value("positionPrecision", 0u).toUInt());
    moduleSettings->set_is_muted(config.value("isMuted", false).toBool());

    std::string adminSerialized = admin.SerializeAsString();

    qDebug() << "createChannelConfigPacket - index:" << channelIndex
             << "role:" << role
             << "name:" << config.value("name").toString()
             << "psk size:" << psk.size()
             << "channelId:" << channelId
             << "hasSessionKey:" << hasSessionKey()
             << "admin payload hex:" << QByteArray(adminSerialized.data(), adminSerialized.size()).toHex();

    return createAdminFrame(nextPacketId(), destNode, myNode, adminSerialized, m_sessionKey);
}

QByteArray MeshtasticProtocol::createRebootPacket(uint32_t destNode, uint32_t myNode, int delaySeconds)
{
    meshtastic::AdminMessage admin;
    admin.set_reboot_seconds(delaySeconds);

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createFactoryResetPacket(uint32_t destNode, uint32_t myNode, bool full)
{
    meshtastic::AdminMessage admin;
    if (full)
        admin.set_factory_reset_device(1);
    else
        admin.set_factory_reset_config(1);

    return createAdminFrame(nextPacketId(), destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createHeartbeatPacket()
{
    meshtastic::ToRadio toRadio;
    toRadio.mutable_heartbeat();  // empty Heartbeat message; firmware replies with QueueStatus

    std::string serialized;
    toRadio.SerializeToString(&serialized);
    return wrapInFrame(serialized);
}

// Expand a single-byte simple key to full AES-128 key
// Matches firmware Channels.cpp: copy defaultpsk, then add (pskIndex - 1) to last byte
QByteArray MeshtasticProtocol::expandSimpleKey(uint8_t keyByte)
{
    // keyByte 0 means no encryption
    if (keyByte == 0)
        return QByteArray();

    static const unsigned char defaultpsk[16] = {
        0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
        0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
    };

    QByteArray key(reinterpret_cast<const char*>(defaultpsk), 16);
    // Bump the last byte by (keyByte - 1); index 1 means no change from defaultpsk
    key[15] = static_cast<char>(static_cast<uint8_t>(key[15]) + keyByte - 1);
    return key;
}

// Check if decrypted data looks like a valid Data protobuf
bool MeshtasticProtocol::isValidDecryptedData(const QByteArray &decrypted)
{
    if (decrypted.isEmpty())
        return false;

    meshtastic::Data data;
    if (!data.ParseFromArray(decrypted.constData(), decrypted.size()))
        return false;

    // Check for reasonable port number (1-511 for known ports)
    int port = static_cast<int>(data.portnum());
    if (port < 1 || port > 511)
        return false;

    // Random bytes from a wrong key parse as protobuf more often than you'd
    // like, so require the payload to be present and well-formed for its port.
    const std::string &payload = data.payload();
    if (payload.empty())
        return false;

    if (port == static_cast<int>(meshtastic::PortNum::TEXT_MESSAGE_APP))
    {
        QByteArray raw(payload.data(), static_cast<int>(payload.size()));
        auto decoder = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString text = decoder.decode(raw);
        if (decoder.hasError())
            return false;
        // Control characters other than tab/newline mean this isn't real text
        for (QChar c : text)
        {
            if (c.unicode() < 0x20 && c != '\t' && c != '\n' && c != '\r')
                return false;
        }
    }

    return true;
}

// Try to decrypt with a specific key
QByteArray MeshtasticProtocol::tryDecryptWithKey(const QByteArray &encrypted, uint32_t packetId, uint32_t fromNode, const QByteArray &key)
{
    if (key.size() != 16 && key.size() != 32)
        return QByteArray();

    // Build nonce: packetId (8 bytes LE as uint64) + fromNode (4 bytes LE) + 4 zero bytes
    // Matches firmware CryptoEngine::initNonce: memcpy(nonce, &packetId_u64, 8) + memcpy(nonce+8, &fromNode, 4)
    unsigned char nonce[16] = {0};
    nonce[0] = packetId & 0xFF;
    nonce[1] = (packetId >> 8) & 0xFF;
    nonce[2] = (packetId >> 16) & 0xFF;
    nonce[3] = (packetId >> 24) & 0xFF;
    // nonce[4..7] = 0 (upper 32 bits of uint64 packetId)
    nonce[8] = fromNode & 0xFF;
    nonce[9] = (fromNode >> 8) & 0xFF;
    nonce[10] = (fromNode >> 16) & 0xFF;
    nonce[11] = (fromNode >> 24) & 0xFF;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return QByteArray();

    const EVP_CIPHER *cipher = (key.size() == 32) ? EVP_aes_256_ctr() : EVP_aes_128_ctr();

    QByteArray decrypted(encrypted.size(), 0);
    int outLen = 0;
    int totalLen = 0;

    if (EVP_DecryptInit_ex(ctx, cipher, nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           nonce) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }

    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(decrypted.data()), &outLen,
                          reinterpret_cast<const unsigned char*>(encrypted.constData()),
                          encrypted.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    totalLen = outLen;

    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(decrypted.data()) + outLen, &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    totalLen += outLen;

    EVP_CIPHER_CTX_free(ctx);
    decrypted.resize(totalLen);

    return decrypted;
}

uint8_t MeshtasticProtocol::xorHash(const QByteArray &data)
{
    uint8_t code = 0;
    for (char c : data)
        code ^= static_cast<uint8_t>(c);
    return code;
}

// Firmware channel names used when a channel has no explicit name. These feed the
// hash, so they must match DisplayFormatters::getModemPresetDisplayName
// (long names) in the firmware exactly, including "Custom" when use_preset is
// off and "Invalid" for values it has no case for (e.g. VERY_LONG_SLOW).
static QString modemPresetChannelName(int modemPreset, bool usePreset)
{
    if (!usePreset)
        return QStringLiteral("Custom");

    switch (modemPreset) {
    case 0:  return QStringLiteral("LongFast");
    case 1:  return QStringLiteral("LongSlow");
    case 3:  return QStringLiteral("MediumSlow");
    case 4:  return QStringLiteral("MediumFast");
    case 5:  return QStringLiteral("ShortSlow");
    case 6:  return QStringLiteral("ShortFast");
    case 7:  return QStringLiteral("LongMod");
    case 8:  return QStringLiteral("ShortTurbo");
    case 9:  return QStringLiteral("LongTurbo");
    case 10: return QStringLiteral("LiteFast");
    case 11: return QStringLiteral("LiteSlow");
    case 12: return QStringLiteral("NarrowFast");
    case 13: return QStringLiteral("NarrowSlow");
    case 14: return QStringLiteral("TinyFast");
    case 15: return QStringLiteral("TinySlow");
    case 16: return QStringLiteral("MediumTurbo");
    default: return QStringLiteral("Invalid");
    }
}

int MeshtasticProtocol::channelHashFor(int channelIndex) const
{
    if (!m_deviceConfig)
        return -1;

    DeviceConfig::ChannelConfig ch = m_deviceConfig->channel(channelIndex);
    if (ch.role == 0)  // disabled
        return -1;

    QByteArray key;
    if (ch.psk.size() == 1)
        key = expandSimpleKey(static_cast<uint8_t>(ch.psk[0]));
    else if (ch.psk.size() == 16 || ch.psk.size() == 32)
        key = ch.psk;

    if (key.isEmpty())
        return -1;

    // Any unnamed channel takes its name from the modem preset (Channels::getName)
    QString name = ch.name;
    if (name.isEmpty()) {
        const auto lora = m_deviceConfig->loraConfig();
        name = modemPresetChannelName(lora.modemPreset, lora.usePreset);
    }

    return xorHash(name.toUtf8()) ^ xorHash(key);
}

QByteArray MeshtasticProtocol::decryptPayload(const QByteArray &encrypted, uint32_t packetId, uint32_t fromNode,
                                              int channelHash, int *foundKeyByte, int *matchedChannel)
{
    if (foundKeyByte)
        *foundKeyByte = -1;
    if (matchedChannel)
        *matchedChannel = -1;

    // The `channel` field on an encrypted MeshPacket is a hash (0-255), not the
    // channel index, so find which configured channel that hash belongs to.
    if (m_deviceConfig) {
        for (int i = 0; i < m_deviceConfig->channels().size(); ++i) {
            if (channelHashFor(i) != channelHash)
                continue;

            DeviceConfig::ChannelConfig chConfig = m_deviceConfig->channel(i);
            QByteArray key;
            if (chConfig.psk.size() == 1)
                key = expandSimpleKey(static_cast<uint8_t>(chConfig.psk[0]));
            else
                key = chConfig.psk;

            QByteArray decrypted = tryDecryptWithKey(encrypted, packetId, fromNode, key);
            if (isValidDecryptedData(decrypted)) {
                if (foundKeyByte && chConfig.psk.size() == 1)
                    *foundKeyByte = static_cast<uint8_t>(chConfig.psk[0]);
                if (matchedChannel)
                    *matchedChannel = i;
                return decrypted;
            }
        }
    }

    // Brute force simple keys (0x00 - 0xFF)
    // This is fast since there are only 256 possibilities
    for (int keyByte = 0; keyByte <= 255; keyByte++) {
        QByteArray key = expandSimpleKey(static_cast<uint8_t>(keyByte));
        QByteArray decrypted = tryDecryptWithKey(encrypted, packetId, fromNode, key);

        if (isValidDecryptedData(decrypted)) {
            qDebug() << "[Protocol] Brute force found key byte:" << keyByte
                     << "(" << QByteArray(1, static_cast<char>(keyByte)).toBase64() << ")";
            if (foundKeyByte)
                *foundKeyByte = keyByte;
            return decrypted;
        }
    }

    return QByteArray();
}
