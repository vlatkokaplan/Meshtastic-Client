#include "SignalScannerWidget.h"
#include "NodeManager.h"
#include "MeshtasticProtocol.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QFrame>

// SignalHistoryModel implementation

SignalHistoryModel::SignalHistoryModel(NodeManager *nodeManager, QObject *parent)
    : QAbstractTableModel(parent), m_nodeManager(nodeManager)
{
}

int SignalHistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_measurements.size();
}

int SignalHistoryModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant SignalHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_measurements.size())
        return QVariant();

    const auto &m = m_measurements[index.row()];

    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case ColTime:
            return m.timestamp.toString("HH:mm:ss");
        case ColNode:
            return formatNodeName(m.nodeNum);
        case ColSNR:
            return QString::number(m.snr, 'f', 1) + " dB";
        case ColRSSI:
            return QString::number(m.rssi) + " dBm";
        case ColHops:
            return m.hopsAway >= 0 ? QString::number(m.hopsAway) : "-";
        case ColQuality:
            return qualityFromSignal(m.snr, m.rssi);
        }
    }
    else if (role == Qt::ForegroundRole)
    {
        if (index.column() == ColQuality)
        {
            return qualityColor(m.snr, m.rssi);
        }
    }
    else if (role == Qt::TextAlignmentRole)
    {
        if (index.column() == ColSNR || index.column() == ColRSSI || index.column() == ColHops)
        {
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    return QVariant();
}

QVariant SignalHistoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section)
    {
    case ColTime:
        return "Time";
    case ColNode:
        return "Node";
    case ColSNR:
        return "SNR";
    case ColRSSI:
        return "RSSI";
    case ColHops:
        return "Hops";
    case ColQuality:
        return "Quality";
    }

    return QVariant();
}

void SignalHistoryModel::addMeasurement(const SignalMeasurement &measurement)
{
    // Filter by target node if set (0 means scan all)
    if (m_targetNode != 0 && measurement.nodeNum != m_targetNode)
        return;

    beginInsertRows(QModelIndex(), 0, 0);
    m_measurements.prepend(measurement);
    endInsertRows();

    // Limit history size
    if (m_measurements.size() > MAX_MEASUREMENTS)
    {
        beginRemoveRows(QModelIndex(), MAX_MEASUREMENTS, m_measurements.size() - 1);
        while (m_measurements.size() > MAX_MEASUREMENTS)
        {
            m_measurements.removeLast();
        }
        endRemoveRows();
    }
}

void SignalHistoryModel::clear()
{
    beginResetModel();
    m_measurements.clear();
    endResetModel();
}

void SignalHistoryModel::setTargetNode(uint32_t nodeNum)
{
    m_targetNode = nodeNum;
}

float SignalHistoryModel::averageSNR() const
{
    if (m_measurements.isEmpty())
        return 0.0f;

    float sum = 0.0f;
    for (const auto &m : m_measurements)
    {
        sum += m.snr;
    }
    return sum / m_measurements.size();
}

int SignalHistoryModel::averageRSSI() const
{
    if (m_measurements.isEmpty())
        return 0;

    int sum = 0;
    for (const auto &m : m_measurements)
    {
        sum += m.rssi;
    }
    return sum / m_measurements.size();
}

QString SignalHistoryModel::formatNodeName(uint32_t nodeNum) const
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

QString SignalHistoryModel::qualityFromSignal(float snr, int rssi) const
{
    // SNR is the primary indicator for LoRa
    // SNR > 5 dB: Excellent
    // SNR 0-5 dB: Good
    // SNR -5 to 0: Moderate
    // SNR < -5 dB: Poor

    if (snr > 5.0f)
        return "Excellent";
    else if (snr > 0.0f)
        return "Good";
    else if (snr > -5.0f)
        return "Moderate";
    else
        return "Poor";
}

QColor SignalHistoryModel::qualityColor(float snr, int rssi) const
{
    if (snr > 5.0f)
        return QColor(0, 180, 0);    // Green
    else if (snr > 0.0f)
        return QColor(100, 180, 0);  // Yellow-green
    else if (snr > -5.0f)
        return QColor(200, 150, 0);  // Orange
    else
        return QColor(200, 50, 50);  // Red
}

