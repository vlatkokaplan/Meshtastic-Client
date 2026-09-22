#include "MainWindow.h"
#include "SerialConnection.h"
#include "TcpConnection.h"
#include "BluetoothConnection.h"
#include "MeshtasticProtocol.h"
#include "NodeManager.h"
#include "PacketListWidget.h"
#include "TracerouteWidget.h"
#include "SignalScannerWidget.h"
#include "TelemetryGraphWidget.h"
#include "Database.h"
#include "MessagesWidget.h"
#include "ConfigWidget.h"
#include "DeviceConfig.h"
#include "AppSettings.h"
#include "AppSettingsTab.h"
#include "Theme.h"
#include "AnalyticsWidget.h"
#include "NodeTableWidget.h"
#include "ReplayBar.h"
#include "TopologyWidget.h"
#include "ConnectionDialog.h"
#include "SimulationConnection.h"

#include "MapWidget.h"
#include "DashboardStatsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QSqlQuery>
#include <QFileInfo>
#include <QFile>
#include <QScrollBar>
#include <QTimer>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QMenu>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextEdit>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCloseEvent>
#include <QSettings>
#include <QApplication>
#include <algorithm>
#include <cstdlib>
#include <ctime>

MainWindow::MainWindow(bool experimentalMode, bool testMode,
                       const QString &simulateScenario, QWidget *parent)
    : QMainWindow(parent), m_experimentalMode(experimentalMode), m_testMode(testMode)
{
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // Initialize app settings
    AppSettings::instance()->open();

    m_serial = new SerialConnection(this);
    m_tcp = new TcpConnection(this);
    m_bluetooth = new BluetoothConnection(this);
    m_protocol = new MeshtasticProtocol(this);
    m_nodeManager = new NodeManager(this);
    m_database = nullptr; // Database opened after connection with device-specific path

    m_mapWidget = nullptr;
    m_dashboardStats = nullptr;
    m_topologyWidget = nullptr;
    m_messagesWidget = nullptr;
    m_configWidget = nullptr;
    m_trayIcon = nullptr;

    setupUI();

    // Set up system tray for notifications
    if (QSystemTrayIcon::isSystemTrayAvailable())
    {
        m_trayIcon = new QSystemTrayIcon(this);
        m_trayIcon->setIcon(QIcon::fromTheme("network-wireless", QIcon(":/icon.png")));
        m_trayIcon->setToolTip("Meshtastic Vibe Client");
        m_trayIcon->show();
    }

    // Config heartbeat timer (fast heartbeat during config)
    m_configHeartbeatTimer = new QTimer(this);
    m_configHeartbeatTimer->setInterval(5000); // 5 seconds
    connect(m_configHeartbeatTimer, &QTimer::timeout, this, [this]()
            {
        if (isDeviceConnected()) {
            qDebug() << "[MainWindow] Sending config heartbeat";
            QByteArray heartbeat = m_protocol->createHeartbeatPacket();
            sendToDevice(heartbeat);
        } });

    // Persistent connection heartbeat (keeps connection alive for long sessions)
    m_connectionHeartbeatTimer = new QTimer(this);
    m_connectionHeartbeatTimer->setInterval(60000); // 60 seconds - slower than config heartbeat
    connect(m_connectionHeartbeatTimer, &QTimer::timeout, this, [this]()
            {
        if (isDeviceConnected()) {
            qDebug() << "[MainWindow] Sending connection keep-alive heartbeat";
            QByteArray heartbeat = m_protocol->createHeartbeatPacket();
            sendToDevice(heartbeat);
        } });

    m_positionRefreshTimer = new QTimer(this);
    connect(m_positionRefreshTimer, &QTimer::timeout, this, &MainWindow::onPositionRefreshTick);
    int refreshSecs = AppSettings::instance()->positionRefreshInterval();
    if (refreshSecs > 0)
        m_positionRefreshTimer->start(refreshSecs * 1000);

    // Connect signals
    connect(m_serial, &SerialConnection::connected,
            this, &MainWindow::onConnected);
    connect(m_serial, &SerialConnection::disconnected,
            this, &MainWindow::onDisconnected);
    connect(m_serial, &SerialConnection::dataReceived,
            this, &MainWindow::onDataReceived);
    connect(m_serial, &SerialConnection::errorOccurred,
            this, &MainWindow::onSerialError);

    // TCP connection signals
    connect(m_tcp, &TcpConnection::connected,
            this, &MainWindow::onConnected);
    connect(m_tcp, &TcpConnection::disconnected,
            this, &MainWindow::onDisconnected);
    connect(m_tcp, &TcpConnection::dataReceived,
            this, &MainWindow::onDataReceived);
    connect(m_tcp, &TcpConnection::errorOccurred,
            this, &MainWindow::onSerialError);

    // Bluetooth connection signals
    connect(m_bluetooth, &BluetoothConnection::connected,
            this, &MainWindow::onConnected);
    connect(m_bluetooth, &BluetoothConnection::disconnected,
            this, &MainWindow::onDisconnected);
    connect(m_bluetooth, &BluetoothConnection::dataReceived,
            this, &MainWindow::onDataReceived);
    connect(m_bluetooth, &BluetoothConnection::errorOccurred,
            this, &MainWindow::onSerialError);
    // BT discovery signals are wired to the ConnectionDialog when it's open

    connect(m_protocol, &MeshtasticProtocol::packetReceived,
            this, &MainWindow::onPacketReceived);
    connect(m_protocol, &MeshtasticProtocol::parseError,
            [this](const QString &error)
            {
                statusBar()->showMessage(error, 5000);
            });

    connect(m_nodeManager, &NodeManager::nodesChanged,
            this, [this]() {
                if (m_nodeTableWidget)
                    m_nodeTableWidget->markSortStale();
                refreshDbNodeCount();
                updateNodeList();
            });

    // Open the last-used database straight away. The analyst views - Analytics
    // and replay - work entirely on stored rows, and previously they were dead
    // until a radio attached, which is backwards: reviewing history is most
    // useful when away from the mesh. If a device does connect later,
    // openDatabaseForNode() either reloads this same database or switches.
    if (simulateScenario.isEmpty())
    {
        uint32_t lastNode = AppSettings::instance()->lastDatabaseNode();
        if (lastNode != 0)
            openDatabaseForNode(lastNode);
    }

    updateStatusLabel();

    // Auto-connect if enabled (skip when running in simulation mode)
    if (simulateScenario.isEmpty() && AppSettings::instance()->autoConnect())
    {
        QString lastPort = AppSettings::instance()->lastPort();
        if (!lastPort.isEmpty())
        {
            // Verify the port exists before auto-connecting
            QList<QSerialPortInfo> allPorts = SerialConnection::availablePorts();
            bool portFound = false;
            for (const QSerialPortInfo &info : allPorts)
            {
                if (info.portName() == lastPort)
                {
                    portFound = true;
                    break;
                }
            }
            if (portFound)
            {
                QTimer::singleShot(500, this, [this, lastPort]() {
                    connectSerial(lastPort);
                });
            }
        }
    }

    // Listen for settings changes
    connect(AppSettings::instance(), &AppSettings::settingChanged,
            this, &MainWindow::onSettingChanged);

    // Simulation mode — skip real connections and use fake device
    if (!simulateScenario.isEmpty()) {
        m_simulateMode = true;
        m_simulation = new SimulationConnection(this);
        connect(m_simulation, &SimulationConnection::connected,
                this, &MainWindow::onConnected);
        connect(m_simulation, &SimulationConnection::disconnected,
                this, &MainWindow::onDisconnected);
        connect(m_simulation, &SimulationConnection::dataReceived,
                this, &MainWindow::onDataReceived);

        SimulationConnection::Scenario sc = simulateScenario == "reconnect"
            ? SimulationConnection::Scenario::Reconnect
            : SimulationConnection::Scenario::Basic;

        QTimer::singleShot(300, this, [this, sc]() {
            m_connectButton->setEnabled(false);
            m_disconnectButton->setEnabled(true);
            m_simulation->start(sc);
        });
    }

    // Restore window state (geometry, splitter sizes)
    restoreWindowState();

}

MainWindow::~MainWindow()
{
    m_bluetooth->disconnectDevice();
    m_tcp->disconnectDevice();
    m_serial->disconnectDevice();
}

void MainWindow::setupUI()
{
    setWindowTitle("Meshtastic Client");

    setupToolbar();

    m_tabWidget = new QTabWidget;
    setCentralWidget(m_tabWidget);

    // Create ConfigWidget early so DeviceConfig is available for DashboardStatsWidget
    m_configWidget = new ConfigWidget;

    // Give protocol access to device config for packet decryption
    m_protocol->setDeviceConfig(m_configWidget->deviceConfig());

    setupMapTab();
    setupMessagesTab();
    setupPacketTab();

    // Traceroute tab (before Config)
    m_tracerouteWidget = new TracerouteWidget(m_nodeManager, m_database);
    m_tabWidget->addTab(m_tracerouteWidget, "Traceroutes");

    // Signal Scanner tab (experimental only)
    if (m_experimentalMode)
    {
        m_signalScannerWidget = new SignalScannerWidget(m_nodeManager);
        m_tabWidget->addTab(m_signalScannerWidget, "Signal Scanner");
    }
    else
    {
        m_signalScannerWidget = nullptr;
    }

    // Telemetry Graph tab
    m_telemetryGraphWidget = new TelemetryGraphWidget(m_nodeManager, m_database);
    m_tabWidget->addTab(m_telemetryGraphWidget, "Telemetry Graph");

    // Topology tab
    m_topologyWidget = new TopologyWidget(m_nodeManager);
    m_tabWidget->addTab(m_topologyWidget, "Topology");

    setupAnalyticsTab();
    setupConfigTab();

    // Connect traceroute selection to map + topology visualization
    connect(m_tracerouteWidget, &TracerouteWidget::tracerouteSelected,
            this, &MainWindow::onTracerouteSelected);

    // Connect traceroute request button → send packet to device
    connect(m_tracerouteWidget, &TracerouteWidget::tracerouteRequested,
            this, &MainWindow::requestTraceroute);

    // Status bar with cooldown indicator
    m_statusLabel = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(m_statusLabel);

    // Traceroute cooldown text label (hidden by default)
    m_tracerouteCooldownLabel = new QLabel;
    m_tracerouteCooldownLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; padding-right: 10px; }")
                                                 .arg(Theme::palette().warning.name()));
    m_tracerouteCooldownLabel->setMaximumWidth(180);
    m_tracerouteCooldownLabel->setVisible(false);
    statusBar()->addPermanentWidget(m_tracerouteCooldownLabel);

    // Traceroute cooldown timer
    m_tracerouteCooldownTimer = new QTimer(this);
    m_tracerouteCooldownTimer->setInterval(100); // Update every 100ms
    connect(m_tracerouteCooldownTimer, &QTimer::timeout, this, &MainWindow::onTracerouteCooldownTick);
}

