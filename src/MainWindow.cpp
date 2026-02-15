#include "MainWindow.h"
#include "SerialConnection.h"
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

#include "MapWidget.h"
#include "DashboardStatsWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
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
#include <algorithm>
#include <cstdlib>
#include <ctime>

MainWindow::MainWindow(bool experimentalMode, bool testMode, QWidget *parent)
    : QMainWindow(parent), m_experimentalMode(experimentalMode), m_testMode(testMode)
{
    // Explicitly set window flags to prevent them from being dropped
    // (QWebEngineView GPU init can cause WM to re-evaluate decorations on Linux)
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // Initialize app settings
    AppSettings::instance()->open();

    m_serial = new SerialConnection(this);
    m_protocol = new MeshtasticProtocol(this);
    m_nodeManager = new NodeManager(this);
    m_database = nullptr; // Database opened after connection with device-specific path

    m_mapWidget = nullptr;
    m_dashboardStats = nullptr;
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
        if (m_serial->isConnected()) {
            qDebug() << "[MainWindow] Sending config heartbeat";
            QByteArray heartbeat = m_protocol->createHeartbeatPacket();
            m_serial->sendData(heartbeat);
        } });

    // Persistent connection heartbeat (keeps connection alive for long sessions)
    m_connectionHeartbeatTimer = new QTimer(this);
    m_connectionHeartbeatTimer->setInterval(60000); // 60 seconds - slower than config heartbeat
    connect(m_connectionHeartbeatTimer, &QTimer::timeout, this, [this]()
            {
        if (m_serial->isConnected()) {
            qDebug() << "[MainWindow] Sending connection keep-alive heartbeat";
            QByteArray heartbeat = m_protocol->createHeartbeatPacket();
            m_serial->sendData(heartbeat);
        } });

    // Connect signals
    connect(m_serial, &SerialConnection::connected,
            this, &MainWindow::onConnected);
    connect(m_serial, &SerialConnection::disconnected,
            this, &MainWindow::onDisconnected);
    connect(m_serial, &SerialConnection::dataReceived,
            this, &MainWindow::onDataReceived);
    connect(m_serial, &SerialConnection::errorOccurred,
            this, &MainWindow::onSerialError);

    connect(m_protocol, &MeshtasticProtocol::packetReceived,
            this, &MainWindow::onPacketReceived);
    connect(m_protocol, &MeshtasticProtocol::parseError,
            [this](const QString &error)
            {
                statusBar()->showMessage(error, 5000);
            });

    connect(m_nodeManager, &NodeManager::nodesChanged,
            this, [this]() {
                m_nodesSortNeeded = true;
                updateNodeList();
            });

    // Initial refresh
    refreshPorts();
    updateStatusLabel();

    // Auto-connect if enabled
    if (AppSettings::instance()->autoConnect())
    {
        QString lastPort = AppSettings::instance()->lastPort();
        if (!lastPort.isEmpty())
        {
            // Find the port in combo box
            for (int i = 0; i < m_portCombo->count(); i++)
            {
                if (m_portCombo->itemData(i).toString() == lastPort)
                {
                    m_portCombo->setCurrentIndex(i);
                    QTimer::singleShot(500, this, &MainWindow::connectToSelected);
                    break;
                }
            }
        }
    }

    // Listen for settings changes
    connect(AppSettings::instance(), &AppSettings::settingChanged,
            this, &MainWindow::onSettingChanged);

    // Restore window state (geometry, splitter sizes)
    restoreWindowState();

    // Workaround: QWebEngineView GPU init can strip window decoration flags on Linux.
    // Re-assert flags after the event loop has processed the initial show.
    QTimer::singleShot(0, this, [this]() {
        if (!(windowFlags() & Qt::WindowMaximizeButtonHint)) {
            setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
            show();
        }
    });
}

