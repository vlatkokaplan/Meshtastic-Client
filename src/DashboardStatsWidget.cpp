#include "DashboardStatsWidget.h"
#include "NodeManager.h"
#include "DeviceConfig.h"
#include <QFrame>

DashboardStatsWidget::DashboardStatsWidget(NodeManager *nodeManager, DeviceConfig *deviceConfig,
                                             QWidget *parent)
    : QWidget(parent)
    , m_nodeManager(nodeManager)
    , m_deviceConfig(deviceConfig)
{
    setupUI();

    connect(m_nodeManager, &NodeManager::nodeUpdated,
            this, &DashboardStatsWidget::onNodeUpdated);
    connect(m_nodeManager, &NodeManager::nodesChanged,
            this, &DashboardStatsWidget::onNodesChanged);
    connect(m_nodeManager, &NodeManager::myNodeNumChanged,
            this, &DashboardStatsWidget::onMyNodeNumChanged);

    connect(m_deviceConfig, &DeviceConfig::loraConfigChanged,
            this, &DashboardStatsWidget::onLoraConfigChanged);
    connect(m_deviceConfig, &DeviceConfig::deviceConfigChanged,
            this, &DashboardStatsWidget::onDeviceConfigChanged);
}

void DashboardStatsWidget::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(0);

    // --- Section 1: Identity ---
    auto *identityLayout = new QGridLayout;
    identityLayout->setContentsMargins(4, 4, 4, 4);
    identityLayout->setSpacing(2);

    m_nameLabel = new QLabel("--");
    m_nameLabel->setStyleSheet("font-weight: bold;");
    m_hwModelLabel = new QLabel;
    m_hwModelLabel->setAlignment(Qt::AlignRight);
    m_nodeIdLabel = new QLabel;
    m_nodeIdLabel->setStyleSheet("color: gray; font-size: 11px;");
    m_fwVersionLabel = new QLabel;
    m_fwVersionLabel->setAlignment(Qt::AlignRight);
    m_fwVersionLabel->setStyleSheet("color: gray; font-size: 11px;");

    identityLayout->addWidget(m_nameLabel, 0, 0);
    identityLayout->addWidget(m_hwModelLabel, 0, 1);
    identityLayout->addWidget(m_nodeIdLabel, 1, 0);
    identityLayout->addWidget(m_fwVersionLabel, 1, 1);
    mainLayout->addLayout(identityLayout);

    // Separator
    auto *sep1 = new QFrame;
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep1);

    // --- Section 2: Telemetry ---
    auto *telemetryLayout = new QGridLayout;
    telemetryLayout->setContentsMargins(4, 4, 4, 4);
    telemetryLayout->setSpacing(2);

    // Progress bar stylesheet (palette-aware base colors)
    QString barStyle =
        "QProgressBar {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 3px;"
        "  text-align: center;"
        "  height: 14px;"
        "  background: palette(base);"
        "}"
        "QProgressBar::chunk {"
        "  border-radius: 2px;"
        "}";

    // Battery row
    auto *battLabel = new QLabel("Battery");
    battLabel->setStyleSheet("font-size: 11px;");
    m_batteryBar = new QProgressBar;
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setValue(0);
    m_batteryBar->setTextVisible(false);
    m_batteryBar->setFixedHeight(14);
    m_batteryBar->setStyleSheet(barStyle + "QProgressBar::chunk { background: #4caf50; }");
    m_batteryPctLabel = new QLabel("--%");
    m_batteryPctLabel->setAlignment(Qt::AlignRight);
    m_batteryPctLabel->setFixedWidth(40);
    m_voltageLabel = new QLabel;
    m_voltageLabel->setStyleSheet("color: gray; font-size: 11px;");

    telemetryLayout->addWidget(battLabel, 0, 0);
    telemetryLayout->addWidget(m_batteryBar, 0, 1);
    telemetryLayout->addWidget(m_batteryPctLabel, 0, 2);
    telemetryLayout->addWidget(m_voltageLabel, 1, 0, 1, 3);

    // Channel utilization row
    auto *chLabel = new QLabel("Ch Util");
    chLabel->setStyleSheet("font-size: 11px;");
    m_chUtilBar = new QProgressBar;
    m_chUtilBar->setRange(0, 1000);
    m_chUtilBar->setValue(0);
    m_chUtilBar->setTextVisible(false);
    m_chUtilBar->setFixedHeight(14);
    m_chUtilBar->setStyleSheet(barStyle + "QProgressBar::chunk { background: #2196f3; }");
    m_chUtilLabel = new QLabel("--%");
    m_chUtilLabel->setAlignment(Qt::AlignRight);
    m_chUtilLabel->setFixedWidth(40);

    telemetryLayout->addWidget(chLabel, 2, 0);
    telemetryLayout->addWidget(m_chUtilBar, 2, 1);
    telemetryLayout->addWidget(m_chUtilLabel, 2, 2);

    // Air TX utilization row
    auto *airLabel = new QLabel("Air TX");
    airLabel->setStyleSheet("font-size: 11px;");
    m_airTxBar = new QProgressBar;
    m_airTxBar->setRange(0, 1000);
    m_airTxBar->setValue(0);
    m_airTxBar->setTextVisible(false);
    m_airTxBar->setFixedHeight(14);
    m_airTxBar->setStyleSheet(barStyle + "QProgressBar::chunk { background: #ff9800; }");
    m_airTxLabel = new QLabel("--%");
    m_airTxLabel->setAlignment(Qt::AlignRight);
    m_airTxLabel->setFixedWidth(40);

    telemetryLayout->addWidget(airLabel, 3, 0);
    telemetryLayout->addWidget(m_airTxBar, 3, 1);
    telemetryLayout->addWidget(m_airTxLabel, 3, 2);

    mainLayout->addLayout(telemetryLayout);

    // Separator
    auto *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep2);

    // --- Section 3: Config ---
    auto *configLayout = new QGridLayout;
    configLayout->setContentsMargins(4, 4, 4, 4);
    configLayout->setSpacing(2);

    auto makeLabelPair = [&](int row, const QString &title) -> QLabel * {
        auto *titleLbl = new QLabel(title);
        titleLbl->setStyleSheet("font-size: 11px; color: gray;");
        auto *valueLbl = new QLabel("--");
        valueLbl->setStyleSheet("font-size: 11px;");
        valueLbl->setAlignment(Qt::AlignRight);
        configLayout->addWidget(titleLbl, row, 0);
        configLayout->addWidget(valueLbl, row, 1);
        return valueLbl;
    };

    m_roleLabel = makeLabelPair(0, "Role");
    m_regionPresetLabel = makeLabelPair(1, "Region");
    m_hopsLabel = makeLabelPair(2, "Hops");
    m_nodeCountLabel = makeLabelPair(3, "Nodes");

    mainLayout->addLayout(configLayout);
}

