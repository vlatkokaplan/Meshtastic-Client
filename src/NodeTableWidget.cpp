#include "NodeTableWidget.h"

#include "AppSettings.h"
#include "MeshtasticProtocol.h"
#include "Theme.h"

#include <QAction>
#include <QBrush>
#include <QFont>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QScrollBar>
#include <QVBoxLayout>

// QTableWidgetItem sorts on its display text, which is wrong for any column
// whose text is not lexically ordered - "1h ago" sorts before "20m ago", and
// "Plugged" sorts against battery percentages. Columns that need a real
// ordering stash the underlying value in SortRole and this compares that.
class SortableTableItem : public QTableWidgetItem
{
public:
    static constexpr int SortRole = Qt::UserRole + 1;
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant mine = data(SortRole);
        const QVariant theirs = other.data(SortRole);
        if (mine.isValid() && theirs.isValid())
        {
            if (mine.typeId() == QMetaType::QDateTime || theirs.typeId() == QMetaType::QDateTime)
                return mine.toDateTime() < theirs.toDateTime();
            return mine.toDouble() < theirs.toDouble();
        }
        return QTableWidgetItem::operator<(other);
    }
};

// "3m ago" instead of a full timestamp: shorter, and the age is what you
// actually want to know at a glance. Full timestamp moves to the tooltip.
static QString relativeTimeText(const QDateTime &when)
{
    // A device can also report a nonsense timestamp; anything at or before the
    // Unix epoch is "never", not "20717 days ago".
    if (!when.isValid() || when.toSecsSinceEpoch() <= 0)
        return QStringLiteral("never");

    qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 0)
        secs = 0;
    if (secs < 60)
        return QStringLiteral("just now");
    if (secs < 3600)
        return QStringLiteral("%1m ago").arg(secs / 60);
    if (secs < 86400)
        return QStringLiteral("%1h ago").arg(secs / 3600);
    return QStringLiteral("%1d ago").arg(secs / 86400);
}


NodeTableWidget::NodeTableWidget(NodeManager *nodes, QWidget *parent)
    : QWidget(parent), m_nodes(nodes)
{
    setupUI();
}

void NodeTableWidget::setupUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::Space::sm);

    m_heading = new QLabel("NODES");
    m_heading->setStyleSheet(QString("font-weight: 700; font-size: 11px; letter-spacing: 1px;"
                                     "color: %1; padding: %2px %3px 0 %3px;")
                                 .arg(Theme::palette().textMuted.name())
                                 .arg(Theme::Space::sm)
                                 .arg(Theme::Space::md));
    layout->addWidget(m_heading);

    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search nodes...");
    m_search->setClearButtonEnabled(true);
    m_search->setText(AppSettings::instance()->value("nodeSearchText").toString());
    m_search->setContentsMargins(Theme::Space::md, 0, Theme::Space::md, 0);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        AppSettings::instance()->setValue("nodeSearchText", text);
        refresh();
    });
    layout->addWidget(m_search);

    m_table = new QTableWidget;
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels(
        {"Name", "Short", "Role", "Last Heard", "Battery", "Signal", "Hops"});

    QHeaderView *header = m_table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(0, QHeaderView::Stretch);  // Name takes the slack
    header->setMinimumSectionSize(52);
    header->setHighlightSections(false);
    header->setFixedHeight(28);

    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->setVisible(false);       // row numbers add nothing here
    m_table->verticalHeader()->setDefaultSectionSize(28);

    // Restore the saved sort column/order
    {
        int col   = AppSettings::instance()->value("nodeSortColumn", -1).toInt();
        int order = AppSettings::instance()->value("nodeSortOrder",   0).toInt();
        if (col >= 0)
            m_table->sortByColumn(col, static_cast<Qt::SortOrder>(order));
    }
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, [](int col, Qt::SortOrder order) {
        AppSettings::instance()->setValue("nodeSortColumn", col);
        AppSettings::instance()->setValue("nodeSortOrder",  static_cast<int>(order));
    });

    connect(m_table, &QTableWidget::itemClicked, this, &NodeTableWidget::onItemClicked);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &NodeTableWidget::onContextMenu);

    layout->addWidget(m_table);
}