MainWindow::~MainWindow()
{
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

    setupConfigTab();

    // Connect traceroute selection to map visualization
    connect(m_tracerouteWidget, &TracerouteWidget::tracerouteSelected,
            this, &MainWindow::onTracerouteSelected);

    // Status bar with cooldown indicator
    m_statusLabel = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(m_statusLabel);

    // Traceroute cooldown text label (hidden by default)
    m_tracerouteCooldownLabel = new QLabel;
    m_tracerouteCooldownLabel->setStyleSheet("QLabel { color: #ff6f00; font-weight: bold; padding-right: 10px; }");
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

    // Port selection
    QLabel *portLabel = new QLabel(" Port: ");
    toolbar->addWidget(portLabel);

    m_portCombo = new QComboBox;
    m_portCombo->setMinimumWidth(200);
    toolbar->addWidget(m_portCombo);

    m_refreshButton = new QPushButton("Refresh");
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    toolbar->addWidget(m_refreshButton);

    toolbar->addSeparator();

    m_connectButton = new QPushButton("Connect");
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectToSelected);
    toolbar->addWidget(m_connectButton);

    m_disconnectButton = new QPushButton("Disconnect");
    m_disconnectButton->setEnabled(false);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnect);
    toolbar->addWidget(m_disconnectButton);

    m_rebootButton = new QPushButton("Reboot Device");
    m_rebootButton->setEnabled(false);
    m_rebootButton->setToolTip("Reboot the connected Meshtastic device");
    connect(m_rebootButton, &QPushButton::clicked, this, &MainWindow::rebootDevice);
    toolbar->addWidget(m_rebootButton);

    toolbar->addSeparator();

    QPushButton *configButton = new QPushButton("Request Config");
    connect(configButton, &QPushButton::clicked, this, &MainWindow::requestConfig);
    toolbar->addWidget(configButton);
}

void MainWindow::setupMapTab()
{
    QWidget *mapTab = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(mapTab);

    // Splitter for map and node list
    m_mapSplitter = new QSplitter(Qt::Horizontal);

    // Map widget
    m_mapWidget = new MapWidget(m_nodeManager);
    m_mapSplitter->addWidget(m_mapWidget);

    // Node list sidebar
    QWidget *sidebar = new QWidget;
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);

    // Dashboard stats panel
    m_dashboardStats = new DashboardStatsWidget(m_nodeManager, m_configWidget->deviceConfig());
    sidebarLayout->addWidget(m_dashboardStats);

    QLabel *nodesLabel = new QLabel("Nodes");
    nodesLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    sidebarLayout->addWidget(nodesLabel);

    // Node search filter
    m_nodeSearchEdit = new QLineEdit;
    m_nodeSearchEdit->setPlaceholderText("Search nodes...");
    m_nodeSearchEdit->setClearButtonEnabled(true);
    connect(m_nodeSearchEdit, &QLineEdit::textChanged,
            this, &MainWindow::updateNodeList);
    sidebarLayout->addWidget(m_nodeSearchEdit);

    // Node table setup
    m_nodeTable = new QTableWidget;
    m_nodeTable->setColumnCount(6);
    m_nodeTable->setHorizontalHeaderLabels({"Name", "Short", "Role", "Last Heard", "Battery", "Signal"});
    m_nodeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeTable->setSortingEnabled(true);
    m_nodeTable->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarLayout->addWidget(m_nodeTable);

    m_mapSplitter->addWidget(sidebar);
    m_mapSplitter->setSizes({800, 200});

    layout->addWidget(m_mapSplitter);
    m_tabWidget->addTab(mapTab, "Map");

    // Connect node table signals
    connect(m_nodeTable, &QTableWidget::itemClicked,
            this, &MainWindow::onNodeSelected);
    connect(m_nodeTable, &QTableWidget::customContextMenuRequested,
            this, &MainWindow::onNodeContextMenu);
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
    }
}