void DashboardStatsWidget::setFirmwareVersion(const QString &version)
{
    m_firmwareVersion = version;
    m_fwVersionLabel->setText("FW " + version);
}

void DashboardStatsWidget::onNodeUpdated(uint32_t nodeNum)
{
    if (nodeNum == m_nodeManager->myNodeNum()) {
        updateIdentity();
        updateTelemetry();
    }
}

void DashboardStatsWidget::onNodesChanged()
{
    updateNodeCount();
}

void DashboardStatsWidget::onMyNodeNumChanged()
{
    updateIdentity();
    updateTelemetry();
    updateConfig();
    updateNodeCount();
}

void DashboardStatsWidget::onLoraConfigChanged()
{
    updateConfig();
}

void DashboardStatsWidget::onDeviceConfigChanged()
{
    updateConfig();
}

void DashboardStatsWidget::updateIdentity()
{
    uint32_t myNode = m_nodeManager->myNodeNum();
    if (myNode == 0)
        return;

    NodeInfo node = m_nodeManager->getNode(myNode);
    QString name = node.longName.isEmpty() ? node.nodeId : node.longName;
    m_nameLabel->setText(name);
    m_hwModelLabel->setText(node.hwModel);
    m_nodeIdLabel->setText(node.nodeId);
}

void DashboardStatsWidget::updateTelemetry()
{
    uint32_t myNode = m_nodeManager->myNodeNum();
    if (myNode == 0)
        return;

    NodeInfo node = m_nodeManager->getNode(myNode);

    // Battery
    int batt = node.batteryLevel;
    if (batt > 0) {
        m_batteryBar->setValue(batt);
        m_batteryPctLabel->setText(QString("%1%").arg(batt));

        // Color-code battery bar
        QString color;
        if (batt < 20)
            color = "#f44336"; // red
        else if (batt < 50)
            color = "#ff9800"; // orange
        else
            color = "#4caf50"; // green

        QString barStyle =
            "QProgressBar {"
            "  border: 1px solid palette(mid);"
            "  border-radius: 3px;"
            "  text-align: center;"
            "  height: 14px;"
            "  background: palette(base);"
            "}"
            "QProgressBar::chunk {"
            "  border-radius: 2px;"
            "  background: " + color + ";"
            "}";
        m_batteryBar->setStyleSheet(barStyle);
    } else {
        m_batteryBar->setValue(0);
        m_batteryPctLabel->setText("--%");
    }

    // Voltage + power source
    QString voltText;
    if (node.voltage > 0)
        voltText = QString("%1V").arg(node.voltage, 0, 'f', 2);
    if (node.isExternalPower)
        voltText += voltText.isEmpty() ? "External" : "  \u00b7  External";
    else if (!voltText.isEmpty())
        voltText += "  \u00b7  Battery";
    m_voltageLabel->setText(voltText);

    // Channel utilization (0-100% stored as float)
    m_chUtilBar->setValue(static_cast<int>(node.channelUtilization * 10));
    m_chUtilLabel->setText(QString("%1%").arg(node.channelUtilization, 0, 'f', 1));

    // Air TX utilization
    m_airTxBar->setValue(static_cast<int>(node.airUtilTx * 10));
    m_airTxLabel->setText(QString("%1%").arg(node.airUtilTx, 0, 'f', 1));
}

void DashboardStatsWidget::updateConfig()
{
    if (!m_deviceConfig)
        return;

    // Role
    if (m_deviceConfig->hasDeviceConfig()) {
        QStringList roles = DeviceConfig::deviceRoleNames();
        int roleIdx = m_deviceConfig->deviceConfig().role;
        m_roleLabel->setText(roleIdx < roles.size() ? roles[roleIdx] : "Unknown");
    }

    // Region + preset
    if (m_deviceConfig->hasLoRaConfig()) {
        auto lora = m_deviceConfig->loraConfig();

        QStringList regions = DeviceConfig::regionNames();
        QStringList presets = DeviceConfig::modemPresetNames();

        QString region = lora.region < regions.size() ? regions[lora.region] : "?";
        QString preset = lora.modemPreset < presets.size() ? presets[lora.modemPreset] : "?";
        m_regionPresetLabel->setText(region + "  \u00b7  " + preset);

        m_hopsLabel->setText(QString::number(lora.hopLimit));
    }
}

void DashboardStatsWidget::updateNodeCount()
{
    int count = m_nodeManager->allNodes().size();
    m_nodeCountLabel->setText(QString("%1 online").arg(count));
}