void MainWindow::setupToolbar()
{
    QToolBar *toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    m_connectButton = new QPushButton("Connect");
    m_connectButton->setToolTip("Open connection dialog (Serial, TCP, or Bluetooth)");
    m_connectButton->setProperty("accent", true);  // primary action
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::showConnectionDialog);
    toolbar->addWidget(m_connectButton);

    m_disconnectButton = new QPushButton("Disconnect");
    m_disconnectButton->setEnabled(false);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnect);
    toolbar->addWidget(m_disconnectButton);

    toolbar->addSeparator();

    QPushButton *configButton = new QPushButton("Request Config");
    connect(configButton, &QPushButton::clicked, this, &MainWindow::requestConfig);
    toolbar->addWidget(configButton);

    m_rebootButton = new QPushButton("Reboot Device");
    m_rebootButton->setEnabled(false);
    m_rebootButton->setToolTip("Reboot the connected Meshtastic device");
    m_rebootButton->setProperty("danger", true);  // destructive action
    connect(m_rebootButton, &QPushButton::clicked, this, &MainWindow::rebootDevice);
    toolbar->addWidget(m_rebootButton);

    // Push the connection indicator to the right-hand end of the toolbar
    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    m_connectionPill = new QLabel;
    m_connectionPill->setTextFormat(Qt::RichText);
    m_connectionPill->setContentsMargins(0, 0, Theme::Space::md, 0);
    toolbar->addWidget(m_connectionPill);

    updateConnectionPill();
}

// A coloured dot plus a short label: connection state readable at a glance,
// without parsing the status bar text.
void MainWindow::updateConnectionPill()
{
    if (!m_connectionPill)
        return;

    const auto &p = Theme::palette();
    QColor dot;
    QString text;

    if (m_tcp->isReconnecting())
    {
        dot = p.warning;
        text = "Reconnecting";
    }
    else if (isDeviceConnected())
    {
        dot = p.success;
        text = connectedDeviceName();
    }
    else
    {
        dot = p.textDisabled;
        text = "Disconnected";
    }

    m_connectionPill->setText(
        QString("<span style='color:%1; font-size:15px;'>&#9679;</span>"
                "<span style='color:%2; font-size:12px;'> %3</span>")
            .arg(dot.name(), p.textMuted.name(), text.toHtmlEscaped()));
}

void MainWindow::setupMapTab()
{
    QWidget *mapTab = new QWidget;
    // Vertical: the map/node-list splitter fills the tab, with the replay bar
    // stacked beneath it rather than beside it.
    QVBoxLayout *layout = new QVBoxLayout(mapTab);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Splitter for map and node list
    m_mapSplitter = new QSplitter(Qt::Horizontal);

    // Map widget
    m_mapWidget = new MapWidget(m_nodeManager);
    m_mapSplitter->addWidget(m_mapWidget);

    // Node list sidebar
    QWidget *sidebar = new QWidget;
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(Theme::Space::sm);

    // Dashboard stats panel
    m_dashboardStats = new DashboardStatsWidget(m_nodeManager, m_configWidget->deviceConfig());
    sidebarLayout->addWidget(m_dashboardStats);

    m_nodeTableWidget = new NodeTableWidget(m_nodeManager);
    connect(m_nodeTableWidget, &NodeTableWidget::nodeActivated,
            this, &MainWindow::onNodeActivated);
    connect(m_nodeTableWidget, &NodeTableWidget::directMessageRequested,
            this, [this](uint32_t n) {
                if (m_messagesWidget)
                {
                    m_messagesWidget->startDirectMessage(n);
                    m_tabWidget->setCurrentWidget(m_messagesWidget);
                }
            });
    connect(m_nodeTableWidget, &NodeTableWidget::tracerouteRequested,
            this, &MainWindow::requestTraceroute);
    connect(m_nodeTableWidget, &NodeTableWidget::nodeInfoRequested,
            this, &MainWindow::requestNodeInfo);
    connect(m_nodeTableWidget, &NodeTableWidget::telemetryRequested,
            this, &MainWindow::requestTelemetry);
    connect(m_nodeTableWidget, &NodeTableWidget::positionRequested,
            this, &MainWindow::requestPosition);
    connect(m_nodeTableWidget, &NodeTableWidget::trackRequested,
            this, &MainWindow::showNodeTrack);
    connect(m_nodeTableWidget, &NodeTableWidget::clearTrackRequested, this, [this]() {
        if (m_mapWidget)
            m_mapWidget->clearTrack();
        statusBar()->showMessage("Movement history cleared", 3000);
    });
    connect(m_nodeTableWidget, &NodeTableWidget::centerOnMapRequested,
            this, &MainWindow::centerMapOnNode);
    sidebarLayout->addWidget(m_nodeTableWidget);

    m_mapSplitter->addWidget(sidebar);
    m_mapSplitter->setSizes({800, 200});

    // Stretch 1 so the splitter takes all spare height and the replay bar below
    // keeps only its natural size.
    layout->addWidget(m_mapSplitter, 1);

    // Replay of recorded activity, beneath the map. Reads the packet log only.
    m_replayBar = new ReplayBar;
    connect(m_replayBar, &ReplayBar::packetReplayed, this, &MainWindow::onPacketReplayed);
    connect(m_replayBar, &ReplayBar::replayStarted, this, [this]() {
        if (m_mapWidget)
            m_mapWidget->clearTrack();
        statusBar()->showMessage("Replaying recorded activity - nothing is transmitted", 4000);
    });
    layout->addWidget(m_replayBar);

    m_tabWidget->addTab(mapTab, "Map");
}

void MainWindow::setupMessagesTab()
{
    m_messagesWidget = new MessagesWidget(m_nodeManager);
    m_messagesTabIndex = m_tabWidget->addTab(m_messagesWidget, "Messages");

    // Connect send message signal
    connect(m_messagesWidget, &MessagesWidget::sendMessage,
            this, &MainWindow::onSendMessage);

    // Connect send reaction signal
    connect(m_messagesWidget, &MessagesWidget::sendReaction,
            this, &MainWindow::onSendReaction);

    // Connect node click to navigate to that node
    connect(m_messagesWidget, &MessagesWidget::nodeClicked,
            this, &MainWindow::navigateToNode);

    // Update tab title with unread count
    connect(m_messagesWidget, &MessagesWidget::unreadCountChanged,
            this, [this](int count) {
                if (count > 0)
                    m_tabWidget->setTabText(m_messagesTabIndex, QString("Messages (%1)").arg(count));
                else
                    m_tabWidget->setTabText(m_messagesTabIndex, "Messages");
            });
}

void MainWindow::setupPacketTab()
{
    m_packetList = new PacketListWidget(m_nodeManager);
    m_tabWidget->addTab(m_packetList, "Packets");
}

void MainWindow::setupAnalyticsTab()
{
    m_analyticsWidget = new AnalyticsWidget(m_nodeManager, m_configWidget->deviceConfig());
    m_tabWidget->addTab(m_analyticsWidget, "Analytics");
}

void MainWindow::setupConfigTab()
{
    m_tabWidget->addTab(m_configWidget, "Config");

    // Connect config save signals
    connect(m_configWidget, &ConfigWidget::saveLoRaConfig,
            this, &MainWindow::onSaveLoRaConfig);
    connect(m_configWidget, &ConfigWidget::saveDeviceConfig,
            this, &MainWindow::onSaveDeviceConfig);
    connect(m_configWidget, &ConfigWidget::savePositionConfig,
            this, &MainWindow::onSavePositionConfig);
    connect(m_configWidget, &ConfigWidget::saveChannelConfig,
            this, &MainWindow::onSaveChannelConfig);

    // Connect export signals from AppSettingsTab
    AppSettingsTab *appSettings = m_configWidget->appSettingsTab();
    if (appSettings)
    {
        connect(appSettings, &AppSettingsTab::exportNodesRequested,
                this, &MainWindow::onExportNodes);
        connect(appSettings, &AppSettingsTab::exportMessagesRequested,
                this, &MainWindow::onExportMessages);
        connect(appSettings, &AppSettingsTab::clearNodeDatabaseRequested,
                this, &MainWindow::onClearNodeDatabase);
        connect(appSettings, &AppSettingsTab::forgetRadioRequested,
                this, &MainWindow::onForgetRadio);
    }
}

void MainWindow::showConnectionDialog()
{
    ConnectionDialog dialog(m_bluetooth, this);

    // Wire BT discovery signals to dialog while it's open
    connect(m_bluetooth, &BluetoothConnection::deviceDiscovered,
            &dialog, &ConnectionDialog::onBtDeviceDiscovered);
    connect(m_bluetooth, &BluetoothConnection::scanFinished,
            &dialog, &ConnectionDialog::onBtScanFinished);

    if (dialog.exec() != QDialog::Accepted)
        return;

    ConnectionDialog::ConnectionResult result = dialog.result();

    switch (result.type) {
    case ConnectionDialog::ConnectionType::Serial:
        connectSerial(result.serialPort);
        break;
    case ConnectionDialog::ConnectionType::Tcp:
        AppSettings::instance()->setLastTcpHost(
            result.tcpHost + ":" + QString::number(result.tcpPort));
        connectTcp(result.tcpHost, result.tcpPort);
        break;
    case ConnectionDialog::ConnectionType::Bluetooth:
        connectBluetooth(result.btDevice);
        break;
    default:
        break;
    }
}

void MainWindow::connectSerial(const QString &port)
{
    m_connectButton->setEnabled(false);
    statusBar()->showMessage("Connecting to " + port + "...");
    if (!m_serial->connectToPort(port))
        m_connectButton->setEnabled(true);
}

void MainWindow::connectTcp(const QString &host, quint16 port)
{
    m_connectButton->setEnabled(false);
    statusBar()->showMessage(QString("Connecting to %1:%2...").arg(host).arg(port));
    m_tcp->connectToHost(host, port);
}

void MainWindow::connectBluetooth(const QBluetoothDeviceInfo &device)
{
    m_connectButton->setEnabled(false);
    statusBar()->showMessage("Connecting via Bluetooth...");
    m_bluetooth->connectToDevice(device);
}

void MainWindow::disconnect()
{
    m_bluetooth->disconnectDevice();
    m_tcp->disconnectDevice();
    m_serial->disconnectDevice();
}

void MainWindow::rebootDevice()
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    if (myNode == 0)
    {
        statusBar()->showMessage("Node info not available yet", 3000);
        return;
    }

    // Ask for confirmation
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Reboot Device",
        "Are you sure you want to reboot the connected device?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    // Send reboot command (5 second delay)
    QByteArray packet = m_protocol->createRebootPacket(myNode, myNode, 5);
    sendToDevice(packet);

    statusBar()->showMessage("Reboot command sent. Device will restart in 5 seconds...", 5000);
}