void MainWindow::refreshPorts()
{
    m_portCombo->clear();

    // First add detected Meshtastic devices
    QList<QSerialPortInfo> meshtasticPorts = SerialConnection::detectMeshtasticDevices();
    for (const QSerialPortInfo &info : meshtasticPorts)
    {
        QString label = QString("%1 - %2 [Meshtastic]")
                            .arg(info.portName())
                            .arg(SerialConnection::deviceDescription(info));
        m_portCombo->addItem(label, info.portName());
    }

    // Then add other ports
    QList<QSerialPortInfo> allPorts = SerialConnection::availablePorts();
    for (const QSerialPortInfo &info : allPorts)
    {
        // Skip if already added as Meshtastic device
        bool isMeshtastic = false;
        for (const QSerialPortInfo &mesh : meshtasticPorts)
        {
            if (mesh.portName() == info.portName())
            {
                isMeshtastic = true;
                break;
            }
        }
        if (isMeshtastic)
            continue;

        QString label = QString("%1 - %2")
                            .arg(info.portName())
                            .arg(SerialConnection::deviceDescription(info));
        m_portCombo->addItem(label, info.portName());
    }

    if (m_portCombo->count() == 0)
    {
        m_portCombo->addItem("No ports found", QString());
    }
}

void MainWindow::connectToSelected()
{
    QString portName = m_portCombo->currentData().toString();
    if (portName.isEmpty())
    {
        QMessageBox::warning(this, "Error", "No port selected");
        return;
    }

    m_connectButton->setEnabled(false);
    statusBar()->showMessage("Connecting to " + portName + "...");

    if (!m_serial->connectToPort(portName))
    {
        m_connectButton->setEnabled(true);
    }
}

void MainWindow::disconnect()
{
    m_serial->disconnectDevice();
}

void MainWindow::rebootDevice()
{
    if (!m_serial->isConnected())
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
    m_serial->sendData(packet);

    statusBar()->showMessage("Reboot command sent. Device will restart in 5 seconds...", 5000);
}

void MainWindow::onConnected()
{
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
    m_rebootButton->setEnabled(true);
    m_portCombo->setEnabled(false);
    m_refreshButton->setEnabled(false);

    // Save last used port for auto-connect
    AppSettings::instance()->setLastPort(m_serial->connectedPortName());

    // Start persistent heartbeat for long sessions
    m_connectionHeartbeatTimer->start();

    // Clean up old data on connect (runs in background)
    QTimer::singleShot(5000, this, [this]() {
        if (m_database) {
            m_database->deleteOldPackets(7);  // Delete packets older than 7 days
            m_database->deleteTelemetryHistory(7);  // Delete telemetry older than 7 days
        }
    });

    updateStatusLabel();
    statusBar()->showMessage("Connected", 3000);

    // Request config after a short delay to let the device initialize
    QTimer::singleShot(500, this, &MainWindow::requestConfig);
}

