#include "AppSettingsTab.h"
#include "Theme.h"
#include "AppSettings.h"

#include <QApplication>
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

    m_hideNeverHeardCheck = new QCheckBox("Hide nodes never heard from");
    m_hideNeverHeardCheck->setToolTip(
        "The device's node database also lists nodes it learned about second-hand "
        "but has never received a packet from. They have no position, signal or "
        "telemetry, so they are hidden by default.");
    connect(m_hideNeverHeardCheck, &QCheckBox::toggled, this, &AppSettingsTab::onHideNeverHeardChanged);
    nodesLayout->addRow(m_hideNeverHeardCheck);

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
    m_tileServerCombo->addItem("Stamen Terrain", "https://tiles.stadiamaps.com/tiles/stamen_terrain/{z}/{x}/{y}.jpg");
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

    m_showPacketFlowLinesCheck = new QCheckBox("Show packet flow lines on map");
    m_showPacketFlowLinesCheck->setToolTip("Draw animated lines showing packet paths between nodes");
    connect(m_showPacketFlowLinesCheck, &QCheckBox::toggled, this, &AppSettingsTab::onShowPacketFlowLinesChanged);
    mapLayout->addRow(m_showPacketFlowLinesCheck);

    m_positionRefreshCombo = new QComboBox;
    m_positionRefreshCombo->addItem("Off", 0);
    m_positionRefreshCombo->addItem("30 seconds", 30);
    m_positionRefreshCombo->addItem("1 minute", 60);
    m_positionRefreshCombo->addItem("2 minutes", 120);
    m_positionRefreshCombo->addItem("5 minutes", 300);
    m_positionRefreshCombo->addItem("10 minutes", 600);
    m_positionRefreshCombo->setToolTip("Periodically request fresh GPS positions from all nodes");
    connect(m_positionRefreshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AppSettingsTab::onPositionRefreshChanged);
    mapLayout->addRow("Auto refresh positions:", m_positionRefreshCombo);

    mainLayout->addWidget(mapGroup);

    // Messages Settings Group
    QGroupBox *messagesGroup = new QGroupBox("Messages");
    QVBoxLayout *messagesLayout = new QVBoxLayout(messagesGroup);

    m_autoPingResponseCheck = new QCheckBox("Enable autoresponder for DMs (!ping, !nodes, !weather, etc)");
    m_autoPingResponseCheck->setToolTip(
        "Automatically respond to !commands in direct messages:\n"
        "!ping - pong\n"
        "!nodes - mesh node count\n"
        "!battery - battery/voltage status\n"
        "!uptime - device uptime\n"
        "!weather - temperature/humidity/pressure\n"
        "!signal - sender's signal quality\n"
        "!pos - GPS position\n"
        "!time - current UTC time\n"
        "!info - device name/model/firmware\n"
        "!help - list commands\n\n"
        "30 second cooldown per sender (except !ping)");
    connect(m_autoPingResponseCheck, &QCheckBox::toggled, this, &AppSettingsTab::onAutoPingResponseChanged);
    messagesLayout->addWidget(m_autoPingResponseCheck);

    mainLayout->addWidget(messagesGroup);

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

    m_savePacketsToDbCheck = new QCheckBox("Save packets to database (for long sessions)");
    m_savePacketsToDbCheck->setToolTip(
        "When enabled, all received packets are saved to the database.\n"
        "Useful for multi-day listening sessions. Old packets are auto-deleted after 7 days.");
    connect(m_savePacketsToDbCheck, &QCheckBox::toggled,
            this, &AppSettingsTab::onSavePacketsToDbChanged);
    packetsLayout->addWidget(m_savePacketsToDbCheck);

    mainLayout->addWidget(packetsGroup);

    // Appearance Settings Group
    QGroupBox *appearanceGroup = new QGroupBox("Appearance");
    QVBoxLayout *appearanceLayout = new QVBoxLayout(appearanceGroup);

    m_darkThemeCheck = new QCheckBox("Dark theme");
    m_darkThemeCheck->setToolTip("Switch between light and dark color schemes");
    connect(m_darkThemeCheck, &QCheckBox::toggled, this, &AppSettingsTab::onDarkThemeChanged);
    appearanceLayout->addWidget(m_darkThemeCheck);

    mainLayout->addWidget(appearanceGroup);

    // Export Data Group
    QGroupBox *exportGroup = new QGroupBox("Export Data");
    QVBoxLayout *exportLayout = new QVBoxLayout(exportGroup);

    QHBoxLayout *nodesExportLayout = new QHBoxLayout;
    QLabel *nodesLabel = new QLabel("Nodes:");
    m_exportNodesCsvBtn = new QPushButton("Export CSV");
    m_exportNodesJsonBtn = new QPushButton("Export JSON");
    m_exportNodesCsvBtn->setToolTip("Export all known nodes to a CSV file");
    m_exportNodesJsonBtn->setToolTip("Export all known nodes to a JSON file");
    connect(m_exportNodesCsvBtn, &QPushButton::clicked, this, &AppSettingsTab::onExportNodesCsv);
    connect(m_exportNodesJsonBtn, &QPushButton::clicked, this, &AppSettingsTab::onExportNodesJson);
    nodesExportLayout->addWidget(nodesLabel);
    nodesExportLayout->addWidget(m_exportNodesCsvBtn);
    nodesExportLayout->addWidget(m_exportNodesJsonBtn);
    nodesExportLayout->addStretch();
    exportLayout->addLayout(nodesExportLayout);

    QHBoxLayout *messagesExportLayout = new QHBoxLayout;
    QLabel *messagesLabel = new QLabel("Messages:");
    m_exportMessagesCsvBtn = new QPushButton("Export CSV");
    m_exportMessagesJsonBtn = new QPushButton("Export JSON");
    m_exportMessagesCsvBtn->setToolTip("Export all messages to a CSV file");
    m_exportMessagesJsonBtn->setToolTip("Export all messages to a JSON file");
    connect(m_exportMessagesCsvBtn, &QPushButton::clicked, this, &AppSettingsTab::onExportMessagesCsv);
    connect(m_exportMessagesJsonBtn, &QPushButton::clicked, this, &AppSettingsTab::onExportMessagesJson);
    messagesExportLayout->addWidget(messagesLabel);
    messagesExportLayout->addWidget(m_exportMessagesCsvBtn);
    messagesExportLayout->addWidget(m_exportMessagesJsonBtn);
    messagesExportLayout->addStretch();
    exportLayout->addLayout(messagesExportLayout);

    mainLayout->addWidget(exportGroup);

    // Local Database Group
    QGroupBox *localDbGroup = new QGroupBox("Local Database");
    QVBoxLayout *localDbLayout = new QVBoxLayout(localDbGroup);

    QLabel *clearNodeDbLabel = new QLabel(
        "Remove every node stored on this PC. The node list is rebuilt from the "
        "device's own node database on the next sync.");
    clearNodeDbLabel->setWordWrap(true);
    clearNodeDbLabel->setStyleSheet(Theme::mutedLabelStyle());
    localDbLayout->addWidget(clearNodeDbLabel);

    QFormLayout *retentionForm = new QFormLayout;
    m_retentionDaysSpin = new QSpinBox;
    m_retentionDaysSpin->setRange(0, 365);
    m_retentionDaysSpin->setSuffix(" days");
    m_retentionDaysSpin->setSpecialValueText("Keep forever");
    m_retentionDaysSpin->setToolTip(
        "How long to keep packets, telemetry, neighbor info and traceroutes. "
        "Pruned on each connect. Nodes and messages are never pruned.");
    connect(m_retentionDaysSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AppSettingsTab::onRetentionDaysChanged);
    retentionForm->addRow("History retention:", m_retentionDaysSpin);
    localDbLayout->addLayout(retentionForm);

    QHBoxLayout *clearNodeDbRow = new QHBoxLayout;
    m_clearNodeDbBtn = new QPushButton("Clear Nodes && Resync");
    m_clearNodeDbBtn->setToolTip("Delete all locally saved nodes, then re-download them from the device");
    connect(m_clearNodeDbBtn, &QPushButton::clicked, this, &AppSettingsTab::onClearNodeDatabase);
    clearNodeDbRow->addWidget(m_clearNodeDbBtn);
    clearNodeDbRow->addStretch();
    localDbLayout->addLayout(clearNodeDbRow);

    mainLayout->addWidget(localDbGroup);

    // Spacer
    mainLayout->addStretch();

    // Version info at bottom
    QLabel *versionLabel = new QLabel("Meshtastic Vibe Client v0.1.0");
    versionLabel->setStyleSheet(Theme::mutedLabelStyle());
    versionLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(versionLabel);
}

