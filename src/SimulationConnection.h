#ifndef SIMULATIONCONNECTION_H
#define SIMULATIONCONNECTION_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QTimer>

// Generates real Meshtastic FromRadio protobuf frames to exercise the full UI
// without a physical device.
//
// Usage:
//   --simulate          basic scenario (connect + populate nodes + config)
//   --simulate reconnect  same + simulates a TCP drop/reconnect after 15s
//
// Intercepts outgoing sendData() calls to extract want_config_id so the
// ConfigCompleteId response matches what the app requested.

class SimulationConnection : public QObject
{
    Q_OBJECT

public:
    enum class Scenario { Basic, Reconnect };

    explicit SimulationConnection(QObject *parent = nullptr);

    void start(Scenario scenario = Scenario::Basic);
    void stop();
    bool isActive() const { return m_active; }

    // Called by MainWindow::sendToDevice() in simulation mode.
    // Inspects outgoing ToRadio packets and schedules appropriate responses.
    bool sendData(const QByteArray &data);

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);

private:
    bool m_active = false;
    Scenario m_scenario = Scenario::Basic;
    uint32_t m_configId = 0;
    bool m_reconnectDone = false;
    QList<QTimer *> m_timers;

    // Packet builders — return a framed, serialized FromRadio
    QByteArray buildMyInfo(uint32_t nodeNum);
    QByteArray buildNodeInfo(uint32_t nodeNum, const QString &longName,
                             const QString &shortName, double lat, double lon,
                             int battery = -1, bool isOwn = false);
    QByteArray buildLoRaConfig();
    QByteArray buildDeviceConfig();
    QByteArray buildPositionConfig();
    QByteArray buildPrimaryChannel();
    QByteArray buildConfigCompleteId(uint32_t configId);
    QByteArray buildNeighborInfo(uint32_t fromNode,
                                 const QList<QPair<uint32_t, float>> &neighbors);
    QByteArray buildTraceroute(uint32_t responder, uint32_t requester,
                               const QList<uint32_t> &route,
                               const QList<int32_t> &snrTowards,
                               const QList<uint32_t> &routeBack,
                               const QList<int32_t> &snrBack,
                               float rxSnr);

    static QByteArray wrapFrame(const std::string &serialized);

    void scheduleConfigDump();
    void scheduleNeighborInfoDump();
    void scheduleTracerouteDump();
    void scheduleReconnect();
    void clearTimers();
};

#endif // SIMULATIONCONNECTION_H
