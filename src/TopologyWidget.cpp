#include "TopologyWidget.h"
#include "NodeManager.h"
#include "Database.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Database integration
// ---------------------------------------------------------------------------

void TopologyWidget::setDatabase(Database *db)
{
    m_database = db;
}

void TopologyWidget::loadFromDatabase()
{
    if (!m_database) return;

    // --- NeighborInfo records (secondary: audibility only) ---
    auto allNeighbors = m_database->loadAllNeighborInfo();
    for (auto it = allNeighbors.constBegin(); it != allNeighbors.constEnd(); ++it) {
        uint32_t fromNode = it.key();
        for (const auto &rec : it.value()) {
            if (rec.neighborNode == 0) continue;
            m_neighborDirs.insert({fromNode, rec.neighborNode});
            auto key = linkKey(fromNode, rec.neighborNode);
            bool bidir = m_neighborDirs.contains({rec.neighborNode, fromNode});
            ingestLink(fromNode, rec.neighborNode, rec.snr, true);
            if (bidir)
                m_linkData[key].bidirectional = true;
        }
    }

    // --- Traceroutes (primary: actual paths with per-hop SNR) ---
    auto traceroutes = m_database->loadTraceroutes(500, 0);
    for (const auto &tr : traceroutes) {
        // Full forward path: fromNode → routeTo[0] → ... → toNode
        QList<uint32_t> path;
        path.append(tr.fromNode);
        for (const QString &s : tr.routeTo) {
            uint32_t n = parseNodeIdStr(s);
            if (n) path.append(n);
        }
        path.append(tr.toNode);

        bool hasBothDirs = !tr.routeBack.isEmpty();

        for (int i = 0; i + 1 < path.size(); ++i) {
            float snr    = -999.0f;
            bool  hasSNR = false;
            if (i < tr.snrTo.size()) {
                bool ok;
                float v = tr.snrTo[i].toFloat(&ok);
                if (ok) { snr = v; hasSNR = true; }
            }
            ingestLink(path[i], path[i + 1], snr, hasSNR);
            if (hasBothDirs)
                m_linkData[linkKey(path[i], path[i + 1])].bidirectional = true;
        }

        // Also accumulate SNR from the return path
        if (hasBothDirs) {
            QList<uint32_t> backPath;
            backPath.append(tr.toNode);
            for (const QString &s : tr.routeBack) {
                uint32_t n = parseNodeIdStr(s);
                if (n) backPath.append(n);
            }
            backPath.append(tr.fromNode);

            for (int i = 0; i + 1 < backPath.size(); ++i) {
                float snr    = -999.0f;
                bool  hasSNR = false;
                if (i < tr.snrBack.size()) {
                    bool ok;
                    float v = tr.snrBack[i].toFloat(&ok);
                    if (ok) { snr = v; hasSNR = true; }
                }
                ingestLink(backPath[i], backPath[i + 1], snr, hasSNR);
            }
        }
    }

    if (!m_allNodes.isEmpty())
        qDebug() << "[Topology] Loaded from DB:" << m_allNodes.size()
                 << "nodes," << m_linkData.size() << "links";

    if (m_graphReady) refreshFromManager();
}

// ---------------------------------------------------------------------------
// Packet handlers
// ---------------------------------------------------------------------------

/*
 * handleTraceroute — primary data source
 *
 * startNode = original requester (packet.to for a response)
 * endNode   = responder         (packet.from for a response)
 * Full forward path: startNode → route[0] → ... → endNode
 */
