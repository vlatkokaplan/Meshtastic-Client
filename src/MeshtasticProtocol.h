#ifndef MESHTASTICPROTOCOL_H
#define MESHTASTICPROTOCOL_H

#include <QObject>
#include <QByteArray>
#include <QVariantMap>
#include <memory>

class DeviceConfig;

// Forward declarations for protobuf types
namespace meshtastic
{
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
    enum class PacketType
    {
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
        ClientNotification,
        ConfigCompleteId,
        LogRecord,
        Rebooted
    };
    Q_ENUM(PacketType)

    // Port numbers for MeshPacket data
    enum class PortNum
    {
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

    struct DecodedPacket
    {
        qint64 timestamp;
        PacketType type;
        uint32_t from;
        uint32_t to;
        PortNum portNum;
        int channelIndex = 0;
        QString typeName;
        QVariantMap fields;
        QByteArray rawData;
    };

    explicit MeshtasticProtocol(QObject *parent = nullptr);
    ~MeshtasticProtocol();

    // Process incoming raw data, emits packetReceived for complete packets
    void processIncomingData(const QByteArray &data);

    // Clear framing state — call on reconnect to discard stale partial frames
    void resetParser();

    // Create a ToRadio packet for sending
    QByteArray createWantConfigPacket(uint32_t configId);
    QByteArray createTraceroutePacket(uint32_t destNode, uint32_t myNode);
    QByteArray createPositionRequestPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createTelemetryRequestPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createNodeInfoRequestPacket(uint32_t destNode, uint32_t myNode);
    // isReaction sets Data.emoji so other clients show a tapback on replyId
    // rather than a reply containing an emoji.
    QByteArray createTextMessagePacket(const QString &text, uint32_t destNode, uint32_t myNode, int channel = 0, uint32_t replyId = 0, uint32_t *outPacketId = nullptr, bool isReaction = false);

    // Longest text we send, in UTF-8 bytes. The firmware's Data payload holds
    // 233 bytes and drops anything larger without telling us; the official
    // apps cap text at 200 to leave room, and so do we.
    static constexpr int MAX_TEXT_BYTES = 200;

    // Create admin packets for config requests
    QByteArray createGetConfigRequestPacket(uint32_t destNode, uint32_t myNode, int configType);
    QByteArray createSessionKeyRequestPacket();  // Request session key for admin ops

    // Session key management
    void setSessionKey(const QByteArray &key) { m_sessionKey = key; }
    QByteArray sessionKey() const { return m_sessionKey; }
    bool hasSessionKey() const { return !m_sessionKey.isEmpty(); }

    // Device config for packet decryption
    void setDeviceConfig(DeviceConfig *config) { m_deviceConfig = config; }

    // Meshtastic channel hash: xorHash(name) ^ xorHash(key), matching firmware
    // Channels::generateHash. The `channel` field of an encrypted MeshPacket
    // carries this hash, not the channel index.
    // channelHashFor() returns -1 when the channel is disabled or has no key.
    static uint8_t xorHash(const QByteArray &data);
    int channelHashFor(int channelIndex) const;

    // Create admin packets for config updates
    QByteArray createLoRaConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createDeviceConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createPositionConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);
    QByteArray createChannelConfigPacket(uint32_t destNode, uint32_t myNode, int channelIndex, const QVariantMap &config);
    // Edits serial_enabled / debug_log_api_enabled on top of config["raw"].
    // Returns an empty array when there is no raw config to start from.
    QByteArray createSecurityConfigPacket(uint32_t destNode, uint32_t myNode, const QVariantMap &config);

    // Group several set_* messages so the device saves (and reboots) once
    QByteArray createBeginEditSettingsPacket(uint32_t destNode, uint32_t myNode);
    QByteArray createCommitEditSettingsPacket(uint32_t destNode, uint32_t myNode);

    // Create admin packets for device actions
    QByteArray createRebootPacket(uint32_t destNode, uint32_t myNode, int delaySeconds = 5);
    // full = factory_reset_device (also clears BLE bonds); otherwise
    // factory_reset_config. Both erase settings, node DB and keys.
    QByteArray createFactoryResetPacket(uint32_t destNode, uint32_t myNode, bool full);

    // Create heartbeat packet to keep connection alive
    QByteArray createHeartbeatPacket();

    // Unique id for an outgoing MeshPacket
    uint32_t nextPacketId();

    // Decode helpers
    static QString nodeIdToString(uint32_t nodeId);
    static uint32_t nodeIdFromString(const QString &nodeId);
    static QString portNumToString(PortNum portNum);
    static QString packetTypeToString(PacketType type);

    // Frame sync bytes (public for helper functions)
    static const uint8_t SYNC_BYTE_1 = 0x94;
    static const uint8_t SYNC_BYTE_2 = 0xC3;

signals:
    void packetReceived(const DecodedPacket &packet);
    void parseError(const QString &error);

private:
    // Frame parsing
    static const int MAX_PACKET_SIZE = 512;

    enum class ParseState
    {
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

    // Low 10 bits of outgoing packet ids; see nextPacketId()
    uint32_t m_packetCounter = 0;

    // Session key for admin operations
    QByteArray m_sessionKey;

    // Device config for decryption keys
    DeviceConfig *m_deviceConfig = nullptr;

    // Decrypt encrypted packet payload
    QByteArray decryptPayload(const QByteArray &encrypted, uint32_t packetId, uint32_t fromNode,
                              int channelHash, int *foundKeyByte = nullptr, int *matchedChannel = nullptr);
    // Helpers for decryption
    QByteArray tryDecryptWithKey(const QByteArray &encrypted, uint32_t packetId, uint32_t fromNode, const QByteArray &key);
    static QByteArray expandSimpleKey(uint8_t keyByte);
    bool isValidDecryptedData(const QByteArray &decrypted);
};

#endif // MESHTASTICPROTOCOL_H
