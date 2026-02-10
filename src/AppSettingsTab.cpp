#include "AppSettingsTab.h"
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

    m_showPacketFlowLinesCheck = new QCheckBox("Show packet flow lines on map");
    m_showPacketFlowLinesCheck->setToolTip("Draw animated lines showing packet paths between nodes");
    connect(m_showPacketFlowLinesCheck, &QCheckBox::toggled, this, &AppSettingsTab::onShowPacketFlowLinesChanged);
    mapLayout->addRow(m_showPacketFlowLinesCheck);

    mainLayout->addWidget(mapGroup);

    // Messages Settings Group
    QGroupBox *messagesGroup = new QGroupBox("Messages");
    QVBoxLayout *messagesLayout = new QVBoxLayout(messagesGroup);

    m_autoPingResponseCheck = new QCheckBox("Auto-respond to 'ping' direct messages with 'pong'");
    m_autoPingResponseCheck->setToolTip("When someone sends you a direct message containing just 'ping', automatically reply with 'pong'");
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
    m_savePacketsToDbCheck->setChecked(settings->savePacketsToDb());
    m_nodeBlinkCheck->setChecked(settings->mapNodeBlinkEnabled());
    m_nodeBlinkDurationSpin->setValue(settings->mapNodeBlinkDuration());
    m_showPacketFlowLinesCheck->setChecked(settings->showPacketFlowLines());
    m_autoPingResponseCheck->setChecked(settings->autoPingResponse());
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
    if (dark) {
        qApp->setStyleSheet(R"(
            QMainWindow, QWidget {
                background-color: #1e1e1e;
                color: #d4d4d4;
            }
            QTabWidget::pane {
                border: 1px solid #3c3c3c;
                background-color: #252526;
            }
            QTabBar::tab {
                background-color: #2d2d2d;
                color: #d4d4d4;
                padding: 8px 16px;
                border: 1px solid #3c3c3c;
            }
            QTabBar::tab:selected {
                background-color: #1e1e1e;
                border-bottom-color: #1e1e1e;
            }
            QTableWidget, QListWidget, QTreeWidget {
                background-color: #252526;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
                gridline-color: #3c3c3c;
            }
            QTableWidget::item, QListWidget::item, QTreeWidget::item {
                color: #d4d4d4;
            }
            QTableWidget::item:selected, QListWidget::item:selected, QTreeWidget::item:selected {
                background-color: #094771;
            }
            QHeaderView::section {
                background-color: #2d2d2d;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
                padding: 4px;
            }
            QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
                background-color: #3c3c3c;
                color: #d4d4d4;
                border: 1px solid #555;
                padding: 4px;
                border-radius: 3px;
            }
            QPushButton {
                background-color: #0e639c;
                color: white;
                border: none;
                padding: 6px 16px;
                border-radius: 3px;
            }
            QPushButton:hover {
                background-color: #1177bb;
            }
            QPushButton:pressed {
                background-color: #094771;
            }
            QPushButton:disabled {
                background-color: #3c3c3c;
                color: #888;
            }
            QGroupBox {
                border: 1px solid #3c3c3c;
                border-radius: 4px;
                margin-top: 8px;
                padding-top: 8px;
                color: #d4d4d4;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px;
            }
            QCheckBox, QRadioButton {
                color: #d4d4d4;
            }
            QLabel {
                color: #d4d4d4;
            }
            QToolBar {
                background-color: #2d2d2d;
                border: none;
                spacing: 4px;
            }
            QStatusBar {
                background-color: #007acc;
                color: white;
            }
            QMenuBar {
                background-color: #2d2d2d;
                color: #d4d4d4;
            }
            QMenuBar::item:selected {
                background-color: #094771;
            }
            QMenu {
                background-color: #252526;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
            }
            QMenu::item:selected {
                background-color: #094771;
            }
            QScrollBar:vertical {
                background-color: #1e1e1e;
                width: 12px;
            }
            QScrollBar::handle:vertical {
                background-color: #5a5a5a;
                border-radius: 4px;
                min-height: 20px;
            }
            QScrollBar::handle:vertical:hover {
                background-color: #787878;
            }
            QSplitter::handle {
                background-color: #3c3c3c;
            }
        )");
    } else {
        qApp->setStyleSheet("");  // Reset to default light theme
    }
}
