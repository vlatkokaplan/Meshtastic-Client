#include "TracerouteWidget.h"
#include "NodeManager.h"
#include "Database.h"
#include "MeshtasticProtocol.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

TracerouteTableModel::TracerouteTableModel(NodeManager *nodeManager, Database *database, QObject *parent)
    : QAbstractTableModel(parent), m_nodeManager(nodeManager), m_database(database)
{
}

int TracerouteTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_traceroutes.size();
}

int TracerouteTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant TracerouteTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_traceroutes.size())
        return QVariant();

    const auto &tr = m_traceroutes[index.row()];

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case ColTime:
            return QDateTime::fromMSecsSinceEpoch(tr.timestamp).toString("HH:mm:ss");
        case ColFrom:
            return formatNodeName(tr.from);
        case ColTo:
            return formatNodeName(tr.to);
        case ColRouteTo:
        {
            // Build complete path: From → [hop (dist)] → To
            QStringList fullPath;
            fullPath.append(formatNodeName(tr.from));
            for (int i = 0; i < tr.routeTo.size(); ++i)
            {
                QString hopName = formatNodeName(MeshtasticProtocol::nodeIdFromString(tr.routeTo[i]));
                if (i < tr.distancesTo.size() && tr.distancesTo[i] > 0)
                    fullPath.append(QString("%1 (%2km)").arg(hopName).arg(tr.distancesTo[i], 0, 'f', 1));
                else
                    fullPath.append(hopName);
            }
            // Last hop to destination
            int lastIdx = tr.distancesTo.size() - 1;
            QString toName = formatNodeName(tr.to);
            if (lastIdx >= 0 && tr.distancesTo[lastIdx] > 0)
                fullPath.append(QString("%1 (%2km)").arg(toName).arg(tr.distancesTo[lastIdx], 0, 'f', 1));
            else
                fullPath.append(toName);

            return fullPath.join(" → ");
        }
        case ColRouteBack:
        {
            // Build complete return path: To → [hop (dist)] → From
            QStringList fullPath;
            fullPath.append(formatNodeName(tr.to));
            for (int i = 0; i < tr.routeBack.size(); ++i)
            {
                QString hopName = formatNodeName(MeshtasticProtocol::nodeIdFromString(tr.routeBack[i]));
                if (i < tr.distancesBack.size() && tr.distancesBack[i] > 0)
                    fullPath.append(QString("%1 (%2km)").arg(hopName).arg(tr.distancesBack[i], 0, 'f', 1));
                else
                    fullPath.append(hopName);
            }
            // Last hop back to source
            int lastIdx = tr.distancesBack.size() - 1;
            QString fromName = formatNodeName(tr.from);
            if (lastIdx >= 0 && tr.distancesBack[lastIdx] > 0)
                fullPath.append(QString("%1 (%2km)").arg(fromName).arg(tr.distancesBack[lastIdx], 0, 'f', 1));
            else
                fullPath.append(fromName);

            return fullPath.join(" → ");
        }
        case ColSnrTo:
            // Simple SNR display - values correspond to each hop
            return tr.snrTo.join(" → ");
        case ColSnrBack:
            return tr.snrBack.join(" → ");
        }
    }

    return QVariant();
}

QVariant TracerouteTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section)
    {
    case ColTime:
        return "Time";
    case ColFrom:
        return "From";
    case ColTo:
        return "To";
    case ColRouteTo:
        return "Route To";
    case ColRouteBack:
        return "Route Back";
    case ColSnrTo:
        return "SNR To (dB)";
    case ColSnrBack:
        return "SNR Back (dB)";
    }

    return QVariant();
}

