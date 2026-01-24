#include "AppSettingsTab.h"
#include "AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFormLayout>

AppSettingsTab::AppSettingsTab(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    loadSettings();
}

void AppSettingsTab::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);

    // Connection Settings Group
    QGroupBox *connectionGroup = new QGroupBox("Connection");
    QVBoxLayout *connectionLayout = new QVBoxLayout(connectionGroup);

    m_autoConnectCheck = new QCheckBox("Auto-connect to last used port on startup");
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, &AppSettingsTab::onAutoConnectChanged);
    connectionLayout->addWidget(m_autoConnectCheck);

    mainLayout->addWidget(connectionGroup);

    // Node Display Settings Group
    QGroupBox *nodesGroup = new QGroupBox("Node Display");
    QFormLayout *nodesLayout = new QFormLayout(nodesGroup);

    m_showOfflineNodesCheck = new QCheckBox("Show offline nodes in list");
    connect(m_showOfflineNodesCheck, &QCheckBox::toggled, this, &AppSettingsTab::onShowOfflineNodesChanged);
    nodesLayout->addRow(m_showOfflineNodesCheck);

    m_offlineThresholdSpin = new QSpinBox;
    m_offlineThresholdSpin->setRange(5, 1440);
    m_offlineThresholdSpin->setSuffix(" minutes");
    m_offlineThresholdSpin->setToolTip("Nodes not heard from within this time are considered offline");
    connect(m_offlineThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AppSettingsTab::onOfflineThresholdChanged);
    nodesLayout->addRow("Offline threshold:", m_offlineThresholdSpin);

    mainLayout->addWidget(nodesGroup);

    // Map Settings Group
    QGroupBox *mapGroup = new QGroupBox("Map");
    QFormLayout *mapLayout = new QFormLayout(mapGroup);

    m_tileServerCombo = new QComboBox;
    m_tileServerCombo->addItem("OpenStreetMap", "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png");
    m_tileServerCombo->addItem("OpenTopoMap", "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png");
    m_tileServerCombo->addItem("Stamen Terrain", "https://stamen-tiles.a.ssl.fastly.net/terrain/{z}/{x}/{y}.jpg");
    m_tileServerCombo->addItem("CartoDB Positron", "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png");
    m_tileServerCombo->addItem("CartoDB Dark Matter", "https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png");
    m_tileServerCombo->addItem("Custom...", "custom");
    connect(m_tileServerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AppSettingsTab::onTileServerChanged);
    mapLayout->addRow("Tile server:", m_tileServerCombo);

    m_customTileServerEdit = new QLineEdit;
    m_customTileServerEdit->setPlaceholderText("https://your-server/{z}/{x}/{y}.png");
    m_customTileServerEdit->setVisible(false);
    connect(m_customTileServerEdit, &QLineEdit::editingFinished,
            this, &AppSettingsTab::onCustomTileServerChanged);
    mapLayout->addRow("Custom URL:", m_customTileServerEdit);

    m_nodeBlinkCheck = new QCheckBox("Blink nodes on map when heard");
    m_nodeBlinkCheck->setToolTip("Shows a pulsing animation on nodes when they transmit");
    connect(m_nodeBlinkCheck, &QCheckBox::toggled, this, &AppSettingsTab::onNodeBlinkEnabledChanged);
    mapLayout->addRow(m_nodeBlinkCheck);

    m_nodeBlinkDurationSpin = new QSpinBox;
    m_nodeBlinkDurationSpin->setRange(1, 60);
    m_nodeBlinkDurationSpin->setSuffix(" seconds");
    m_nodeBlinkDurationSpin->setToolTip("How long the blink animation lasts");
    connect(m_nodeBlinkDurationSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AppSettingsTab::onNodeBlinkDurationChanged);
    mapLayout->addRow("Blink duration:", m_nodeBlinkDurationSpin);

    mainLayout->addWidget(mapGroup);

    // Notification Settings Group
    QGroupBox *notifyGroup = new QGroupBox("Notifications");
    QVBoxLayout *notifyLayout = new QVBoxLayout(notifyGroup);

    m_notificationsCheck = new QCheckBox("Enable desktop notifications for new messages");
    connect(m_notificationsCheck, &QCheckBox::toggled, this, &AppSettingsTab::onNotificationsChanged);
    notifyLayout->addWidget(m_notificationsCheck);

    m_soundCheck = new QCheckBox("Play sound for new messages");
    connect(m_soundCheck, &QCheckBox::toggled, this, &AppSettingsTab::onSoundChanged);
    notifyLayout->addWidget(m_soundCheck);

    mainLayout->addWidget(notifyGroup);

    // Packet Display Settings Group
    QGroupBox *packetsGroup = new QGroupBox("Packet Display");
    QVBoxLayout *packetsLayout = new QVBoxLayout(packetsGroup);

    m_hideLocalDevicePacketsCheck = new QCheckBox("Hide local device packets (show only RF traffic)");
    m_hideLocalDevicePacketsCheck->setToolTip(
        "When enabled, hides config/status packets from the connected device.\n"
        "Only shows actual mesh packets that are transmitted/received over RF.");
    connect(m_hideLocalDevicePacketsCheck, &QCheckBox::toggled,
            this, &AppSettingsTab::onHideLocalDevicePacketsChanged);
    packetsLayout->addWidget(m_hideLocalDevicePacketsCheck);

    mainLayout->addWidget(packetsGroup);

    // Spacer
    mainLayout->addStretch();

    // Version info at bottom
    QLabel *versionLabel = new QLabel("Meshtastic Vibe Client v0.1.0");
    versionLabel->setStyleSheet("color: #888; font-size: 11px;");
    versionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(versionLabel);
}

