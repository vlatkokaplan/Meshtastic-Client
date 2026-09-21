#include "DashboardStatsWidget.h"
#include "Theme.h"
#include <QSizePolicy>
#include "NodeManager.h"
#include "DeviceConfig.h"
#include <QFrame>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDesktopServices>
#include <QUrl>

DashboardStatsWidget::DashboardStatsWidget(NodeManager *nodeManager, DeviceConfig *deviceConfig,
                                             QWidget *parent)
    : QWidget(parent)
    , m_nodeManager(nodeManager)
    , m_deviceConfig(deviceConfig)
    , m_networkManager(new QNetworkAccessManager(this))
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


namespace {

// A thin, flush metric bar. Colour comes from Theme so light/dark stay in sync.
QString meterStyle(const QColor &fill)
{
    const auto &p = Theme::palette();
    return QString(
        "QProgressBar {"
        "  border: none;"
        "  border-radius: 3px;"
        "  background-color: %1;"
        "  height: 7px;"
        "  text-align: center;"
        "}"
        "QProgressBar::chunk {"
        "  border-radius: 3px;"
        "  background-color: %2;"
        "}")
        // The track must read against the panel in both modes: a dark-on-dark
        // track disappears entirely at 0%.
        .arg(Theme::isDark() ? p.borderStrong.name() : p.border.name(), fill.name());
}

QString captionStyle()
{
    return QString("color: %1; font-size: 11px;").arg(Theme::palette().textMuted.name());
}

QString valueStyle()
{
    return QString("color: %1; font-size: 11px; font-weight: 600;").arg(Theme::palette().text.name());
}

} // namespace

