#include "NodeManager.h"
#include "MeshtasticProtocol.h"
#include "Database.h"
#include <QDebug>
#include <QMutexLocker>

NodeManager::NodeManager(QObject *parent)
    : QObject(parent)
{
    // Debounce timer - coalesce rapid updates into one signal
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(100); // 100ms debounce
    connect(m_updateTimer, &QTimer::timeout, this, [this]()
            {
        m_pendingUpdate = false;
        emit nodesChanged(); });
}

void NodeManager::scheduleUpdate()
{
    if (!m_pendingUpdate)
    {
        m_pendingUpdate = true;
        m_updateTimer->start();
    }
}

void NodeManager::setMyNodeNum(uint32_t nodeNum)
{
    if (m_myNodeNum != nodeNum)
    {
        m_myNodeNum = nodeNum;
        emit myNodeNumChanged();
    }
}

void NodeManager::updateNodeFromPacket(const QVariantMap &fields)
{
    // Handle NodeInfo packets
    if (fields.contains("nodeNum"))
    {
        QMutexLocker locker(&m_mutex);
        uint32_t nodeNum = fields["nodeNum"].toUInt();
        ensureNode(nodeNum);

        NodeInfo &node = m_nodes[nodeNum];

        if (fields.contains("longName"))
        {
            node.longName = fields["longName"].toString();
        }
        if (fields.contains("shortName"))
        {
            node.shortName = fields["shortName"].toString();
        }
        if (fields.contains("userId"))
        {
            node.nodeId = fields["userId"].toString();
        }
        if (fields.contains("hwModel"))
        {
            node.hwModel = hwModelToString(fields["hwModel"].toInt());
        }
        if (fields.contains("role"))
        {
            node.role = fields["role"].toInt();
        }
        if (fields.contains("snr"))
        {
            node.snr = fields["snr"].toFloat();
        }
        if (fields.contains("lastHeard"))
        {
            node.lastHeard = QDateTime::fromSecsSinceEpoch(fields["lastHeard"].toLongLong());
        }

        if (fields.contains("latitude") && fields.contains("longitude"))
        {
            double lat = fields["latitude"].toDouble();
            double lon = fields["longitude"].toDouble();
            if (lat != 0.0 || lon != 0.0)
            {
                node.latitude = lat;
                node.longitude = lon;
                node.hasPosition = true;
                if (fields.contains("altitude"))
                {
                    node.altitude = fields["altitude"].toInt();
                }
                emit nodePositionUpdated(nodeNum, lat, lon);
            }
        }

        if (fields.contains("isFavorite"))
        {
            node.isFavorite = fields["isFavorite"].toBool();
        }

        persistNode(nodeNum);
        emit nodeUpdated(nodeNum);
        scheduleUpdate();
    }
}

void NodeManager::updateNodePosition(uint32_t nodeNum, double lat, double lon, int altitude)
{
    if (lat == 0.0 && lon == 0.0)
    {
        return;
    }

    QMutexLocker locker(&m_mutex);
    ensureNode(nodeNum);
    NodeInfo &node = m_nodes[nodeNum];

    node.latitude = lat;
    node.longitude = lon;
    node.altitude = altitude;
    node.hasPosition = true;
    node.lastHeard = QDateTime::currentDateTime();

    persistNode(nodeNum);
    emit nodeUpdated(nodeNum);
    emit nodePositionUpdated(nodeNum, lat, lon);
    scheduleUpdate();
}

void NodeManager::updateNodeUser(uint32_t nodeNum, const QString &longName,
                                 const QString &shortName, const QString &userId,
                                 const QString &hwModel)
{
    QMutexLocker locker(&m_mutex);
    ensureNode(nodeNum);
    NodeInfo &node = m_nodes[nodeNum];

    if (!longName.isEmpty())
        node.longName = longName;
    if (!shortName.isEmpty())
        node.shortName = shortName;
    if (!userId.isEmpty())
        node.nodeId = userId;
    if (!hwModel.isEmpty())
        node.hwModel = hwModel;
    node.lastHeard = QDateTime::currentDateTime();

    persistNode(nodeNum);
    emit nodeUpdated(nodeNum);
    scheduleUpdate();
}

