#include "MeshtasticProtocol.h"
#include <QDateTime>
#include <QDebug>

// Include generated protobuf headers
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/admin.pb.h"

MeshtasticProtocol::MeshtasticProtocol(QObject *parent)
    : QObject(parent), m_parseState(ParseState::WaitingForSync1), m_expectedLength(0)
{
}

MeshtasticProtocol::~MeshtasticProtocol() = default;

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
        result.fields = decodeMeshPacket(packet, result.portNum);
        result.fields["hopLimit"] = packet.hop_limit();
        result.fields["hopStart"] = packet.hop_start();
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
        {
            result.fields["configType"] = "device";
            const auto &dev = config.device();
            result.fields["role"] = static_cast<int>(dev.role());
            result.fields["serialEnabled"] = dev.serial_enabled();
            result.fields["debugLogEnabled"] = dev.debug_log_enabled();
            result.fields["buttonGpio"] = dev.button_gpio();
            result.fields["buzzerGpio"] = dev.buzzer_gpio();
            result.fields["rebroadcastMode"] = static_cast<int>(dev.rebroadcast_mode());
            result.fields["nodeInfoBroadcastSecs"] = dev.node_info_broadcast_secs();
            result.fields["doubleTapAsButtonPress"] = dev.double_tap_as_button_press();
            result.fields["isManaged"] = dev.is_managed();
            result.fields["disableTripleClick"] = dev.disable_triple_click();
            result.fields["tzdef"] = QString::fromStdString(dev.tzdef());
            result.fields["ledHeartbeatDisabled"] = dev.led_heartbeat_disabled();
            break;
        }
        case meshtastic::Config::kPosition:
        {
            result.fields["configType"] = "position";
            const auto &pos = config.position();
            result.fields["positionBroadcastSecs"] = pos.position_broadcast_secs();
            result.fields["smartPositionEnabled"] = pos.position_broadcast_smart_enabled();
            result.fields["fixedPosition"] = pos.fixed_position();
            result.fields["gpsEnabled"] = pos.gps_enabled();
            result.fields["gpsUpdateInterval"] = pos.gps_update_interval();
            result.fields["gpsAttemptTime"] = pos.gps_attempt_time();
            result.fields["positionFlags"] = pos.position_flags();
            result.fields["broadcastSmartMinDistance"] = pos.broadcast_smart_minimum_distance();
            result.fields["broadcastSmartMinIntervalSecs"] = pos.broadcast_smart_minimum_interval_secs();
            result.fields["gpsMode"] = static_cast<int>(pos.gps_mode());
            break;
        }
        case meshtastic::Config::kLora:
        {
            result.fields["configType"] = "lora";
            const auto &lora = config.lora();
            result.fields["usePreset"] = lora.use_preset();
            result.fields["modemPreset"] = static_cast<int>(lora.modem_preset());
            result.fields["bandwidth"] = lora.bandwidth();
            result.fields["spreadFactor"] = lora.spread_factor();
            result.fields["codingRate"] = lora.coding_rate();
            result.fields["frequencyOffset"] = lora.frequency_offset();
            result.fields["region"] = static_cast<int>(lora.region());
            result.fields["hopLimit"] = lora.hop_limit();
            result.fields["txEnabled"] = lora.tx_enabled();
            result.fields["txPower"] = lora.tx_power();
            result.fields["channelNum"] = lora.channel_num();
            result.fields["overrideDutyCycle"] = lora.override_duty_cycle();
            break;
        }
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
            if (routing.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                if (routing.has_error_reason())
                {
                    fields["errorReason"] = static_cast<int>(routing.error_reason());
                }
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
            if (routeData.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                // Route towards destination
                QVariantList routeList;
                for (const auto &node : routeData.route())
                {
                    routeList.append(nodeIdToString(node));
                }
                fields["route"] = routeList;

                // SNR values towards destination
                QVariantList snrTowardsList;
                // Add the received packet's SNR as first hop if available
                if (packet.rx_snr() != 0)
                {
                    snrTowardsList.append(packet.rx_snr() / 4.0); // Convert from 0.25 dB units
                }
                for (const auto &snr : routeData.snr_towards())
                {
                    snrTowardsList.append(snr / 4.0); // SNR is stored as int * 4
                }
                fields["snrTowards"] = snrTowardsList;

                // Route back
                QVariantList routeBackList;
                for (const auto &node : routeData.route_back())
                {
                    routeBackList.append(nodeIdToString(node));
                }
                fields["routeBack"] = routeBackList;

                // SNR values back
                QVariantList snrBackList;
                // Add the received packet's SNR as first hop if available
                if (packet.rx_snr() != 0)
                {
                    snrBackList.append(packet.rx_snr() / 4.0); // Convert from 0.25 dB units
                }
                for (const auto &snr : routeData.snr_back())
                {
                    snrBackList.append(snr / 4.0); // SNR is stored as int * 4
                }
                fields["snrBack"] = snrBackList;
            }
            break;
        }

        case PortNum::Admin:
        {
            meshtastic::AdminMessage admin;
            if (admin.ParseFromArray(payloadData.constData(), payloadData.size()))
            {
                fields["adminType"] = "response";

                // Handle get_config_response
                if (admin.has_get_config_response())
                {
                    const auto &config = admin.get_config_response();
                    switch (config.payload_variant_case())
                    {
                    case meshtastic::Config::kDevice:
                    {
                        fields["configType"] = "device";
                        const auto &dev = config.device();
                        fields["role"] = static_cast<int>(dev.role());
                        fields["serialEnabled"] = dev.serial_enabled();
                        fields["debugLogEnabled"] = dev.debug_log_enabled();
                        fields["buttonGpio"] = dev.button_gpio();
                        fields["buzzerGpio"] = dev.buzzer_gpio();
                        fields["rebroadcastMode"] = static_cast<int>(dev.rebroadcast_mode());
                        fields["nodeInfoBroadcastSecs"] = dev.node_info_broadcast_secs();
                        fields["doubleTapAsButtonPress"] = dev.double_tap_as_button_press();
                        fields["isManaged"] = dev.is_managed();
                        fields["disableTripleClick"] = dev.disable_triple_click();
                        fields["tzdef"] = QString::fromStdString(dev.tzdef());
                        fields["ledHeartbeatDisabled"] = dev.led_heartbeat_disabled();
                        break;
                    }
                    case meshtastic::Config::kPosition:
                    {
                        fields["configType"] = "position";
                        const auto &pos = config.position();
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
                        break;
                    }
                    case meshtastic::Config::kLora:
                    {
                        fields["configType"] = "lora";
                        const auto &lora = config.lora();
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
                        break;
                    }
                    default:
                        fields["configType"] = "unknown";
                        break;
                    }
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
    else if (packet.has_encrypted())
    {
        portNum = PortNum::Unknown;
        fields["encrypted"] = true;
        fields["encryptedData"] = QByteArray(packet.encrypted().data(), packet.encrypted().size()).toHex();
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

QVariantMap MeshtasticProtocol::decodeTelemetry(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::Telemetry telemetry;

    if (telemetry.ParseFromArray(data.constData(), data.size()))
    {
        fields["telemetryTime"] = telemetry.time();

        switch (telemetry.variant_case())
        {
        case meshtastic::Telemetry::kDeviceMetrics:
        {
            const auto &dm = telemetry.device_metrics();
            fields["telemetryType"] = "device";
            if (dm.battery_level() != 0)
            {
                fields["batteryLevel"] = dm.battery_level();
            }
            if (dm.voltage() != 0)
            {
                fields["voltage"] = dm.voltage();
            }
            if (dm.channel_utilization() != 0)
            {
                fields["channelUtilization"] = dm.channel_utilization();
            }
            if (dm.air_util_tx() != 0)
            {
                fields["airUtilTx"] = dm.air_util_tx();
            }
            if (dm.uptime_seconds() != 0)
            {
                fields["uptimeSeconds"] = dm.uptime_seconds();
            }
            break;
        }

        case meshtastic::Telemetry::kEnvironmentMetrics:
        {
            const auto &em = telemetry.environment_metrics();
            fields["telemetryType"] = "environment";
            if (em.temperature() != 0)
            {
                fields["temperature"] = em.temperature();
            }
            if (em.relative_humidity() != 0)
            {
                fields["relativeHumidity"] = em.relative_humidity();
            }
            if (em.barometric_pressure() != 0)
            {
                fields["barometricPressure"] = em.barometric_pressure();
            }
            if (em.gas_resistance() != 0)
            {
                fields["gasResistance"] = em.gas_resistance();
            }
            if (em.iaq() != 0)
            {
                fields["iaq"] = em.iaq();
            }
            break;
        }

        case meshtastic::Telemetry::kPowerMetrics:
        {
            const auto &pm = telemetry.power_metrics();
            fields["telemetryType"] = "power";
            if (pm.ch1_voltage() != 0)
            {
                fields["ch1Voltage"] = pm.ch1_voltage();
            }
            if (pm.ch1_current() != 0)
            {
                fields["ch1Current"] = pm.ch1_current();
            }
            break;
        }

        default:
            fields["telemetryType"] = "unknown";
            break;
        }
    }

    return fields;
}

QByteArray MeshtasticProtocol::createWantConfigPacket(uint32_t configId)
{
    meshtastic::ToRadio toRadio;
    toRadio.set_want_config_id(configId);

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
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
    packet->set_id(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::TRACEROUTE_APP);

    // Empty RouteDiscovery as payload
    meshtastic::RouteDiscovery route;
    decoded->set_payload(route.SerializeAsString());
    decoded->set_want_response(true);

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

QByteArray MeshtasticProtocol::createPositionRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::POSITION_APP);
    decoded->set_want_response(true);

    // Empty position as request
    meshtastic::Position pos;
    decoded->set_payload(pos.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

QByteArray MeshtasticProtocol::createTelemetryRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::TELEMETRY_APP);
    decoded->set_want_response(true);

    // Empty telemetry as request
    meshtastic::Telemetry telem;
    decoded->set_payload(telem.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

QByteArray MeshtasticProtocol::createNodeInfoRequestPacket(uint32_t destNode, uint32_t myNode)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::NODEINFO_APP);
    decoded->set_want_response(true);

    // Empty user as request
    meshtastic::User user;
    decoded->set_payload(user.SerializeAsString());

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

QByteArray MeshtasticProtocol::createTextMessagePacket(const QString &text, uint32_t destNode, uint32_t myNode, int channel, uint32_t replyId, uint32_t *outPacketId)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_channel(channel);
    packet->set_want_ack(true);
    uint32_t packetId = QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF;
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

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(SYNC_BYTE_1));
    frame.append(static_cast<char>(SYNC_BYTE_2));
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

// Helper to create admin message frame
static QByteArray createAdminFrame(uint32_t destNode, uint32_t myNode, const std::string &adminPayload)
{
    meshtastic::ToRadio toRadio;
    auto *packet = toRadio.mutable_packet();

    packet->set_to(destNode);
    packet->set_from(myNode);
    packet->set_want_ack(true);
    packet->set_id(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);

    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(meshtastic::PortNum::ADMIN_APP);
    decoded->set_payload(adminPayload);

    std::string serialized;
    toRadio.SerializeToString(&serialized);

    QByteArray frame;
    frame.append(static_cast<char>(0x94)); // SYNC_BYTE_1
    frame.append(static_cast<char>(0xC3)); // SYNC_BYTE_2
    frame.append(static_cast<char>((serialized.size() >> 8) & 0xFF));
    frame.append(static_cast<char>(serialized.size() & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));

    return frame;
}

QByteArray MeshtasticProtocol::createGetConfigRequestPacket(uint32_t destNode, uint32_t myNode, int configType)
{
    meshtastic::AdminMessage admin;
    // configType: 1=Device, 2=Position, 3=Power, 4=Network, 5=Display, 6=LoRa, 7=Bluetooth
    admin.set_get_config_request(configType);

    return createAdminFrame(destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createLoRaConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *lora = setConfig->mutable_lora();

    lora->set_use_preset(config.value("usePreset", true).toBool());
    lora->set_modem_preset(config.value("modemPreset", 0).toUInt());
    lora->set_region(config.value("region", 0).toUInt());
    lora->set_hop_limit(config.value("hopLimit", 3).toUInt());
    lora->set_tx_enabled(config.value("txEnabled", true).toBool());
    lora->set_tx_power(config.value("txPower", 0).toInt());
    lora->set_channel_num(config.value("channelNum", 0).toUInt());
    lora->set_override_duty_cycle(config.value("overrideDutyCycle", false).toBool());
    lora->set_frequency_offset(config.value("frequencyOffset", 0.0).toFloat());

    return createAdminFrame(destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createDeviceConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *device = setConfig->mutable_device();

    device->set_role(config.value("role", 0).toUInt());
    device->set_serial_enabled(config.value("serialEnabled", true).toBool());
    device->set_debug_log_enabled(config.value("debugLogEnabled", false).toBool());
    device->set_button_gpio(config.value("buttonGpio", 0).toUInt());
    device->set_buzzer_gpio(config.value("buzzerGpio", 0).toUInt());
    device->set_rebroadcast_mode(config.value("rebroadcastMode", 0).toUInt());
    device->set_node_info_broadcast_secs(config.value("nodeInfoBroadcastSecs", 900).toUInt());
    device->set_double_tap_as_button_press(config.value("doubleTapAsButtonPress", false).toBool());
    device->set_is_managed(config.value("isManaged", false).toBool());
    device->set_disable_triple_click(config.value("disableTripleClick", false).toBool());
    device->set_tzdef(config.value("tzdef").toString().toStdString());
    device->set_led_heartbeat_disabled(config.value("ledHeartbeatDisabled", false).toBool());

    return createAdminFrame(destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createPositionConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setConfig = admin.mutable_set_config();
    auto *position = setConfig->mutable_position();

    position->set_position_broadcast_secs(config.value("positionBroadcastSecs", 900).toUInt());
    position->set_position_broadcast_smart_enabled(config.value("smartPositionEnabled", true).toBool());
    position->set_fixed_position(config.value("fixedPosition", false).toBool());
    position->set_gps_enabled(config.value("gpsEnabled", true).toBool());
    position->set_gps_update_interval(config.value("gpsUpdateInterval", 120).toUInt());
    position->set_gps_attempt_time(config.value("gpsAttemptTime", 120).toUInt());
    position->set_position_flags(config.value("positionFlags", 0).toUInt());
    position->set_broadcast_smart_minimum_distance(config.value("broadcastSmartMinDistance", 100).toUInt());
    position->set_broadcast_smart_minimum_interval_secs(config.value("broadcastSmartMinIntervalSecs", 30).toUInt());
    position->set_gps_mode(config.value("gpsMode", 0).toUInt());

    return createAdminFrame(destNode, myNode, admin.SerializeAsString());
}

QByteArray MeshtasticProtocol::createChannelConfigPacket(uint32_t destNode, uint32_t myNode, int channelIndex, const QVariantMap &config)
{
    meshtastic::AdminMessage admin;
    auto *setChannel = admin.mutable_set_channel();

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

    settings->set_uplink_enabled(config.value("uplinkEnabled", false).toBool());
    settings->set_downlink_enabled(config.value("downlinkEnabled", false).toBool());

    return createAdminFrame(destNode, myNode, admin.SerializeAsString());
}
