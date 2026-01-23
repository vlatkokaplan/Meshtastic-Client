#include "MainWindow.h"
#include "SerialConnection.h"
#include "MeshtasticProtocol.h"
#include "NodeManager.h"
#include "PacketListWidget.h"
#include "Database.h"

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
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_serial = new SerialConnection(this);
    m_protocol = new MeshtasticProtocol(this);
    m_nodeManager = new NodeManager(this);
    m_database = nullptr;  // Database opened after connection with device-specific path

    m_mapWidget = nullptr;

    setupUI();

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
            [this](const QString &error) {
                statusBar()->showMessage(error, 5000);
            });

    connect(m_nodeManager, &NodeManager::nodesChanged,
            this, &MainWindow::updateNodeList);

    // Initial refresh
    refreshPorts();
    updateStatusLabel();
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
    setupPacketTab();

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

    m_nodeList = new QListWidget;
    m_nodeList->setMinimumWidth(200);
    connect(m_nodeList, &QListWidget::itemClicked,
            this, &MainWindow::onNodeSelected);
    sidebarLayout->addWidget(m_nodeList);

    splitter->addWidget(sidebar);
    splitter->setSizes({800, 200});

    layout->addWidget(splitter);
    m_tabWidget->addTab(mapTab, "Map");
}

void MainWindow::setupPacketTab()
{
    m_packetList = new PacketListWidget(m_nodeManager);
    m_tabWidget->addTab(m_packetList, "Packets");
}

void MainWindow::refreshPorts()
{
    m_portCombo->clear();

    // First add detected Meshtastic devices
    QList<QSerialPortInfo> meshtasticPorts = SerialConnection::detectMeshtasticDevices();
    for (const QSerialPortInfo &info : meshtasticPorts) {
        QString label = QString("%1 - %2 [Meshtastic]")
            .arg(info.portName())
            .arg(info.description());
        m_portCombo->addItem(label, info.portName());
    }

    // Then add other ports
    QList<QSerialPortInfo> allPorts = SerialConnection::availablePorts();
    for (const QSerialPortInfo &info : allPorts) {
        // Skip if already added as Meshtastic device
        bool isMeshtastic = false;
        for (const QSerialPortInfo &mesh : meshtasticPorts) {
            if (mesh.portName() == info.portName()) {
                isMeshtastic = true;
                break;
            }
        }
        if (isMeshtastic) continue;

        QString label = QString("%1 - %2")
            .arg(info.portName())
            .arg(info.description());
        m_portCombo->addItem(label, info.portName());
    }

    if (m_portCombo->count() == 0) {
        m_portCombo->addItem("No ports found", QString());
    }
}

