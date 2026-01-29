#include "PacketListWidget.h"
#include "NodeManager.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QDateTime>
#include <QScrollBar>

// PacketTableModel implementation
PacketTableModel::PacketTableModel(NodeManager *nodeManager, QObject *parent)
    : QAbstractTableModel(parent), m_nodeManager(nodeManager)
{
}

int PacketTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_packets.size();
}

int PacketTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant PacketTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_packets.size())
    {
        return QVariant();
    }

    const auto &packet = m_packets[index.row()];

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case ColTime:
            return QDateTime::fromMSecsSinceEpoch(packet.timestamp).toString("HH:mm:ss.zzz");
        case ColType:
            return packet.typeName;
        case ColFrom:
            return formatNodeName(packet.from);
        case ColTo:
            return formatNodeName(packet.to);
        case ColPortNum:
            if (packet.type == MeshtasticProtocol::PacketType::PacketReceived)
            {
                return MeshtasticProtocol::portNumToString(packet.portNum);
            }
            return QString();
        case ColContent:
            return formatContent(packet);
        }
    }
    else if (role == Qt::ToolTipRole)
    {
        // Show raw fields as tooltip
        QString tooltip;
        for (auto it = packet.fields.begin(); it != packet.fields.end(); ++it)
        {
            tooltip += QString("%1: %2\n").arg(it.key(), it.value().toString());
        }
        return tooltip.trimmed();
    }
    else if (role == Qt::ForegroundRole)
    {
        // Color code by type
        if (packet.type == MeshtasticProtocol::PacketType::PacketReceived)
        {
            switch (packet.portNum)
            {
            case MeshtasticProtocol::PortNum::TextMessage:
                return QColor(Qt::darkGreen);
            case MeshtasticProtocol::PortNum::Position:
                return QColor(Qt::darkBlue);
            case MeshtasticProtocol::PortNum::Telemetry:
                return QColor(Qt::darkCyan);
            case MeshtasticProtocol::PortNum::NodeInfo:
                return QColor(Qt::darkMagenta);
            default:
                break;
            }
        }
    }

    return QVariant();
}

QVariant PacketTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QVariant();
    }

    switch (section)
    {
    case ColTime:
        return "Time";
    case ColType:
        return "Type";
    case ColFrom:
        return "From";
    case ColTo:
        return "To";
    case ColPortNum:
        return "Port";
    case ColContent:
        return "Content";
    }

    return QVariant();
}

void PacketTableModel::addPacket(const MeshtasticProtocol::DecodedPacket &packet)
{
    // Prepend new packets to show most recent at top
    beginInsertRows(QModelIndex(), 0, 0);
    m_packets.prepend(packet);
    endInsertRows();

    // Limit packet count
    if (m_packets.size() > MAX_PACKETS)
    {
        beginRemoveRows(QModelIndex(), MAX_PACKETS, m_packets.size() - 1);
        while (m_packets.size() > MAX_PACKETS)
        {
            m_packets.removeLast();
        }
        endRemoveRows();
    }
}

void PacketTableModel::clear()
{
    beginResetModel();
    m_packets.clear();
    endResetModel();
}

const MeshtasticProtocol::DecodedPacket &PacketTableModel::packetAt(int row) const
{
    return m_packets[row];
}

QString PacketTableModel::formatNodeName(uint32_t nodeNum) const
{
    if (nodeNum == 0)
    {
        return QString();
    }
    if (nodeNum == 0xFFFFFFFF)
    {
        return "Broadcast";
    }

    if (m_nodeManager && m_nodeManager->hasNode(nodeNum))
    {
        NodeInfo node = m_nodeManager->getNode(nodeNum);
        if (!node.shortName.isEmpty())
        {
            return node.shortName;
        }
        if (!node.longName.isEmpty())
        {
            return node.longName;
        }
    }

    return MeshtasticProtocol::nodeIdToString(nodeNum);
}