void DashboardStatsWidget::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(Theme::Space::md, Theme::Space::md,
                                  Theme::Space::md, Theme::Space::md);
    mainLayout->setSpacing(Theme::Space::md);

    // --- Section 1: Identity ---
    auto *identityLayout = new QGridLayout;
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(Theme::Space::xs);

    m_nameLabel = new QLabel("--");
    m_nameLabel->setStyleSheet(QString("font-size: 15px; font-weight: 700; color: %1;")
                                   .arg(Theme::palette().text.name()));
    m_hwModelLabel = new QLabel;
    m_hwModelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_hwModelLabel->setStyleSheet(captionStyle());
    m_nodeIdLabel = new QLabel;
    m_nodeIdLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: monospace;")
                                     .arg(Theme::palette().textMuted.name()));
    m_fwVersionLabel = new QLabel;
    m_fwVersionLabel->setAlignment(Qt::AlignRight);
    m_fwVersionLabel->setStyleSheet(captionStyle());

    m_checkFirmwareButton = new QPushButton("Check Updates");
    m_checkFirmwareButton->setStyleSheet("font-size: 11px; padding: 4px 10px;");
    m_checkFirmwareButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_checkFirmwareButton->setToolTip("Check for firmware updates on GitHub");
    connect(m_checkFirmwareButton, &QPushButton::clicked, this, &DashboardStatsWidget::onCheckFirmware);

    m_firmwareStatusLabel = new QLabel;
    m_firmwareStatusLabel->setStyleSheet(captionStyle());
    m_firmwareStatusLabel->setAlignment(Qt::AlignRight);
    m_firmwareStatusLabel->hide();

    identityLayout->addWidget(m_nameLabel, 0, 0);
    identityLayout->addWidget(m_hwModelLabel, 0, 1);
    identityLayout->addWidget(m_nodeIdLabel, 1, 0);
    identityLayout->addWidget(m_fwVersionLabel, 1, 1);
    identityLayout->addWidget(m_checkFirmwareButton, 2, 0);
    identityLayout->addWidget(m_firmwareStatusLabel, 2, 1);
    mainLayout->addLayout(identityLayout);

    // Separator
    auto *sep1 = new QFrame;
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Plain);
    sep1->setFixedHeight(1);
    sep1->setStyleSheet(QString("background-color: %1; border: none;")
                            .arg(Theme::palette().border.name()));
    mainLayout->addWidget(sep1);

    // --- Section 2: Telemetry ---
    auto *telemetryLayout = new QGridLayout;
    telemetryLayout->setContentsMargins(0, 0, 0, 0);
    telemetryLayout->setVerticalSpacing(Theme::Space::sm);
    telemetryLayout->setHorizontalSpacing(Theme::Space::md);

    // Battery row
    auto *battLabel = new QLabel("Battery");
    battLabel->setStyleSheet(captionStyle());
    m_batteryBar = new QProgressBar;
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setValue(0);
    m_batteryBar->setTextVisible(false);
    m_batteryBar->setFixedHeight(7);
    m_batteryBar->setStyleSheet(meterStyle(Theme::palette().success));
    m_batteryPctLabel = new QLabel("--%");
    m_batteryPctLabel->setAlignment(Qt::AlignRight);
    m_batteryPctLabel->setFixedWidth(42);
    m_batteryPctLabel->setStyleSheet(valueStyle());
    m_voltageLabel = new QLabel;
    m_voltageLabel->setStyleSheet(captionStyle());

    telemetryLayout->addWidget(battLabel, 0, 0);
    telemetryLayout->addWidget(m_batteryBar, 0, 1);
    telemetryLayout->addWidget(m_batteryPctLabel, 0, 2);
    telemetryLayout->addWidget(m_voltageLabel, 1, 0, 1, 3);

    // Channel utilization row
    auto *chLabel = new QLabel("Ch Util");
    chLabel->setStyleSheet(captionStyle());
    m_chUtilBar = new QProgressBar;
    m_chUtilBar->setRange(0, 1000);
    m_chUtilBar->setValue(0);
    m_chUtilBar->setTextVisible(false);
    m_chUtilBar->setFixedHeight(7);
    m_chUtilBar->setStyleSheet(meterStyle(Theme::palette().info));
    m_chUtilLabel = new QLabel("--%");
    m_chUtilLabel->setAlignment(Qt::AlignRight);
    m_chUtilLabel->setFixedWidth(42);
    m_chUtilLabel->setStyleSheet(valueStyle());

    telemetryLayout->addWidget(chLabel, 2, 0);
    telemetryLayout->addWidget(m_chUtilBar, 2, 1);
    telemetryLayout->addWidget(m_chUtilLabel, 2, 2);

    // Air TX utilization row
    auto *airLabel = new QLabel("Air TX");
    airLabel->setStyleSheet(captionStyle());
    m_airTxBar = new QProgressBar;
    m_airTxBar->setRange(0, 1000);
    m_airTxBar->setValue(0);
    m_airTxBar->setTextVisible(false);
    m_airTxBar->setFixedHeight(7);
    m_airTxBar->setStyleSheet(meterStyle(Theme::palette().warning));
    m_airTxLabel = new QLabel("--%");
    m_airTxLabel->setAlignment(Qt::AlignRight);
    m_airTxLabel->setFixedWidth(42);
    m_airTxLabel->setStyleSheet(valueStyle());

    telemetryLayout->addWidget(airLabel, 3, 0);
    telemetryLayout->addWidget(m_airTxBar, 3, 1);
    telemetryLayout->addWidget(m_airTxLabel, 3, 2);

    // Environment row
    m_envTitleLabel = new QLabel("Environ");
    m_envTitleLabel->setStyleSheet(captionStyle());
    m_envLabel = new QLabel;
    m_envLabel->setStyleSheet(valueStyle());
    m_envLabel->setAlignment(Qt::AlignRight);
    telemetryLayout->addWidget(m_envTitleLabel, 4, 0);
    telemetryLayout->addWidget(m_envLabel, 4, 1, 1, 2);
    m_envTitleLabel->hide();
    m_envLabel->hide();

    // Uptime row
    m_uptimeTitleLabel = new QLabel("Uptime");
    m_uptimeTitleLabel->setStyleSheet(captionStyle());
    m_uptimeLabel = new QLabel;
    m_uptimeLabel->setStyleSheet(valueStyle());
    m_uptimeLabel->setAlignment(Qt::AlignRight);
    telemetryLayout->addWidget(m_uptimeTitleLabel, 5, 0);
    telemetryLayout->addWidget(m_uptimeLabel, 5, 1, 1, 2);
    m_uptimeTitleLabel->hide();
    m_uptimeLabel->hide();

    // Signal row
    m_signalTitleLabel = new QLabel("Signal");
    m_signalTitleLabel->setStyleSheet(captionStyle());
    m_signalLabel = new QLabel;
    m_signalLabel->setStyleSheet(valueStyle());
    m_signalLabel->setAlignment(Qt::AlignRight);
    telemetryLayout->addWidget(m_signalTitleLabel, 6, 0);
    telemetryLayout->addWidget(m_signalLabel, 6, 1, 1, 2);
    m_signalTitleLabel->hide();
    m_signalLabel->hide();

    mainLayout->addLayout(telemetryLayout);

    // Separator
    auto *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Plain);
    sep2->setFixedHeight(1);
    sep2->setStyleSheet(QString("background-color: %1; border: none;")
                            .arg(Theme::palette().border.name()));
    mainLayout->addWidget(sep2);

    // --- Section 3: Config ---
    auto *configLayout = new QGridLayout;
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setVerticalSpacing(Theme::Space::xs);
    configLayout->setHorizontalSpacing(Theme::Space::md);

    auto makeLabelPair = [&](int row, const QString &title) -> QLabel * {
        auto *titleLbl = new QLabel(title);
        titleLbl->setStyleSheet(captionStyle());
        auto *valueLbl = new QLabel("--");
        valueLbl->setStyleSheet(valueStyle());
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

        m_batteryBar->setStyleSheet(meterStyle(Theme::batteryColor(batt, node.isExternalPower)));
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

    // Environment telemetry
    if (node.hasEnvironmentTelemetry) {
        QStringList parts;
        if (node.temperature != 0.0f)
            parts << QString("%1\u00b0C").arg(node.temperature, 0, 'f', 1);
        if (node.relativeHumidity != 0.0f)
            parts << QString("%1% RH").arg(node.relativeHumidity, 0, 'f', 0);
        if (node.barometricPressure != 0.0f)
            parts << QString("%1 hPa").arg(node.barometricPressure, 0, 'f', 1);
        m_envLabel->setText(parts.join("  \u00b7  "));
        m_envTitleLabel->show();
        m_envLabel->show();
    }

    // Uptime
    if (node.uptimeSeconds > 0) {
        uint32_t secs = node.uptimeSeconds;
        uint32_t days = secs / 86400; secs %= 86400;
        uint32_t hours = secs / 3600; secs %= 3600;
        uint32_t mins = secs / 60;
        QStringList parts;
        if (days > 0) parts << QString("%1d").arg(days);
        if (hours > 0) parts << QString("%1h").arg(hours);
        parts << QString("%1m").arg(mins);
        m_uptimeLabel->setText(parts.join(" "));
        m_uptimeTitleLabel->show();
        m_uptimeLabel->show();
    }

    // Signal quality
    if (node.snr != 0.0f || node.rssi != 0) {
        QStringList parts;
        if (node.snr != 0.0f)
            parts << QString("SNR %1 dB").arg(node.snr, 0, 'f', 1);
        if (node.rssi != 0)
            parts << QString("RSSI %1 dBm").arg(node.rssi);
        m_signalLabel->setText(parts.join("  /  "));
        m_signalTitleLabel->show();
        m_signalLabel->show();
    }
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

void DashboardStatsWidget::onCheckFirmware()
{
    m_checkFirmwareButton->setEnabled(false);
    m_firmwareStatusLabel->setText("Checking...");
    m_firmwareStatusLabel->setStyleSheet(captionStyle());
    m_firmwareStatusLabel->show();

    QNetworkRequest request(QUrl("https://api.github.com/repos/meshtastic/firmware/releases/latest"));
    request.setRawHeader("Accept", "application/vnd.github.v3+json");
    request.setRawHeader("User-Agent", "MeshtasticVibeClient/1.0");

    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_checkFirmwareButton->setEnabled(true);

        if (reply->error() != QNetworkReply::NoError) {
            m_firmwareStatusLabel->setText("Check failed");
            m_firmwareStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::palette().danger.name()));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QString latestVersion = doc.object().value("tag_name").toString();
        QString downloadUrl = doc.object().value("html_url").toString();

        if (latestVersion.isEmpty()) {
            m_firmwareStatusLabel->setText("Could not parse version");
            m_firmwareStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::palette().danger.name()));
            return;
        }

        // Normalize versions for comparison (strip leading 'v')
        QString currentNorm = m_firmwareVersion;
        QString latestNorm = latestVersion;
        if (currentNorm.startsWith('v')) currentNorm = currentNorm.mid(1);
        if (latestNorm.startsWith('v')) latestNorm = latestNorm.mid(1);

        if (currentNorm.isEmpty()) {
            m_firmwareStatusLabel->setText(QString("Latest: %1").arg(latestVersion));
            m_firmwareStatusLabel->setStyleSheet(captionStyle());
        } else if (currentNorm == latestNorm) {
            m_firmwareStatusLabel->setText("Up to date");
            m_firmwareStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::palette().success.name()));
        } else {
            m_firmwareStatusLabel->setText(QString("<a href=\"%1\">Update: %2</a>").arg(downloadUrl, latestVersion));
            m_firmwareStatusLabel->setTextFormat(Qt::RichText);
            m_firmwareStatusLabel->setOpenExternalLinks(true);
            m_firmwareStatusLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(Theme::palette().warning.name()));
        }
        m_firmwareStatusLabel->show();
    });
}
