#include "TracerouteWidget.h"
#include "NodeManager.h"
#include "Database.h"
#include "MeshtasticProtocol.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDateTime>

TracerouteTableModel::TracerouteTableModel(NodeManager *nodeManager, QObject *parent)
    : QAbstractTableModel(parent), m_nodeManager(nodeManager)
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
            return QDateTime::fromMSecsSinceEpoch(tr.timestamp).toString("HH:mm:ss.zzz");
        case ColFrom:
            return formatNodeName(tr.from);
        case ColTo:
            return formatNodeName(tr.to);
        case ColRouteTo:
            return tr.routeTo.join(" → ");
        case ColRouteBack:
            return tr.routeBack.join(" → ");
        case ColSnrTo:
            return tr.snrTo.join(", ");
        case ColSnrBack:
            return tr.snrBack.join(", ");
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
    tr.from = packet.from;
    tr.to = packet.to;

    // Extract route and SNR data from packet fields
    if (packet.fields.contains("route"))
    {
        QVariantList routeList = packet.fields["route"].toList();
        for (const auto &node : routeList)
        {
            tr.routeTo.append(node.toString());
        }
    }

    if (packet.fields.contains("routeBack"))
    {
        QVariantList routeBackList = packet.fields["routeBack"].toList();
        for (const auto &node : routeBackList)
        {
            tr.routeBack.append(node.toString());
        }
    }

    if (packet.fields.contains("snrTowards"))
    {
        QVariantList snrList = packet.fields["snrTowards"].toList();
        for (const auto &snr : snrList)
        {
            tr.snrTo.append(QString::number(snr.toFloat(), 'f', 1));
        }
    }

    if (packet.fields.contains("snrBack"))
    {
        QVariantList snrBackList = packet.fields["snrBack"].toList();
        for (const auto &snr : snrBackList)
        {
            tr.snrBack.append(QString::number(snr.toFloat(), 'f', 1));
        }
    }

    beginInsertRows(QModelIndex(), 0, 0);
    m_traceroutes.prepend(tr);
    endInsertRows();

    // Limit traceroute count
    if (m_traceroutes.size() > MAX_TRACEROUTES)
    {
        beginRemoveRows(QModelIndex(), MAX_TRACEROUTES, m_traceroutes.size() - 1);
        while (m_traceroutes.size() > MAX_TRACEROUTES)
        {
            m_traceroutes.removeLast();
        }
        endRemoveRows();
    }
}

void TracerouteTableModel::clear()
{
    beginResetModel();
    m_traceroutes.clear();
    endResetModel();
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

// TracerouteWidget implementation
TracerouteWidget::TracerouteWidget(NodeManager *nodeManager, Database *database, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager), m_database(database)
{
    setupUI();
}

void TracerouteWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_model = new TracerouteTableModel(m_nodeManager, this);

    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(TracerouteTableModel::ColRouteTo, QHeaderView::Stretch);
    m_tableView->horizontalHeader()->setSectionResizeMode(TracerouteTableModel::ColRouteBack, QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(20);

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

void TracerouteWidget::clear()
{
    m_model->clear();
}