void AppSettingsTab::loadSettings()
{
    AppSettings *settings = AppSettings::instance();

    m_autoConnectCheck->setChecked(settings->autoConnect());
    m_showOfflineNodesCheck->setChecked(settings->showOfflineNodes());
    m_hideNeverHeardCheck->setChecked(settings->hideNeverHeardNodes());
    m_offlineThresholdSpin->setValue(settings->offlineThresholdMinutes());
    m_notificationsCheck->setChecked(settings->notificationsEnabled());
    m_soundCheck->setChecked(settings->soundEnabled());
    m_hideLocalDevicePacketsCheck->setChecked(settings->hideLocalDevicePackets());
    m_savePacketsToDbCheck->setChecked(settings->savePacketsToDb());
    m_nodeBlinkCheck->setChecked(settings->mapNodeBlinkEnabled());
    m_nodeBlinkDurationSpin->setValue(settings->mapNodeBlinkDuration());
    m_showPacketFlowLinesCheck->setChecked(settings->showPacketFlowLines());
    m_autoPingResponseCheck->setChecked(settings->autoPingResponse());

    m_retentionDaysSpin->setValue(settings->dataRetentionDays());

    int refreshSecs = settings->positionRefreshInterval();
    for (int i = 0; i < m_positionRefreshCombo->count(); i++) {
        if (m_positionRefreshCombo->itemData(i).toInt() == refreshSecs) {
            m_positionRefreshCombo->setCurrentIndex(i);
            break;
        }
    }
    m_darkThemeCheck->setChecked(settings->darkTheme());
    applyTheme(settings->darkTheme());

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

void AppSettingsTab::onHideNeverHeardChanged(bool checked)
{
    AppSettings::instance()->setHideNeverHeardNodes(checked);
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

void AppSettingsTab::onDarkThemeChanged(bool checked)
{
    AppSettings::instance()->setDarkTheme(checked);
    applyTheme(checked);
}

void AppSettingsTab::onAutoPingResponseChanged(bool checked)
{
    AppSettings::instance()->setAutoPingResponse(checked);
}

void AppSettingsTab::onShowPacketFlowLinesChanged(bool checked)
{
    AppSettings::instance()->setShowPacketFlowLines(checked);
}

void AppSettingsTab::onSavePacketsToDbChanged(bool checked)
{
    AppSettings::instance()->setSavePacketsToDb(checked);
}

void AppSettingsTab::onPositionRefreshChanged(int index)
{
    int secs = m_positionRefreshCombo->itemData(index).toInt();
    AppSettings::instance()->setPositionRefreshInterval(secs);
}

void AppSettingsTab::onClearNodeDatabase()
{
    emit clearNodeDatabaseRequested();
}

void AppSettingsTab::onRetentionDaysChanged(int value)
{
    AppSettings::instance()->setDataRetentionDays(value);
}

void AppSettingsTab::onExportNodesCsv()
{
    emit exportNodesRequested("csv");
}

void AppSettingsTab::onExportNodesJson()
{
    emit exportNodesRequested("json");
}

void AppSettingsTab::onExportMessagesCsv()
{
    emit exportMessagesRequested("csv");
}

void AppSettingsTab::onExportMessagesJson()
{
    emit exportMessagesRequested("json");
}

void AppSettingsTab::applyTheme(bool dark)
{
    Theme::apply(dark);
}