void MainWindow::onConnected()
{
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
    m_rebootButton->setEnabled(true);

    // Save last connection info
    if (m_serial->isConnected())
    {
        AppSettings::instance()->setLastPort(m_serial->connectedPortName());
    }

    // Start persistent heartbeat for long sessions
    m_connectionHeartbeatTimer->start();

    // Clean up old data on connect (runs in background)
    QTimer::singleShot(5000, this, [this]() {
        if (m_database) {
            // The raw packet log grows fastest by far and is pruned on its own
            // schedule; telemetry, traceroutes and neighbour info are small and
            // feed the Analytics views, so they are kept much longer.
            int packetDays = AppSettings::instance()->packetRetentionDays();
            if (packetDays > 0)
                m_database->deleteOldPackets(packetDays);

            int historyDays = AppSettings::instance()->dataRetentionDays();
            if (historyDays > 0)
            {
                m_database->deleteTelemetryHistory(historyDays);
                m_database->deleteOldNeighborInfo(historyDays);
                m_database->deleteTraceroutes(historyDays);
            }
        }
    });

    // Clear any stale partial frame from the previous session
    m_protocol->resetParser();

    updateStatusLabel();
    statusBar()->showMessage("Connected", 3000);

    // Request config after a short delay to let the device initialize
    QTimer::singleShot(500, this, &MainWindow::requestConfig);
}

void MainWindow::onDisconnected()
{
    // If TCP is auto-reconnecting, keep all state intact - just update status
    if (m_tcp->isReconnecting()) {
        m_statusLabel->setText("Reconnecting...");
        statusBar()->showMessage("Connection lost, reconnecting...", 0);
        return;
    }

    // Only update UI if no transport is still connected
    if (isDeviceConnected())
        return;

    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
    m_rebootButton->setEnabled(false);

    // Stop heartbeat timers
    m_connectionHeartbeatTimer->stop();
    m_configHeartbeatTimer->stop();

    // Close database and clear nodes
    closeDatabase();
    m_openNodeNum = 0;

    updateStatusLabel();
    statusBar()->showMessage("Disconnected", 3000);
}

void MainWindow::onDataReceived(const QByteArray &data)
{
    m_protocol->processIncomingData(data);
}

void MainWindow::onPacketReceived(const MeshtasticProtocol::DecodedPacket &packet)
{
    // Add to packet list
    m_packetList->addPacket(packet);

    // Save to database if enabled
    if (m_database && AppSettings::instance()->savePacketsToDb())
    {
        Database::PacketRecord rec;
        rec.timestamp = packet.timestamp;
        rec.packetType = static_cast<int>(packet.type);
        rec.fromNode = packet.from;
        rec.toNode = packet.to;
        rec.portNum = static_cast<int>(packet.portNum);
        rec.channel = packet.channelIndex;
        rec.typeName = packet.typeName;
        rec.rawData = packet.rawData;
        // Serialize fields to JSON
        QJsonObject fieldsObj = QJsonObject::fromVariantMap(packet.fields);
        rec.fieldsJson = QString::fromUtf8(QJsonDocument(fieldsObj).toJson(QJsonDocument::Compact));
        m_database->savePacket(rec);
    }

    // Check for session key in admin responses
    if (packet.fields.contains("sessionPasskey"))
    {
        QByteArray sessionKey = packet.fields["sessionPasskey"].toByteArray();
        if (!sessionKey.isEmpty() && m_protocol)
        {
            m_protocol->setSessionKey(sessionKey);
            qDebug() << "[MainWindow] Session key stored, size:" << sessionKey.size();
        }
    }

    // Process packet for node tracking
    switch (packet.type)
    {
    case MeshtasticProtocol::PacketType::MyInfo:
        if (packet.fields.contains("myNodeNum"))
        {
            uint32_t myNodeNum = packet.fields["myNodeNum"].toUInt();
            m_nodeManager->setMyNodeNum(myNodeNum);
            openDatabaseForNode(myNodeNum);
        }
        break;

    case MeshtasticProtocol::PacketType::NodeInfo:
        m_nodeManager->updateNodeFromPacket(packet.fields);
        break;

    case MeshtasticProtocol::PacketType::Channel:
        if (packet.fields.contains("index"))
        {
            int index = packet.fields["index"].toInt();
            QString name = packet.fields.value("channelName").toString();
            int role = packet.fields.value("role", 0).toInt();
            // role: 0=disabled, 1=primary, 2=secondary
            bool enabled = (role > 0);

            qDebug() << "<<< Received channel from device - index:" << index
                     << "name:" << name << "role:" << role;

            // Update MessagesWidget
            if (m_messagesWidget)
            {
                m_messagesWidget->setChannel(index, name, enabled);
            }

            // Update DeviceConfig for config tab
            if (m_configWidget && m_configWidget->deviceConfig())
            {
                m_configWidget->deviceConfig()->updateFromChannelPacket(packet.fields);
            }
        }
        break;

    case MeshtasticProtocol::PacketType::Config:
        if (m_configWidget && m_configWidget->deviceConfig())
        {
            QString configType = packet.fields.value("configType").toString();
            DeviceConfig *devConfig = m_configWidget->deviceConfig();

            qDebug() << "Received Config packet, type:" << configType;

            if (configType == "lora")
            {
                qDebug() << "  LoRa config - region:" << packet.fields.value("region")
                         << "preset:" << packet.fields.value("modemPreset")
                         << "hopLimit:" << packet.fields.value("hopLimit");
                devConfig->updateFromLoRaPacket(packet.fields);
            }
            else if (configType == "device")
            {
                qDebug() << "  Device config - role:" << packet.fields.value("role");
                devConfig->updateFromDevicePacket(packet.fields);
            }
            else if (configType == "position")
            {
                qDebug() << "  Position config - gpsMode:" << packet.fields.value("gpsMode");
                devConfig->updateFromPositionPacket(packet.fields);
            }
        }
        break;

    case MeshtasticProtocol::PacketType::PacketReceived:
    {
        // Visualize packet flow on map (if enabled in settings or experimental mode)
        bool showLines = m_experimentalMode || AppSettings::instance()->showPacketFlowLines();
        if (showLines && m_mapWidget)
        {
            uint32_t fromNode = packet.from;
            uint32_t toNode = packet.to;

            // Only draw line for direct packets (not broadcasts)
            if (toNode != 0xFFFFFFFF && toNode != 0 && fromNode != 0)
            {
                qDebug() << "[Experimental] Packet flow: from" << QString::number(fromNode, 16)
                         << "to" << QString::number(toNode, 16);

                // Check if both nodes have positions
                if (m_nodeManager->hasNode(fromNode) && m_nodeManager->hasNode(toNode))
                {
                    NodeInfo nodeFrom = m_nodeManager->getNode(fromNode);
                    NodeInfo nodeTo = m_nodeManager->getNode(toNode);

                    qDebug() << "[Experimental] Nodes exist. From hasPos:" << nodeFrom.hasPosition
                             << "lat:" << nodeFrom.latitude << "lon:" << nodeFrom.longitude;
                    qDebug() << "[Experimental] To hasPos:" << nodeTo.hasPosition
                             << "lat:" << nodeTo.latitude << "lon:" << nodeTo.longitude;

                    if (nodeFrom.hasPosition && nodeTo.hasPosition)
                    {
                        qDebug() << "[Experimental] Drawing packet flow line";
                        m_mapWidget->drawPacketFlow(fromNode, toNode, nodeFrom.latitude, nodeFrom.longitude,
                                                    nodeTo.latitude, nodeTo.longitude);
                    }
                }
                else
                {
                    qDebug() << "[Experimental] One or both nodes missing. From exists:" << m_nodeManager->hasNode(fromNode)
                             << "To exists:" << m_nodeManager->hasNode(toNode);
                }
            }
        }

        // Check if we should ignore packets from local device
        bool isFromLocalNode = (packet.from == m_nodeManager->myNodeNum());
        bool hideLocal = AppSettings::instance()->hideLocalDevicePackets();

        // Update node info from received packets (skip local node if hiding)
        if (packet.from != 0 && !(isFromLocalNode && hideLocal))
        {
            if (packet.fields.contains("rxSnr") || packet.fields.contains("rxRssi"))
            {
                int hops = -1;
                if (packet.fields.contains("hopStart") && packet.fields.contains("hopLimit"))
                {
                    hops = packet.fields["hopStart"].toInt() - packet.fields["hopLimit"].toInt();
                }
                m_nodeManager->updateNodeSignal(
                    packet.from,
                    packet.fields.value("rxSnr", 0).toFloat(),
                    packet.fields.value("rxRssi", 0).toInt(),
                    hops);
            }
        }

        // Handle specific port types (skip local node updates if hiding)
        switch (packet.portNum)
        {
        case MeshtasticProtocol::PortNum::Position:
            if (packet.fields.contains("latitude") && packet.fields.contains("longitude"))
            {
                if (!(isFromLocalNode && hideLocal))
                {
                    m_nodeManager->updateNodePosition(
                        packet.from,
                        packet.fields["latitude"].toDouble(),
                        packet.fields["longitude"].toDouble(),
                        packet.fields.value("altitude", 0).toInt());
                }
            }
            break;

        case MeshtasticProtocol::PortNum::NodeInfo:
            if (!(isFromLocalNode && hideLocal))
            {
                m_nodeManager->updateNodeUser(
                    packet.from,
                    packet.fields.value("longName").toString(),
                    packet.fields.value("shortName").toString(),
                    packet.fields.value("userId").toString(),
                    MeshtasticProtocol::nodeIdToString(packet.from));
            }
            break;

        case MeshtasticProtocol::PortNum::Telemetry:
            if (!(isFromLocalNode && hideLocal))
            {
                m_nodeManager->updateNodeTelemetry(packet.from, packet.fields);

                // Save telemetry to history
                if (m_database)
                {
                    NodeInfo node = m_nodeManager->getNode(packet.from);
                    Database::TelemetryRecord rec;
                    rec.nodeNum = packet.from;
                    rec.timestamp = QDateTime::currentDateTime();
                    rec.temperature = node.temperature;
                    rec.humidity = node.relativeHumidity;
                    rec.pressure = node.barometricPressure;
                    rec.batteryLevel = node.batteryLevel;
                    rec.voltage = node.voltage;
                    rec.snr = node.snr;
                    rec.rssi = node.rssi;
                    rec.channelUtil = node.channelUtilization;
                    rec.airUtilTx = node.airUtilTx;
                    m_database->saveTelemetryRecord(rec);

                    // Notify telemetry graph widget
                    if (m_telemetryGraphWidget)
                        m_telemetryGraphWidget->onTelemetryReceived(packet.from);
                }
            }
            break;

        case MeshtasticProtocol::PortNum::TextMessage:
        {
            // A message belongs in the Messages tab when we know which of our
            // channels it arrived on: either the device decoded it, or we
            // decrypted it with the key of a configured channel, which sets
            // resolvedChannel.
            //
            // Three cases, and each belongs somewhere different:
            //   the device decoded it            -> one of our channels
            //   we decrypted it via a channel key -> one of our channels
            //   we recovered it by key sweep      -> someone else's channel
            // Everything the app decrypted used to be dropped, which lost the
            // middle case entirely and left the last one invisible.
            const bool deviceDecoded = !packet.fields.contains("decrypted");
            const bool onKnownChannel = packet.fields.contains("resolvedChannel");

            // Traffic recovered by sweeping the well-known keys belongs to a
            // channel we are not configured for. It is readable, so dropping it
            // silently is wrong, but it must not be mixed in with our own
            // channels either - it gets a read-only conversation of its own,
            // named by the channel hash it arrived under.
            if (m_messagesWidget && packet.fields.contains("text")
                && !deviceDecoded && !onKnownChannel)
            {
                m_messagesWidget->addForeignChannel(packet.channelIndex);
            }

            if (m_messagesWidget && packet.fields.contains("text"))
            {
                const bool foreign = !deviceDecoded && !onKnownChannel;
                ChatMessage msg;
                msg.fromNode = packet.from;
                msg.toNode = packet.to;
                msg.text = packet.fields["text"].toString();
                msg.channelIndex = foreign ? foreignChannelKey(packet.channelIndex)
                                           : packet.channelIndex;
                msg.timestamp = QDateTime::currentDateTime();
                msg.packetId = packet.fields.value("packetId", 0).toUInt();
                m_messagesWidget->addMessage(msg);

                // Autoresponder: handle !commands in DMs
                uint32_t myNode = m_nodeManager->myNodeNum();
                // Never autorespond on a channel we hold no key for: the reply
                // could not be encrypted for it, and answering traffic we only
                // happened to overhear is not ours to do.
                bool isDM = !foreign && (packet.to == myNode && packet.to != 0xFFFFFFFF);
                bool isFromOther = (packet.from != myNode);
                QString trimmed = msg.text.trimmed();

                if (isDM && isFromOther && AppSettings::instance()->autoPingResponse())
                {
                    // Normalize: accept both "!ping" and "ping" (legacy)
                    QString cmd = trimmed.toLower();
                    if (!cmd.startsWith('!') && cmd == "ping")
                        cmd = "!ping";

                    if (cmd.startsWith('!'))
                    {
                        // Per-sender cooldown (except !ping which is always allowed)
                        bool cooldownOk = true;
                        if (cmd != "!ping")
                        {
                            QDateTime now = QDateTime::currentDateTime();
                            if (m_autoresponderCooldowns.contains(packet.from))
                            {
                                int elapsed = m_autoresponderCooldowns[packet.from].secsTo(now);
                                if (elapsed < AUTORESPONDER_COOLDOWN_SECS)
                                    cooldownOk = false;
                            }
                            if (cooldownOk)
                                m_autoresponderCooldowns[packet.from] = now;
                        }

                        if (cooldownOk)
                        {
                            QString response = handleAutoresponderCommand(cmd, packet.from);
                            if (!response.isEmpty())
                            {
                                qDebug() << "[Autoresponder]" << cmd << "from"
                                         << QString::number(packet.from, 16) << "->" << response;
                                QTimer::singleShot(500, this, [this, response, fromNode = packet.from]() {
                                    onSendMessage(response, fromNode, 0);
                                });
                            }
                        }
                    }
                }

                // Show notification for incoming messages (not from ourselves)
                if (packet.from != m_nodeManager->myNodeNum())
                {
                    NodeInfo fromNode = m_nodeManager->getNode(packet.from);
                    QString senderName = fromNode.longName.isEmpty()
                                             ? MeshtasticProtocol::nodeIdToString(packet.from)
                                             : fromNode.longName;
                    showNotification(QString("Message from %1").arg(senderName), msg.text);
                }
            }
            break;
        }

        case MeshtasticProtocol::PortNum::Traceroute:
            if (packet.fields.contains("route") || packet.fields.contains("routeBack"))
            {
                showTracerouteResult(packet);
                m_tracerouteWidget->addTraceroute(packet);
                // For a response: packet.from=responder, packet.to=requester.
                // Full path: requester(packet.to) → route → responder(packet.from)
                if (m_topologyWidget)
                    m_topologyWidget->handleTraceroute(packet.to, packet.from, packet.fields);
            }
            break;

        case MeshtasticProtocol::PortNum::Neighborinfo:
            // Forward neighbor info to topology widget
            if (m_topologyWidget) {
                m_topologyWidget->handleNeighborInfo(packet.from, packet.fields);
            }
            break;

        case MeshtasticProtocol::PortNum::Routing:
            // Handle routing responses to update message status
            if (m_messagesWidget && packet.fields.contains("errorReason"))
            {
                uint32_t packetId = packet.fields.value("packetId", 0).toUInt();
                int errorReason = packet.fields["errorReason"].toInt();

                // Reason 0 = NONE (success/ACK), only log actual errors
                if (errorReason == 0)
                {
                    qDebug() << "Message ACK received for packet" << packetId;

                    // Check if this is a delivery confirmation from a specific node (private message acknowledgment)
                    // A delivery confirmation is a routing ACK coming from the destination node (not a relay)
                    // The packet.from will be the node that received our message
                    uint32_t myNode = m_nodeManager->myNodeNum();
                    if (packet.from != myNode)
                    {
                        // This routing ACK came from an intermediate node or destination
                        // If we have a message to this node with matching packetId, mark it as delivered
                        qDebug() << "Delivery confirmation from node" << QString::number(packet.from, 16);
                        m_messagesWidget->updateMessageDelivered(packetId, packet.from);
                    }
                }
                else
                {
                    qDebug() << "Routing error for packet" << packetId << "- reason:" << errorReason;
                }

                m_messagesWidget->updateMessageStatus(packetId, errorReason);
            }
            break;

        case MeshtasticProtocol::PortNum::Admin:
            // Handle admin config responses
            if (packet.fields.contains("configType") && m_configWidget && m_configWidget->deviceConfig())
            {
                QString configType = packet.fields.value("configType").toString();
                DeviceConfig *devConfig = m_configWidget->deviceConfig();

                qDebug() << "Admin config response received, type:" << configType;

                if (configType == "lora")
                {
                    devConfig->updateFromLoRaPacket(packet.fields);
                }
                else if (configType == "device")
                {
                    devConfig->updateFromDevicePacket(packet.fields);
                }
                else if (configType == "position")
                {
                    devConfig->updateFromPositionPacket(packet.fields);
                }
            }
            break;

        default:
            break;
        }
        break;
    }

    case MeshtasticProtocol::PacketType::Metadata:
        if (packet.fields.contains("firmwareVersion"))
        {
            m_firmwareVersion = packet.fields["firmwareVersion"].toString();
            if (m_dashboardStats)
            {
                m_dashboardStats->setFirmwareVersion(m_firmwareVersion);
            }
        }
        if (packet.fields.contains("hwModel"))
        {
            uint32_t myNode = m_nodeManager->myNodeNum();
            int hwId = packet.fields["hwModel"].toInt();
            qDebug() << "[MainWindow] Metadata received - hwModel ID:" << hwId;
            if (myNode != 0)
            {
                m_nodeManager->updateNodeUser(myNode, "", "", "", m_nodeManager->hwModelToString(hwId));
            }
        }
        break;

    case MeshtasticProtocol::PacketType::ConfigCompleteId:
        if (packet.fields.contains("configId"))
        {
            onConfigCompleteIdReceived(packet.fields["configId"].toUInt());
        }
        break;

    default:
        break;
    }

    updateStatusLabel();
}

