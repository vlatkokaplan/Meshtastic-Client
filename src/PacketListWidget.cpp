#include "PacketListWidget.h"
#include <algorithm>
#include <QSignalBlocker>
#include <QJsonObject>
#include <QJsonDocument>
#include "Database.h"
#include "NodeManager.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QDateTime>
#include <QScrollBar>
#include <QPushButton>
#include <QSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

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
        case ColFromAddr:
            return formatNodeId(packet.from);
        case ColTo:
            return formatNodeName(packet.to);
        case ColToAddr:
            return formatNodeId(packet.to);
        case ColChannel:
            if (packet.type == MeshtasticProtocol::PacketType::PacketReceived &&
                packet.channelIndex >= 0 && packet.channelIndex <= 7)
            {
                return packet.channelIndex;
            }
            return QVariant();
        case ColPortNum:
            if (packet.type == MeshtasticProtocol::PacketType::PacketReceived)
            {
                return MeshtasticProtocol::portNumToString(packet.portNum);
            }
            return QString();
        case ColKey:
            if (packet.fields.contains("foundKey"))
            {
                return packet.fields.value("foundKey").toString();
            }
            return QVariant();
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
    case ColFromAddr:
        return "From ID";
    case ColTo:
        return "To";
    case ColToAddr:
        return "To ID";
    case ColChannel:
        return "Ch";
    case ColPortNum:
        return "Port";
    case ColKey:
        return "Key";
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