void NodeManager::updateNodeTelemetry(uint32_t nodeNum, const QVariantMap &telemetry)
{
    QMutexLocker locker(&m_mutex);
    ensureNode(nodeNum);
    NodeInfo &node = m_nodes[nodeNum];

    if (telemetry.contains("batteryLevel"))
    {
        int level = telemetry["batteryLevel"].toInt();
        // Battery level > 100 means external power (same as web client)
        if (level > 100)
        {
            node.isExternalPower = true;
            // Display as 100% when on external power
            level = 100;
        }
        else
        {
            node.isExternalPower = false;
        }
        node.batteryLevel = level;
    }
    if (telemetry.contains("voltage"))
    {
        node.voltage = telemetry["voltage"].toFloat();
    }
    if (telemetry.contains("channelUtilization"))
    {
        node.channelUtilization = telemetry["channelUtilization"].toFloat();
    }
    if (telemetry.contains("airUtilTx"))
    {
        node.airUtilTx = telemetry["airUtilTx"].toFloat();
    }
    // Legacy support: explicit externalPower field (if provided)
    if (telemetry.contains("externalPower"))
    {
        node.isExternalPower = telemetry["externalPower"].toBool();
    }

    // Environment telemetry
    if (telemetry.contains("temperature"))
    {
        node.temperature = telemetry["temperature"].toFloat();
        node.hasEnvironmentTelemetry = true;
    }
    if (telemetry.contains("relativeHumidity"))
    {
        node.relativeHumidity = telemetry["relativeHumidity"].toFloat();
        node.hasEnvironmentTelemetry = true;
    }
    if (telemetry.contains("barometricPressure"))
    {
        node.barometricPressure = telemetry["barometricPressure"].toFloat();
        node.hasEnvironmentTelemetry = true;
    }
    if (telemetry.contains("uptimeSeconds"))
    {
        node.uptimeSeconds = telemetry["uptimeSeconds"].toUInt();
    }

    node.lastHeard = QDateTime::currentDateTime();

    persistNode(nodeNum);
    emit nodeUpdated(nodeNum);
    scheduleUpdate();
}

void NodeManager::updateNodeSignal(uint32_t nodeNum, float snr, int rssi, int hopsAway)
{
    QMutexLocker locker(&m_mutex);
    ensureNode(nodeNum);
    NodeInfo &node = m_nodes[nodeNum];

    node.snr = snr;
    node.rssi = rssi;
    if (hopsAway >= 0)
    {
        node.hopsAway = hopsAway;
    }
    node.lastHeard = QDateTime::currentDateTime();

    persistNode(nodeNum);
    emit nodeUpdated(nodeNum);
    scheduleUpdate();
}

void NodeManager::setNodeFavorite(uint32_t nodeNum, bool favorite)
{
    QMutexLocker locker(&m_mutex);
    if (m_nodes.contains(nodeNum))
    {
        NodeInfo &node = m_nodes[nodeNum];
        if (node.isFavorite != favorite)
        {
            node.isFavorite = favorite;
            persistNode(nodeNum);
            emit nodeUpdated(nodeNum);
            scheduleUpdate();
        }
    }
}

NodeInfo NodeManager::getNode(uint32_t nodeNum) const
{
    QMutexLocker locker(&m_mutex);
    return m_nodes.value(nodeNum);
}

QList<NodeInfo> NodeManager::allNodes() const
{
    QMutexLocker locker(&m_mutex);
    return m_nodes.values();
}

QList<NodeInfo> NodeManager::nodesWithPosition() const
{
    QMutexLocker locker(&m_mutex);
    QList<NodeInfo> result;
    for (const NodeInfo &node : m_nodes)
    {
        if (node.hasPosition)
        {
            result.append(node);
        }
    }
    return result;
}

bool NodeManager::hasNode(uint32_t nodeNum) const
{
    QMutexLocker locker(&m_mutex);
    return m_nodes.contains(nodeNum);
}

void NodeManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_nodes.clear();
    m_updateTimer->stop();
    m_pendingUpdate = false;
    locker.unlock();  // Unlock before emitting to avoid deadlock
    emit nodesChanged(); // Immediate emit for clear
}

QVariantList NodeManager::getNodesForMap() const
{
    QMutexLocker locker(&m_mutex);
    QVariantList list;
    for (const NodeInfo &node : m_nodes)
    {
        if (node.hasPosition)
        {
            list.append(node.toVariantMap());
        }
    }
    return list;
}

void NodeManager::ensureNode(uint32_t nodeNum)
{
    if (!m_nodes.contains(nodeNum))
    {
        NodeInfo node;
        node.nodeNum = nodeNum;
        node.nodeId = MeshtasticProtocol::nodeIdToString(nodeNum);
        m_nodes[nodeNum] = node;
    }
}

void NodeManager::persistNode(uint32_t nodeNum)
{
    if (m_database && m_nodes.contains(nodeNum))
    {
        m_database->saveNode(m_nodes[nodeNum]);
    }
}

void NodeManager::setDatabase(Database *db)
{
    m_database = db;
}