QString PacketTableModel::formatContent(const MeshtasticProtocol::DecodedPacket &packet) const
{
    const QVariantMap &f = packet.fields;

    switch (packet.type)
    {
    case MeshtasticProtocol::PacketType::PacketReceived:
        switch (packet.portNum)
        {
        case MeshtasticProtocol::PortNum::TextMessage:
            return f.value("text").toString();

        case MeshtasticProtocol::PortNum::Position:
            if (f.contains("latitude") && f.contains("longitude"))
            {
                return QString("Lat: %1, Lon: %2, Alt: %3m")
                    .arg(f["latitude"].toDouble(), 0, 'f', 6)
                    .arg(f["longitude"].toDouble(), 0, 'f', 6)
                    .arg(f.value("altitude", 0).toInt());
            }
            break;

        case MeshtasticProtocol::PortNum::Telemetry:
            if (f.contains("telemetryType"))
            {
                QString type = f["telemetryType"].toString();
                if (type == "device")
                {
                    return QString("Battery: %1%, Voltage: %2V, ChUtil: %3%")
                        .arg(f.value("batteryLevel", 0).toInt())
                        .arg(f.value("voltage", 0).toFloat(), 0, 'f', 2)
                        .arg(f.value("channelUtilization", 0).toFloat(), 0, 'f', 1);
                }
                else if (type == "environment")
                {
                    return QString("Temp: %1°C, Humidity: %2%")
                        .arg(f.value("temperature", 0).toFloat(), 0, 'f', 1)
                        .arg(f.value("relativeHumidity", 0).toFloat(), 0, 'f', 1);
                }
            }
            break;

        case MeshtasticProtocol::PortNum::NodeInfo:
            if (f.contains("longName"))
            {
                return QString("%1 (%2)")
                    .arg(f["longName"].toString())
                    .arg(f.value("shortName").toString());
            }
            break;

        case MeshtasticProtocol::PortNum::Traceroute:
            if (f.contains("route"))
            {
                return QString("Route: %1").arg(f["route"].toStringList().join(" -> "));
            }
            break;

        case MeshtasticProtocol::PortNum::Routing:
            if (f.contains("errorReason"))
            {
                int errorReason = f["errorReason"].toInt();
                QString status;
                switch (errorReason)
                {
                case 0:
                    status = "ACK";
                    break;
                case 1:
                    status = "NO_ROUTE";
                    break;
                case 2:
                    status = "GOT_NAK";
                    break;
                case 3:
                    status = "TIMEOUT";
                    break;
                case 5:
                    status = "MAX_RETRANSMIT";
                    break;
                case 8:
                    status = "NO_RESPONSE";
                    break;
                default:
                    status = QString("ERROR_%1").arg(errorReason);
                    break;
                }
                return QString("Routing: %1 (packet %2)")
                    .arg(status)
                    .arg(f.value("packetId").toUInt());
            }
            break;

        default:
            if (f.contains("encrypted"))
            {
                return "[Encrypted]";
            }
            break;
        }
        break;

    case MeshtasticProtocol::PacketType::MyInfo:
        return QString("Node: %1, Reboots: %2")
            .arg(MeshtasticProtocol::nodeIdToString(f.value("myNodeNum").toUInt()))
            .arg(f.value("rebootCount", 0).toInt());

    case MeshtasticProtocol::PacketType::NodeInfo:
        if (f.contains("longName"))
        {
            return QString("%1 (%2) - %3")
                .arg(f["longName"].toString())
                .arg(f.value("shortName").toString())
                .arg(f.value("hwModel").toString());
        }
        break;

    case MeshtasticProtocol::PacketType::Channel:
        return QString("Channel %1: %2")
            .arg(f.value("index", 0).toInt())
            .arg(f.value("channelName", "").toString());

    case MeshtasticProtocol::PacketType::Metadata:
        return QString("Firmware: %1").arg(f.value("firmwareVersion").toString());

    case MeshtasticProtocol::PacketType::QueueStatus:
        return QString("Queue: %1 free").arg(f.value("free", 0).toInt());

    default:
        break;
    }

    // Fallback: show first few fields
    QStringList parts;
    int count = 0;
    for (auto it = f.begin(); it != f.end() && count < 3; ++it, ++count)
    {
        if (it.key() != "id" && it.key() != "portnum")
        {
            parts << QString("%1=%2").arg(it.key(), it.value().toString());
        }
    }
    return parts.join(", ");
}