void MainWindow::onSerialError(const QString &error)
{
    // Suppress button flicker during TCP auto-reconnect attempts
    if (m_tcp->isReconnecting())
        return;
    if (!isDeviceConnected())
        m_connectButton->setEnabled(true);
    statusBar()->showMessage("Error: " + error, 5000);
}

void MainWindow::onNodeActivated(uint32_t nodeNum)
{
    centerMapOnNode(nodeNum);
}

void MainWindow::navigateToNode(uint32_t nodeNum)
{
    m_tabWidget->setCurrentIndex(0);   // Map tab
    if (m_nodeTableWidget)
        m_nodeTableWidget->selectNode(nodeNum);

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    if (node.hasPosition && m_mapWidget)
    {
        m_mapWidget->centerOnLocation(node.latitude, node.longitude);
        m_mapWidget->setZoomLevel(14);
        m_mapWidget->selectNode(nodeNum);
    }
}

void MainWindow::onTracerouteSelected(uint32_t fromNode, uint32_t toNode)
{
    Q_UNUSED(fromNode);
    Q_UNUSED(toNode);

    if (!m_mapWidget || !m_tracerouteWidget)
        return;

    // Get the selected route from the traceroute widget (now contains historical positions)
    auto routeNodes = m_tracerouteWidget->getSelectedRoute();

    if (routeNodes.isEmpty())
    {
        m_mapWidget->clearTraceroute();
        return;
    }

    // Build route points for the map
    QList<MapWidget::RoutePoint> routePoints;
    for (const auto &node : routeNodes)
    {
        if (node.latitude == 0.0 && node.longitude == 0.0)
            continue;

        MapWidget::RoutePoint pt;
        pt.lat = node.latitude;
        pt.lon = node.longitude;
        pt.name = node.name;
        pt.snr = node.snr;
        routePoints.append(pt);
    }

    if (routePoints.size() >= 2)
    {
        m_mapWidget->drawTraceroute(routePoints);
        // Switch to Map tab
        m_tabWidget->setCurrentIndex(0);
    }
    else
    {
        m_mapWidget->clearTraceroute();
    }

    // Also highlight the path on the topology graph
    if (m_topologyWidget && !routeNodes.isEmpty())
    {
        QList<uint32_t> pathNodes;
        for (const auto &node : routeNodes)
            pathNodes.append(node.nodeNum);
        m_topologyWidget->highlightPath(pathNodes);
    }
}


void MainWindow::requestTraceroute(uint32_t nodeNum)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    // Check if cooldown is still active
    if (m_tracerouteCooldownTimer->isActive())
    {
        int secondsRemaining = (m_tracerouteCooldownRemaining + 999) / 1000; // Round up
        statusBar()->showMessage(QString("Traceroute on cooldown - %1s remaining").arg(secondsRemaining), 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createTraceroutePacket(nodeNum, myNode);
    sendToDevice(packet);

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    QString name = node.longName.isEmpty() ? node.nodeId : node.longName;
    statusBar()->showMessage(QString("Traceroute request sent to %1...").arg(name), 5000);

    // Log the outgoing traceroute request (without response yet)
    if (m_database && m_tracerouteWidget)
    {
        Database::Traceroute tr;
        tr.fromNode = myNode;
        tr.toNode = nodeNum;
        tr.timestamp = QDateTime::currentDateTime();
        tr.isResponse = false;  // This is a request, not a response
        m_database->saveTraceroute(tr);
        m_tracerouteWidget->loadFromDatabase();  // Refresh the list
    }

    // Start 30-second cooldown
    m_tracerouteCooldownRemaining = TRACEROUTE_COOLDOWN_MS;
    m_tracerouteCooldownLabel->setVisible(true);
    m_tracerouteCooldownLabel->setText("Traceroute timeout: 30s");
    m_tracerouteCooldownTimer->start();
}

void MainWindow::onTracerouteCooldownTick()
{
    m_tracerouteCooldownRemaining -= 100; // Timer interval is 100ms

    if (m_tracerouteCooldownRemaining <= 0)
    {
        // Cooldown complete
        m_tracerouteCooldownTimer->stop();
        m_tracerouteCooldownLabel->setVisible(false);
        statusBar()->showMessage("Traceroute ready", 2000);
        return;
    }

    // Update text label with countdown
    int secondsRemaining = (m_tracerouteCooldownRemaining + 999) / 1000; // Round up
    m_tracerouteCooldownLabel->setText(QString("Traceroute timeout: %1s").arg(secondsRemaining));
}

void MainWindow::requestNodeInfo(uint32_t nodeNum)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createNodeInfoRequestPacket(nodeNum, myNode);
    sendToDevice(packet);

    statusBar()->showMessage("Node info request sent...", 3000);
}

