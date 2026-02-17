#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QSplitter>
#include <QHeaderView>
#include <QSystemTrayIcon>
#include <QLineEdit>

#include "MeshtasticProtocol.h" // Need full include for nested type
#include "NodeManager.h"        // For NodeInfo
#include <QBluetoothDeviceInfo>

class QTimer;
class SerialConnection;
class TcpConnection;
class BluetoothConnection;
class NodeManager;
class PacketListWidget;
class Database;
class MessagesWidget;
class ConfigWidget;
class TracerouteWidget;
class SignalScannerWidget;
class TelemetryGraphWidget;

class MapWidget;
class DashboardStatsWidget;
class TopologyWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool experimentalMode = false, bool testMode = false, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void disconnect();
    void rebootDevice();
    void onConnected();
    void onDisconnected();
    void onDataReceived(const QByteArray &data);
    void onPacketReceived(const MeshtasticProtocol::DecodedPacket &packet);
    void onSerialError(const QString &error);
    void onConfigCompleteIdReceived(uint32_t configId);

    // UI Setup
    void onNodeSelected(QTableWidgetItem *item);
    void onNodeContextMenu(const QPoint &pos);
    void requestConfig();
    void requestTraceroute(uint32_t nodeNum);
    void onTracerouteCooldownTick();
    void requestNodeInfo(uint32_t nodeNum);
    void requestTelemetry(uint32_t nodeNum);
    void requestPosition(uint32_t nodeNum);
    void onSendMessage(const QString &text, uint32_t toNode, int channel);
    void onSendReaction(const QString &emoji, uint32_t toNode, int channel, uint32_t replyId);
    void onSettingChanged(const QString &key, const QVariant &value);

    // Config save handlers
    void onSaveLoRaConfig();
    void onSaveDeviceConfig();
    void onSavePositionConfig();
    void onSaveChannelConfig(int channelIndex);

    // Export handlers
    void onExportNodes(const QString &format);
    void onExportMessages(const QString &format);

    // Test features
    void drawTestNodeLines();

    // Navigation
    void navigateToNode(uint32_t nodeNum);

    // Traceroute visualization
    void onTracerouteSelected(uint32_t fromNode, uint32_t toNode);

private:
    // Config loading state
    uint32_t m_expectedConfigId = 0;
    QTimer *m_configHeartbeatTimer = nullptr;
    QTimer *m_connectionHeartbeatTimer = nullptr;  // Persistent heartbeat for long sessions

    // Traceroute cooldown state
    QTimer *m_tracerouteCooldownTimer = nullptr;
    QLabel *m_tracerouteCooldownLabel = nullptr;
    int m_tracerouteCooldownRemaining = 0;
    static const int TRACEROUTE_COOLDOWN_MS = 30000; // 30 seconds

    // Experimental features
    bool m_experimentalMode = false;
    bool m_testMode = false;

    // Node list caching
    QList<NodeInfo> m_sortedNodes;
    bool m_nodesSortNeeded = true;

    // Core components
    SerialConnection *m_serial;
    TcpConnection *m_tcp;
    BluetoothConnection *m_bluetooth;
    MeshtasticProtocol *m_protocol;
    NodeManager *m_nodeManager;
    Database *m_database;

    // UI components
    QTabWidget *m_tabWidget;
    QPushButton *m_connectButton;
    QPushButton *m_disconnectButton;
    QPushButton *m_rebootButton;
    QLabel *m_statusLabel;
    PacketListWidget *m_packetList;
    QTableWidget *m_nodeTable;
    QLineEdit *m_nodeSearchEdit;
    MessagesWidget *m_messagesWidget;
    ConfigWidget *m_configWidget;
    TracerouteWidget *m_tracerouteWidget;
    SignalScannerWidget *m_signalScannerWidget;
    TelemetryGraphWidget *m_telemetryGraphWidget;

    MapWidget *m_mapWidget;
    DashboardStatsWidget *m_dashboardStats;
    TopologyWidget *m_topologyWidget;
    QSplitter *m_mapSplitter;
    QString m_firmwareVersion;
    QSystemTrayIcon *m_trayIcon;
    int m_messagesTabIndex = -1;

    bool isDeviceConnected() const;
    bool sendToDevice(const QByteArray &data);
    QString connectedDeviceName() const;

    void setupUI();
    void showNotification(const QString &title, const QString &message);
    void showTracerouteResult(const MeshtasticProtocol::DecodedPacket &packet);
    void setupToolbar();
    void setupMapTab();
    void setupMessagesTab();
    void setupPacketTab();
    void setupConfigTab();
    void updateNodeList();
    void updateStatusLabel();
    void openDatabaseForNode(uint32_t nodeNum);
    void closeDatabase();
    void saveWindowState();
    void restoreWindowState();

    // Connection dialog
    void showConnectionDialog();
    void connectSerial(const QString &port);
    void connectTcp(const QString &host, quint16 port);
    void connectBluetooth(const QBluetoothDeviceInfo &device);

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // MAINWINDOW_H