// PacketFilterModel implementation
PacketFilterModel::PacketFilterModel(NodeManager *nodeManager, QObject *parent)
    : QSortFilterProxyModel(parent), m_nodeManager(nodeManager)
{
}

void PacketFilterModel::setTypeFilter(const QString &type)
{
    m_typeFilter = type;
    invalidateFilter();
}

void PacketFilterModel::setPortNumFilter(const QString &portNum)
{
    m_portNumFilter = portNum;
    invalidateFilter();
}

void PacketFilterModel::setHideLocalDevicePackets(bool hide)
{
    m_hideLocalDevicePackets = hide;
    invalidateFilter();
}

bool PacketFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);

    PacketTableModel *model = qobject_cast<PacketTableModel *>(sourceModel());
    if (!model)
        return true;

    const auto &packet = model->packetAt(sourceRow);

    // Hide local device packets filter
    if (m_hideLocalDevicePackets)
    {
        // Only show actual mesh packets (PacketReceived), not config/status from local device
        if (packet.type != MeshtasticProtocol::PacketType::PacketReceived)
        {
            return false;
        }
        // Also hide packets FROM our own node (device reporting its own telemetry/position via serial)
        if (m_nodeManager && packet.from == m_nodeManager->myNodeNum())
        {
            return false;
        }
    }

    if (!m_typeFilter.isEmpty() && m_typeFilter != "All")
    {
        if (packet.typeName != m_typeFilter)
        {
            return false;
        }
    }

    if (!m_portNumFilter.isEmpty() && m_portNumFilter != "All")
    {
        if (MeshtasticProtocol::portNumToString(packet.portNum) != m_portNumFilter)
        {
            return false;
        }
    }

    return true;
}

// PacketListWidget implementation
PacketListWidget::PacketListWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager)
{
    setupUI();
}

void PacketListWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Filter bar
    QHBoxLayout *filterLayout = new QHBoxLayout;

    filterLayout->addWidget(new QLabel("Type:"));
    m_typeFilter = new QComboBox;
    m_typeFilter->addItems({"All", "Packet", "MyInfo", "NodeInfo", "Channel", "Config", "Metadata"});
    filterLayout->addWidget(m_typeFilter);

    filterLayout->addWidget(new QLabel("Port:"));
    m_portNumFilter = new QComboBox;
    m_portNumFilter->addItems({"All", "TEXT_MESSAGE", "POSITION", "NODEINFO", "TELEMETRY",
                               "ROUTING", "TRACEROUTE", "ADMIN"});
    filterLayout->addWidget(m_portNumFilter);

    filterLayout->addStretch();
    layout->addLayout(filterLayout);

    // Table view
    m_model = new PacketTableModel(m_nodeManager, this);
    m_filterModel = new PacketFilterModel(m_nodeManager, this);
    m_filterModel->setSourceModel(m_model);

    // Apply initial setting and connect for changes
    m_filterModel->setHideLocalDevicePackets(AppSettings::instance()->hideLocalDevicePackets());
    connect(AppSettings::instance(), &AppSettings::settingChanged,
            this, [this](const QString &key, const QVariant &value)
            {
                if (key == "packets/hide_local_device") {
                    m_filterModel->setHideLocalDevicePackets(value.toBool());
                } });

    m_tableView = new QTableView;
    m_tableView->setModel(m_filterModel);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setSectionResizeMode(PacketTableModel::ColContent, QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(20);

    layout->addWidget(m_tableView);

    // Connect filters
    connect(m_typeFilter, &QComboBox::currentTextChanged,
            m_filterModel, &PacketFilterModel::setTypeFilter);
    connect(m_portNumFilter, &QComboBox::currentTextChanged,
            m_filterModel, &PacketFilterModel::setPortNumFilter);

    // Connect selection
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &PacketListWidget::onRowSelected);
}

void PacketListWidget::addPacket(const MeshtasticProtocol::DecodedPacket &packet)
{
    m_model->addPacket(packet);
}

void PacketListWidget::clear()
{
    m_model->clear();
}

void PacketListWidget::onRowSelected(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid())
        return;

    QModelIndex sourceIndex = m_filterModel->mapToSource(current);
    if (sourceIndex.isValid())
    {
        emit packetSelected(m_model->packetAt(sourceIndex.row()));
    }
}
