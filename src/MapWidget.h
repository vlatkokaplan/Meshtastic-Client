#ifndef MAPWIDGET_H
#define MAPWIDGET_H

#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>

class NodeManager;

class MapBridge : public QObject
{
    Q_OBJECT

public:
    explicit MapBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Methods callable from JavaScript
    Q_INVOKABLE void notifyMapReady() { emit mapReady(); }
    Q_INVOKABLE void notifyNodeClicked(uint32_t nodeNum) { emit nodeClicked(nodeNum); }

signals:
    void nodeClicked(uint32_t nodeNum);
    void mapReady();
};

class MapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapWidget(NodeManager *nodeManager, QWidget *parent = nullptr);

    void centerOnLocation(double latitude, double longitude);
    void setZoomLevel(int level);
    void refreshNodes();
    void fitToNodes();
    void blinkNode(uint32_t nodeNum, int durationMs = 5000);
    void selectNode(uint32_t nodeNum);
    void setTileServer(const QString &url);
    void drawPacketFlow(uint32_t fromNode, uint32_t toNode, double fromLat, double fromLon, double toLat, double toLon);

    // Traceroute visualization
    struct RoutePoint {
        double lat;
        double lon;
        QString name;
        float snr;
    };
    void drawTraceroute(const QList<RoutePoint> &routePoints);
    void clearTraceroute();

signals:
    void nodeClicked(uint32_t nodeNum);

private slots:
    void onNodePositionUpdated(uint32_t nodeNum, double latitude, double longitude);
    void onNodeUpdated(uint32_t nodeNum);
    void onMapReady();
    void onBridgeNodeClicked(uint32_t nodeNum);

private:
    QWebEngineView *m_webView;
    QWebChannel *m_channel;
    MapBridge *m_bridge;
    NodeManager *m_nodeManager;
    bool m_mapReady;
    double m_pendingLat;
    double m_pendingLon;

    void setupUI();
    void runJavaScript(const QString &script);
};

#endif // MAPWIDGET_H