void MainWindow::requestTelemetry(uint32_t nodeNum)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createTelemetryRequestPacket(nodeNum, myNode);
    sendToDevice(packet);

    statusBar()->showMessage("Telemetry request sent...", 3000);
}

void MainWindow::requestPosition(uint32_t nodeNum)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createPositionRequestPacket(nodeNum, myNode);
    sendToDevice(packet);

    statusBar()->showMessage("Position request sent...", 3000);
}

void MainWindow::onPositionRefreshTick()
{
    if (!isDeviceConnected())
        return;

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createPositionRequestPacket(0xFFFFFFFF, myNode);
    sendToDevice(packet);
    qDebug() << "[MainWindow] Auto position refresh broadcast sent";
}

void MainWindow::onClearNodeDatabase()
{
    int savedNodes = m_database ? m_database->nodeCount() : 0;

    QString detail = isDeviceConnected()
        ? "The node list will be re-downloaded from the device immediately."
        : "The node list will be re-downloaded the next time you connect.";

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Clear Nodes",
        QString("Delete all %1 node(s) saved on this PC?\n\nThis cannot be undone. %2")
            .arg(savedNodes).arg(detail),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    // A failed delete used to pass silently, leaving every node in place while
    // the UI reported success.
    if (m_database && !m_database->deleteAllNodes())
    {
        QMessageBox::warning(this, "Clear Nodes",
                             "The saved nodes could not be deleted from the local database.\n\n"
                             "Nothing was changed. See the log for details.");
        statusBar()->showMessage("Failed to clear nodes", 5000);
        return;
    }

    // Drops the in-memory nodes and repaints the node list and map
    m_nodeManager->clear();
    refreshDbNodeCount();

    if (isDeviceConnected())
    {
        statusBar()->showMessage("Nodes cleared, resyncing from device...", 5000);
        requestConfig();
    }
    else
    {
        statusBar()->showMessage("Nodes cleared", 3000);
    }
}


// Draws a node's recorded position fixes on the map. Reads position_history,
// which has been collected all along but had nothing displaying it.
void MainWindow::showNodeTrack(uint32_t nodeNum)
{
    if (!m_mapWidget)
        return;

    if (!m_database || !m_database->isOpen())
    {
        statusBar()->showMessage("No database open", 3000);
        return;
    }

    const int days = AppSettings::instance()->dataRetentionDays();
    const qint64 since = days > 0
        ? QDateTime::currentDateTime().addDays(-days).toSecsSinceEpoch()
        : 0;

    const auto records = m_database->loadPositionTrack(nodeNum, since);
    NodeInfo node = m_nodeManager->getNode(nodeNum);
    const QString name = node.longName.isEmpty() ? node.nodeId : node.longName;

    if (records.size() < 2)
    {
        statusBar()->showMessage(
            QString("Only %1 recorded position%2 for %3 - not enough for a track")
                .arg(records.size()).arg(records.size() == 1 ? "" : "s").arg(name),
            5000);
        m_mapWidget->clearTrack();
        return;
    }

    QList<MapWidget::TrackPoint> points;
    points.reserve(records.size());
    for (const auto &r : records)
    {
        MapWidget::TrackPoint p;
        p.latitude = r.latitude;
        p.longitude = r.longitude;
        p.when = r.timestamp.toString("yyyy-MM-dd HH:mm");
        points.append(p);
    }

    m_mapWidget->drawTrack(nodeNum, points, Theme::palette().accent);
    m_tabWidget->setCurrentIndex(0);
    statusBar()->showMessage(
        QString("%1 position fixes for %2").arg(records.size()).arg(name), 5000);
}

// Visualises one recorded packet during replay. Draws between nodes that have
// a position, and blinks the sender otherwise, so activity still reads for the
// three quarters of a mesh that never reports a location.
void MainWindow::onPacketReplayed(uint32_t fromNode, uint32_t toNode, int portNum)
{
    Q_UNUSED(portNum);
    if (!m_mapWidget)
        return;

    NodeInfo from = m_nodeManager->getNode(fromNode);
    if (from.hasPosition)
        m_mapWidget->blinkNode(fromNode, 1500);

    if (toNode != 0 && toNode != 0xFFFFFFFF)
    {
        NodeInfo to = m_nodeManager->getNode(toNode);
        if (from.hasPosition && to.hasPosition)
        {
            m_mapWidget->drawPacketFlow(fromNode, toNode,
                                        from.latitude, from.longitude,
                                        to.latitude, to.longitude);
        }
    }
}


// Deletes everything stored locally for the connected radio and starts over.
//
// "Clear Nodes" only empties the nodes table and then re-reads the radio's own
// node database, so anything the radio still remembers comes straight back -
// which is not what "start fresh" means to someone looking at a stale list.
// This removes the database file itself, so nothing survives but what the radio
// sends from now on.
void MainWindow::onForgetRadio()
{
    if (m_simulateMode)
    {
        statusBar()->showMessage("Not available in simulation mode", 3000);
        return;
    }

    const uint32_t nodeNum = m_openNodeNum != 0 ? m_openNodeNum : m_nodeManager->myNodeNum();
    if (nodeNum == 0)
    {
        QMessageBox::information(this, "Forget This Radio",
                                 "No radio database is open, so there is nothing to forget.");
        return;
    }

    const QString nodeId = MeshtasticProtocol::nodeIdToString(nodeNum);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dbPath = QString("%1/meshtastic_%2.db").arg(dataDir, nodeId);

    // Count what is about to go, so the confirmation is specific rather than a
    // vague warning about "all data".
    int nodes = 0, messages = 0, packets = 0;
    if (m_database && m_database->isOpen())
    {
        QSqlQuery q(m_database->connection());
        if (q.exec("SELECT (SELECT COUNT(*) FROM nodes), (SELECT COUNT(*) FROM messages), "
                   "(SELECT COUNT(*) FROM packets)") && q.next())
        {
            nodes = q.value(0).toInt();
            messages = q.value(1).toInt();
            packets = q.value(2).toInt();
        }
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Forget This Radio");
    box.setText(QString("Delete everything stored for %1?").arg(nodeId));
    box.setInformativeText(
        QString("This removes the local database for this radio:\n\n"
                "    %1 nodes\n    %2 messages\n    %3 recorded packets\n"
                "    telemetry, positions and traceroutes\n\n"
                "It cannot be undone. The radio itself is not changed - its own "
                "node database stays as it is, and whatever it reports after "
                "reconnecting will be stored afresh.")
            .arg(nodes).arg(messages).arg(packets));
    box.setStandardButtons(QMessageBox::Cancel);
    QPushButton *forget = box.addButton("Forget Everything", QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != forget)
        return;

    // Close first: SQLite holds the file open, and the -wal/-shm companions are
    // only removable once the connection is gone.
    closeDatabase();
    m_openNodeNum = 0;
    m_nodeManager->clear();

    QStringList failed;
    for (const QString &suffix : {"", "-wal", "-shm"})
    {
        const QString path = dbPath + suffix;
        if (QFile::exists(path) && !QFile::remove(path))
            failed << QFileInfo(path).fileName();
    }

    if (!failed.isEmpty())
    {
        QMessageBox::warning(this, "Forget This Radio",
                             QString("Could not delete:\n\n%1\n\nThe database has been "
                                     "closed; the files can be removed by hand from\n%2")
                                 .arg(failed.join("\n"), dataDir));
        statusBar()->showMessage("Could not delete the radio database", 5000);
        return;
    }

    AppSettings::instance()->setLastDatabaseNode(0);
    qDebug() << "[MainWindow] Forgot radio" << nodeId << "- removed" << dbPath;

    if (isDeviceConnected())
    {
        // A fresh database, then ask the radio for everything again
        openDatabaseForNode(nodeNum);
        requestConfig();
        statusBar()->showMessage(
            QString("Forgot %1 - resyncing from the radio").arg(nodeId), 5000);
    }
    else
    {
        refreshDbNodeCount();
        updateStatusLabel();
        updateNodeList();
        statusBar()->showMessage(
            QString("Forgot %1 - reconnect to start collecting again").arg(nodeId), 5000);
    }
}

void MainWindow::updateNodeList()
{
    if (m_nodeTableWidget)
        m_nodeTableWidget->refresh();

    if (m_testMode && m_mapWidget)
        drawTestNodeLines();
}

void MainWindow::centerMapOnNode(uint32_t nodeNum)
{
    if (!m_mapWidget)
        return;
    NodeInfo node = m_nodeManager->getNode(nodeNum);
    if (!node.hasPosition)
        return;
    m_mapWidget->centerOnLocation(node.latitude, node.longitude);
    m_mapWidget->setZoomLevel(15);
    m_mapWidget->selectNode(nodeNum);
    m_tabWidget->setCurrentIndex(0);
}

void MainWindow::refreshDbNodeCount()
{
    m_dbNodeCount = (m_database && m_database->isOpen()) ? m_database->nodeCount() : 0;
}

void MainWindow::updateStatusLabel()
{
    // Don't overwrite "Reconnecting..." while TCP is mid-reconnect
    if (m_tcp->isReconnecting())
        return;

    QString status;
    // Called for every received packet, so neither of these may do real work:
    // nodeCount() reads the map size instead of deep-copying every NodeInfo, and
    // the DB count is cached rather than re-running SELECT COUNT(*) per packet.
    int nodeCount = m_nodeManager->nodeCount();

    if (isDeviceConnected())
    {
        status = QString("Connected: %1 | Nodes: %2 (DB: %3)")
                     .arg(connectedDeviceName())
                     .arg(nodeCount)
                     .arg(m_dbNodeCount);
    }
    else
    {
        status = "Disconnected";
    }
    m_statusLabel->setText(status);
    updateConnectionPill();
}

void MainWindow::requestConfig()
{
    if (!isDeviceConnected())
    {
        return;
    }

    // Generate random 32-bit config ID
    m_expectedConfigId = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    // Ensure non-zero
    if (m_expectedConfigId == 0)
        m_expectedConfigId = 1;

    qDebug() << "[MainWindow] Starting config request flow. ConfigID:" << m_expectedConfigId;
    statusBar()->showMessage(QString("Requesting configuration (ID: %1)...").arg(m_expectedConfigId));

    // Send want_config_id
    sendToDevice(m_protocol->createWantConfigPacket(m_expectedConfigId));

    // Start fast heartbeat for config phase
    if (!m_configHeartbeatTimer->isActive())
    {
        m_configHeartbeatTimer->start();
    }

    // Also clear previous config state if needed?
    // For now, reliance on configCompleteId is better.
}

void MainWindow::openDatabaseForNode(uint32_t nodeNum)
{
    // Reconnect to same node: DB is still open, just reload to merge any missed data
    if (m_database && m_database->isOpen() && m_openNodeNum == nodeNum) {
        qDebug() << "[MainWindow] Reconnected to same node, reloading DB (no clear)";
        m_nodeManager->loadFromDatabase();
        refreshDbNodeCount();
        updateStatusLabel();
        statusBar()->showMessage("Reconnected", 3000);
        return;
    }

    closeDatabase();
    m_openNodeNum = nodeNum;
    QString nodeId = MeshtasticProtocol::nodeIdToString(nodeNum);
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    // Simulation mode uses an in-memory database so it never writes to disk
    QString dbPath = m_simulateMode
        ? ":memory:"
        : QString("%1/meshtastic_%2.db").arg(dataDir, nodeId);
    m_database = new Database(this);
    if (m_database->open(dbPath))
    {
        m_nodeManager->setDatabase(m_database);

        // Save any nodes received before database was ready, then load all
        m_nodeManager->saveToDatabase();
        m_nodeManager->loadFromDatabase();

        if (m_messagesWidget)
        {
            m_messagesWidget->setDatabase(m_database);
            m_messagesWidget->loadFromDatabase();
        }

        if (m_telemetryGraphWidget)
        {
            m_telemetryGraphWidget->setDatabase(m_database);
        }

        if (m_tracerouteWidget)
        {
            m_tracerouteWidget->setDatabase(m_database);
        }

        if (m_topologyWidget)
        {
            m_topologyWidget->setDatabase(m_database);
            m_topologyWidget->loadFromDatabase();
        }

        if (m_analyticsWidget)
        {
            m_analyticsWidget->setDatabase(m_database);
        }

        if (m_replayBar)
        {
            m_replayBar->setDatabase(m_database);
        }

        if (m_packetList)
        {
            m_packetList->setDatabase(m_database);
        }

        refreshDbNodeCount();
        if (!m_simulateMode)
            AppSettings::instance()->setLastDatabaseNode(nodeNum);
        statusBar()->showMessage(QString("Database loaded: %1 nodes").arg(m_dbNodeCount), 3000);
    }
    else
    {
        // Leaving a non-null but closed Database here would satisfy every
        // `if (m_database)` guard in the app and silently fail every query.
        qWarning() << "[MainWindow] Database open failed for" << dbPath;
        delete m_database;
        m_database = nullptr;
        m_openNodeNum = 0;
        m_dbNodeCount = 0;
        statusBar()->showMessage("Failed to open database - data will not be saved", 8000);
    }
    updateStatusLabel();
}

void MainWindow::closeDatabase()
{
    if (!m_database)
        return;

    // 1. Notify all consumers to stop using the database
    m_nodeManager->setDatabase(nullptr);
    if (m_messagesWidget)
    {
        m_messagesWidget->setDatabase(nullptr);
        m_messagesWidget->clear();
    }
    if (m_telemetryGraphWidget)
    {
        m_telemetryGraphWidget->setDatabase(nullptr);
    }
    if (m_tracerouteWidget)
    {
        m_tracerouteWidget->setDatabase(nullptr);
        m_tracerouteWidget->clear();
    }
    if (m_topologyWidget)
    {
        m_topologyWidget->setDatabase(nullptr);
    }
    if (m_analyticsWidget)
    {
        m_analyticsWidget->setDatabase(nullptr);
    }
    if (m_replayBar)
    {
        m_replayBar->setDatabase(nullptr);
    }
    if (m_packetList)
    {
        m_packetList->setDatabase(nullptr);
    }

    // 2. Clear local node state
    m_nodeManager->clear();

    // 3. Close and destroy the database
    m_database->close();
    delete m_database;
    m_database = nullptr;
    m_dbNodeCount = 0;
}

void MainWindow::onSendMessage(const QString &text, uint32_t toNode, int channel)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    uint32_t packetId = 0;
    QByteArray packet = m_protocol->createTextMessagePacket(text, toNode, myNode, channel, 0, &packetId);
    bool sent = sendToDevice(packet);

    qDebug() << "[MainWindow] Sent message with packetId:" << packetId << "ok:" << sent;

    // Add the outgoing message to our local display
    ChatMessage msg;
    msg.fromNode = myNode;
    msg.toNode = toNode;
    msg.channelIndex = channel;
    msg.text = text;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isOutgoing = true;
    msg.packetId = packetId;
    // A write that never reached the device will never be ACKed, so don't leave
    // it spinning on "Sending..."
    msg.status = sent ? MessageStatus::Sending : MessageStatus::Failed;
    m_messagesWidget->addMessage(msg);

    QString destName;
    if (toNode == 0xFFFFFFFF)
    {
        destName = QString("Channel %1").arg(channel);
    }
    else
    {
        NodeInfo node = m_nodeManager->getNode(toNode);
        destName = node.longName.isEmpty() ? node.nodeId : node.longName;
    }
    statusBar()->showMessage(sent
        ? QString("Message sent to %1").arg(destName)
        : QString("Failed to send message to %1 - check the connection").arg(destName), 5000);
}

