#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QList>
#include "NodeManager.h"

struct ChatMessage; // Forward declaration

class Database : public QObject
{
    Q_OBJECT

public:
    explicit Database(QObject *parent = nullptr);
    ~Database();

    bool open(const QString &path = QString());
    void close();
    bool isOpen() const;

    // Node operations
    bool saveNode(const NodeInfo &node);
    bool saveNodes(const QList<NodeInfo> &nodes);
    NodeInfo loadNode(uint32_t nodeNum);
    QList<NodeInfo> loadAllNodes();
    bool deleteNode(uint32_t nodeNum);
    int nodeCount();

    // Message operations (for future use)
    struct Message
    {
        qint64 id = 0;
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        QString channel;
        QString text;
        QDateTime timestamp;
        bool read = false;
        int portNum = 0;
        QByteArray payload;
        int status = 0; // MessageStatus enum value
        uint32_t packetId = 0;
    };

    bool saveMessage(const Message &msg);
    QList<Message> loadMessages(int limit = 100, int offset = 0);

    // Traceroute operations
    struct Traceroute
    {
        qint64 id = 0;
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        QStringList routeTo;
        QStringList routeBack;
        QStringList snrTo;
        QStringList snrBack;
        QDateTime timestamp;
        bool isResponse = false; // true if this is a response, false if request sent
    };

    bool saveTraceroute(const Traceroute &tr);
    QList<Traceroute> loadTraceroutes(int limit = 100, int offset = 0);
    bool deleteTraceroutes(int daysOld = 30);

    // Telemetry history operations
    struct TelemetryRecord
    {
        qint64 id = 0;
        uint32_t nodeNum = 0;
        QDateTime timestamp;
        float temperature = 0.0f;
        float humidity = 0.0f;
        float pressure = 0.0f;
        int batteryLevel = 0;
        float voltage = 0.0f;
        float snr = 0.0f;
        int rssi = 0;
        float channelUtil = 0.0f;
        float airUtilTx = 0.0f;
    };

    bool saveTelemetryRecord(const TelemetryRecord &record);
    QList<TelemetryRecord> loadTelemetryHistory(uint32_t nodeNum, int hours = 24);
    QList<uint32_t> getNodesWithTelemetry();
    bool deleteTelemetryHistory(int daysOld = 7);

    // Position history operations
    struct PositionRecord
    {
        uint32_t nodeNum = 0;
        double latitude = 0.0;
        double longitude = 0.0;
        int altitude = 0;
        QDateTime timestamp;
    };

    bool savePosition(const PositionRecord &record);
    PositionRecord loadPositionAt(uint32_t nodeNum, qint64 timestamp);

    // Raw packet storage (for long-running sessions)
    struct PacketRecord
    {
        qint64 id = 0;
        qint64 timestamp = 0;
        int packetType = 0;
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        int portNum = 0;
        int channel = 0;
        QString typeName;
        QByteArray rawData;
        QString fieldsJson;
    };

    bool savePacket(const PacketRecord &record);
    QList<PacketRecord> loadPackets(int limit = 1000, int offset = 0);
    bool deleteOldPackets(int daysOld = 7);

    QList<Message> loadMessagesForNode(uint32_t nodeNum, int limit = 100);
    bool markMessageRead(qint64 messageId);
    bool updateMessageStatus(uint32_t packetId, int status);
    int unreadMessageCount();
    bool deleteMessagesWithNode(uint32_t nodeNum);
    QList<ChatMessage> getAllMessages(); // For export

private:
    QSqlDatabase m_db;
    QString m_connectionName;

    // Cached prepared statements
    QSqlQuery *m_saveNodeStmt = nullptr;
    QSqlQuery *m_saveMessageStmt = nullptr;

    bool createTables();
    bool migrateSchema(int fromVersion, int toVersion);
    int getSchemaVersion();
    void setSchemaVersion(int version);
    bool prepareStatements();
    void cleanupStatements();
};

#endif // DATABASE_H