void TracerouteTableModel::addTraceroute(const MeshtasticProtocol::DecodedPacket &packet)
{
    Traceroute tr;
    tr.timestamp = packet.timestamp;
    // By default, packet.from is the destination node, packet.to is the requesting node
    tr.from = packet.to;
    tr.to = packet.from;

    // Extract basic route and SNR data
    if (packet.fields.contains("routeBack"))
    {
        QVariantList routeList = packet.fields["routeBack"].toList();
        for (const auto &node : routeList)
            tr.routeTo.append(node.toString());
    }

    if (packet.fields.contains("route"))
    {
        QVariantList routeBackList = packet.fields["route"].toList();
        for (const auto &node : routeBackList)
            tr.routeBack.append(node.toString());
    }

    if (packet.fields.contains("snrBack"))
    {
        QVariantList snrList = packet.fields["snrBack"].toList();
        for (const auto &snr : snrList)
            tr.snrTo.append(QString::number(snr.toFloat(), 'f', 1));
    }

    if (packet.fields.contains("snrTowards"))
    {
        QVariantList snrBackList = packet.fields["snrTowards"].toList();
        for (const auto &snr : snrBackList)
            tr.snrBack.append(QString::number(snr.toFloat(), 'f', 1));
    }

    // Calculate distances for each hop (Inspiration from Malla: use historical locations)
    if (m_database) {
        auto getPos = [&](uint32_t nodeNum) {
            return m_database->loadPositionAt(nodeNum, tr.timestamp / 1000); // loadPositionAt uses seconds
        };

        // Forward path distance calculation
        Database::PositionRecord prevPos = getPos(tr.from);
        for (const QString &nodeId : tr.routeTo) {
            uint32_t hopNode = MeshtasticProtocol::nodeIdFromString(nodeId);
            Database::PositionRecord hopPos = getPos(hopNode);
            float dist = calculateDistance(prevPos.latitude, prevPos.longitude, hopPos.latitude, hopPos.longitude);
            tr.distancesTo.append(dist);
            tr.totalDistance += dist;
            prevPos = hopPos;
        }
        Database::PositionRecord destPos = getPos(tr.to);
        float lastDist = calculateDistance(prevPos.latitude, prevPos.longitude, destPos.latitude, destPos.longitude);
        tr.distancesTo.append(lastDist);
        tr.totalDistance += lastDist;

        // Return path distance calculation
        prevPos = destPos;
        for (const QString &nodeId : tr.routeBack) {
            uint32_t hopNode = MeshtasticProtocol::nodeIdFromString(nodeId);
            Database::PositionRecord hopPos = getPos(hopNode);
            float dist = calculateDistance(prevPos.latitude, prevPos.longitude, hopPos.latitude, hopPos.longitude);
            tr.distancesBack.append(dist);
            prevPos = hopPos;
        }
        Database::PositionRecord finalPos = getPos(tr.from);
        tr.distancesBack.append(calculateDistance(prevPos.latitude, prevPos.longitude, finalPos.latitude, finalPos.longitude));
    }

    beginInsertRows(QModelIndex(), 0, 0);
    m_traceroutes.prepend(tr);
    endInsertRows();

    // Limit traceroute count
    if (m_traceroutes.size() > MAX_TRACEROUTES)
    {
        beginRemoveRows(QModelIndex(), MAX_TRACEROUTES, m_traceroutes.size() - 1);
        while (m_traceroutes.size() > MAX_TRACEROUTES)
            m_traceroutes.removeLast();
        endRemoveRows();
    }
}

void TracerouteTableModel::addTracerouteFromDb(uint32_t from, uint32_t to, quint64 timestamp,
                                               const QStringList &routeTo, const QStringList &routeBack,
                                               const QStringList &snrTo, const QStringList &snrBack)
{
    Traceroute tr;
    tr.timestamp = timestamp;
    tr.from = from;
    tr.to = to;
    tr.routeTo = routeTo;
    tr.routeBack = routeBack;
    tr.snrTo = snrTo;
    tr.snrBack = snrBack;

    // Recalculate distances from database positions
    if (m_database) {
        auto getPos = [&](uint32_t nodeNum) {
            return m_database->loadPositionAt(nodeNum, tr.timestamp / 1000);
        };

        Database::PositionRecord prevPos = getPos(tr.from);
        for (const QString &nodeId : tr.routeTo) {
            uint32_t hopNode = MeshtasticProtocol::nodeIdFromString(nodeId);
            Database::PositionRecord hopPos = getPos(hopNode);
            float dist = calculateDistance(prevPos.latitude, prevPos.longitude, hopPos.latitude, hopPos.longitude);
            tr.distancesTo.append(dist);
            tr.totalDistance += dist;
            prevPos = hopPos;
        }
        Database::PositionRecord destPos = getPos(tr.to);
        float lastDist = calculateDistance(prevPos.latitude, prevPos.longitude, destPos.latitude, destPos.longitude);
        tr.distancesTo.append(lastDist);
        tr.totalDistance += lastDist;

        prevPos = destPos;
        for (const QString &nodeId : tr.routeBack) {
            uint32_t hopNode = MeshtasticProtocol::nodeIdFromString(nodeId);
            Database::PositionRecord hopPos = getPos(hopNode);
            float dist = calculateDistance(prevPos.latitude, prevPos.longitude, hopPos.latitude, hopPos.longitude);
            tr.distancesBack.append(dist);
            prevPos = hopPos;
        }
        Database::PositionRecord finalPos = getPos(tr.from);
        tr.distancesBack.append(calculateDistance(prevPos.latitude, prevPos.longitude, finalPos.latitude, finalPos.longitude));
    }

    beginInsertRows(QModelIndex(), m_traceroutes.size(), m_traceroutes.size());
    m_traceroutes.append(tr);
    endInsertRows();
}