QString PacketTableModel::formatNodeId(uint32_t nodeNum) const
{
    if (nodeNum == 0)
    {
        return QString();
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
                // Only fields the sender set are present; list those
                QStringList parts;
                auto add = [&](const char *key, const QString &fmt, int decimals = 1) {
                    if (f.contains(key))
                        parts << fmt.arg(f.value(key).toDouble(), 0, 'f', decimals);
                };
                if (type == "device")
                {
                    const int battery = f.value("batteryLevel", -1).toInt();
                    if (battery > 100)
                        parts << "Powered";
                    else if (battery >= 0)
                        parts << QString("Battery: %1%").arg(battery);
                    add("voltage", "Voltage: %1V", 2);
                    add("channelUtilization", "ChUtil: %1%");
                    add("airUtilTx", "AirTx: %1%");
                }
                else if (type == "environment")
                {
                    add("temperature", "Temp: %1°C");
                    add("relativeHumidity", "Humidity: %1%");
                    add("barometricPressure", "Pressure: %1 hPa");
                    add("iaq", "IAQ: %1", 0);
                    add("lux", "Light: %1 lx", 0);
                    add("windSpeed", "Wind: %1 m/s");
                }
                else if (type == "airQuality")
                {
                    add("pm25Standard", "PM2.5: %1 µg/m³", 0);
                    add("pm100Standard", "PM10: %1 µg/m³", 0);
                    add("co2", "CO₂: %1 ppm", 0);
                }
                else if (type == "power")
                {
                    for (int ch = 1; ch <= 8; ++ch)
                    {
                        const QString v = QString("ch%1Voltage").arg(ch);
                        const QString c = QString("ch%1Current").arg(ch);
                        if (f.contains(v) || f.contains(c))
                            parts << QString("Ch%1: %2V %3mA").arg(ch)
                                         .arg(f.value(v).toDouble(), 0, 'f', 2)
                                         .arg(f.value(c).toDouble(), 0, 'f', 0);
                    }
                }
                else if (type == "localStats")
                {
                    add("numOnlineNodes", "Online: %1", 0);
                    add("numPacketsTx", "Tx: %1", 0);
                    add("numPacketsRx", "Rx: %1", 0);
                    add("numPacketsRxBad", "Bad: %1", 0);
                    add("noiseFloor", "Noise: %1 dBm", 0);
                }
                else if (type == "health")
                {
                    add("heartBpm", "Heart: %1 bpm", 0);
                    add("spO2", "SpO₂: %1%", 0);
                    add("temperature", "Body: %1°C");
                }
                else if (type == "host")
                {
                    add("load1", "Load: %1", 0);
                    add("freememBytes", "Free: %1 B", 0);
                }
                if (!parts.isEmpty())
                    return QString("[%1] %2").arg(type, parts.join(", "));
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
            if (f.contains("decrypted"))
            {
                return QString("[Decrypted] %1").arg(f.value("payloadHex").toString().left(32));
            }
            if (f.contains("decryptFailed"))
            {
                return "[Decrypt Failed - wrong key?]";
            }
            if (f.contains("pkiEncrypted"))
            {
                return "[Encrypted DM - public key]";
            }
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

void PacketFilterModel::setChannelFilter(int channel)
{
    m_channelFilter = channel;
    invalidateFilter();
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

    if (m_channelFilter >= 0 && packet.channelIndex != m_channelFilter)
    {
        return false;
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

    filterLayout->addWidget(new QLabel("Ch:"));
    m_channelFilter = new QComboBox;
    m_channelFilter->setToolTip(
        "Filter by the channel a packet arrived on. Packets we could decode carry "
        "a channel index; ones we hold no key for carry only the channel hash, "
        "which is shown as such.");
    m_channelFilter->addItem("All", -1);
    m_channelFilter->setMinimumWidth(130);
    filterLayout->addWidget(m_channelFilter);

    filterLayout->addStretch();

    // Export button
    filterLayout->addWidget(new QLabel("Export last:"));
    QSpinBox *countSpinBox = new QSpinBox;
    countSpinBox->setRange(10, 10000);
    countSpinBox->setValue(100);
    countSpinBox->setSuffix(" packets");
    countSpinBox->setObjectName("exportCountSpinBox");
    filterLayout->addWidget(countSpinBox);

    QPushButton *exportBtn = new QPushButton("Dump to File");
    connect(exportBtn, &QPushButton::clicked, this, &PacketListWidget::onDumpPackets);
    filterLayout->addWidget(exportBtn);

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
    connect(m_channelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
                m_filterModel->setChannelFilter(m_channelFilter->itemData(i).toInt());
            });
    connect(m_portNumFilter, &QComboBox::currentTextChanged,
            m_filterModel, &PacketFilterModel::setPortNumFilter);

    // Connect selection
    connect(m_tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &PacketListWidget::onRowSelected);
}

void PacketListWidget::addPacket(const MeshtasticProtocol::DecodedPacket &packet)
{
    m_model->addPacket(packet);
    if (!m_knownChannels.contains(packet.channelIndex))
        rebuildChannelFilter();
}

void PacketTableModel::setPackets(const QList<MeshtasticProtocol::DecodedPacket> &packets)
{
    beginResetModel();
    m_packets = packets;
    if (m_packets.size() > MAX_PACKETS)
        m_packets = m_packets.mid(0, MAX_PACKETS);
    endResetModel();
}

void PacketListWidget::clear()
{
    m_model->clear();
}

void PacketListWidget::setChannelNames(const QMap<int, QString> &names)
{
    if (m_channelNames == names)
        return;
    m_channelNames = names;
    rebuildChannelFilter();
}

// The combo offers only the channels actually present, so it never lists
// options that would match nothing. Rebuilt on change rather than on every
// packet, and the current selection is kept across rebuilds.
void PacketListWidget::rebuildChannelFilter()
{
    if (!m_channelFilter)
        return;

    QList<int> present;
    for (int row = 0; row < m_model->rowCount(); ++row)
    {
        const int ch = m_model->packetAt(row).channelIndex;
        if (!present.contains(ch))
            present.append(ch);
    }
    std::sort(present.begin(), present.end());

    if (present == m_knownChannels)
        return;
    m_knownChannels = present;

    const int previous = m_channelFilter->currentData().toInt();

    QSignalBlocker block(m_channelFilter);
    m_channelFilter->clear();
    m_channelFilter->addItem("All", -1);

    for (int ch : present)
    {
        QString label;
        if (m_channelNames.contains(ch))
            label = QString("%1 - %2").arg(ch).arg(m_channelNames.value(ch));
        else if (ch < 8)
            label = QString("Channel %1").arg(ch);
        else
            // Not an index: a channel we hold no key for, identified only by
            // the hash it arrived under.
            label = QString("hash %1 - not ours").arg(ch);

        m_channelFilter->addItem(label, ch);
    }

    const int restore = m_channelFilter->findData(previous);
    m_channelFilter->setCurrentIndex(restore >= 0 ? restore : 0);
    if (restore < 0)
        m_filterModel->setChannelFilter(-1);   // the old choice is gone
}

void PacketListWidget::setDatabase(Database *db)
{
    m_database = db;
    if (m_database)
        loadFromDatabase();
    else
        m_model->clear();
}

// Rebuilds a DecodedPacket from a stored row. Everything the list shows is
// recoverable: the fields map was serialised to JSON when the packet arrived.
void PacketListWidget::loadFromDatabase()
{
    if (!m_database || !m_database->isOpen())
        return;

    const auto rows = m_database->loadRecentPackets();
    QList<MeshtasticProtocol::DecodedPacket> packets;
    packets.reserve(rows.size());

    for (const auto &rec : rows)
    {
        MeshtasticProtocol::DecodedPacket p;
        p.type = static_cast<MeshtasticProtocol::PacketType>(rec.packetType);
        p.from = rec.fromNode;
        p.to = rec.toNode;
        p.portNum = static_cast<MeshtasticProtocol::PortNum>(rec.portNum);
        p.channelIndex = rec.channel;
        p.timestamp = rec.timestamp;
        p.typeName = rec.typeName;

        if (!rec.fieldsJson.isEmpty())
        {
            QJsonParseError err;
            const auto doc = QJsonDocument::fromJson(rec.fieldsJson.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                p.fields = doc.object().toVariantMap();
        }
        packets.append(p);
    }

    // loadRecentPackets() returns newest first, which is the order the list
    // displays, so no reordering is needed.
    m_model->setPackets(packets);
    rebuildChannelFilter();
    qDebug() << "[PacketList] Loaded" << packets.size() << "packets from database";
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

void PacketListWidget::onDumpPackets()
{
    QSpinBox *countSpinBox = findChild<QSpinBox *>("exportCountSpinBox");
    if (!countSpinBox)
        return;

    int count = countSpinBox->value();

    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export Packets to File"),
        QString("packets_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        tr("Text Files (*.txt);;All Files (*)"));

    if (fileName.isEmpty())
        return;

    dumpPacketsToFile(fileName, count);
}

void PacketListWidget::dumpPacketsToFile(const QString &filePath, int count)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Export Failed"),
                             tr("Could not open file for writing: %1").arg(filePath));
        return;
    }

    QTextStream out(&file);
    out << "Meshtastic Packet Dump\n";
    out << "Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << QString("=").repeated(80) << "\n\n";

    int totalPackets = m_model->rowCount();
    int startRow = qMax(0, totalPackets - count);
    int exported = 0;

    for (int i = startRow; i < totalPackets; ++i)
    {
        const auto &packet = m_model->packetAt(i);

        out << "[" << QDateTime::fromMSecsSinceEpoch(packet.timestamp).toString("yyyy-MM-dd HH:mm:ss.zzz") << "] ";
        out << packet.typeName << " ";
        out << "From: " << packet.from << " ";
        out << "To: " << packet.to << "\n";

        if (packet.type == MeshtasticProtocol::PacketType::PacketReceived)
        {
            out << "  Port: " << MeshtasticProtocol::portNumToString(packet.portNum) << "\n";
        }

        // Raw fields
        if (!packet.fields.isEmpty())
        {
            out << "  Fields:\n";
            for (auto it = packet.fields.begin(); it != packet.fields.end(); ++it)
            {
                out << "    " << it.key() << ": " << it.value().toString() << "\n";
            }
        }

        out << "\n";
        exported++;
    }

    out << QString("=").repeated(80) << "\n";
    out << QString("Exported %1 of %2 total packets\n").arg(exported).arg(totalPackets);

    file.close();

    QMessageBox::information(this, tr("Export Complete"),
                             tr("Exported %1 packet(s) to:\n%2").arg(exported).arg(filePath));
}