void MainWindow::onSendReaction(const QString &emoji, uint32_t toNode, int channel, uint32_t replyId)
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    uint32_t packetId = 0;
    QByteArray packet = m_protocol->createTextMessagePacket(emoji, toNode, myNode, channel, replyId, &packetId);
    sendToDevice(packet);

    // Add the reaction to our local display
    ChatMessage msg;
    msg.fromNode = myNode;
    msg.toNode = toNode;
    msg.channelIndex = channel;
    msg.text = emoji;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isOutgoing = true;
    msg.packetId = packetId;
    m_messagesWidget->addMessage(msg);

    statusBar()->showMessage(QString("Reaction %1 sent").arg(emoji), 3000);
}

void MainWindow::onSettingChanged(const QString &key, const QVariant &value)
{
    if (key == "nodes/show_offline" || key == "nodes/offline_threshold_minutes")
    {
        // Refresh node list with new filter settings
        updateNodeList();
    }
    else if (key == "map/tile_server")
    {
        if (m_mapWidget)
            m_mapWidget->setTileServer(value.toString());
    }
    else if (key == "map/position_refresh_interval")
    {
        int secs = value.toInt();
        m_positionRefreshTimer->stop();
        if (secs > 0)
            m_positionRefreshTimer->start(secs * 1000);
    }
}

void MainWindow::showNotification(const QString &title, const QString &message)
{
    if (!AppSettings::instance()->notificationsEnabled())
    {
        return;
    }

    if (m_trayIcon && QSystemTrayIcon::supportsMessages())
    {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
    }

    if (AppSettings::instance()->soundEnabled())
        QApplication::beep();
}

void MainWindow::showTracerouteResult(const MeshtasticProtocol::DecodedPacket &packet)
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("Traceroute Result");
    dialog->setMinimumSize(500, 400);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    // Header with source and destination
    // packet.from = responder (the node we tracerouted), packet.to = requester (us)
    // So for display: origin = packet.to (us), destination = packet.from (target)
    QString fromName, toName;
    if (packet.to != 0)
    {
        NodeInfo originNode = m_nodeManager->getNode(packet.to);
        fromName = originNode.longName.isEmpty()
                       ? MeshtasticProtocol::nodeIdToString(packet.to)
                       : originNode.longName;
    }
    if (packet.from != 0)
    {
        NodeInfo destNode = m_nodeManager->getNode(packet.from);
        toName = destNode.longName.isEmpty()
                     ? MeshtasticProtocol::nodeIdToString(packet.from)
                     : destNode.longName;
    }

    QLabel *headerLabel = new QLabel(QString("<h3>Traceroute: %1 → %2</h3>").arg(fromName, toName));
    headerLabel->setTextFormat(Qt::RichText);
    layout->addWidget(headerLabel);

    // Build the traceroute display
    QTextEdit *resultText = new QTextEdit;
    resultText->setReadOnly(true);
    resultText->setFont(QFont("monospace", 10));

    // This is rendered HTML, so it needs the palette injected - a hardcoded
    // light table is unreadable once the dark theme is on.
    const auto &pal = Theme::palette();
    QString html;
    html += QString("<style>"
                    "table { border-collapse: collapse; width: 100%; margin: 10px 0; }"
                    "th, td { border: 1px solid %1; padding: 8px; text-align: left; }"
                    "th { background-color: %2; color: %3; }"
                    "td { color: %4; }"
                    ".snr-good { color: %5; font-weight: bold; }"
                    ".snr-ok { color: %6; font-weight: bold; }"
                    ".snr-bad { color: %7; font-weight: bold; }"
                    ".arrow { font-size: 16px; color: %3; text-align: center; }"
                    "</style>")
                .arg(pal.border.name(), pal.surfaceAlt.name(), pal.textMuted.name(),
                     pal.text.name(), pal.success.name(), pal.warning.name(), pal.danger.name());

    // Outgoing route (towards destination)
    QVariantList route = packet.fields.value("route").toList();
    QVariantList snrTowards = packet.fields.value("snrTowards").toList();

    html += "<h4>📡 Outgoing Route (to destination)</h4>";
    if (route.isEmpty())
    {
        html += "<p><i>Direct connection (no hops)</i></p>";
    }
    else
    {
        html += "<table><tr><th>Hop</th><th>Node</th><th>SNR (dB)</th></tr>";

        // Start from source
        html += QString("<tr><td>0</td><td><b>%1</b> (origin)</td><td>-</td></tr>").arg(fromName);

        for (int i = 0; i < route.size(); i++)
        {
            QString nodeId = route[i].toString();
            uint32_t nodeNum = MeshtasticProtocol::nodeIdFromString(nodeId);
            QString nodeName;
            if (nodeNum == 0xFFFFFFFF) {
                nodeName = "<i>Unknown Node</i>";
            } else {
                NodeInfo node = m_nodeManager->getNode(nodeNum);
                nodeName = node.longName.isEmpty() ? nodeId : node.longName;
            }

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrTowards.size() && !snrTowards[i].isNull())
            {
                double snr = snrTowards[i].toDouble();
                snrStr = QString::number(snr, 'f', 1);
                if (snr >= 5.0)
                    snrClass = "snr-good";
                else if (snr >= 0.0)
                    snrClass = "snr-ok";
                else
                    snrClass = "snr-bad";
            }

            html += QString("<tr><td>%1</td><td>%2</td><td class='%3'>%4</td></tr>")
                        .arg(i + 1)
                        .arg(nodeName)
                        .arg(snrClass)
                        .arg(snrStr);
        }

        // Destination
        html += QString("<tr><td>%1</td><td><b>%2</b> (destination)</td><td>-</td></tr>")
                    .arg(route.size() + 1)
                    .arg(toName);

        html += "</table>";
    }

    // Return route (back from destination)
    QVariantList routeBack = packet.fields.value("routeBack").toList();
    QVariantList snrBack = packet.fields.value("snrBack").toList();

    html += "<h4>🔙 Return Route (from destination)</h4>";
    if (routeBack.isEmpty())
    {
        html += "<p><i>Direct return (no hops) or same as outgoing route</i></p>";
    }
    else
    {
        html += "<table><tr><th>Hop</th><th>Node</th><th>SNR (dB)</th></tr>";

        // Start from destination
        html += QString("<tr><td>0</td><td><b>%1</b> (destination)</td><td>-</td></tr>").arg(toName);

        for (int i = 0; i < routeBack.size(); i++)
        {
            QString nodeId = routeBack[i].toString();
            uint32_t nodeNum = MeshtasticProtocol::nodeIdFromString(nodeId);
            QString nodeName;
            if (nodeNum == 0xFFFFFFFF) {
                nodeName = "<i>Unknown Node</i>";
            } else {
                NodeInfo node = m_nodeManager->getNode(nodeNum);
                nodeName = node.longName.isEmpty() ? nodeId : node.longName;
            }

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrBack.size() && !snrBack[i].isNull())
            {
                double snr = snrBack[i].toDouble();
                snrStr = QString::number(snr, 'f', 1);
                if (snr >= 5.0)
                    snrClass = "snr-good";
                else if (snr >= 0.0)
                    snrClass = "snr-ok";
                else
                    snrClass = "snr-bad";
            }

            html += QString("<tr><td>%1</td><td>%2</td><td class='%3'>%4</td></tr>")
                        .arg(i + 1)
                        .arg(nodeName)
                        .arg(snrClass)
                        .arg(snrStr);
        }

        // Origin
        html += QString("<tr><td>%1</td><td><b>%2</b> (origin)</td><td>-</td></tr>")
                    .arg(routeBack.size() + 1)
                    .arg(fromName);

        html += "</table>";
    }

    // Summary
    int totalHops = route.size() + routeBack.size();
    html += QString("<p><b>Total hops:</b> %1 outgoing + %2 return = %3</p>")
                .arg(route.size())
                .arg(routeBack.size())
                .arg(totalHops);

    // Legend
    html += "<hr><p><small><b>SNR Legend:</b> "
            "<span class='snr-good'>≥5 dB (Good)</span> | "
            "<span class='snr-ok'>0-5 dB (OK)</span> | "
            "<span class='snr-bad'>&lt;0 dB (Weak)</span></small></p>";

    resultText->setHtml(html);
    layout->addWidget(resultText);

    // Close button
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog->show();
}

