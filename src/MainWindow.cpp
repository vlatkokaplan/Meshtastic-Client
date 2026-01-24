#include "MainWindow.h"
#include "SerialConnection.h"
#include "MeshtasticProtocol.h"
#include "NodeManager.h"
#include "PacketListWidget.h"
#include "Database.h"
#include "MessagesWidget.h"
#include "ConfigWidget.h"
#include "DeviceConfig.h"
#include "AppSettings.h"

#include "MapWidget.h"

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
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Initialize app settings
    AppSettings::instance()->open();

    m_serial = new SerialConnection(this);
    m_protocol = new MeshtasticProtocol(this);
    m_nodeManager = new NodeManager(this);
    m_database = nullptr; // Database opened after connection with device-specific path

    m_mapWidget = nullptr;
    m_messagesWidget = nullptr;
    m_configWidget = nullptr;
    m_trayIcon = nullptr;

    setupUI();

    // Set up system tray for notifications
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        m_trayIcon->setIcon(QIcon::fromTheme("network-wireless", QIcon(":/icon.png")));
        m_trayIcon->setToolTip("Meshtastic Vibe Client");
        m_trayIcon->show();
    }

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
            this, &MainWindow::updateNodeList);

    // Initial refresh
    refreshPorts();
    updateStatusLabel();

    // Auto-connect if enabled
    if (AppSettings::instance()->autoConnect()) {
        QString lastPort = AppSettings::instance()->lastPort();
        if (!lastPort.isEmpty()) {
            // Find the port in combo box
            for (int i = 0; i < m_portCombo->count(); i++) {
                if (m_portCombo->itemData(i).toString() == lastPort) {
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
}

MainWindow::~MainWindow()
{
    m_serial->disconnect();
}

void MainWindow::setupUI()
{
    setWindowTitle("Meshtastic Client");
    resize(1200, 800);

    setupToolbar();

    m_tabWidget = new QTabWidget;
    setCentralWidget(m_tabWidget);

    setupMapTab();
    setupMessagesTab();
    setupPacketTab();
    setupConfigTab();

    // Status bar
    m_statusLabel = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(m_statusLabel);
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
    QSplitter *splitter = new QSplitter(Qt::Horizontal);

    // Map widget
    m_mapWidget = new MapWidget(m_nodeManager);
    splitter->addWidget(m_mapWidget);

    // Node list sidebar
    QWidget *sidebar = new QWidget;
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nodesLabel = new QLabel("Nodes");
    nodesLabel->setStyleSheet("font-weight: bold; padding: 5px;");
    sidebarLayout->addWidget(nodesLabel);

    // Node table setup (remove color column)
    m_nodeTable = new QTableWidget;
    m_nodeTable->setColumnCount(3);
    m_nodeTable->setHorizontalHeaderLabels({"Node Name", "Last Heard", "Battery %"});
    m_nodeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_nodeTable->setSortingEnabled(true);
    m_nodeTable->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarLayout->addWidget(m_nodeTable);

    splitter->addWidget(sidebar);
    splitter->setSizes({800, 200});

    layout->addWidget(splitter);
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
    m_tabWidget->addTab(m_messagesWidget, "Messages");

    // Connect send message signal
    connect(m_messagesWidget, &MessagesWidget::sendMessage,
            this, &MainWindow::onSendMessage);

    // Connect send reaction signal
    connect(m_messagesWidget, &MessagesWidget::sendReaction,
            this, &MainWindow::onSendReaction);
}

void MainWindow::setupPacketTab()
{
    m_packetList = new PacketListWidget(m_nodeManager);
    m_tabWidget->addTab(m_packetList, "Packets");
}

void MainWindow::setupConfigTab()
{
    m_configWidget = new ConfigWidget;
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
                            .arg(info.description());
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
                            .arg(info.description());
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
    m_serial->disconnect();
}

void MainWindow::onConnected()
{
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(true);
    m_portCombo->setEnabled(false);
    m_refreshButton->setEnabled(false);

    // Save last used port for auto-connect
    AppSettings::instance()->setLastPort(m_serial->connectedPortName());

    updateStatusLabel();
    statusBar()->showMessage("Connected", 3000);

    // Request config after a short delay to let the device initialize
    QTimer::singleShot(500, this, &MainWindow::requestConfig);
}

void MainWindow::onDisconnected()
{
    m_connectButton->setEnabled(true);
    m_disconnectButton->setEnabled(false);
    m_portCombo->setEnabled(true);
    m_refreshButton->setEnabled(true);

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

            // Update MessagesWidget
            if (m_messagesWidget) {
                m_messagesWidget->setChannel(index, name, enabled);
            }

            // Update DeviceConfig for config tab
            if (m_configWidget && m_configWidget->deviceConfig()) {
                m_configWidget->deviceConfig()->updateFromChannelPacket(packet.fields);
            }
        }
        break;

    case MeshtasticProtocol::PacketType::Config:
        if (m_configWidget && m_configWidget->deviceConfig())
        {
            QString configType = packet.fields.value("configType").toString();
            DeviceConfig *devConfig = m_configWidget->deviceConfig();

            if (configType == "lora") {
                devConfig->updateFromLoRaPacket(packet.fields);
            } else if (configType == "device") {
                devConfig->updateFromDevicePacket(packet.fields);
            } else if (configType == "position") {
                devConfig->updateFromPositionPacket(packet.fields);
            }
        }
        break;

    case MeshtasticProtocol::PacketType::PacketReceived:
    {
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
            }
            break;

        case MeshtasticProtocol::PortNum::TextMessage:
            if (m_messagesWidget && packet.fields.contains("text"))
            {
                ChatMessage msg;
                msg.fromNode = packet.from;
                msg.toNode = packet.to;
                msg.text = packet.fields["text"].toString();
                msg.channelIndex = packet.fields.value("channel", 0).toInt();
                msg.timestamp = QDateTime::currentDateTime();
                msg.packetId = packet.fields.value("id", 0).toUInt();
                m_messagesWidget->addMessage(msg);

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
            }
            break;

        default:
            break;
        }
        break;
    }

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

void MainWindow::onNodeContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = m_nodeTable->itemAt(pos);
    if (!item)
        return;
    int row = item->row();
    uint32_t nodeNum = m_nodeTable->item(row, 0)->data(Qt::UserRole).toUInt();
    NodeInfo node = m_nodeManager->getNode(nodeNum);
    QMenu menu(this);
    QString nodeName = node.longName.isEmpty() ? node.shortName : node.longName;
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

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createTraceroutePacket(nodeNum, myNode);
    m_serial->sendData(packet);

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    QString name = node.longName.isEmpty() ? node.nodeId : node.longName;
    statusBar()->showMessage(QString("Traceroute request sent to %1...").arg(name), 5000);
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
    QList<NodeInfo> nodes = m_nodeManager->allNodes();
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo &a, const NodeInfo &b)
              { return a.lastHeard > b.lastHeard; });

    // Get offline filter settings
    bool showOffline = AppSettings::instance()->showOfflineNodes();
    int offlineThresholdMins = AppSettings::instance()->offlineThresholdMinutes();
    QDateTime offlineThreshold = QDateTime::currentDateTime().addSecs(-offlineThresholdMins * 60);

    int row = 0;
    for (const NodeInfo &node : nodes)
    {
        // Filter offline nodes if setting is disabled
        if (!showOffline && node.lastHeard.isValid() && node.lastHeard < offlineThreshold) {
            continue;
        }
        m_nodeTable->insertRow(row);
        // Node Name
        QString name = node.longName.isEmpty() ? node.nodeId : node.longName;
        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, node.nodeNum);
        if (!node.hasPosition)
        {
            nameItem->setForeground(QBrush(Qt::gray));
        }
        m_nodeTable->setItem(row, 0, nameItem);
        // Last Heard
        QTableWidgetItem *heardItem = new QTableWidgetItem(node.lastHeard.toString("yyyy-MM-dd HH:mm:ss"));
        if (!node.hasPosition)
        {
            heardItem->setForeground(QBrush(Qt::gray));
        }
        m_nodeTable->setItem(row, 1, heardItem);
        // Battery icon and value
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
                {
                    batteryItem->setIcon(QIcon::fromTheme("battery-full"));
                }
                else if (level > 60)
                {
                    batteryItem->setIcon(QIcon::fromTheme("battery-good"));
                }
                else if (level > 40)
                {
                    batteryItem->setIcon(QIcon::fromTheme("battery-medium"));
                }
                else if (level > 20)
                {
                    batteryItem->setIcon(QIcon::fromTheme("battery-low"));
                }
                else
                {
                    batteryItem->setIcon(QIcon::fromTheme("battery-caution"));
                }
                batteryItem->setText(QString::number(level) + "%");
            }
        }
        else
        {
            batteryItem->setText("?");
        }
        m_nodeTable->setItem(row, 2, batteryItem);
        row++;
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
    // Send want_config request with a random config ID
    uint32_t configId = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    QByteArray packet = m_protocol->createWantConfigPacket(configId);
    m_serial->sendData(packet);
    statusBar()->showMessage("Requested configuration...", 3000);
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
        m_nodeManager->loadFromDatabase();
        if (m_messagesWidget)
        {
            m_messagesWidget->setDatabase(m_database);
            m_messagesWidget->loadFromDatabase();
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
    if (m_database)
    {
        m_database->close();
        delete m_database;
        m_database = nullptr;
    }
    m_nodeManager->setDatabase(nullptr);
    m_nodeManager->clear();
    if (m_messagesWidget)
    {
        m_messagesWidget->setDatabase(nullptr);
        m_messagesWidget->clear();
    }
}

