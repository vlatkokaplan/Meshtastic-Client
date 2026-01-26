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

class SerialConnection;
class NodeManager;
class PacketListWidget;
class Database;
class MessagesWidget;
class ConfigWidget;

class MapWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void refreshPorts();
    void connectToSelected();
    void disconnect();
    void onConnected();
    void onDisconnected();
    void onDataReceived(const QByteArray &data);
    void onPacketReceived(const MeshtasticProtocol::DecodedPacket &packet);
    void onSerialError(const QString &error);
    void onConfigCompleteIdReceived(uint32_t configId);

    // Config loading state
    uint32_t m_expectedConfigId = 0;
    QTimer *m_configHeartbeatTimer = nullptr;
    
    // UI Setup
    void onNodeSelected(QTableWidgetItem *item);
    void onNodeContextMenu(const QPoint &pos);
    void requestConfig();
    void requestTraceroute(uint32_t nodeNum);
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

private:
    // Core components
    SerialConnection *m_serial;
    MeshtasticProtocol *m_protocol;
    NodeManager *m_nodeManager;
    Database *m_database;

    // UI components
    QTabWidget *m_tabWidget;
    QComboBox *m_portCombo;
    QPushButton *m_connectButton;
    QPushButton *m_disconnectButton;
    QPushButton *m_refreshButton;
    QLabel *m_statusLabel;
    PacketListWidget *m_packetList;
    QTableWidget *m_nodeTable;
    QLineEdit *m_nodeSearchEdit;
    MessagesWidget *m_messagesWidget;
    ConfigWidget *m_configWidget;

    MapWidget *m_mapWidget;
    QSystemTrayIcon *m_trayIcon;

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
};

#endif // MAINWINDOW_H