// SignalScannerWidget implementation

SignalScannerWidget::SignalScannerWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager)
{
    setupUI();

    // Connect to node manager signals
    connect(m_nodeManager, &NodeManager::nodeUpdated, this, &SignalScannerWidget::onNodeSignalUpdated);
    connect(m_nodeManager, &NodeManager::nodesChanged, this, &SignalScannerWidget::refreshNodeList);

    // Initial population
    refreshNodeList();
}

void SignalScannerWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Top controls
    QHBoxLayout *controlsLayout = new QHBoxLayout;

    QLabel *targetLabel = new QLabel("Target Node:");
    m_nodeCombo = new QComboBox;
    m_nodeCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_nodeCombo->setMinimumWidth(200);

    m_scanAllButton = new QPushButton("Scan All");
    m_scanAllButton->setCheckable(true);
    m_scanAllButton->setToolTip("Monitor signals from all nodes");

    m_clearButton = new QPushButton("Clear");
    m_clearButton->setToolTip("Clear measurement history");

    controlsLayout->addWidget(targetLabel);
    controlsLayout->addWidget(m_nodeCombo);
    controlsLayout->addWidget(m_scanAllButton);
    controlsLayout->addWidget(m_clearButton);
    controlsLayout->addStretch();

    mainLayout->addLayout(controlsLayout);

    // Current signal display
    QGroupBox *currentGroup = new QGroupBox("Current Signal");
    QHBoxLayout *currentLayout = new QHBoxLayout(currentGroup);

    // Left side: Signal values
    QFormLayout *signalForm = new QFormLayout;
    signalForm->setSpacing(4);

    m_currentSnrLabel = new QLabel("-");
    m_currentSnrLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    signalForm->addRow("SNR:", m_currentSnrLabel);

    m_currentRssiLabel = new QLabel("-");
    m_currentRssiLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    signalForm->addRow("RSSI:", m_currentRssiLabel);

    m_currentHopsLabel = new QLabel("-");
    m_currentHopsLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    signalForm->addRow("Hops:", m_currentHopsLabel);

    currentLayout->addLayout(signalForm);

    // Separator
    QFrame *sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    currentLayout->addWidget(sep);

    // Right side: Quality indicator
    QVBoxLayout *qualityLayout = new QVBoxLayout;
    qualityLayout->setAlignment(Qt::AlignCenter);

    m_currentQualityLabel = new QLabel("No Signal");
    m_currentQualityLabel->setStyleSheet("font-weight: bold; font-size: 16px;");
    m_currentQualityLabel->setAlignment(Qt::AlignCenter);
    qualityLayout->addWidget(m_currentQualityLabel);

    m_signalBar = new QProgressBar;
    m_signalBar->setMinimum(0);
    m_signalBar->setMaximum(100);
    m_signalBar->setValue(0);
    m_signalBar->setTextVisible(false);
    m_signalBar->setFixedWidth(200);
    m_signalBar->setFixedHeight(20);
    qualityLayout->addWidget(m_signalBar);

    currentLayout->addLayout(qualityLayout);
    currentLayout->addStretch();

    mainLayout->addWidget(currentGroup);

    // Last hop warning label (shown when node is >0 hops away)
    m_lastHopWarningLabel = new QLabel("Note: Signal shows last hop only, not the distant node's actual signal.");
    m_lastHopWarningLabel->setStyleSheet("QLabel { color: #ff9800; font-style: italic; padding: 4px; }");
    m_lastHopWarningLabel->setVisible(false);
    mainLayout->addWidget(m_lastHopWarningLabel);

    // Statistics
    QGroupBox *statsGroup = new QGroupBox("Statistics");
    QHBoxLayout *statsLayout = new QHBoxLayout(statsGroup);

    m_avgSnrLabel = new QLabel("Avg SNR: -");
    m_avgRssiLabel = new QLabel("Avg RSSI: -");
    m_countLabel = new QLabel("Samples: 0");

    statsLayout->addWidget(m_avgSnrLabel);
    statsLayout->addWidget(m_avgRssiLabel);
    statsLayout->addWidget(m_countLabel);
    statsLayout->addStretch();

    mainLayout->addWidget(statsGroup);

    // History table
    QGroupBox *historyGroup = new QGroupBox("Signal History");
    QVBoxLayout *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setContentsMargins(4, 4, 4, 4);

    m_model = new SignalHistoryModel(m_nodeManager, this);

    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->setWordWrap(false);

    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->verticalHeader()->setDefaultSectionSize(22);

    historyLayout->addWidget(m_tableView);

    mainLayout->addWidget(historyGroup, 1);

    // Connect signals
    connect(m_nodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SignalScannerWidget::onTargetNodeChanged);
    connect(m_clearButton, &QPushButton::clicked, this, &SignalScannerWidget::onClearHistory);
    connect(m_scanAllButton, &QPushButton::toggled, this, &SignalScannerWidget::onScanAllToggled);
}

