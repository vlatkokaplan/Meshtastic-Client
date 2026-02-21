#ifndef TOPOLOGYWIDGET_H
#define TOPOLOGYWIDGET_H

#include <QWidget>
#include <QVariantMap>
#include <QMap>
#include <QSet>

class Database;

#if HAVE_WEBENGINE
#include <QWebEngineView>
#include <QWebChannel>
#endif

class NodeManager;

class TopologyBridge : public QObject
{
    Q_OBJECT
public:
    explicit TopologyBridge(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void notifyReady() { emit graphReady(); }

signals:
    void graphReady();
};

class TopologyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TopologyWidget(NodeManager *nodeManager, QWidget *parent = nullptr);

    // Primary source: traceroute responses (per-hop SNR, confirmed bidirectional)
    void handleTraceroute(uint32_t startNode, uint32_t endNode, const QVariantMap &fields);

    // Secondary source: NeighborInfo broadcasts (audibility, no path confirmation)
    void handleNeighborInfo(uint32_t fromNode, const QVariantMap &fields);

    void setDatabase(Database *db);
    void loadFromDatabase();
    void refreshFromManager();
    void highlightPath(const QList<uint32_t> &nodeList);

private slots:
    void onGraphReady();

private:
    void setupUI();
    void runJavaScript(const QString &script);
    void addNodeToGraph(uint32_t nodeNum);
    void addLinkToGraph(uint32_t a, uint32_t b);

    // Unified link store — key is always {min(a,b), max(a,b)}
    struct LinkData {
        float snrSum    = 0.0f;
        int   snrCount  = 0;
        int   count     = 0;      // times this link was observed in any path
        bool  bidirectional = false;

        float avgSnr() const {
            return snrCount > 0 ? snrSum / snrCount : -999.0f;
        }
    };

    static QPair<uint32_t,uint32_t> linkKey(uint32_t a, uint32_t b) {
        return a < b ? qMakePair(a, b) : qMakePair(b, a);
    }
    static uint32_t parseNodeIdStr(const QString &s);

    // Ingest one directed observation of a link (a→b at given snr).
    // Uses symmetric key so both directions accumulate into one record.
    void ingestLink(uint32_t a, uint32_t b, float snr, bool hasSNR = true);

    QMap<QPair<uint32_t,uint32_t>, LinkData> m_linkData;
    QSet<uint32_t>                           m_allNodes;

    // Track NeighborInfo directions for bidirectional detection:
    // contains {A,B} if A's NeighborInfo listed B
    QSet<QPair<uint32_t,uint32_t>>           m_neighborDirs;

    NodeManager *m_nodeManager;
    Database    *m_database  = nullptr;
    bool         m_graphReady;

#if HAVE_WEBENGINE
    QWebEngineView *m_webView;
    QWebChannel    *m_channel;
#endif
    TopologyBridge *m_bridge;
};

#endif // TOPOLOGYWIDGET_H
