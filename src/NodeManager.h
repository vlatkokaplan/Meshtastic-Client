#ifndef NODEMANAGER_H
#define NODEMANAGER_H

#include <QObject>
#include <QMap>
#include <QVariantMap>
#include <QDateTime>
#include <QTimer>

class Database;

struct NodeInfo
{
    uint32_t nodeNum = 0;
    QString nodeId;
    QString longName;
    QString shortName;
    QString hwModel;

    double latitude = 0.0;
    double longitude = 0.0;
    int altitude = 0;
    bool hasPosition = false;

    int batteryLevel = 0;
    float voltage = 0.0;
    float channelUtilization = 0.0;
    float airUtilTx = 0.0;

    float snr = 0.0;
    int rssi = 0;
    QDateTime lastHeard;
    int hopsAway = -1;

    bool isExternalPower = false; // True if node is externally powered
    bool isFavorite = false;      // True if node is marked as favorite

    // Environment telemetry
    float temperature = 0.0;
    float relativeHumidity = 0.0;
    float barometricPressure = 0.0;
    uint32_t uptimeSeconds = 0;
    bool hasEnvironmentTelemetry = false;

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map["nodeNum"] = nodeNum;
        map["nodeId"] = nodeId;
        map["longName"] = longName;
        map["shortName"] = shortName;
        map["hwModel"] = hwModel;
        map["latitude"] = latitude;
        map["longitude"] = longitude;
        map["altitude"] = altitude;
        map["hasPosition"] = hasPosition;
        map["batteryLevel"] = batteryLevel;
        map["voltage"] = voltage;
        map["snr"] = snr;
        map["rssi"] = rssi;
        map["lastHeard"] = lastHeard;
        map["hopsAway"] = hopsAway;
        map["isExternalPower"] = isExternalPower;
        map["isFavorite"] = isFavorite;
        map["temperature"] = temperature;
        map["relativeHumidity"] = relativeHumidity;
        map["barometricPressure"] = barometricPressure;
        map["uptimeSeconds"] = uptimeSeconds;
        map["hasEnvironmentTelemetry"] = hasEnvironmentTelemetry;
        return map;
    }
};

class NodeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(uint32_t myNodeNum READ myNodeNum NOTIFY myNodeNumChanged)

public:
    explicit NodeManager(QObject *parent = nullptr);

    void setMyNodeNum(uint32_t nodeNum);
    uint32_t myNodeNum() const { return m_myNodeNum; }

    void updateNodeFromPacket(const QVariantMap &packetFields);
    void updateNodePosition(uint32_t nodeNum, double lat, double lon, int altitude);
    void updateNodeUser(uint32_t nodeNum, const QString &longName, const QString &shortName,
                        const QString &userId, const QString &hwModel);
    void updateNodeTelemetry(uint32_t nodeNum, const QVariantMap &telemetry);
    void updateNodeSignal(uint32_t nodeNum, float snr, int rssi, int hopsAway);
    void setNodeFavorite(uint32_t nodeNum, bool favorite);

    NodeInfo getNode(uint32_t nodeNum) const;
    QList<NodeInfo> allNodes() const;
    QList<NodeInfo> nodesWithPosition() const;
    bool hasNode(uint32_t nodeNum) const;

    void clear();

    // Database integration
    void setDatabase(Database *db);
    void loadFromDatabase();
    void saveToDatabase();

    // For QML binding
    Q_INVOKABLE QVariantList getNodesForMap() const;

signals:
    void nodeUpdated(uint32_t nodeNum);
    void nodePositionUpdated(uint32_t nodeNum, double latitude, double longitude);
    void myNodeNumChanged();
    void nodesChanged();

private:
    QMap<uint32_t, NodeInfo> m_nodes;
    uint32_t m_myNodeNum = 0;
    Database *m_database = nullptr;
    QTimer *m_updateTimer = nullptr;
    bool m_pendingUpdate = false;

    void ensureNode(uint32_t nodeNum);
    void scheduleUpdate();
    void persistNode(uint32_t nodeNum);
    QString hwModelToString(int model);
};

#endif // NODEMANAGER_H