void MainWindow::onSendMessage(const QString &text, uint32_t toNode, int channel)
{
    if (!m_serial->isConnected())
    {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createTextMessagePacket(text, toNode, myNode, channel);
    m_serial->sendData(packet);

    // Add the outgoing message to our local display
    ChatMessage msg;
    msg.fromNode = myNode;
    msg.toNode = toNode;
    msg.channelIndex = channel;
    msg.text = text;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isOutgoing = true;
    msg.packetId = QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF;
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
    QByteArray packet = m_protocol->createTextMessagePacket(emoji, toNode, myNode, channel, replyId);
    m_serial->sendData(packet);

    // Add the reaction to our local display
    ChatMessage msg;
    msg.fromNode = myNode;
    msg.toNode = toNode;
    msg.channelIndex = channel;
    msg.text = emoji;
    msg.timestamp = QDateTime::currentDateTime();
    msg.isOutgoing = true;
    msg.packetId = QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF;
    m_messagesWidget->addMessage(msg);

    statusBar()->showMessage(QString("Reaction %1 sent").arg(emoji), 3000);
}

void MainWindow::onSettingChanged(const QString &key, const QVariant &value)
{
    if (key == "nodes/show_offline" || key == "nodes/offline_threshold_minutes") {
        // Refresh node list with new filter settings
        updateNodeList();
    } else if (key == "map/tile_server") {
        // Update map tile server
        if (m_mapWidget) {
            m_mapWidget->setTileServer(value.toString());
        }
    }
}

void MainWindow::showNotification(const QString &title, const QString &message)
{
    if (!AppSettings::instance()->notificationsEnabled()) {
        return;
    }

    if (m_trayIcon && QSystemTrayIcon::supportsMessages()) {
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
    if (packet.from != 0) {
        NodeInfo fromNode = m_nodeManager->getNode(packet.from);
        fromName = fromNode.longName.isEmpty()
            ? MeshtasticProtocol::nodeIdToString(packet.from)
            : fromNode.longName;
    }
    if (packet.to != 0) {
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
    if (route.isEmpty()) {
        html += "<p><i>Direct connection (no hops)</i></p>";
    } else {
        html += "<table><tr><th>Hop</th><th>Node</th><th>SNR (dB)</th></tr>";

        // Start from source
        html += QString("<tr><td>0</td><td><b>%1</b> (origin)</td><td>-</td></tr>").arg(fromName);

        for (int i = 0; i < route.size(); i++) {
            QString nodeId = route[i].toString();
            uint32_t nodeNum = MeshtasticProtocol::nodeIdFromString(nodeId);
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            QString nodeName = node.longName.isEmpty() ? nodeId : node.longName;

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrTowards.size()) {
                double snr = snrTowards[i].toDouble();
                snrStr = QString::number(snr, 'f', 1);
                if (snr >= 5.0) snrClass = "snr-good";
                else if (snr >= 0.0) snrClass = "snr-ok";
                else snrClass = "snr-bad";
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
    if (routeBack.isEmpty()) {
        html += "<p><i>Direct return (no hops) or same as outgoing route</i></p>";
    } else {
        html += "<table><tr><th>Hop</th><th>Node</th><th>SNR (dB)</th></tr>";

        // Start from destination
        html += QString("<tr><td>0</td><td><b>%1</b> (destination)</td><td>-</td></tr>").arg(toName);

        for (int i = 0; i < routeBack.size(); i++) {
            QString nodeId = routeBack[i].toString();
            uint32_t nodeNum = MeshtasticProtocol::nodeIdFromString(nodeId);
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            QString nodeName = node.longName.isEmpty() ? nodeId : node.longName;

            QString snrStr = "-";
            QString snrClass = "";
            if (i < snrBack.size()) {
                double snr = snrBack[i].toDouble();
                snrStr = QString::number(snr, 'f', 1);
                if (snr >= 5.0) snrClass = "snr-good";
                else if (snr >= 0.0) snrClass = "snr-ok";
                else snrClass = "snr-bad";
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
    if (!m_serial->isConnected()) {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig) return;

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
    if (!m_serial->isConnected()) {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig) return;

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
    if (!m_serial->isConnected()) {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig) return;

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
    if (!m_serial->isConnected()) {
        statusBar()->showMessage("Not connected", 3000);
        return;
    }

    DeviceConfig *devConfig = m_configWidget->deviceConfig();
    if (!devConfig) return;

    const auto &ch = devConfig->channel(channelIndex);
    QVariantMap config;
    config["role"] = ch.role;
    config["name"] = ch.name;
    config["psk"] = ch.psk;
    config["uplinkEnabled"] = ch.uplinkEnabled;
    config["downlinkEnabled"] = ch.downlinkEnabled;

    uint32_t myNode = m_nodeManager->myNodeNum();
    QByteArray packet = m_protocol->createChannelConfigPacket(myNode, myNode, channelIndex, config);
    m_serial->sendData(packet);

    statusBar()->showMessage(QString("Channel %1 config saved to device").arg(channelIndex), 3000);
}
