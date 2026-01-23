#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QSplitter>

#include "MeshtasticProtocol.h"  // Need full include for nested type

class SerialConnection;
class NodeManager;
class PacketListWidget;
class Database;

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
    void onNodeSelected(QListWidgetItem *item);
    void requestConfig();

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
    QListWidget *m_nodeList;

    MapWidget *m_mapWidget;

    void setupUI();
    void setupToolbar();
    void setupMapTab();
    void setupPacketTab();
    void updateNodeList();
    void updateStatusLabel();
    void openDatabaseForNode(uint32_t nodeNum);
    void closeDatabase();
};

#endif // MAINWINDOW_H