void MainWindow::connectToSelected()
{
    QString portName = m_portCombo->currentData().toString();
    if (portName.isEmpty()) {
        QMessageBox::warning(this, "Error", "No port selected");
        return;
    }

    m_connectButton->setEnabled(false);
    statusBar()->showMessage("Connecting to " + portName + "...");

    if (!m_serial->connectToPort(portName)) {
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
    switch (packet.type) {
    case MeshtasticProtocol::PacketType::MyInfo:
        if (packet.fields.contains("myNodeNum")) {
            uint32_t myNodeNum = packet.fields["myNodeNum"].toUInt();
            m_nodeManager->setMyNodeNum(myNodeNum);
            openDatabaseForNode(myNodeNum);
        }
        break;

    case MeshtasticProtocol::PacketType::NodeInfo:
        m_nodeManager->updateNodeFromPacket(packet.fields);
        break;

    case MeshtasticProtocol::PacketType::PacketReceived:
        // Update node info from received packets
        if (packet.from != 0) {
            if (packet.fields.contains("rxSnr") || packet.fields.contains("rxRssi")) {
                int hops = -1;
                if (packet.fields.contains("hopStart") && packet.fields.contains("hopLimit")) {
                    hops = packet.fields["hopStart"].toInt() - packet.fields["hopLimit"].toInt();
                }
                m_nodeManager->updateNodeSignal(
                    packet.from,
                    packet.fields.value("rxSnr", 0).toFloat(),
                    packet.fields.value("rxRssi", 0).toInt(),
                    hops
                );
            }
        }

        // Handle specific port types
        switch (packet.portNum) {
        case MeshtasticProtocol::PortNum::Position:
            if (packet.fields.contains("latitude") && packet.fields.contains("longitude")) {
                m_nodeManager->updateNodePosition(
                    packet.from,
                    packet.fields["latitude"].toDouble(),
                    packet.fields["longitude"].toDouble(),
                    packet.fields.value("altitude", 0).toInt()
                );
            }
            break;

        case MeshtasticProtocol::PortNum::NodeInfo:
            m_nodeManager->updateNodeUser(
                packet.from,
                packet.fields.value("longName").toString(),
                packet.fields.value("shortName").toString(),
                packet.fields.value("userId").toString(),
                MeshtasticProtocol::nodeIdToString(packet.from)
            );
            break;

        case MeshtasticProtocol::PortNum::Telemetry:
            m_nodeManager->updateNodeTelemetry(packet.from, packet.fields);
            break;

        default:
            break;
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

void MainWindow::onNodeSelected(QListWidgetItem *item)
{
    uint32_t nodeNum = item->data(Qt::UserRole).toUInt();
    NodeInfo node = m_nodeManager->getNode(nodeNum);

    if (node.hasPosition && m_mapWidget) {
        m_mapWidget->centerOnLocation(node.latitude, node.longitude);
        m_mapWidget->setZoomLevel(15);
        m_mapWidget->selectNode(nodeNum);
    }
}

void MainWindow::requestConfig()
{
    if (!m_serial->isConnected()) {
        return;
    }

    // Send want_config request with a random config ID
    uint32_t configId = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
    QByteArray packet = m_protocol->createWantConfigPacket(configId);
    m_serial->sendData(packet);

    statusBar()->showMessage("Requested configuration...", 3000);
}

void MainWindow::updateNodeList()
{
    m_nodeList->clear();

    QList<NodeInfo> nodes = m_nodeManager->allNodes();

    // Sort by last heard (most recent first)
    std::sort(nodes.begin(), nodes.end(), [](const NodeInfo &a, const NodeInfo &b) {
        if (!a.lastHeard.isValid()) return false;
        if (!b.lastHeard.isValid()) return true;
        return a.lastHeard > b.lastHeard;
    });

    for (const NodeInfo &node : nodes) {
        QString name;
        if (!node.longName.isEmpty()) {
            name = QString("%1 (%2)").arg(node.longName, node.shortName);
        } else if (!node.shortName.isEmpty()) {
            name = node.shortName;
        } else {
            name = node.nodeId;
        }

        // Add battery indicator if available
        if (node.batteryLevel > 0) {
            name += QString(" [%1%]").arg(node.batteryLevel);
        }

        // Add position indicator
        if (node.hasPosition) {
            name += " *";
        }

        // Add heard time
        QString heardStr;
        if (node.lastHeard.isValid()) {
            qint64 secsAgo = node.lastHeard.secsTo(QDateTime::currentDateTime());
            if (secsAgo < 60) {
                heardStr = "now";
            } else if (secsAgo < 3600) {
                heardStr = QString("%1m").arg(secsAgo / 60);
            } else if (secsAgo < 86400) {
                heardStr = QString("%1h").arg(secsAgo / 3600);
            } else {
                heardStr = QString("%1d").arg(secsAgo / 86400);
            }
        } else {
            heardStr = "-";
        }

        QString label = QString("%1  [%2]").arg(name, heardStr);

        QListWidgetItem *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, node.nodeNum);

        // Color code by recency
        if (node.lastHeard.isValid()) {
            qint64 secsAgo = node.lastHeard.secsTo(QDateTime::currentDateTime());
            if (secsAgo < 300) {
                item->setForeground(Qt::darkGreen);
            } else if (secsAgo < 3600) {
                item->setForeground(Qt::darkBlue);
            } else {
                item->setForeground(Qt::gray);
            }
        }

        m_nodeList->addItem(item);
    }
}

void MainWindow::updateStatusLabel()
{
    QString status;
    int nodeCount = m_nodeManager->allNodes().count();
    int dbCount = m_database && m_database->isOpen() ? m_database->nodeCount() : 0;

    if (m_serial->isConnected()) {
        status = QString("Connected: %1 | Nodes: %2 (DB: %3)")
            .arg(m_serial->connectedPortName())
            .arg(nodeCount)
            .arg(dbCount);
    } else {
        status = "Disconnected";
    }
    m_statusLabel->setText(status);
}

void MainWindow::openDatabaseForNode(uint32_t nodeNum)
{
    // Close existing database if any
    closeDatabase();

    // Create database with device-specific path
    QString nodeId = MeshtasticProtocol::nodeIdToString(nodeNum);
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = QString("%1/meshtastic_%2.db").arg(dataDir, nodeId);

    m_database = new Database(this);
    if (m_database->open(dbPath)) {
        m_nodeManager->setDatabase(m_database);
        m_nodeManager->loadFromDatabase();
        statusBar()->showMessage(QString("Database loaded: %1 nodes").arg(m_database->nodeCount()), 3000);
    } else {
        statusBar()->showMessage("Failed to open database", 5000);
    }

    updateStatusLabel();
}

void MainWindow::closeDatabase()
{
    if (m_database) {
        m_database->close();
        delete m_database;
        m_database = nullptr;
    }

    m_nodeManager->setDatabase(nullptr);
    m_nodeManager->clear();
}