void TracerouteTableModel::clear()
{
    beginResetModel();
    m_traceroutes.clear();
    endResetModel();
}

void TracerouteTableModel::setDatabase(Database *database)
{
    m_database = database;
}

QString TracerouteTableModel::formatNodeName(uint32_t nodeNum) const
{
    if (nodeNum == 0)
        return QString();

    if (m_nodeManager && m_nodeManager->hasNode(nodeNum))
    {
        NodeInfo node = m_nodeManager->getNode(nodeNum);
        if (!node.shortName.isEmpty())
            return node.shortName;
        if (!node.longName.isEmpty())
            return node.longName;
    }

    return MeshtasticProtocol::nodeIdToString(nodeNum);
}

float TracerouteTableModel::calculateDistance(double lat1, double lon1, double lat2, double lon2) const
{
    if (lat1 == 0.0 || lon1 == 0.0 || lat2 == 0.0 || lon2 == 0.0)
        return 0.0f;

    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double rLat1 = lat1 * M_PI / 180.0;
    double rLat2 = lat2 * M_PI / 180.0;

    double a = sin(dLat / 2) * sin(dLat / 2) +
               sin(dLon / 2) * sin(dLon / 2) * cos(rLat1) * cos(rLat2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return 6371.0f * c; // Earth radius in km
}

TracerouteTableModel::TracerouteData TracerouteTableModel::getTraceroute(int row) const
{
    TracerouteData data;
    if (row >= 0 && row < m_traceroutes.size())
    {
        const auto &tr = m_traceroutes[row];
        data.timestamp = tr.timestamp;
        data.from = tr.from;
        data.to = tr.to;
        data.routeTo = tr.routeTo;
        data.routeBack = tr.routeBack;
        data.snrTo = tr.snrTo;
        data.snrBack = tr.snrBack;
        data.distancesTo = tr.distancesTo;
        data.distancesBack = tr.distancesBack;
        data.totalDistance = tr.totalDistance;
    }
    return data;
}

// TracerouteWidget implementation
TracerouteWidget::TracerouteWidget(NodeManager *nodeManager, Database *database, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager), m_database(database)
{
    setupUI();

    // Connect selection change
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TracerouteWidget::onSelectionChanged);
}

void TracerouteWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_model = new TracerouteTableModel(m_nodeManager, m_database, this);

    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->setWordWrap(false);

    // All columns resize to fit their content
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setStretchLastSection(false);

    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(24);

    layout->addWidget(m_tableView);
}

void TracerouteWidget::addTraceroute(const MeshtasticProtocol::DecodedPacket &packet)
{
    m_model->addTraceroute(packet);

    // Save to database if available
    if (m_database)
    {
        Database::Traceroute dbTracer;
        dbTracer.fromNode = packet.from;
        dbTracer.toNode = packet.to;
        dbTracer.timestamp = QDateTime::fromMSecsSinceEpoch(packet.timestamp);
        dbTracer.isResponse = true; // We're receiving a response packet

        // Extract route and SNR data
        if (packet.fields.contains("route"))
        {
            QVariantList routeList = packet.fields["route"].toList();
            for (const auto &node : routeList)
            {
                dbTracer.routeTo.append(node.toString());
            }
        }

        if (packet.fields.contains("routeBack"))
        {
            QVariantList routeBackList = packet.fields["routeBack"].toList();
            for (const auto &node : routeBackList)
            {
                dbTracer.routeBack.append(node.toString());
            }
        }

        if (packet.fields.contains("snrTowards"))
        {
            QVariantList snrList = packet.fields["snrTowards"].toList();
            for (const auto &snr : snrList)
            {
                dbTracer.snrTo.append(QString::number(snr.toFloat(), 'f', 1));
            }
        }

        if (packet.fields.contains("snrBack"))
        {
            QVariantList snrBackList = packet.fields["snrBack"].toList();
            for (const auto &snr : snrBackList)
            {
                dbTracer.snrBack.append(QString::number(snr.toFloat(), 'f', 1));
            }
        }

        m_database->saveTraceroute(dbTracer);
    }
}

