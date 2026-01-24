#ifndef DATABASE_H
#define DATABASE_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QList>
#include "NodeManager.h"

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
    struct Message {
        qint64 id = 0;
        uint32_t fromNode = 0;
        uint32_t toNode = 0;
        QString channel;
        QString text;
        QDateTime timestamp;
        bool read = false;
        int portNum = 0;
        QByteArray payload;
    };

    bool saveMessage(const Message &msg);
    QList<Message> loadMessages(int limit = 100, int offset = 0);
    QList<Message> loadMessagesForNode(uint32_t nodeNum, int limit = 100);
    bool markMessageRead(qint64 messageId);
    int unreadMessageCount();
    bool deleteMessagesWithNode(uint32_t nodeNum);

private:
    QSqlDatabase m_db;
    QString m_connectionName;

    bool createTables();
    bool migrateSchema(int fromVersion, int toVersion);
    int getSchemaVersion();
    void setSchemaVersion(int version);
};

#endif // DATABASE_H