uint32_t NodeTableWidget::nodeNumForRow(int row) const
{
    QTableWidgetItem *first = m_table->item(row, 0);
    return first ? first->data(Qt::UserRole).toUInt() : 0;
}

uint32_t NodeTableWidget::selectedNode() const
{
    QTableWidgetItem *current = m_table->currentItem();
    return current ? nodeNumForRow(current->row()) : 0;
}

void NodeTableWidget::onItemClicked(QTableWidgetItem *item)
{
    if (!item)
        return;
    const uint32_t nodeNum = nodeNumForRow(item->row());
    if (nodeNum != 0)
        emit nodeActivated(nodeNum);
}

bool NodeTableWidget::selectNode(uint32_t nodeNum)
{
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        if (nodeNumForRow(row) != nodeNum)
            continue;
        m_table->selectRow(row);
        if (QTableWidgetItem *item = m_table->item(row, 0))
            m_table->scrollToItem(item);
        return true;
    }
    return false;   // filtered out of the current view
}

void NodeTableWidget::refresh()
{
    // The table is rebuilt from scratch below, which drops the selection and
    // jumps back to the top. Remember both and put them back afterwards.
    uint32_t selectedNodeNum = 0;
    if (QTableWidgetItem *sel = m_table->currentItem())
    {
        if (QTableWidgetItem *col0 = m_table->item(sel->row(), 0))
            selectedNodeNum = col0->data(Qt::UserRole).toUInt();
    }
    int scrollPos = m_table->verticalScrollBar()->value();

    m_table->setUpdatesEnabled(false);
    const bool sortingWasEnabled = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    // Only re-sort when node data has changed, not just filter changes
    if (m_sortNeeded)
    {
        m_sorted = m_nodes->allNodes();
        uint32_t myNode = m_nodes->myNodeNum();
        std::sort(m_sorted.begin(), m_sorted.end(),
                  [myNode](const NodeInfo &a, const NodeInfo &b)
                  {
                      if (a.nodeNum == myNode) return true;
                      if (b.nodeNum == myNode) return false;
                      return a.lastHeard > b.lastHeard;
                  });
        m_sortNeeded = false;
    }
    const QList<NodeInfo> &nodes = m_sorted;

    // Get offline filter settings
    bool showOffline = AppSettings::instance()->showOfflineNodes();
    bool hideNeverHeard = AppSettings::instance()->hideNeverHeardNodes();
    int offlineThresholdMins = AppSettings::instance()->offlineThresholdMinutes();
    QDateTime offlineThreshold = QDateTime::currentDateTime().addSecs(-offlineThresholdMins * 60);

    // Get search filter
    QString searchTerm = m_search ? m_search->text().trimmed().toLower() : QString();

    uint32_t myNode = m_nodes->myNodeNum();

    // Columns that no visible node has data for are hidden rather than left as
    // a stripe of blank cells - on a real mesh only a couple of nodes report a
    // battery, and roles are often unknown.
    bool anyBattery = false;
    bool anyRole = false;
    bool anySignal = false;
    bool anyHops = false;

    // Why nodes were left out, so "11 of 41" does not leave the user guessing
    int hiddenNeverHeard = 0;
    int hiddenOffline = 0;
    int hiddenBySearch = 0;

    int row = 0;
    for (const NodeInfo &node : nodes)
    {
        // Nodes the device knows of but has never received a packet from. They
        // carry no position, signal or telemetry, so they are noise in the list
        // by default. Never hide our own node.
        bool neverHeard = !node.lastHeard.isValid();
        if (neverHeard && hideNeverHeard && node.nodeNum != myNode)
        {
            hiddenNeverHeard++;
            continue;
        }

        // Filter offline nodes if setting is disabled. A never-heard node used
        // to slip through here because of the isValid() check - it was the one
        // category that could never be hidden.
        if (!showOffline && (neverHeard || node.lastHeard < offlineThreshold)
            && node.nodeNum != myNode)
        {
            hiddenOffline++;
            continue;
        }

        // Filter by search term
        if (!searchTerm.isEmpty())
        {
            bool matches = node.longName.toLower().contains(searchTerm) ||
                           node.shortName.toLower().contains(searchTerm) ||
                           node.nodeId.toLower().contains(searchTerm);
            if (!matches)
            {
                hiddenBySearch++;
                continue;
            }
        }

        bool isMyNode = (node.nodeNum == myNode);

        m_table->insertRow(row);

        // Col 0: Node Name
        // Fall back through long name -> short name -> node id. A node showing
        // only its id has never sent a NodeInfo, so it is set in italic muted
        // text rather than reading as an equal of the named nodes.
        bool unnamed = node.longName.isEmpty() && node.shortName.isEmpty();
        QString name = node.longName;
        if (name.isEmpty())
            name = node.shortName;
        if (name.isEmpty())
            name = node.nodeId;
        if (node.isFavorite)
        {
            name = QStringLiteral("\u2605 ") + name;  // star
        }

        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, node.nodeNum);

        QFont nameFont = nameItem->font();
        if (unnamed)
        {
            nameFont.setItalic(true);
            nameItem->setForeground(QBrush(Theme::palette().textMuted));
        }
        if (isMyNode)
            nameFont.setBold(true);
        nameItem->setFont(nameFont);

        // Only about a quarter of a real mesh's nodes report a position, and
        // only those can ever appear on the map. Mark them so it is obvious
        // which rows "Center on Map" will do anything for.
        if (node.hasPosition)
        {
            nameItem->setIcon(Theme::positionPin(isMyNode ? Theme::palette().accent
                                                          : Theme::palette().textMuted));
            nameItem->setToolTip(QString("%1\nPosition: %2, %3")
                                     .arg(node.nodeId)
                                     .arg(node.latitude, 0, 'f', 5)
                                     .arg(node.longitude, 0, 'f', 5));
        }
        else
        {
            nameItem->setToolTip(QString("%1\nNo position reported").arg(node.nodeId));
        }
        m_table->setItem(row, 0, nameItem);

        // Col 1: Short Name
        QTableWidgetItem *shortItem = new QTableWidgetItem(node.shortName);
        shortItem->setData(Qt::UserRole, node.nodeNum);
        shortItem->setTextAlignment(Qt::AlignCenter);
        if (isMyNode)
        {
            QFont boldFont = shortItem->font();
            boldFont.setBold(true);
            shortItem->setFont(boldFont);
        }
        m_table->setItem(row, 1, shortItem);

        // Col 2: Role
        QString roleText = m_nodes->roleToString(node.role);
        if (!roleText.isEmpty())
            anyRole = true;
        QTableWidgetItem *roleItem = new QTableWidgetItem(roleText);
        roleItem->setData(Qt::UserRole, node.nodeNum);
        m_table->setItem(row, 2, roleItem);

        // Col 3: Last Heard
        SortableTableItem *heardItem = new SortableTableItem(relativeTimeText(node.lastHeard));
        heardItem->setToolTip(node.lastHeard.isValid()
                                  ? node.lastHeard.toString("yyyy-MM-dd HH:mm:ss")
                                  : QStringLiteral("Never heard from this node"));
        // Sort on the real timestamp, not the "3m ago" text
        heardItem->setData(SortableTableItem::SortRole, node.lastHeard);
        m_table->setItem(row, 3, heardItem);

        // Col 4: Battery
        SortableTableItem *batteryItem = new SortableTableItem;
        batteryItem->setData(SortableTableItem::SortRole,
                             node.isExternalPower ? 1000 : node.batteryLevel);
        // QIcon::fromTheme silently returns a blank icon when the desktop icon
        // theme lacks the name, which is why this column looked empty. Draw it.
        if (node.isExternalPower)
        {
            batteryItem->setIcon(Theme::batteryPip(100, true));
            batteryItem->setToolTip("Running on external power");
            anyBattery = true;
        }
        else if (node.batteryLevel > 0)
        {
            batteryItem->setIcon(Theme::batteryPip(node.batteryLevel, false));
            batteryItem->setText(QString::number(node.batteryLevel) + "%");
            if (node.voltage > 0)
                batteryItem->setToolTip(QString("%1%  ·  %2 V")
                                            .arg(node.batteryLevel)
                                            .arg(node.voltage, 0, 'f', 2));
            anyBattery = true;
        }
        else if (node.voltage > 0)
        {
            batteryItem->setText(QString("%1 V").arg(node.voltage, 0, 'f', 2));
            anyBattery = true;
        }
        m_table->setItem(row, 4, batteryItem);

        // Col 5: Signal - SNR only. Hop count lives in its own column now;
        // ranking both on one key meant every multi-hop node sorted below every
        // node with any SNR at all, so neither could be sorted usefully.
        SortableTableItem *signalItem = new SortableTableItem;
        signalItem->setTextAlignment(Qt::AlignCenter);
        bool hasSnr = (node.snr != 0.0f || node.rssi != 0);
        // Unknown sorts last in the useful (descending, best first) direction
        signalItem->setData(SortableTableItem::SortRole, hasSnr ? node.snr : -1000.0);
        if (hasSnr)
        {
            float snr = node.snr;
            QString bars = snr >= 10.0f ? "||||"
                         : snr >= 5.0f  ? "|||"
                         : snr >= 0.0f  ? "||"
                         : snr >= -5.0f ? "|"
                                        : "\u00b7";
            signalItem->setText(bars);
            signalItem->setForeground(QBrush(Theme::signalColor(snr)));
            signalItem->setToolTip(QString("SNR %1 dB  \u00b7  RSSI %2 dBm")
                                       .arg(node.snr, 0, 'f', 1).arg(node.rssi));
            anySignal = true;
        }
        else
        {
            signalItem->setText("-");
            signalItem->setForeground(QBrush(Theme::palette().textMuted));
        }
        m_table->setItem(row, 5, signalItem);

        // Col 6: Hops - sorts on its own scale, 0 (direct) first, unknown last
        SortableTableItem *hopsItem = new SortableTableItem;
        hopsItem->setTextAlignment(Qt::AlignCenter);
        if (node.hopsAway >= 0)
        {
            hopsItem->setData(SortableTableItem::SortRole, node.hopsAway);
            hopsItem->setText(node.hopsAway == 0 ? QStringLiteral("direct")
                                                 : QString::number(node.hopsAway));
            hopsItem->setToolTip(node.hopsAway == 0
                                     ? QStringLiteral("Heard directly, no relays")
                                     : QString("%1 relay hop%2 away")
                                           .arg(node.hopsAway)
                                           .arg(node.hopsAway > 1 ? "s" : ""));
            if (node.hopsAway == 0)
                hopsItem->setForeground(QBrush(Theme::palette().success));
            anyHops = true;
        }
        else
        {
            hopsItem->setData(SortableTableItem::SortRole, 999);  // unknown last
            hopsItem->setText("-");
            hopsItem->setForeground(QBrush(Theme::palette().textMuted));
        }
        m_table->setItem(row, 6, hopsItem);
        row++;
    }

    m_table->setColumnHidden(2, !anyRole);      // Role
    m_table->setColumnHidden(4, !anyBattery);   // Battery
    m_table->setColumnHidden(5, !anySignal);    // Signal
    m_table->setColumnHidden(6, !anyHops);      // Hops

    m_table->setSortingEnabled(sortingWasEnabled);

    if (m_heading)
    {
        const int total = nodes.size();
        m_heading->setText(row == total ? QStringLiteral("NODES")
                                        : QStringLiteral("NODES  %1 OF %2").arg(row).arg(total));

        // Spell out what is missing and where to change it, rather than leaving
        // a bare "11 of 41" to be puzzled over.
        QStringList reasons;
        if (hiddenNeverHeard > 0)
            reasons << QString("%1 never heard from - this radio knows of them but has "
                               "not received a packet from them").arg(hiddenNeverHeard);
        if (hiddenOffline > 0)
            reasons << QString("%1 not heard from recently").arg(hiddenOffline);
        if (hiddenBySearch > 0)
            reasons << QString("%1 do not match the search").arg(hiddenBySearch);

        if (reasons.isEmpty())
        {
            m_heading->setToolTip(QString("%1 nodes, all shown").arg(total));
        }
        else
        {
            m_heading->setToolTip(
                QString("Showing %1 of %2 nodes.\n\nHidden:\n  %3\n\n"
                        "Change what is hidden in Config > App Settings > Node Display.")
                    .arg(row).arg(total).arg(reasons.join("\n  ")));
        }
    }

    // Restore the selection and scroll position from before the rebuild
    if (selectedNodeNum != 0)
    {
        for (int r = 0; r < m_table->rowCount(); ++r)
        {
            QTableWidgetItem *col0 = m_table->item(r, 0);
            if (col0 && col0->data(Qt::UserRole).toUInt() == selectedNodeNum)
            {
                m_table->setCurrentCell(r, 0, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                break;
            }
        }
    }
    m_table->verticalScrollBar()->setValue(scrollPos);

    m_table->setUpdatesEnabled(true);
}
void NodeTableWidget::onContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = m_table->itemAt(pos);
    if (!item)
        return;

    // Get column 0 item which stores the nodeNum in UserRole
    QTableWidgetItem *col0Item = m_table->item(item->row(), 0);
    if (!col0Item)
        return;

    uint32_t nodeNum = col0Item->data(Qt::UserRole).toUInt();
    if (nodeNum == 0)
        return;

    NodeInfo node = m_nodes->getNode(nodeNum);

    QMenu menu(this);
    QString nodeName = node.longName;
    if (nodeName.isEmpty())
        nodeName = node.nodeId;
    QAction *headerAction = menu.addAction(nodeName);
    headerAction->setEnabled(false);
    QFont boldFont = headerAction->font();
    boldFont.setBold(true);
    headerAction->setFont(boldFont);
    menu.addSeparator();

    // Send DM option (only if not our own node)
    QAction *sendDmAction = nullptr;
    if (nodeNum != m_nodes->myNodeNum())
    {
        sendDmAction = menu.addAction("Send Direct Message");
        sendDmAction->setIcon(QIcon::fromTheme("mail-message-new"));
        menu.addSeparator();
    }

    QAction *tracerouteAction = menu.addAction("Traceroute");
    tracerouteAction->setIcon(QIcon::fromTheme("network-wired"));
    QAction *nodeInfoAction = menu.addAction("Request Node Info");
    nodeInfoAction->setIcon(QIcon::fromTheme("user-identity"));
    QAction *telemetryAction = menu.addAction("Request Telemetry");
    telemetryAction->setIcon(QIcon::fromTheme("utilities-system-monitor"));
    QAction *positionAction = menu.addAction("Request Position");
    positionAction->setIcon(QIcon::fromTheme("find-location"));
    menu.addSeparator();
    QAction *centerMapAction = menu.addAction("Center on Map");
    centerMapAction->setIcon(QIcon::fromTheme("zoom-fit-best"));
    centerMapAction->setEnabled(node.hasPosition);

    QAction *trackAction = menu.addAction("Show Movement History");
    trackAction->setToolTip("Draw this node's recorded positions on the map");
    QAction *clearTrackAction = menu.addAction("Clear Movement History");
    QAction *selectedAction = menu.exec(m_table->viewport()->mapToGlobal(pos));

    if (sendDmAction && selectedAction == sendDmAction)
    {
        emit directMessageRequested(nodeNum);
    }
    else if (selectedAction == tracerouteAction)
    {
        emit tracerouteRequested(nodeNum);
    }
    else if (selectedAction == nodeInfoAction)
    {
        emit nodeInfoRequested(nodeNum);
    }
    else if (selectedAction == telemetryAction)
    {
        emit telemetryRequested(nodeNum);
    }
    else if (selectedAction == positionAction)
    {
        emit positionRequested(nodeNum);
    }
    else if (selectedAction == trackAction)
    {
        emit trackRequested(nodeNum);
    }
    else if (selectedAction == clearTrackAction)
    {
        emit clearTrackRequested();
    }
    else if (selectedAction == centerMapAction && node.hasPosition)
    {
        emit centerOnMapRequested(nodeNum);
    }
}