void MainWindow::onSaveLoRaConfig()
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig)
        return;

    const auto &lora = devConfig->loraConfig();
    QVariantMap config;
    config["usePreset"] = lora.usePreset;
    config["modemPreset"] = lora.modemPreset;
    config["region"] = lora.region;
    config["hopLimit"] = lora.hopLimit;
    config["txEnabled"] = lora.txEnabled;
    config["txPower"] = lora.txPower;
    config["channelNum"] = lora.channelNum;
    config["overrideDutyCycle"] = lora.overrideDutyCycle;
    config["frequencyOffset"] = lora.frequencyOffset;

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createLoRaConfigPacket(myNode, myNode, config);
    if (!sendToDevice(packet))
    {
        statusBar()->showMessage("Failed to send LoRa config - check the connection", 5000);
        return;
    }

    m_configWidget->notifyLoRaSaved();
    statusBar()->showMessage("LoRa config saved to device", 3000);
}

void MainWindow::onSaveDeviceConfig()
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig)
        return;

    const auto &device = devConfig->deviceConfig();
    QVariantMap config;
    config["role"] = device.role;
    config["serialEnabled"] = device.serialEnabled;
    config["debugLogEnabled"] = device.debugLogEnabled;
    config["buttonGpio"] = device.buttonGpio;
    config["buzzerGpio"] = device.buzzerGpio;
    config["rebroadcastMode"] = device.rebroadcastMode;
    config["nodeInfoBroadcastSecs"] = device.nodeInfoBroadcastSecs;
    config["doubleTapAsButtonPress"] = device.doubleTapAsButtonPress;
    config["isManaged"] = device.isManaged;
    config["disableTripleClick"] = device.disableTripleClick;
    config["tzdef"] = device.tzdef;
    config["ledHeartbeatDisabled"] = device.ledHeartbeatDisabled;

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createDeviceConfigPacket(myNode, myNode, config);
    if (!sendToDevice(packet))
    {
        statusBar()->showMessage("Failed to send device config - check the connection", 5000);
        return;
    }

    m_configWidget->notifyDeviceSaved();
    statusBar()->showMessage("Device config saved to device", 3000);
}

void MainWindow::onSavePositionConfig()
{
    if (!isDeviceConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig)
        return;

    const auto &pos = devConfig->positionConfig();
    QVariantMap config;
    config["positionBroadcastSecs"] = pos.positionBroadcastSecs;
    config["smartPositionEnabled"] = pos.smartPositionEnabled;
    config["fixedPosition"] = pos.fixedPosition;
    config["gpsEnabled"] = pos.gpsEnabled;
    config["gpsUpdateInterval"] = pos.gpsUpdateInterval;
    config["gpsAttemptTime"] = pos.gpsAttemptTime;
    config["positionFlags"] = pos.positionFlags;
    config["broadcastSmartMinDistance"] = pos.broadcastSmartMinDistance;
    config["broadcastSmartMinIntervalSecs"] = pos.broadcastSmartMinIntervalSecs;
    config["gpsMode"] = pos.gpsMode;

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createPositionConfigPacket(myNode, myNode, config);
    if (!sendToDevice(packet))
    {
        statusBar()->showMessage("Failed to send position config - check the connection", 5000);
        return;
    }

    m_configWidget->notifyPositionSaved();
    statusBar()->showMessage("Position config saved to device", 3000);
}

void MainWindow::onSaveChannelConfig(int channelIndex)
{
    qDebug() << "=== onSaveChannelConfig called for channel" << channelIndex << "===";

    if (!isDeviceConnected())
    {
        qDebug() << "Not connected!";
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig)
    {
        qDebug() << "No device config!";
        return;
    }

    const auto &ch = devConfig->channel(channelIndex);
    qDebug() << "Channel config - role:" << ch.role << "name:" << ch.name
             << "psk size:" << ch.psk.size();

    QVariantMap config;
    config["role"] = ch.role;
    config["name"] = ch.name;
    config["psk"] = ch.psk;
    config["uplinkEnabled"] = ch.uplinkEnabled;
    config["downlinkEnabled"] = ch.downlinkEnabled;

    uint32_t myNode = m_nodeManager->myNodeNum();
    qDebug() << "Creating packet for node:" << QString::number(myNode, 16);

    QByteArray packet = m_protocol->createChannelConfigPacket(myNode, myNode, channelIndex, config);
    qDebug() << "Packet size:" << packet.size() << "bytes";

    if (!sendToDevice(packet))
    {
        statusBar()->showMessage(
            QString("Failed to send channel %1 config - check the connection").arg(channelIndex), 5000);
        return;
    }
    qDebug() << "Packet sent to device";

    // Update MessagesWidget immediately (don't wait for device response)
    if (m_messagesWidget)
    {
        bool enabled = (ch.role > 0);  // role: 0=disabled, 1=primary, 2=secondary
        m_messagesWidget->setChannel(channelIndex, ch.name, enabled);
    }

    m_configWidget->notifyChannelSaved();
    statusBar()->showMessage(QString("Channel %1 config saved to device").arg(channelIndex), 3000);
}

void MainWindow::onExportNodes(const QString &format)
{
    QList<NodeInfo> nodes = m_nodeManager->allNodes();
    if (nodes.isEmpty())
    {
        QMessageBox::information(this, "Export Nodes", "No nodes to export.");
        return;
    }

    QString filter = format == "csv" ? "CSV Files (*.csv)" : "JSON Files (*.json)";
    QString defaultName = format == "csv" ? "nodes.csv" : "nodes.json";
    QString fileName = QFileDialog::getSaveFileName(this, "Export Nodes", defaultName, filter);
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "Export Error", "Could not open file for writing.");
        return;
    }

    if (format == "csv")
    {
        QTextStream out(&file);
        out << "NodeNum,NodeID,LongName,ShortName,Latitude,Longitude,Altitude,BatteryLevel,Voltage,LastHeard,SNR,RSSI,Hops\n";
        for (const NodeInfo &node : nodes)
        {
            QString longName = QString(node.longName).replace("\"", "\"\"");
            QString shortName = QString(node.shortName).replace("\"", "\"\"");
            out << node.nodeNum << ","
                << "\"" << node.nodeId << "\","
                << "\"" << longName << "\","
                << "\"" << shortName << "\","
                << (node.hasPosition ? QString::number(node.latitude, 'f', 6) : "") << ","
                << (node.hasPosition ? QString::number(node.longitude, 'f', 6) : "") << ","
                << (node.hasPosition ? QString::number(node.altitude) : "") << ","
                << (node.batteryLevel >= 0 ? QString::number(node.batteryLevel) : "") << ","
                << (node.voltage > 0 ? QString::number(node.voltage, 'f', 2) : "") << ","
                << node.lastHeard.toString(Qt::ISODate) << ","
                << QString::number(node.snr, 'f', 1) << ","
                << node.rssi << ","
                << (node.hopsAway >= 0 ? QString::number(node.hopsAway) : "") << "\n";
        }
    }
    else
    {
        QJsonArray nodesArray;
        for (const NodeInfo &node : nodes)
        {
            QJsonObject obj;
            obj["nodeNum"] = static_cast<qint64>(node.nodeNum);
            obj["nodeId"] = node.nodeId;
            obj["longName"] = node.longName;
            obj["shortName"] = node.shortName;
            if (node.hasPosition)
            {
                obj["latitude"] = node.latitude;
                obj["longitude"] = node.longitude;
                obj["altitude"] = node.altitude;
            }
            if (node.batteryLevel >= 0)
            {
                obj["batteryLevel"] = node.batteryLevel;
            }
            if (node.voltage > 0)
            {
                obj["voltage"] = node.voltage;
            }
            obj["lastHeard"] = node.lastHeard.toString(Qt::ISODate);
            obj["snr"] = node.snr;
            obj["rssi"] = node.rssi;
            if (node.hopsAway >= 0)
            {
                obj["hops"] = node.hopsAway;
            }
            obj["isExternalPower"] = node.isExternalPower;
            nodesArray.append(obj);
        }

        QJsonObject root;
        root["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["nodeCount"] = nodes.size();
        root["nodes"] = nodesArray;

        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
    }

    file.close();
    statusBar()->showMessage(QString("Exported %1 nodes to %2").arg(nodes.size()).arg(fileName), 5000);
}