void MainWindow::onDisconnected()
{
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
    m_rebootButton->setEnabled(false);
    m_portCombo->setEnabled(true);
    m_refreshButton->setEnabled(true);

    // Stop heartbeat timers
    m_connectionHeartbeatTimer->stop();
    m_configHeartbeatTimer->stop();

    // Close database and clear nodes
    closeDatabase();

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
            if (m_messagesWidget && packet.fields.contains("text") && !packet.fields.contains("decrypted"))
            {
                // Only route device-decoded messages to Messages widget.
                // Brute-force decrypted packets (fields["decrypted"]=true) are from
                // unknown channels and should only appear in the Packet List.
                ChatMessage msg;
                msg.fromNode = packet.from;
                msg.toNode = packet.to;
                msg.text = packet.fields["text"].toString();
                msg.channelIndex = packet.channelIndex;
                msg.timestamp = QDateTime::currentDateTime();
                msg.packetId = packet.fields.value("packetId", 0).toUInt();
                m_messagesWidget->addMessage(msg);

                // Auto-respond to ping if enabled (DMs only, not from self)
                uint32_t myNode = m_nodeManager->myNodeNum();
                bool isDM = (packet.to == myNode && packet.to != 0xFFFFFFFF);
                bool isFromOther = (packet.from != myNode);
                bool isPing = msg.text.trimmed().compare("ping", Qt::CaseInsensitive) == 0;

                if (isDM && isFromOther && isPing && AppSettings::instance()->autoPingResponse())
                {
                    qDebug() << "[MainWindow] Auto-responding to ping from" << QString::number(packet.from, 16);
                    // Respond with "pong" after a short delay
                    QTimer::singleShot(500, this, [this, fromNode = packet.from]() {
                        onSendMessage("pong", fromNode, 0);
                    });
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

        case MeshtasticProtocol::PortNum::Traceroute:
            if (packet.fields.contains("route") || packet.fields.contains("routeBack"))
            {
                showTracerouteResult(packet);
                m_tracerouteWidget->addTraceroute(packet);
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
    statusBar()->showMessage("Error: " + error, 5000);
}

void MainWindow::onNodeSelected(QTableWidgetItem *item)
{
    if (!item)
        return;
    int row = item->row();
    uint32_t nodeNum = m_nodeTable->item(row, 0)->data(Qt::UserRole).toUInt();
    if (m_mapWidget)
    {
        NodeInfo node = m_nodeManager->getNode(nodeNum);
        if (node.hasPosition)
        {
            m_mapWidget->centerOnLocation(node.latitude, node.longitude);
            m_mapWidget->setZoomLevel(15);
            m_mapWidget->selectNode(nodeNum);
            m_tabWidget->setCurrentIndex(0); // Switch to map tab
        }
    }
}

void MainWindow::navigateToNode(uint32_t nodeNum)
{
    // Switch to Map tab (index 0)
    m_tabWidget->setCurrentIndex(0);

    // Find and select the node in the table
    for (int row = 0; row < m_nodeTable->rowCount(); ++row)
    {
        QTableWidgetItem *item = m_nodeTable->item(row, 0);
        if (item && item->data(Qt::UserRole).toUInt() == nodeNum)
        {
            m_nodeTable->selectRow(row);
            m_nodeTable->scrollToItem(item);

            // Also center map on node if it has position
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            if (node.hasPosition && m_mapWidget)
            {
                m_mapWidget->centerOnLocation(node.latitude, node.longitude);
                m_mapWidget->setZoomLevel(14);
                m_mapWidget->selectNode(nodeNum);
            }
            break;
        }
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
}

void MainWindow::onNodeContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = m_nodeTable->itemAt(pos);
    if (!item)
        return;

    // Ensure we have the data
    uint32_t nodeNum = item->data(Qt::UserRole).toUInt();
    if (nodeNum == 0)
        return;

    NodeInfo node = m_nodeManager->getNode(nodeNum);

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
    if (nodeNum != m_nodeManager->myNodeNum())
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
    QAction *selectedAction = menu.exec(m_nodeTable->viewport()->mapToGlobal(pos));

    if (selectedAction == sendDmAction && sendDmAction)
    {
        // Switch to Messages tab and start DM with this node
        m_messagesWidget->startDirectMessage(nodeNum);
        m_tabWidget->setCurrentWidget(m_messagesWidget);
    }
    else if (selectedAction == tracerouteAction)
    {
        requestTraceroute(nodeNum);
    }
    else if (selectedAction == nodeInfoAction)
    {
        requestNodeInfo(nodeNum);
    }
    else if (selectedAction == telemetryAction)
    {
        requestTelemetry(nodeNum);
    }
    else if (selectedAction == positionAction)
    {
        requestPosition(nodeNum);
    }
    else if (selectedAction == centerMapAction && node.hasPosition)
    {
        m_mapWidget->centerOnLocation(node.latitude, node.longitude);
        m_mapWidget->setZoomLevel(15);
        m_mapWidget->selectNode(nodeNum);
        m_tabWidget->setCurrentIndex(0); // Switch to map tab
    }
}

void MainWindow::requestTraceroute(uint32_t nodeNum)
{
    if (!m_serial->isConnected())
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
    m_serial->sendData(packet);

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
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createNodeInfoRequestPacket(nodeNum, myNode);
    m_serial->sendData(packet);

    statusBar()->showMessage("Node info request sent...", 3000);
}

void MainWindow::requestTelemetry(uint32_t nodeNum)
{
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createTelemetryRequestPacket(nodeNum, myNode);
    m_serial->sendData(packet);

    statusBar()->showMessage("Telemetry request sent...", 3000);
}

void MainWindow::requestPosition(uint32_t nodeNum)
{
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createPositionRequestPacket(nodeNum, myNode);
    m_serial->sendData(packet);

    statusBar()->showMessage("Position request sent...", 3000);
}

void MainWindow::updateNodeList()
{
    m_nodeTable->setRowCount(0);

    // Only re-sort when node data has changed, not just filter changes
    if (m_nodesSortNeeded)
    {
        m_sortedNodes = m_nodeManager->allNodes();
        uint32_t myNode = m_nodeManager->myNodeNum();
        std::sort(m_sortedNodes.begin(), m_sortedNodes.end(),
                  [myNode](const NodeInfo &a, const NodeInfo &b)
                  {
                      if (a.nodeNum == myNode) return true;
                      if (b.nodeNum == myNode) return false;
                      return a.lastHeard > b.lastHeard;
                  });
        m_nodesSortNeeded = false;
    }
    const QList<NodeInfo> &nodes = m_sortedNodes;

    // Get offline filter settings
    bool showOffline = AppSettings::instance()->showOfflineNodes();
    int offlineThresholdMins = AppSettings::instance()->offlineThresholdMinutes();
    QDateTime offlineThreshold = QDateTime::currentDateTime().addSecs(-offlineThresholdMins * 60);

    // Get search filter
    QString searchTerm = m_nodeSearchEdit ? m_nodeSearchEdit->text().trimmed().toLower() : QString();

    uint32_t myNode = m_nodeManager->myNodeNum();

    int row = 0;
    for (const NodeInfo &node : nodes)
    {
        // Filter offline nodes if setting is disabled
        if (!showOffline && node.lastHeard.isValid() && node.lastHeard < offlineThreshold)
        {
            continue;
        }

        // Filter by search term
        if (!searchTerm.isEmpty())
        {
            bool matches = node.longName.toLower().contains(searchTerm) ||
                           node.shortName.toLower().contains(searchTerm) ||
                           node.nodeId.toLower().contains(searchTerm);
            if (!matches)
                continue;
        }

        bool isMyNode = (node.nodeNum == myNode);

        m_nodeTable->insertRow(row);

        // Col 0: Node Name
        QString name = node.longName.isEmpty() ? node.nodeId : node.longName;
        if (node.isFavorite)
        {
            name = "[*] " + name;
        }
        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, node.nodeNum);
        if (!node.hasPosition)
        {
            nameItem->setForeground(QBrush(Qt::gray));
        }
        if (isMyNode)
        {
            QFont boldFont = nameItem->font();
            boldFont.setBold(true);
            nameItem->setFont(boldFont);
        }
        m_nodeTable->setItem(row, 0, nameItem);

        // Col 1: Short Name
        QTableWidgetItem *shortItem = new QTableWidgetItem(node.shortName);
        shortItem->setData(Qt::UserRole, node.nodeNum);
        shortItem->setTextAlignment(Qt::AlignCenter);
        if (!node.hasPosition)
        {
            shortItem->setForeground(QBrush(Qt::gray));
        }
        if (isMyNode)
        {
            QFont boldFont = shortItem->font();
            boldFont.setBold(true);
            shortItem->setFont(boldFont);
        }
        m_nodeTable->setItem(row, 1, shortItem);

        // Col 2: Role
        QTableWidgetItem *roleItem = new QTableWidgetItem(m_nodeManager->roleToString(node.role));
        roleItem->setData(Qt::UserRole, node.nodeNum);
        if (!node.hasPosition)
        {
            roleItem->setForeground(QBrush(Qt::gray));
        }
        m_nodeTable->setItem(row, 2, roleItem);

        // Col 3: Last Heard
        QTableWidgetItem *heardItem = new QTableWidgetItem(node.lastHeard.toString("yyyy-MM-dd HH:mm:ss"));
        if (!node.hasPosition)
        {
            heardItem->setForeground(QBrush(Qt::gray));
        }
        m_nodeTable->setItem(row, 3, heardItem);

        // Col 4: Battery
        QTableWidgetItem *batteryItem = new QTableWidgetItem;
        if (!node.hasPosition)
        {
            batteryItem->setForeground(QBrush(Qt::gray));
        }
        if (node.batteryLevel >= 0)
        {
            if (node.isExternalPower)
            {
                batteryItem->setIcon(QIcon::fromTheme("battery-charging"));
                batteryItem->setText("Plugged");
            }
            else
            {
                int level = node.batteryLevel;
                if (level > 80)
                    batteryItem->setIcon(QIcon::fromTheme("battery-full"));
                else if (level > 60)
                    batteryItem->setIcon(QIcon::fromTheme("battery-good"));
                else if (level > 40)
                    batteryItem->setIcon(QIcon::fromTheme("battery-medium"));
                else if (level > 20)
                    batteryItem->setIcon(QIcon::fromTheme("battery-low"));
                else
                    batteryItem->setIcon(QIcon::fromTheme("battery-caution"));
                batteryItem->setText(QString::number(level) + "%");
            }
        }
        else
        {
            batteryItem->setText("?");
        }
        m_nodeTable->setItem(row, 4, batteryItem);

        // Col 5: Signal (bars for 0-hop, hop count for multi-hop)
        QTableWidgetItem *signalItem = new QTableWidgetItem;
        signalItem->setTextAlignment(Qt::AlignCenter);
        if (node.hopsAway == 0)
        {
            // Direct node - show signal bars based on SNR
            QString bars;
            QColor color;
            float snr = node.snr;
            if (snr >= 10.0f) {
                bars = "||||";
                color = QColor("#2e7d32"); // green
            } else if (snr >= 5.0f) {
                bars = "|||";
                color = QColor("#2e7d32"); // green
            } else if (snr >= 0.0f) {
                bars = "||";
                color = QColor("#f57c00"); // orange
            } else if (snr >= -5.0f) {
                bars = "|";
                color = QColor("#c62828"); // red
            } else {
                bars = " ";
                color = QColor("#c62828"); // red
            }
            signalItem->setText(bars);
            signalItem->setForeground(QBrush(color));
            signalItem->setToolTip(QString("SNR: %1 dB / RSSI: %2").arg(node.snr, 0, 'f', 1).arg(node.rssi));
        }
        else if (node.hopsAway > 0)
        {
            signalItem->setText(QString("%1 hop%2").arg(node.hopsAway).arg(node.hopsAway > 1 ? "s" : ""));
            signalItem->setForeground(QBrush(QColor("#6c757d")));
        }
        else
        {
            signalItem->setText("-");
            if (!node.hasPosition)
                signalItem->setForeground(QBrush(Qt::gray));
        }
        m_nodeTable->setItem(row, 5, signalItem);
        row++;
    }

    // Draw test lines if test mode is enabled
    if (m_testMode && m_mapWidget)
    {
        drawTestNodeLines();
    }
}

void MainWindow::updateStatusLabel()
{
    QString status;
    int nodeCount = m_nodeManager->allNodes().count();
    int dbCount = m_database && m_database->isOpen() ? m_database->nodeCount() : 0;

    if (m_serial->isConnected())
    {
        status = QString("Connected: %1 | Nodes: %2 (DB: %3)")
                     .arg(m_serial->connectedPortName())
                     .arg(nodeCount)
                     .arg(dbCount);
    }
    else
    {
        status = "Disconnected";
    }
    m_statusLabel->setText(status);
}

void MainWindow::requestConfig()
{
    if (!m_serial->isConnected())
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
    m_serial->sendData(m_protocol->createWantConfigPacket(m_expectedConfigId));

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
    closeDatabase();
    QString nodeId = MeshtasticProtocol::nodeIdToString(nodeNum);
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = QString("%1/meshtastic_%2.db").arg(dataDir, nodeId);
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

        statusBar()->showMessage(QString("Database loaded: %1 nodes").arg(m_database->nodeCount()), 3000);
    }
    else
    {
        statusBar()->showMessage("Failed to open database", 5000);
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

    // 2. Clear local node state
    m_nodeManager->clear();

    // 3. Close and destroy the database
    m_database->close();
    delete m_database;
    m_database = nullptr;
}

void MainWindow::onSendMessage(const QString &text, uint32_t toNode, int channel)
{
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    uint32_t packetId = 0;
    QByteArray packet = m_protocol->createTextMessagePacket(text, toNode, myNode, channel, 0, &packetId);
    m_serial->sendData(packet);

    qDebug() << "[MainWindow] Sent message with packetId:" << packetId;

    // Add the outgoing message to our local display
    ChatMessage msg;
    msg.fromNode = myNode;
    msg.toNode = toNode;
    msg.channelIndex = channel;
    msg.text = text;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isOutgoing = true;
    msg.packetId = packetId;
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
    statusBar()->showMessage(QString("Message sent to %1").arg(destName), 3000);
}

void MainWindow::onSendReaction(const QString &emoji, uint32_t toNode, int channel, uint32_t replyId)
{
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    uint32_t packetId = 0;
    QByteArray packet = m_protocol->createTextMessagePacket(emoji, toNode, myNode, channel, replyId, &packetId);
    m_serial->sendData(packet);

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
        // Update map tile server
        if (m_mapWidget)
        {
            m_mapWidget->setTileServer(value.toString());
        }
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
}

void MainWindow::showTracerouteResult(const MeshtasticProtocol::DecodedPacket &packet)
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("Traceroute Result");
    dialog->setMinimumSize(500, 400);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    // Header with source and destination
    QString fromName, toName;
    if (packet.from != 0)
    {
        NodeInfo fromNode = m_nodeManager->getNode(packet.from);
        fromName = fromNode.longName.isEmpty()
                       ? MeshtasticProtocol::nodeIdToString(packet.from)
                       : fromNode.longName;
    }
    if (packet.to != 0)
    {
        NodeInfo toNode = m_nodeManager->getNode(packet.to);
        toName = toNode.longName.isEmpty()
                     ? MeshtasticProtocol::nodeIdToString(packet.to)
                     : toNode.longName;
    }

    QLabel *headerLabel = new QLabel(QString("<h3>Traceroute: %1 → %2</h3>").arg(fromName, toName));
    headerLabel->setTextFormat(Qt::RichText);
    layout->addWidget(headerLabel);

    // Build the traceroute display
    QTextEdit *resultText = new QTextEdit;
    resultText->setReadOnly(true);
    resultText->setFont(QFont("monospace", 10));

    QString html;
    html += "<style>"
            "table { border-collapse: collapse; width: 100%; margin: 10px 0; }"
            "th, td { border: 1px solid #ccc; padding: 8px; text-align: left; }"
            "th { background-color: #f0f0f0; }"
            ".snr-good { color: #2e7d32; font-weight: bold; }"
            ".snr-ok { color: #f57c00; font-weight: bold; }"
            ".snr-bad { color: #c62828; font-weight: bold; }"
            ".arrow { font-size: 16px; color: #666; text-align: center; }"
            "</style>";

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
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            QString nodeName = node.longName.isEmpty() ? nodeId : node.longName;

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrTowards.size())
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
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            QString nodeName = node.longName.isEmpty() ? nodeId : node.longName;

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrBack.size())
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
    if (!m_serial->isConnected())
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
    m_serial->sendData(packet);

    statusBar()->showMessage("LoRa config saved to device", 3000);
}

void MainWindow::onSaveDeviceConfig()
{
    if (!m_serial->isConnected())
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
    m_serial->sendData(packet);

    statusBar()->showMessage("Device config saved to device", 3000);
}

void MainWindow::onSavePositionConfig()
{
    if (!m_serial->isConnected())
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
    m_serial->sendData(packet);

    statusBar()->showMessage("Position config saved to device", 3000);
}

void MainWindow::onSaveChannelConfig(int channelIndex)
{
    qDebug() << "=== onSaveChannelConfig called for channel" << channelIndex << "===";

    if (!m_serial->isConnected())
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

    m_serial->sendData(packet);
    qDebug() << "Packet sent to serial";

    // Update MessagesWidget immediately (don't wait for device response)
    if (m_messagesWidget)
    {
        bool enabled = (ch.role > 0);  // role: 0=disabled, 1=primary, 2=secondary
        m_messagesWidget->setChannel(channelIndex, ch.name, enabled);
    }

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
        if (m_serial && m_serial->isConnected() && m_protocol)
        {
            qDebug() << "[MainWindow] Requesting session key for admin operations";
            m_serial->sendData(m_protocol->createSessionKeyRequestPacket());
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