void TopologyWidget::handleTraceroute(uint32_t startNode, uint32_t endNode,
                                      const QVariantMap &fields)
{
    if (startNode == 0 || endNode == 0) return;

    QVariantList route       = fields.value("route").toList();
    QVariantList snrTowards  = fields.value("snrTowards").toList();
    QVariantList routeBack   = fields.value("routeBack").toList();
    QVariantList snrBack     = fields.value("snrBack").toList();

    // Build forward path
    QList<uint32_t> fwd;
    fwd.append(startNode);
    for (const QVariant &v : route) {
        uint32_t n = parseNodeIdStr(v.toString());
        if (n) fwd.append(n);
    }
    fwd.append(endNode);

    bool hasBothDirs = !routeBack.isEmpty();

    for (int i = 0; i + 1 < fwd.size(); ++i) {
        float snr    = -999.0f;
        bool  hasSNR = false;
        if (i < snrTowards.size() && !snrTowards[i].isNull()) {
            snr   = snrTowards[i].toFloat();
            hasSNR = (snr > -100.0f);
        }
        ingestLink(fwd[i], fwd[i + 1], snr, hasSNR);
        if (hasBothDirs)
            m_linkData[linkKey(fwd[i], fwd[i + 1])].bidirectional = true;
    }

    // Accumulate return-path SNR
    if (hasBothDirs) {
        QList<uint32_t> back;
        back.append(endNode);
        for (const QVariant &v : routeBack) {
            uint32_t n = parseNodeIdStr(v.toString());
            if (n) back.append(n);
        }
        back.append(startNode);

        for (int i = 0; i + 1 < back.size(); ++i) {
            float snr    = -999.0f;
            bool  hasSNR = false;
            if (i < snrBack.size() && !snrBack[i].isNull()) {
                snr   = snrBack[i].toFloat();
                hasSNR = (snr > -100.0f);
            }
            ingestLink(back[i], back[i + 1], snr, hasSNR);
        }
    }

    qDebug() << "[Topology] Traceroute" << QString::number(startNode, 16)
             << "→" << QString::number(endNode, 16)
             << "path" << fwd.size() << "nodes";

    if (m_graphReady) {
        for (uint32_t n : std::as_const(m_allNodes))
            addNodeToGraph(n);
        for (auto it = m_linkData.constBegin(); it != m_linkData.constEnd(); ++it)
            addLinkToGraph(it.key().first, it.key().second);
        runJavaScript("restartSimulation();");
    }
}

/*
 * handleNeighborInfo — secondary data source
 *
 * Supplements the graph with audibility data from nodes that haven't been
 * part of any traceroute yet.  Also persists to the database.
 */
void TopologyWidget::handleNeighborInfo(uint32_t fromNode, const QVariantMap &fields)
{
    QVariantList neighbors = fields.value("neighbors").toList();
    if (neighbors.isEmpty()) return;

    qDebug() << "[Topology] NeighborInfo from" << QString::number(fromNode, 16)
             << "- neighbors:" << neighbors.size();

    for (const QVariant &nv : neighbors) {
        QVariantMap nm         = nv.toMap();
        uint32_t    neighborNode = nm.value("nodeId").toUInt();
        float       snr          = nm.value("snr").toFloat();
        if (neighborNode == 0) continue;

        m_neighborDirs.insert({fromNode, neighborNode});
        ingestLink(fromNode, neighborNode, snr, true);

        // Mark bidirectional if we've seen the reverse direction too
        if (m_neighborDirs.contains({neighborNode, fromNode}))
            m_linkData[linkKey(fromNode, neighborNode)].bidirectional = true;
    }

    // Persist
    if (m_database) {
        QList<Database::NeighborRecord> dbRecords;
        for (const QVariant &nv : neighbors) {
            QVariantMap nm = nv.toMap();
            uint32_t nb   = nm.value("nodeId").toUInt();
            if (nb == 0) continue;
            Database::NeighborRecord rec;
            rec.nodeNum      = fromNode;
            rec.neighborNode = nb;
            rec.snr          = nm.value("snr").toFloat();
            dbRecords.append(rec);
        }
        m_database->saveNeighborInfo(fromNode, dbRecords);
    }

    if (m_graphReady) {
        addNodeToGraph(fromNode);
        for (const QVariant &nv : neighbors) {
            uint32_t nb = nv.toMap().value("nodeId").toUInt();
            if (nb == 0) continue;
            addNodeToGraph(nb);
            addLinkToGraph(fromNode, nb);
        }
        runJavaScript("restartSimulation();");
    }
}