void NodeManager::loadFromDatabase()
{
    if (!m_database)
        return;

    QList<NodeInfo> nodes = m_database->loadAllNodes();
    {
        QMutexLocker locker(&m_mutex);
        for (const NodeInfo &node : nodes)
        {
            if (node.nodeNum != 0)
            {
                m_nodes[node.nodeNum] = node;
            }
        }
    }

    if (!nodes.isEmpty())
    {
        emit nodesChanged(); // Immediate emit for bulk load
    }

    qDebug() << "Loaded" << nodes.size() << "nodes from database";
}

void NodeManager::saveToDatabase()
{
    if (!m_database)
        return;

    QList<NodeInfo> nodesCopy;
    {
        QMutexLocker locker(&m_mutex);
        nodesCopy = m_nodes.values();
    }
    m_database->saveNodes(nodesCopy);
    qDebug() << "Saved" << nodesCopy.size() << "nodes to database";
}

QString NodeManager::hwModelToString(int model)
{
    // Common hardware models from Meshtastic
    switch (model)
    {
    case 0:
        return "Unset";
    case 1:
        return "TLORA_V2";
    case 2:
        return "TLORA_V1";
    case 3:
        return "TLORA_V2_1_1P6";
    case 4:
        return "TBEAM";
    case 5:
        return "HELTEC_V2_0";
    case 6:
        return "TBEAM_V0P7";
    case 7:
        return "T_ECHO";
    case 8:
        return "TLORA_V1_1P3";
    case 9:
        return "RAK4631";
    case 10:
        return "HELTEC_V2_1";
    case 11:
        return "HELTEC_V1";
    case 12:
        return "LILYGO_TBEAM_S3_CORE";
    case 13:
        return "RAK11200";
    case 14:
        return "NANO_G1";
    case 15:
        return "TLORA_V2_1_1P8";
    case 16:
        return "TLORA_T3_S3";
    case 17:
        return "NANO_G1_EXPLORER";
    case 18:
        return "NANO_G2_ULTRA";
    case 19:
        return "LORA_TYPE";
    case 20:
        return "WIPHONE";
    case 21:
        return "WIO_WM1110";
    case 22:
        return "RAK2560";
    case 23:
        return "HELTEC_HRU_3601";
    case 25:
        return "STATION_G1";
    case 26:
        return "RAK11310";
    case 29:
        return "SENSELORA_RP2040";
    case 30:
        return "SENSELORA_S3";
    case 32:
        return "CANARYONE";
    case 33:
        return "RP2040_LORA";
    case 34:
        return "STATION_G2";
    case 35:
        return "LORA_RELAY_V1";
    case 36:
        return "NRF52840DK";
    case 37:
        return "PPR";
    case 38:
        return "GENIEBLOCKS";
    case 39:
        return "NRF52_UNKNOWN";
    case 40:
        return "PORTDUINO";
    case 41:
        return "ANDROID_SIM";
    case 42:
        return "DIY_V1";
    case 43:
        return "NRF52840_PCA10059";
    case 44:
        return "DR_DEV";
    case 45:
        return "M5STACK";
    case 46:
        return "HELTEC_V3";
    case 47:
        return "HELTEC_WSL_V3";
    case 48:
        return "BETAFPV_2400_TX";
    case 49:
        return "BETAFPV_900_NANO_TX";
    case 50:
        return "RPI_PICO";
    case 51:
        return "HELTEC_WIRELESS_TRACKER";
    case 52:
        return "HELTEC_WIRELESS_PAPER";
    case 53:
        return "T_DECK";
    case 54:
        return "T_WATCH_S3";
    case 55:
        return "PICOMPUTER_S3";
    case 56:
        return "HELTEC_HT62";
    case 57:
        return "EBYTE_ESP32_S3";
    case 58:
        return "ESP32_S3_PICO";
    case 59:
        return "CHATTER_2";
    case 60:
        return "HELTEC_WIRELESS_PAPER_V1_0";
    case 61:
        return "HELTEC_WIRELESS_TRACKER_V1_0";
    case 255:
        return "Private/Custom";
    default:
        return QString("Unknown(%1)").arg(model);
    }
}

QString NodeManager::roleToString(int role)
{
    switch (role)
    {
    case 0:
        return ""; // Unknown or default role - display nothing
    case 1:
        return "Client Mute";
    case 2:
        return "Router";
    case 3:
        return "Router Client";
    case 4:
        return "Repeater";
    case 5:
        return "Tracker";
    case 6:
        return "Sensor";
    case 7:
        return "TAK";
    case 8:
        return "Client Hidden";
    case 9:
        return "Lost and Found";
    case 10:
        return "TAK Tracker";
    default:
        return QString("Unknown(%1)").arg(role);
    }
}