void TracerouteWidget::loadFromDatabase()
{
    if (!m_database)
        return;

    m_model->clear();

    QList<Database::Traceroute> traceroutes = m_database->loadTraceroutes(100, 0);

    // DB returns newest first (ORDER BY timestamp DESC), so iterate forward
    for (int i = 0; i < traceroutes.size(); i++)
    {
        const auto &tr = traceroutes[i];
        m_model->addTracerouteFromDb(
            tr.fromNode, tr.toNode,
            tr.timestamp.toMSecsSinceEpoch(),
            tr.routeTo, tr.routeBack,
            tr.snrTo, tr.snrBack
        );
    }
}

void TracerouteWidget::clear()
{
    m_model->clear();
}

void TracerouteWidget::setDatabase(Database *database)
{
    m_database = database;
    m_model->setDatabase(database);
    if (database)
        loadFromDatabase();
}

void TracerouteWidget::onSelectionChanged()
{
    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty())
        return;

    int row = selected.first().row();
    auto data = m_model->getTraceroute(row);

    emit tracerouteSelected(data.from, data.to);
}

QList<TracerouteWidget::RouteNode> TracerouteWidget::getSelectedRoute() const
{
    QList<RouteNode> route;

    QModelIndexList selected = m_tableView->selectionModel()->selectedRows();
    if (selected.isEmpty())
        return route;

    int row = selected.first().row();
    auto data = m_model->getTraceroute(row);

    // Helper for historical position lookup
    auto getHistoricalPos = [&](uint32_t nodeNum) {
        if (m_database) {
            Database::PositionRecord rec = m_database->loadPositionAt(nodeNum, data.timestamp / 1000);
            if (rec.latitude != 0.0 || rec.longitude != 0.0) {
                return QPair<double, double>(rec.latitude, rec.longitude);
            }
        }
        // Fallback to current position if no historical data
        if (m_nodeManager && m_nodeManager->hasNode(nodeNum)) {
            NodeInfo info = m_nodeManager->getNode(nodeNum);
            return QPair<double, double>(info.latitude, info.longitude);
        }
        return QPair<double, double>(0.0, 0.0);
    };

    // Build route: From -> [hops] -> To
    // Add starting node
    RouteNode startNode;
    startNode.nodeNum = data.from;
    startNode.name = m_nodeManager->hasNode(data.from) ?
        (m_nodeManager->getNode(data.from).shortName.isEmpty() ?
         m_nodeManager->getNode(data.from).longName :
         m_nodeManager->getNode(data.from).shortName) :
        MeshtasticProtocol::nodeIdToString(data.from);
    auto startPos = getHistoricalPos(data.from);
    startNode.latitude = startPos.first;
    startNode.longitude = startPos.second;
    startNode.snr = 0;
    route.append(startNode);

    // Add intermediate hops
    for (int i = 0; i < data.routeTo.size(); i++)
    {
        RouteNode hopNode;
        hopNode.nodeNum = MeshtasticProtocol::nodeIdFromString(data.routeTo[i]);
        hopNode.name = m_nodeManager->hasNode(hopNode.nodeNum) ?
            (m_nodeManager->getNode(hopNode.nodeNum).shortName.isEmpty() ?
             m_nodeManager->getNode(hopNode.nodeNum).longName :
             m_nodeManager->getNode(hopNode.nodeNum).shortName) :
            data.routeTo[i];
        
        auto hopPos = getHistoricalPos(hopNode.nodeNum);
        hopNode.latitude = hopPos.first;
        hopNode.longitude = hopPos.second;
        
        hopNode.snr = (i < data.snrTo.size()) ? data.snrTo[i].toFloat() : 0;
        route.append(hopNode);
    }

    // Add destination node
    RouteNode endNode;
    endNode.nodeNum = data.to;
    endNode.name = m_nodeManager->hasNode(data.to) ?
        (m_nodeManager->getNode(data.to).shortName.isEmpty() ?
         m_nodeManager->getNode(data.to).longName :
         m_nodeManager->getNode(data.to).shortName) :
        MeshtasticProtocol::nodeIdToString(data.to);
    
    auto endPos = getHistoricalPos(data.to);
    endNode.latitude = endPos.first;
    endNode.longitude = endPos.second;
    
    endNode.snr = 0;
    route.append(endNode);

    return route;
}