// ---------------------------------------------------------------------------
// Graph building
// ---------------------------------------------------------------------------

void TopologyWidget::ingestLink(uint32_t a, uint32_t b, float snr, bool hasSNR)
{
    if (a == 0 || b == 0 || a == b) return;
    m_allNodes.insert(a);
    m_allNodes.insert(b);

    auto      key = linkKey(a, b);
    LinkData &ld  = m_linkData[key];
    ld.count++;

    if (hasSNR && snr > -100.0f) {
        ld.snrSum += snr;
        ld.snrCount++;
    }
}

uint32_t TopologyWidget::parseNodeIdStr(const QString &s)
{
    if (s.startsWith('!'))
        return s.mid(1).toUInt(nullptr, 16);
    return s.toUInt(nullptr, 16);
}

void TopologyWidget::addNodeToGraph(uint32_t nodeNum)
{
    if (!m_graphReady) return;

    NodeInfo node      = m_nodeManager->getNode(nodeNum);
    QString  name      = node.longName.isEmpty()
                             ? QString("!%1").arg(nodeNum, 8, 16, QChar('0'))
                             : node.longName;
    QString  shortName = node.shortName.isEmpty() ? "?" : node.shortName;
    bool     isMyNode  = (nodeNum == m_nodeManager->myNodeNum());
    int      role      = node.role;

    // Compute avg SNR across all links touching this node
    float snrSum   = 0.0f;
    int   snrCount = 0;
    int   linkCount = 0;
    for (auto it = m_linkData.constBegin(); it != m_linkData.constEnd(); ++it) {
        if (it.key().first != nodeNum && it.key().second != nodeNum) continue;
        if (it.value().snrCount > 0) {
            snrSum += it.value().avgSnr();
            snrCount++;
        }
        linkCount += it.value().count;
    }
    float avgSnr = snrCount > 0 ? snrSum / snrCount : -999.0f;
    int   count  = qMax(1, linkCount);

    QString js = QString("addNode(%1,'%2','%3',%4,%5,%6,%7);")
        .arg(nodeNum)
        .arg(name.replace("'", "\\'"))
        .arg(shortName.replace("'", "\\'"))
        .arg(isMyNode ? "true" : "false")
        .arg(role)
        .arg(avgSnr, 0, 'f', 1)
        .arg(count);

    runJavaScript(js);
}

void TopologyWidget::addLinkToGraph(uint32_t a, uint32_t b)
{
    if (!m_graphReady) return;

    auto key = linkKey(a, b);
    if (!m_linkData.contains(key)) return;

    const LinkData &ld = m_linkData[key];

    QString js = QString("addLink(%1,%2,%3,%4,%5);")
        .arg(a)
        .arg(b)
        .arg(ld.avgSnr(), 0, 'f', 1)
        .arg(ld.bidirectional ? "true" : "false")
        .arg(ld.count);

    runJavaScript(js);
}

void TopologyWidget::refreshFromManager()
{
    if (!m_graphReady) return;

    runJavaScript("clearGraph();");

    // Make sure own node is always present
    uint32_t myNode = m_nodeManager->myNodeNum();
    if (myNode != 0) m_allNodes.insert(myNode);

    for (uint32_t n : std::as_const(m_allNodes))
        addNodeToGraph(n);

    for (auto it = m_linkData.constBegin(); it != m_linkData.constEnd(); ++it)
        addLinkToGraph(it.key().first, it.key().second);

    runJavaScript("restartSimulation();");
}

void TopologyWidget::runJavaScript(const QString &script)
{
#if HAVE_WEBENGINE
    if (m_webView && m_graphReady)
        m_webView->page()->runJavaScript(script);
#else
    Q_UNUSED(script)
#endif
}

void TopologyWidget::highlightPath(const QList<uint32_t> &nodeList)
{
    if (!m_graphReady || nodeList.isEmpty()) return;
    QStringList parts;
    for (uint32_t n : nodeList) parts << QString::number(n);
    runJavaScript(QString("highlightPath([%1]);").arg(parts.join(",")));
}
