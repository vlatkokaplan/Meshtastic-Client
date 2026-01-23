#include "MeshtasticProtocol.h"
#include <QDateTime>
#include <QDebug>

// Include generated protobuf headers
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "meshtastic/telemetry.pb.h"

MeshtasticProtocol::MeshtasticProtocol(QObject *parent)
    : QObject(parent)
    , m_parseState(ParseState::WaitingForSync1)
    , m_expectedLength(0)
{
}

MeshtasticProtocol::~MeshtasticProtocol() = default;

void MeshtasticProtocol::processIncomingData(const QByteArray &data)
{
    for (int i = 0; i < data.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(data[i]);

        switch (m_parseState) {
        case ParseState::WaitingForSync1:
            if (byte == SYNC_BYTE_1) {
                m_parseState = ParseState::WaitingForSync2;
            }
            break;

        case ParseState::WaitingForSync2:
            if (byte == SYNC_BYTE_2) {
                m_parseState = ParseState::WaitingForMSB;
            } else if (byte == SYNC_BYTE_1) {
                // Stay in sync2 state
            } else {
                m_parseState = ParseState::WaitingForSync1;
            }
            break;

        case ParseState::WaitingForMSB:
            m_expectedLength = byte << 8;
            m_parseState = ParseState::WaitingForLSB;
            break;

        case ParseState::WaitingForLSB:
            m_expectedLength |= byte;
            if (m_expectedLength > 0 && m_expectedLength <= MAX_PACKET_SIZE) {
                m_frameBuffer.clear();
                m_frameBuffer.reserve(m_expectedLength);
                m_parseState = ParseState::ReadingPayload;
            } else {
                qWarning() << "Invalid packet length:" << m_expectedLength;
                m_parseState = ParseState::WaitingForSync1;
            }
            break;

        case ParseState::ReadingPayload:
            m_frameBuffer.append(static_cast<char>(byte));
            if (m_frameBuffer.size() >= m_expectedLength) {
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
    try {
        DecodedPacket decoded = decodeFromRadio(frame);
        emit packetReceived(decoded);
    } catch (const std::exception &e) {
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
    if (!fromRadio.ParseFromArray(data.constData(), data.size())) {
        throw std::runtime_error("Failed to parse FromRadio message");
    }

    result.fields["id"] = fromRadio.id();

    switch (fromRadio.payload_variant_case()) {
    case meshtastic::FromRadio::kPacket: {
        result.type = PacketType::PacketReceived;
        const auto &packet = fromRadio.packet();
        result.from = packet.from();
        result.to = packet.to();
        result.fields = decodeMeshPacket(packet, result.portNum);
        result.fields["hopLimit"] = packet.hop_limit();
        result.fields["hopStart"] = packet.hop_start();
        if (packet.rx_time() > 0) {
            result.fields["rxTime"] = QDateTime::fromSecsSinceEpoch(packet.rx_time()).toString(Qt::ISODate);
        }
        if (packet.rx_snr() != 0) {
            result.fields["rxSnr"] = packet.rx_snr();
        }
        if (packet.rx_rssi() != 0) {
            result.fields["rxRssi"] = packet.rx_rssi();
        }
        break;
    }

    case meshtastic::FromRadio::kMyInfo: {
        result.type = PacketType::MyInfo;
        const auto &myInfo = fromRadio.my_info();
        result.fields["myNodeNum"] = myInfo.my_node_num();
        result.fields["rebootCount"] = myInfo.reboot_count();
        result.fields["minAppVersion"] = myInfo.min_app_version();
        break;
    }

    case meshtastic::FromRadio::kNodeInfo: {
        result.type = PacketType::NodeInfo;
        const auto &nodeInfo = fromRadio.node_info();
        result.fields["nodeNum"] = nodeInfo.num();
        result.fields["lastHeard"] = nodeInfo.last_heard();
        result.fields["snr"] = nodeInfo.snr();
        if (nodeInfo.has_user()) {
            const auto &user = nodeInfo.user();
            result.fields["userId"] = QString::fromStdString(user.id());
            result.fields["longName"] = QString::fromStdString(user.long_name());
            result.fields["shortName"] = QString::fromStdString(user.short_name());
            result.fields["hwModel"] = static_cast<int>(user.hw_model());
        }
        if (nodeInfo.has_position()) {
            const auto &pos = nodeInfo.position();
            if (pos.latitude_i() != 0 || pos.longitude_i() != 0) {
                result.fields["latitude"] = pos.latitude_i() / 1e7;
                result.fields["longitude"] = pos.longitude_i() / 1e7;
                if (pos.altitude() != 0) {
                    result.fields["altitude"] = pos.altitude();
                }
            }
        }
        break;
    }

    case meshtastic::FromRadio::kChannel: {
        result.type = PacketType::Channel;
        const auto &channel = fromRadio.channel();
        result.fields["index"] = channel.index();
        result.fields["role"] = static_cast<int>(channel.role());
        if (channel.has_settings()) {
            result.fields["channelName"] = QString::fromStdString(channel.settings().name());
        }
        break;
    }

    case meshtastic::FromRadio::kConfig: {
        result.type = PacketType::Config;
        result.fields["configType"] = "config";
        break;
    }

    case meshtastic::FromRadio::kModuleConfig: {
        result.type = PacketType::ModuleConfig;
        result.fields["configType"] = "moduleConfig";
        break;
    }

    case meshtastic::FromRadio::kQueueStatus: {
        result.type = PacketType::QueueStatus;
        const auto &status = fromRadio.queuestatus();
        result.fields["free"] = status.free();
        result.fields["maxLen"] = status.maxlen();
        result.fields["meshPacketId"] = status.mesh_packet_id();
        break;
    }

    case meshtastic::FromRadio::kMetadata: {
        result.type = PacketType::Metadata;
        const auto &meta = fromRadio.metadata();
        result.fields["firmwareVersion"] = QString::fromStdString(meta.firmware_version());
        result.fields["deviceStateVersion"] = meta.device_state_version();
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

    if (packet.has_decoded()) {
        const auto &decoded = packet.decoded();
        portNum = static_cast<PortNum>(decoded.portnum());
        fields["portnum"] = portNumToString(portNum);

        const std::string &payload = decoded.payload();
        QByteArray payloadData(payload.data(), payload.size());

        switch (portNum) {
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

        case PortNum::Routing: {
            meshtastic::Routing routing;
            if (routing.ParseFromArray(payloadData.constData(), payloadData.size())) {
                if (routing.has_error_reason()) {
                    fields["errorReason"] = static_cast<int>(routing.error_reason());
                }
            }
            break;
        }

        case PortNum::Traceroute: {
            meshtastic::RouteDiscovery route;
            if (route.ParseFromArray(payloadData.constData(), payloadData.size())) {
                QVariantList routeList;
                for (const auto &node : route.route()) {
                    routeList.append(nodeIdToString(node));
                }
                fields["route"] = routeList;
            }
            break;
        }

        default:
            fields["payloadHex"] = payloadData.toHex();
            break;
        }

        if (decoded.request_id() != 0) {
            fields["requestId"] = decoded.request_id();
        }
    } else if (packet.has_encrypted()) {
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

    if (pos.ParseFromArray(data.constData(), data.size())) {
        if (pos.latitude_i() != 0 || pos.longitude_i() != 0) {
            fields["latitude"] = pos.latitude_i() / 1e7;
            fields["longitude"] = pos.longitude_i() / 1e7;
        }
        if (pos.altitude() != 0) {
            fields["altitude"] = pos.altitude();
        }
        if (pos.time() != 0) {
            fields["positionTime"] = QDateTime::fromSecsSinceEpoch(pos.time()).toString(Qt::ISODate);
        }
        if (pos.ground_speed() != 0) {
            fields["groundSpeed"] = pos.ground_speed();
        }
        if (pos.ground_track() != 0) {
            fields["groundTrack"] = pos.ground_track();
        }
        if (pos.sats_in_view() != 0) {
            fields["satsInView"] = pos.sats_in_view();
        }
        if (pos.precision_bits() != 0) {
            fields["precisionBits"] = pos.precision_bits();
        }
    }

    return fields;
}

QVariantMap MeshtasticProtocol::decodeUser(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::User user;

    if (user.ParseFromArray(data.constData(), data.size())) {
        fields["userId"] = QString::fromStdString(user.id());
        fields["longName"] = QString::fromStdString(user.long_name());
        fields["shortName"] = QString::fromStdString(user.short_name());
        fields["hwModel"] = static_cast<int>(user.hw_model());
        if (user.is_licensed()) {
            fields["isLicensed"] = true;
        }
    }

    return fields;
}

QVariantMap MeshtasticProtocol::decodeTelemetry(const QByteArray &data)
{
    QVariantMap fields;
    meshtastic::Telemetry telemetry;

    if (telemetry.ParseFromArray(data.constData(), data.size())) {
        fields["telemetryTime"] = telemetry.time();

        switch (telemetry.variant_case()) {
        case meshtastic::Telemetry::kDeviceMetrics: {
            const auto &dm = telemetry.device_metrics();
            fields["telemetryType"] = "device";
            if (dm.battery_level() != 0) {
                fields["batteryLevel"] = dm.battery_level();
            }
            if (dm.voltage() != 0) {
                fields["voltage"] = dm.voltage();
            }
            if (dm.channel_utilization() != 0) {
                fields["channelUtilization"] = dm.channel_utilization();
            }
            if (dm.air_util_tx() != 0) {
                fields["airUtilTx"] = dm.air_util_tx();
            }
            if (dm.uptime_seconds() != 0) {
                fields["uptimeSeconds"] = dm.uptime_seconds();
            }
            break;
        }

        case meshtastic::Telemetry::kEnvironmentMetrics: {
            const auto &em = telemetry.environment_metrics();
            fields["telemetryType"] = "environment";
            if (em.temperature() != 0) {
                fields["temperature"] = em.temperature();
            }
            if (em.relative_humidity() != 0) {
                fields["relativeHumidity"] = em.relative_humidity();
            }
            if (em.barometric_pressure() != 0) {
                fields["barometricPressure"] = em.barometric_pressure();
            }
            if (em.gas_resistance() != 0) {
                fields["gasResistance"] = em.gas_resistance();
            }
            if (em.iaq() != 0) {
                fields["iaq"] = em.iaq();
            }
            break;
        }

        case meshtastic::Telemetry::kPowerMetrics: {
            const auto &pm = telemetry.power_metrics();
            fields["telemetryType"] = "power";
            if (pm.ch1_voltage() != 0) {
                fields["ch1Voltage"] = pm.ch1_voltage();
            }
            if (pm.ch1_current() != 0) {
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

QString MeshtasticProtocol::portNumToString(PortNum portNum)
{
    switch (portNum) {
    case PortNum::TextMessage: return "TEXT_MESSAGE";
    case PortNum::RemoteHardware: return "REMOTE_HARDWARE";
    case PortNum::Position: return "POSITION";
    case PortNum::NodeInfo: return "NODEINFO";
    case PortNum::Routing: return "ROUTING";
    case PortNum::Admin: return "ADMIN";
    case PortNum::TextMessageCompressed: return "TEXT_MESSAGE_COMPRESSED";
    case PortNum::Waypoint: return "WAYPOINT";
    case PortNum::Audio: return "AUDIO";
    case PortNum::Detection: return "DETECTION";
    case PortNum::Reply: return "REPLY";
    case PortNum::IpTunnel: return "IP_TUNNEL";
    case PortNum::Paxcounter: return "PAXCOUNTER";
    case PortNum::Serial: return "SERIAL";
    case PortNum::StoreForward: return "STORE_FORWARD";
    case PortNum::RangeTest: return "RANGE_TEST";
    case PortNum::Telemetry: return "TELEMETRY";
    case PortNum::ZPS: return "ZPS";
    case PortNum::Simulator: return "SIMULATOR";
    case PortNum::Traceroute: return "TRACEROUTE";
    case PortNum::Neighborinfo: return "NEIGHBORINFO";
    case PortNum::Atak: return "ATAK";
    case PortNum::Map: return "MAP";
    case PortNum::PowerStress: return "POWERSTRESS";
    case PortNum::Private: return "PRIVATE";
    default: return QString("UNKNOWN(%1)").arg(static_cast<int>(portNum));
    }
}

QString MeshtasticProtocol::packetTypeToString(PacketType type)
{
    switch (type) {
    case PacketType::PacketReceived: return "Packet";
    case PacketType::MyInfo: return "MyInfo";
    case PacketType::NodeInfo: return "NodeInfo";
    case PacketType::Channel: return "Channel";
    case PacketType::Config: return "Config";
    case PacketType::ModuleConfig: return "ModuleConfig";
    case PacketType::QueueStatus: return "QueueStatus";
    case PacketType::XModemPacket: return "XModem";
    case PacketType::Metadata: return "Metadata";
    case PacketType::MqttClientProxyMessage: return "MqttProxy";
    case PacketType::FileInfo: return "FileInfo";
    case PacketType::ClientNotification: return "Notification";
    default: return "Unknown";
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
