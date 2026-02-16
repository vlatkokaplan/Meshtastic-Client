#ifndef TOPOLOGYWIDGET_H
#define TOPOLOGYWIDGET_H

#include <QWidget>
#include <QVariantMap>
#include <QMap>

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

    void handleNeighborInfo(uint32_t fromNode, const QVariantMap &fields);
    void refreshFromManager();
    void highlightPath(const QList<uint32_t> &nodeList);

private slots:
    void onGraphReady();

private:
    void setupUI();
    void runJavaScript(const QString &script);
    void addNodeToGraph(uint32_t nodeNum);
    void addLinkToGraph(uint32_t fromNode, uint32_t toNode, float snr, bool bidirectional);

    NodeManager *m_nodeManager;
    bool m_graphReady;

#if HAVE_WEBENGINE
    QWebEngineView *m_webView;
    QWebChannel *m_channel;
#endif
    TopologyBridge *m_bridge;

    // Store neighbor data for rebuilding graph
    struct NeighborEntry {
        uint32_t neighborNode;
        float snr;
    };
    QMap<uint32_t, QList<NeighborEntry>> m_neighborData;
};

#endif // TOPOLOGYWIDGET_H