void MainWindow::onExportMessages(const QString &format)
{
    if (!m_database || !m_database->isOpen())
    {
        QMessageBox::information(this, "Export Messages", "No database connected. Connect to a device first.");
        return;
    }

    // Get messages from database
    QList<ChatMessage> messages = m_database->getAllMessages();
    if (messages.isEmpty())
    {
        QMessageBox::information(this, "Export Messages", "No messages to export.");
        return;
    }

    QString filter = format == "csv" ? "CSV Files (*.csv)" : "JSON Files (*.json)";
    QString defaultName = format == "csv" ? "messages.csv" : "messages.json";
    QString fileName = QFileDialog::getSaveFileName(this, "Export Messages", defaultName, filter);
    if (fileName.isEmpty())
        return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "Export Error", "Could not open file for writing.");
        return;
    }

    if (format == "csv")
    {
        QTextStream out(&file);
        out << "Timestamp,FromNode,ToNode,Channel,Text,PacketID\n";
        for (const ChatMessage &msg : messages)
        {
            QString fromId = MeshtasticProtocol::nodeIdToString(msg.fromNode);
            QString toId = msg.toNode == 0xFFFFFFFF ? "broadcast" : MeshtasticProtocol::nodeIdToString(msg.toNode);
            QString text = QString(msg.text).replace("\"", "\"\"").replace("\n", "\\n");
            out << msg.timestamp.toString(Qt::ISODate) << ","
                << "\"" << fromId << "\","
                << "\"" << toId << "\","
                << msg.channelIndex << ","
                << "\"" << text << "\","
                << msg.packetId << "\n";
        }
    }
    else
    {
        QJsonArray messagesArray;
        for (const ChatMessage &msg : messages)
        {
            QJsonObject obj;
            obj["timestamp"] = msg.timestamp.toString(Qt::ISODate);
            obj["fromNode"] = MeshtasticProtocol::nodeIdToString(msg.fromNode);
            obj["fromNodeNum"] = static_cast<qint64>(msg.fromNode);
            obj["toNode"] = msg.toNode == 0xFFFFFFFF ? "broadcast" : MeshtasticProtocol::nodeIdToString(msg.toNode);
            obj["toNodeNum"] = static_cast<qint64>(msg.toNode);
            obj["channel"] = msg.channelIndex;
            obj["text"] = msg.text;
            obj["packetId"] = static_cast<qint64>(msg.packetId);
            messagesArray.append(obj);
        }

        QJsonObject root;
        root["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["messageCount"] = messages.size();
        root["messages"] = messagesArray;

        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
    }

    file.close();
    statusBar()->showMessage(QString("Exported %1 messages to %2").arg(messages.size()).arg(fileName), 5000);
}

void MainWindow::onConfigCompleteIdReceived(uint32_t configId)
{
    qDebug() << "[MainWindow] Received ConfigCompleteId:" << configId;

    if (configId == m_expectedConfigId)
    {
        qDebug() << "[MainWindow] Config ID matches! Configuration complete.";
        statusBar()->showMessage("Configuration loaded successfully", 3000);

        // Stop fast heartbeat
        if (m_configHeartbeatTimer)
            m_configHeartbeatTimer->stop();

        // Request session key for admin operations
        if (isDeviceConnected() && m_protocol)
        {
            qDebug() << "[MainWindow] Requesting session key for admin operations";
            sendToDevice(m_protocol->createSessionKeyRequestPacket());
        }

        // Explicitly refresh all tabs or signal config ready
        // For now, logging success is sufficient as tabs listen to config changes
    }
    else
    {
        qWarning() << "[MainWindow] Mismatched Config ID. Expected:" << m_expectedConfigId << "Got:" << configId;
    }
}

void MainWindow::drawTestNodeLines()
{
    if (!m_mapWidget)
        return;

    QList<NodeInfo> allNodes = m_nodeManager->allNodes();

    // Filter nodes with positions
    QList<NodeInfo> nodesWithPos;
    for (const NodeInfo &node : allNodes)
    {
        if (node.hasPosition)
        {
            nodesWithPos.append(node);
        }
    }

    if (nodesWithPos.size() < 2)
    {
        qDebug() << "[Test] Not enough nodes with positions to draw test lines";
        return;
    }

    // Use fewer nodes if we have less than 10
    int numNodesToConnect = qMin(10, nodesWithPos.size());

    qDebug() << "[Test] Drawing lines between" << numNodesToConnect << "random nodes";

    // Seed random generator
    srand(time(nullptr));

    // Draw lines between random pairs
    for (int i = 0; i < numNodesToConnect - 1; i++)
    {
        int idx1 = rand() % nodesWithPos.size();
        int idx2 = rand() % nodesWithPos.size();

        // Make sure we don't draw from same node to itself
        while (idx2 == idx1)
        {
            idx2 = rand() % nodesWithPos.size();
        }

        const NodeInfo &from = nodesWithPos[idx1];
        const NodeInfo &to = nodesWithPos[idx2];

        qDebug() << "[Test] Drawing line from" << from.shortName << "to" << to.shortName;
        m_mapWidget->drawPacketFlow(from.nodeNum, to.nodeNum, from.latitude, from.longitude, to.latitude, to.longitude);
    }
}

bool MainWindow::isDeviceConnected() const
{
    return m_serial->isConnected() || m_tcp->isConnected() || m_bluetooth->isConnected()
           || (m_simulation && m_simulation->isActive());
}

bool MainWindow::sendToDevice(const QByteArray &data)
{
    bool ok = false;

    if (m_simulation && m_simulation->isActive())
        ok = m_simulation->sendData(data);
    else if (m_bluetooth->isConnected())
        ok = m_bluetooth->sendData(data);
    else if (m_tcp->isConnected())
        ok = m_tcp->sendData(data);
    else if (m_serial->isConnected())
        ok = m_serial->sendData(data);
    else
        qWarning() << "[MainWindow] sendToDevice with no active connection";

    if (!ok)
        qWarning() << "[MainWindow] Failed to send" << data.size() << "bytes to device";

    return ok;
}

QString MainWindow::connectedDeviceName() const
{
    if (m_simulation && m_simulation->isActive())
        return "Simulation";
    if (m_bluetooth->isConnected())
        return m_bluetooth->connectedDeviceName();
    if (m_tcp->isConnected())
        return m_tcp->connectedAddress();
    if (m_serial->isConnected())
        return m_serial->connectedPortName();
    return QString();
}

QString MainWindow::handleAutoresponderCommand(const QString &command, uint32_t fromNode)
{
    if (command == "!ping")
        return "pong";

    if (command == "!nodes")
    {
        int count = m_nodeManager->allNodes().count();
        return QString("%1 nodes seen on mesh").arg(count);
    }

    if (command == "!battery" || command == "!batt")
    {
        NodeInfo myInfo = m_nodeManager->getNode(m_nodeManager->myNodeNum());
        QStringList parts;
        if (myInfo.batteryLevel > 0)
            parts << QString("%1%").arg(myInfo.batteryLevel);
        if (myInfo.voltage > 0)
            parts << QString("%1V").arg(myInfo.voltage, 0, 'f', 2);
        if (myInfo.isExternalPower)
            parts << "ext power";
        return parts.isEmpty() ? "Battery: unknown" : "Battery: " + parts.join(", ");
    }

    if (command == "!uptime")
    {
        NodeInfo myInfo = m_nodeManager->getNode(m_nodeManager->myNodeNum());
        if (myInfo.uptimeSeconds == 0)
            return "Uptime: unknown";
        uint32_t secs = myInfo.uptimeSeconds;
        int days = secs / 86400;
        int hours = (secs % 86400) / 3600;
        int mins = (secs % 3600) / 60;
        QStringList parts;
        if (days > 0) parts << QString("%1d").arg(days);
        if (hours > 0) parts << QString("%1h").arg(hours);
        parts << QString("%1m").arg(mins);
        return "Uptime: " + parts.join(" ");
    }

    if (command == "!weather" || command == "!wx")
    {
        NodeInfo myInfo = m_nodeManager->getNode(m_nodeManager->myNodeNum());
        if (!myInfo.hasEnvironmentTelemetry)
            return "No weather sensor data";
        QStringList parts;
        if (myInfo.temperature != 0.0f)
            parts << QString("Temp: %1C").arg(myInfo.temperature, 0, 'f', 1);
        if (myInfo.relativeHumidity != 0.0f)
            parts << QString("Humidity: %1%").arg(myInfo.relativeHumidity, 0, 'f', 0);
        if (myInfo.barometricPressure != 0.0f)
            parts << QString("Pressure: %1hPa").arg(myInfo.barometricPressure, 0, 'f', 1);
        return parts.isEmpty() ? "No weather sensor data" : parts.join(", ");
    }

    if (command == "!signal")
    {
        NodeInfo senderInfo = m_nodeManager->getNode(fromNode);
        QStringList parts;
        if (senderInfo.snr != 0.0f || senderInfo.rssi != 0)
        {
            parts << QString("SNR: %1dB").arg(senderInfo.snr, 0, 'f', 1);
            parts << QString("RSSI: %1dBm").arg(senderInfo.rssi);
        }
        if (senderInfo.hopsAway >= 0)
            parts << QString("%1 hop%2").arg(senderInfo.hopsAway).arg(senderInfo.hopsAway != 1 ? "s" : "");
        return parts.isEmpty() ? "No signal data for your node" : "Your signal: " + parts.join(", ");
    }

    if (command == "!pos" || command == "!position")
    {
        NodeInfo myInfo = m_nodeManager->getNode(m_nodeManager->myNodeNum());
        if (!myInfo.hasPosition)
            return "No position available";
        QString pos = QString("%1, %2").arg(myInfo.latitude, 0, 'f', 5).arg(myInfo.longitude, 0, 'f', 5);
        if (myInfo.altitude != 0)
            pos += QString(", alt %1m").arg(myInfo.altitude);
        return "Position: " + pos;
    }

    if (command == "!time")
    {
        return QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm UTC");
    }

    if (command == "!info")
    {
        NodeInfo myInfo = m_nodeManager->getNode(m_nodeManager->myNodeNum());
        QStringList parts;
        if (!myInfo.longName.isEmpty())
            parts << myInfo.longName;
        if (!myInfo.hwModel.isEmpty())
            parts << myInfo.hwModel;
        if (myInfo.role != 0)
            parts << m_nodeManager->roleToString(myInfo.role);
        if (!m_firmwareVersion.isEmpty())
            parts << "fw " + m_firmwareVersion;
        return parts.isEmpty() ? "No device info" : parts.join(" / ");
    }

    if (command == "!help")
    {
        return "Commands: !ping !nodes !battery !uptime !weather !signal !pos !time !info !help";
    }

    return QString(); // Unknown command, ignore
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState(1));
    if (m_mapSplitter)
    {
        settings.setValue("mapSplitterSizes", QVariant::fromValue(m_mapSplitter->sizes()));
    }
    settings.endGroup();
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    if (settings.contains("geometry"))
    {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("windowState"))
    {
        restoreState(settings.value("windowState").toByteArray(), 1);
    }
    if (settings.contains("mapSplitterSizes") && m_mapSplitter)
    {
        QList<int> sizes = settings.value("mapSplitterSizes").value<QList<int>>();
        if (!sizes.isEmpty())
        {
            m_mapSplitter->setSizes(sizes);
        }
    }
    settings.endGroup();
}