void SignalScannerWidget::refreshNodeList()
{
    uint32_t currentNode = m_nodeCombo->currentData().toUInt();

    m_nodeCombo->blockSignals(true);
    m_nodeCombo->clear();

    m_nodeCombo->addItem("Select a node...", 0);

    QList<NodeInfo> nodes = m_nodeManager->allNodes();

    // Sort by name
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo &a, const NodeInfo &b) {
        QString nameA = a.shortName.isEmpty() ? a.longName : a.shortName;
        QString nameB = b.shortName.isEmpty() ? b.longName : b.shortName;
        return nameA.toLower() < nameB.toLower();
    });

    uint32_t myNode = m_nodeManager->myNodeNum();

    for (const auto &node : nodes)
    {
        if (node.nodeNum == myNode)
            continue; // Skip own node

        QString name = node.shortName.isEmpty() ? node.longName : node.shortName;
        if (name.isEmpty())
            name = MeshtasticProtocol::nodeIdToString(node.nodeNum);

        QString display = QString("%1 (%2)").arg(name, MeshtasticProtocol::nodeIdToString(node.nodeNum));
        m_nodeCombo->addItem(display, node.nodeNum);
    }

    // Restore selection
    int idx = m_nodeCombo->findData(currentNode);
    if (idx >= 0)
        m_nodeCombo->setCurrentIndex(idx);

    m_nodeCombo->blockSignals(false);
}

void SignalScannerWidget::onTargetNodeChanged(int index)
{
    uint32_t nodeNum = m_nodeCombo->itemData(index).toUInt();
    m_model->setTargetNode(nodeNum);

    if (nodeNum != 0)
    {
        // Update current signal display with selected node's data
        updateCurrentSignal(nodeNum);
    }
    else
    {
        // Reset display
        m_currentSnrLabel->setText("-");
        m_currentRssiLabel->setText("-");
        m_currentHopsLabel->setText("-");
        m_currentQualityLabel->setText("No Signal");
        m_currentQualityLabel->setStyleSheet("font-weight: bold; font-size: 16px;");
        m_signalBar->setValue(0);
        m_lastHopWarningLabel->setVisible(false);
    }
}

void SignalScannerWidget::onClearHistory()
{
    m_model->clear();
    updateStats();
}

void SignalScannerWidget::onScanAllToggled(bool checked)
{
    m_scanAll = checked;

    if (checked)
    {
        m_nodeCombo->setEnabled(false);
        m_model->setTargetNode(0); // 0 = accept all nodes
        m_scanAllButton->setText("Stop Scan");
    }
    else
    {
        m_nodeCombo->setEnabled(true);
        uint32_t nodeNum = m_nodeCombo->currentData().toUInt();
        m_model->setTargetNode(nodeNum);
        m_scanAllButton->setText("Scan All");
    }
}

