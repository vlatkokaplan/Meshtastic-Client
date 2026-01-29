#include "MapWidget.h"
#include "NodeManager.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

MapWidget::MapWidget(NodeManager *nodeManager, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager), m_mapReady(false), m_pendingLat(0), m_pendingLon(0)
{
    setupUI();

    connect(m_nodeManager, &NodeManager::nodePositionUpdated,
            this, &MapWidget::onNodePositionUpdated);
    connect(m_nodeManager, &NodeManager::nodesChanged,
            this, &MapWidget::refreshNodes);
    connect(m_nodeManager, &NodeManager::nodeUpdated,
            this, &MapWidget::onNodeUpdated);
}

void MapWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_webView = new QWebEngineView(this);

    // Set up web channel for C++ <-> JavaScript communication
    m_channel = new QWebChannel(this);
    m_bridge = new MapBridge(this);
    m_channel->registerObject(QStringLiteral("mapBridge"), m_bridge);
    m_webView->page()->setWebChannel(m_channel);

    // Connect bridge signals
    connect(m_bridge, &MapBridge::mapReady, this, &MapWidget::onMapReady);
    connect(m_bridge, &MapBridge::nodeClicked, this, &MapWidget::onBridgeNodeClicked);

    // Find and load the HTML file
    QUrl htmlUrl;

    // Check Qt resources first
    if (QFile::exists(":/map.html"))
    {
        htmlUrl = QUrl("qrc:/map.html");
    }
    else
    {
        // Try filesystem paths
        QStringList searchPaths = {
            QCoreApplication::applicationDirPath() + "/resources/map.html",
            QCoreApplication::applicationDirPath() + "/../resources/map.html",
            QCoreApplication::applicationDirPath() + "/../share/meshtastic-client/map.html",
            "resources/map.html",
            "../resources/map.html"};

        for (const QString &path : searchPaths)
        {
            if (QFile::exists(path))
            {
                htmlUrl = QUrl::fromLocalFile(QDir(path).absolutePath());
                break;
            }
        }
    }

    if (htmlUrl.isEmpty())
    {
        qWarning() << "Could not find map.html";
        m_webView->setHtml("<html><body><h2>Map unavailable</h2><p>Could not load map resources.</p></body></html>");
    }
    else
    {
        qDebug() << "Loading map from:" << htmlUrl;
        m_webView->setUrl(htmlUrl);
    }

    layout->addWidget(m_webView);
}

void MapWidget::runJavaScript(const QString &script)
{
    if (m_webView && m_webView->page())
    {
        m_webView->page()->runJavaScript(script);
    }
}

void MapWidget::centerOnLocation(double latitude, double longitude)
{
    if (!m_mapReady)
    {
        m_pendingLat = latitude;
        m_pendingLon = longitude;
        return;
    }

    QString script = QString("window.mapAPI.centerOn(%1, %2);")
                         .arg(latitude, 0, 'f', 6)
                         .arg(longitude, 0, 'f', 6);
    runJavaScript(script);
}

void MapWidget::setZoomLevel(int level)
{
    if (!m_mapReady)
        return;

    QString script = QString("window.mapAPI.setZoom(%1);").arg(level);
    runJavaScript(script);
}

void MapWidget::refreshNodes()
{
    if (!m_mapReady)
        return;

    QVariantList nodes = m_nodeManager->getNodesForMap();

    // Convert to JSON for JavaScript
    QJsonArray jsonArray;
    for (const QVariant &nodeVar : nodes)
    {
        QVariantMap nodeMap = nodeVar.toMap();
        QJsonObject obj;
        obj["nodeNum"] = static_cast<qint64>(nodeMap["nodeNum"].toUInt());
        obj["latitude"] = nodeMap["latitude"].toDouble();
        obj["longitude"] = nodeMap["longitude"].toDouble();
        obj["shortName"] = nodeMap["shortName"].toString();
        obj["longName"] = nodeMap["longName"].toString();
        obj["nodeId"] = nodeMap["nodeId"].toString();
        obj["altitude"] = nodeMap["altitude"].toInt();
        obj["batteryLevel"] = nodeMap["batteryLevel"].toInt();
        jsonArray.append(obj);
    }

    QJsonDocument doc(jsonArray);
    QString jsonStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    QString script = QString("window.mapAPI.updateNodes(%1);").arg(jsonStr);
    runJavaScript(script);
}

void MapWidget::onMapReady()
{
    qDebug() << "Map is ready";
    m_mapReady = true;

    // Apply saved tile server
    QString savedTileServer = AppSettings::instance()->mapTileServer();
    if (!savedTileServer.isEmpty() && savedTileServer != "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png")
    {
        setTileServer(savedTileServer);
    }

    refreshNodes();

    if (m_pendingLat != 0 || m_pendingLon != 0)
    {
        centerOnLocation(m_pendingLat, m_pendingLon);
    }
    else
    {
        // Auto-fit to nodes if we have any
        fitToNodes();
    }
}

void MapWidget::fitToNodes()
{
    if (!m_mapReady)
        return;

    runJavaScript("window.mapAPI.fitToNodes();");
}

void MapWidget::onNodePositionUpdated(uint32_t nodeNum, double latitude, double longitude)
{
    Q_UNUSED(nodeNum);
    Q_UNUSED(latitude);
    Q_UNUSED(longitude);

    refreshNodes();

    // Auto-fit to show all nodes after first position update
    static bool firstFit = true;
    if (firstFit && m_mapReady)
    {
        firstFit = false;
        // Small delay to let the markers be added first
        QTimer::singleShot(100, this, &MapWidget::fitToNodes);
    }
}

void MapWidget::onBridgeNodeClicked(uint32_t nodeNum)
{
    emit nodeClicked(nodeNum);
}

void MapWidget::onNodeUpdated(uint32_t nodeNum)
{
    // Blink the node on the map when it's heard (if enabled)
    if (AppSettings::instance()->mapNodeBlinkEnabled())
    {
        int durationMs = AppSettings::instance()->mapNodeBlinkDuration() * 1000;
        blinkNode(nodeNum, durationMs);
    }
}

void MapWidget::blinkNode(uint32_t nodeNum, int durationMs)
{
    if (!m_mapReady)
        return;

    QString script = QString("window.mapAPI.blinkNode(%1, %2);")
                         .arg(nodeNum)
                         .arg(durationMs);
    runJavaScript(script);
}

void MapWidget::selectNode(uint32_t nodeNum)
{
    if (!m_mapReady)
        return;

    QString script = QString("window.mapAPI.selectNode(%1);").arg(nodeNum);
    runJavaScript(script);
}

void MapWidget::setTileServer(const QString &url)
{
    if (!m_mapReady)
        return;

    // Escape the URL for JavaScript
    QString escapedUrl = url;
    escapedUrl.replace("'", "\\'");

    QString script = QString("window.mapAPI.setTileServer('%1');").arg(escapedUrl);
    runJavaScript(script);
}
