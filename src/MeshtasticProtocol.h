#ifndef MESHTASTICPROTOCOL_H
#define MESHTASTICPROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <QVariantMap>
#include <memory>

// Forward declarations for protobuf types
namespace meshtastic {
class FromRadio;
class ToRadio;
class MeshPacket;
class Position;
class User;
class Data;
}

class MeshtasticProtocol : public QObject
{
    Q_OBJECT

public:
    // Packet types from FromRadio
    enum class PacketType {
        Unknown,
        PacketReceived,
        MyInfo,
        NodeInfo,
        Channel,
        Config,
        ModuleConfig,
        QueueStatus,
        XModemPacket,
        Metadata,
        MqttClientProxyMessage,
        FileInfo,
        ClientNotification
    };
    Q_ENUM(PacketType)

    // Port numbers for MeshPacket data
    enum class PortNum {
        Unknown = 0,
        TextMessage = 1,
        RemoteHardware = 2,
        Position = 3,
        NodeInfo = 4,
        Routing = 5,
        Admin = 6,
        TextMessageCompressed = 7,
        Waypoint = 8,
        Audio = 9,
        Detection = 10,
        Reply = 32,
        IpTunnel = 33,
        Paxcounter = 34,
        Serial = 64,
        StoreForward = 65,
        RangeTest = 66,
        Telemetry = 67,
        ZPS = 68,
        Simulator = 69,
        Traceroute = 70,
        Neighborinfo = 71,
        Atak = 72,
        Map = 73,
        PowerStress = 74,
        Private = 256,
        Max = 511
    };
    Q_ENUM(PortNum)

    struct DecodedPacket {
        qint64 timestamp;
        PacketType type;
        uint32_t from;
        uint32_t to;
        PortNum portNum;
        QString typeName;
        QVariantMap fields;
        QByteArray rawData;
    };

    explicit MeshtasticProtocol(QObject *parent = nullptr);
    ~MeshtasticProtocol();

    // Process incoming raw data, emits packetReceived for complete packets
    void processIncomingData(const QByteArray &data);

    // Create a ToRadio packet for sending
    QByteArray createWantConfigPacket(uint32_t configId);
    QByteArray createTraceroutePacket(uint32_t destNode, uint32_t myNode);
    QByteArray createPositionRequestPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createTelemetryRequestPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createNodeInfoRequestPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createTextMessagePacket(const QString &text, uint32_t destNode, uint32_t myNode, int channel = 0, uint32_t replyId = 0);

    // Create admin packets for config updates
    QByteArray createLoRaConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createDeviceConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createPositionConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createChannelConfigPacket(uint32_t destNode, uint32_t myNode, int channelIndex, const QVariantMap &config);

    // Decode helpers
    static QString nodeIdToString(uint32_t nodeId);
    static uint32_t nodeIdFromString(const QString &nodeId);
    static QString portNumToString(PortNum portNum);
    static QString packetTypeToString(PacketType type);

signals:
    void packetReceived(const DecodedPacket &packet);
    void parseError(const QString &error);

private:
    // Frame parsing
    static const uint8_t SYNC_BYTE_1 = 0x94;
    static const uint8_t SYNC_BYTE_2 = 0xC3;
    static const int MAX_PACKET_SIZE = 512;

    enum class ParseState {
        WaitingForSync1,
        WaitingForSync2,
        WaitingForMSB,
        WaitingForLSB,
        ReadingPayload
    };

    ParseState m_parseState;
    QByteArray m_frameBuffer;
    int m_expectedLength;

    void processFrame(const QByteArray &frame);
    DecodedPacket decodeFromRadio(const QByteArray &data);
    QVariantMap decodeMeshPacket(const meshtastic::MeshPacket &packet, PortNum &portNum);
    QVariantMap decodePosition(const QByteArray &data);
    QVariantMap decodeUser(const QByteArray &data);
    QVariantMap decodeTelemetry(const QByteArray &data);
    QVariantMap decodeTextMessage(const QByteArray &data);
};

#endif // MESHTASTICPROTOCOL_H