void SignalScannerWidget::onNodeSignalUpdated(uint32_t nodeNum)
{
    // Skip own node
    if (nodeNum == m_nodeManager->myNodeNum())
        return;

    uint32_t targetNode = m_model->targetNode();

    // Check if we should record this measurement
    if (!m_scanAll && targetNode != 0 && nodeNum != targetNode)
        return;

    NodeInfo node = m_nodeManager->getNode(nodeNum);

    // Only record if we have signal data
    if (node.snr == 0.0f && node.rssi == 0)
        return;

    SignalMeasurement measurement;
    measurement.timestamp = QDateTime::currentDateTime();
    measurement.nodeNum = nodeNum;
    measurement.snr = node.snr;
    measurement.rssi = node.rssi;
    measurement.hopsAway = node.hopsAway;

    m_model->addMeasurement(measurement);

    // Update current display if this is the target node or scan all
    if (m_scanAll || nodeNum == targetNode)
    {
        updateCurrentSignal(nodeNum);
    }

    updateStats();
}

void SignalScannerWidget::updateCurrentSignal(uint32_t nodeNum)
{
    if (!m_nodeManager->hasNode(nodeNum))
        return;

    NodeInfo node = m_nodeManager->getNode(nodeNum);

    m_currentSnrLabel->setText(QString::number(node.snr, 'f', 1) + " dB");
    m_currentRssiLabel->setText(QString::number(node.rssi) + " dBm");
    m_currentHopsLabel->setText(node.hopsAway >= 0 ? QString::number(node.hopsAway) : "-");

    QString quality = qualityFromSignal(node.snr, node.rssi);
    m_currentQualityLabel->setText(quality);

    // Set quality color
    QString colorStyle;
    if (node.snr > 5.0f)
        colorStyle = "color: #00b400;"; // Green
    else if (node.snr > 0.0f)
        colorStyle = "color: #64b400;"; // Yellow-green
    else if (node.snr > -5.0f)
        colorStyle = "color: #c89600;"; // Orange
    else
        colorStyle = "color: #c83232;"; // Red

    m_currentQualityLabel->setStyleSheet("font-weight: bold; font-size: 16px; " + colorStyle);

    // Update progress bar
    int pct = signalPercentage(node.snr, node.rssi);
    m_signalBar->setValue(pct);

    // Set progress bar color via stylesheet
    if (pct >= 75)
        m_signalBar->setStyleSheet("QProgressBar::chunk { background-color: #00b400; }");
    else if (pct >= 50)
        m_signalBar->setStyleSheet("QProgressBar::chunk { background-color: #64b400; }");
    else if (pct >= 25)
        m_signalBar->setStyleSheet("QProgressBar::chunk { background-color: #c89600; }");
    else
        m_signalBar->setStyleSheet("QProgressBar::chunk { background-color: #c83232; }");

    // Show warning for multi-hop nodes (signal is from last relay, not the actual node)
    m_lastHopWarningLabel->setVisible(node.hopsAway > 0);
}

void SignalScannerWidget::updateStats()
{
    int count = m_model->measurementCount();
    m_countLabel->setText(QString("Samples: %1").arg(count));

    if (count > 0)
    {
        m_avgSnrLabel->setText(QString("Avg SNR: %1 dB").arg(m_model->averageSNR(), 0, 'f', 1));
        m_avgRssiLabel->setText(QString("Avg RSSI: %1 dBm").arg(m_model->averageRSSI()));
    }
    else
    {
        m_avgSnrLabel->setText("Avg SNR: -");
        m_avgRssiLabel->setText("Avg RSSI: -");
    }
}

QString SignalScannerWidget::qualityFromSignal(float snr, int rssi) const
{
    if (snr > 5.0f)
        return "Excellent";
    else if (snr > 0.0f)
        return "Good";
    else if (snr > -5.0f)
        return "Moderate";
    else
        return "Poor";
}

int SignalScannerWidget::signalPercentage(float snr, int rssi) const
{
    // Map SNR from -15 dB to +10 dB to 0-100%
    // Typical LoRa SNR range is around -20 to +10 dB
    float normalized = (snr + 15.0f) / 25.0f; // -15 -> 0%, +10 -> 100%
    int pct = static_cast<int>(normalized * 100.0f);
    return qBound(0, pct, 100);
}