void AppSettingsTab::loadSettings()
{
    AppSettings *settings = AppSettings::instance();

    m_autoConnectCheck->setChecked(settings->autoConnect());
    m_showOfflineNodesCheck->setChecked(settings->showOfflineNodes());
    m_offlineThresholdSpin->setValue(settings->offlineThresholdMinutes());
    m_notificationsCheck->setChecked(settings->notificationsEnabled());
    m_soundCheck->setChecked(settings->soundEnabled());
    m_hideLocalDevicePacketsCheck->setChecked(settings->hideLocalDevicePackets());
    m_nodeBlinkCheck->setChecked(settings->mapNodeBlinkEnabled());
    m_nodeBlinkDurationSpin->setValue(settings->mapNodeBlinkDuration());

    // Find matching tile server or set to custom
    QString currentServer = settings->mapTileServer();
    bool found = false;
    for (int i = 0; i < m_tileServerCombo->count() - 1; i++) {
        if (m_tileServerCombo->itemData(i).toString() == currentServer) {
            m_tileServerCombo->setCurrentIndex(i);
            found = true;
            break;
        }
    }
    if (!found && !currentServer.isEmpty()) {
        m_tileServerCombo->setCurrentIndex(m_tileServerCombo->count() - 1);  // Custom
        m_customTileServerEdit->setText(currentServer);
        m_customTileServerEdit->setVisible(true);
    }
}

void AppSettingsTab::onAutoConnectChanged(bool checked)
{
    AppSettings::instance()->setAutoConnect(checked);
}

void AppSettingsTab::onShowOfflineNodesChanged(bool checked)
{
    AppSettings::instance()->setShowOfflineNodes(checked);
}

void AppSettingsTab::onOfflineThresholdChanged(int value)
{
    AppSettings::instance()->setOfflineThresholdMinutes(value);
}

void AppSettingsTab::onNotificationsChanged(bool checked)
{
    AppSettings::instance()->setNotificationsEnabled(checked);
}

void AppSettingsTab::onSoundChanged(bool checked)
{
    AppSettings::instance()->setSoundEnabled(checked);
}

void AppSettingsTab::onTileServerChanged(int index)
{
    QString serverUrl = m_tileServerCombo->itemData(index).toString();

    if (serverUrl == "custom") {
        m_customTileServerEdit->setVisible(true);
        // Don't save yet, wait for custom URL input
    } else {
        m_customTileServerEdit->setVisible(false);
        AppSettings::instance()->setMapTileServer(serverUrl);
    }
}

void AppSettingsTab::onCustomTileServerChanged()
{
    QString url = m_customTileServerEdit->text().trimmed();
    if (!url.isEmpty()) {
        AppSettings::instance()->setMapTileServer(url);
    }
}

void AppSettingsTab::onHideLocalDevicePacketsChanged(bool checked)
{
    AppSettings::instance()->setHideLocalDevicePackets(checked);
}

void AppSettingsTab::onNodeBlinkEnabledChanged(bool checked)
{
    AppSettings::instance()->setMapNodeBlinkEnabled(checked);
}

void AppSettingsTab::onNodeBlinkDurationChanged(int value)
{
    AppSettings::instance()->setMapNodeBlinkDuration(value);
}
