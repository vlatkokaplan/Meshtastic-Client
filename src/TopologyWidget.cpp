#include "TopologyWidget.h"
#include "NodeManager.h"
#include "Database.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDebug>

TopologyWidget::TopologyWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent)
    , m_nodeManager(nodeManager)
    , m_graphReady(false)
    , m_bridge(new TopologyBridge(this))
{
    setupUI();
    connect(m_bridge, &TopologyBridge::graphReady, this, &TopologyWidget::onGraphReady);
}

void TopologyWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

#if HAVE_WEBENGINE
    m_webView = new QWebEngineView;
    m_channel = new QWebChannel(this);
    m_channel->registerObject("bridge", m_bridge);
    m_webView->page()->setWebChannel(m_channel);
    m_webView->setUrl(QUrl("qrc:/topology.html"));
    layout->addWidget(m_webView);
#else
    QLabel *label = new QLabel("Topology view requires Qt WebEngine");
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
#endif

    // Refresh button
    QPushButton *refreshButton = new QPushButton("Refresh Topology");
    refreshButton->setMaximumWidth(200);
    connect(refreshButton, &QPushButton::clicked, this, &TopologyWidget::refreshFromManager);
    layout->addWidget(refreshButton);
}

void TopologyWidget::onGraphReady()
{
    qDebug() << "[Topology] Graph ready";
    m_graphReady = true;
    refreshFromManager();
}

void TopologyWidget::setDatabase(Database *db)
{
    m_database = db;
}

void TopologyWidget::loadFromDatabase()
{
    if (!m_database)
        return;

    auto allNeighbors = m_database->loadAllNeighborInfo();
    if (allNeighbors.isEmpty())
        return;

    qDebug() << "[Topology] Loading neighbor info from DB for" << allNeighbors.size() << "nodes";

    // Convert DB records to internal format
    m_neighborData.clear();
    for (auto it = allNeighbors.constBegin(); it != allNeighbors.constEnd(); ++it)
    {
        uint32_t fromNode = it.key();
        QList<NeighborEntry> entries;
        for (const auto &rec : it.value())
        {
            NeighborEntry entry;
            entry.neighborNode = rec.neighborNode;
            entry.snr = rec.snr;
            entries.append(entry);
        }
        m_neighborData[fromNode] = entries;
    }

    // Refresh graph if ready
    if (m_graphReady)
        refreshFromManager();
}

void TopologyWidget::runJavaScript(const QString &script)
{
#if HAVE_WEBENGINE
    if (m_webView && m_graphReady) {
        m_webView->page()->runJavaScript(script);
    }
#else
    Q_UNUSED(script);
#endif
}

void TopologyWidget::handleNeighborInfo(uint32_t fromNode, const QVariantMap &fields)
{
    // Parse neighbor info: fields should contain "neighbors" as a list of maps
    // Each neighbor has "nodeId" (uint32) and "snr" (float)
    QVariantList neighbors = fields.value("neighbors").toList();

    QList<NeighborEntry> entries;
    for (const QVariant &n : neighbors) {
        QVariantMap nm = n.toMap();
        NeighborEntry entry;
        entry.neighborNode = nm.value("nodeId").toUInt();
        entry.snr = nm.value("snr").toFloat();
        if (entry.neighborNode != 0) {
            entries.append(entry);
        }
    }

    qDebug() << "[Topology] Neighbor info from" << QString::number(fromNode, 16)
             << "- neighbors:" << entries.size();

    m_neighborData[fromNode] = entries;

    // Persist to database
    if (m_database)
    {
        QList<Database::NeighborRecord> dbRecords;
        for (const auto &entry : entries)
        {
            Database::NeighborRecord rec;
            rec.nodeNum = fromNode;
            rec.neighborNode = entry.neighborNode;
            rec.snr = entry.snr;
            dbRecords.append(rec);
        }
        m_database->saveNeighborInfo(fromNode, dbRecords);
    }

    // Update graph
    addNodeToGraph(fromNode);
    for (const auto &entry : entries) {
        addNodeToGraph(entry.neighborNode);

        // Check if bidirectional
        bool bidirectional = false;
        if (m_neighborData.contains(entry.neighborNode)) {
            for (const auto &reverseEntry : m_neighborData[entry.neighborNode]) {
                if (reverseEntry.neighborNode == fromNode) {
                    bidirectional = true;
                    break;
                }
            }
        }

        addLinkToGraph(fromNode, entry.neighborNode, entry.snr, bidirectional);
    }

    runJavaScript("restartSimulation();");
}

void TopologyWidget::addNodeToGraph(uint32_t nodeNum)
{
    if (!m_graphReady) return;

    NodeInfo node = m_nodeManager->getNode(nodeNum);
    QString name = node.longName.isEmpty()
                       ? QString("!%1").arg(nodeNum, 8, 16, QChar('0'))
                       : node.longName;
    QString shortName = node.shortName.isEmpty() ? "?" : node.shortName;
    bool isMyNode = (nodeNum == m_nodeManager->myNodeNum());
    int hopsAway = node.hopsAway;
    int role = node.role;

    QString js = QString(
        "addNode(%1, %2, %3, %4, %5, %6);")
        .arg(nodeNum)
        .arg(QString("'%1'").arg(name.replace("'", "\\'")))
        .arg(QString("'%1'").arg(shortName.replace("'", "\\'")))
        .arg(isMyNode ? "true" : "false")
        .arg(hopsAway)
        .arg(role);

    runJavaScript(js);
}

void TopologyWidget::addLinkToGraph(uint32_t fromNode, uint32_t toNode, float snr, bool bidirectional)
{
    if (!m_graphReady) return;

    QString js = QString(
        "addLink(%1, %2, %3, %4);")
        .arg(fromNode)
        .arg(toNode)
        .arg(snr, 0, 'f', 1)
        .arg(bidirectional ? "true" : "false");

    runJavaScript(js);
}

void TopologyWidget::refreshFromManager()
{
    if (!m_graphReady) return;

    runJavaScript("clearGraph();");

    // Add all known nodes
    uint32_t myNode = m_nodeManager->myNodeNum();
    if (myNode != 0) {
        addNodeToGraph(myNode);
    }

    // Re-add all neighbor data
    for (auto it = m_neighborData.constBegin(); it != m_neighborData.constEnd(); ++it) {
        uint32_t fromNode = it.key();
        addNodeToGraph(fromNode);

        for (const auto &entry : it.value()) {
            addNodeToGraph(entry.neighborNode);

            bool bidirectional = false;
            if (m_neighborData.contains(entry.neighborNode)) {
                for (const auto &reverseEntry : m_neighborData[entry.neighborNode]) {
                    if (reverseEntry.neighborNode == fromNode) {
                        bidirectional = true;
                        break;
                    }
                }
            }

            addLinkToGraph(fromNode, entry.neighborNode, entry.snr, bidirectional);
        }
    }

    runJavaScript("restartSimulation();");
}

void TopologyWidget::highlightPath(const QList<uint32_t> &nodeList)
{
    if (!m_graphReady || nodeList.isEmpty()) return;

    QStringList parts;
    for (uint32_t n : nodeList) {
        parts << QString::number(n);
    }

    QString js = QString("highlightPath([%1]);").arg(parts.join(","));
    runJavaScript(js);
}
