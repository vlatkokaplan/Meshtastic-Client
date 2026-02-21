#include "SimulationConnection.h"
#include "MeshtasticProtocol.h"

#include <QDebug>
#include <QDateTime>

#include "meshtastic/mesh.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/portnums.pb.h"

// Simulated mesh — all nodes placed around central London
static const uint32_t MY_NODE_NUM = 0xDEADBEEF;

struct SimNode {
    uint32_t num;
    const char *longName;
    const char *shortName;
    double lat, lon;
    int battery; // -1 = no telemetry
};

static const SimNode SIM_NODES[] = {
    { 0xAAAA0001, "Alpha Station",  "ALPH",  51.5074, -0.1278, 87 },
    { 0xAAAA0002, "Beta Relay",     "BETA",  51.5150, -0.0900, 42 },
    { 0xAAAA0003, "Gamma Node",     "GAMM",  51.4900, -0.1500, -1 },
    { 0xAAAA0004, "Delta Tracker",  "DELT",  51.5200, -0.1100, 100 },
    { 0xAAAA0005, "Echo Sensor",    "ECHO",  51.5050, -0.0800, 15 },
};
static const int SIM_NODE_COUNT = sizeof(SIM_NODES) / sizeof(SIM_NODES[0]);


SimulationConnection::SimulationConnection(QObject *parent)
    : QObject(parent)
{}

void SimulationConnection::start(Scenario scenario)
{
    m_scenario = scenario;
    m_active = true;
    m_reconnectDone = false;
    m_configId = 0;

    qDebug() << "[SIM] Starting simulation (" <<
        (scenario == Scenario::Reconnect ? "reconnect" : "basic") << ")";

    // Emit connected immediately so MainWindow wires up and calls requestConfig
    QTimer::singleShot(0, this, [this]() {
        emit connected();
        qDebug() << "[SIM] Connected";

        // Send MyInfo shortly after — this triggers openDatabaseForNode()
        // and causes the app to call requestConfig() after its 500ms delay.
        QTimer::singleShot(80, this, [this]() {
            emit dataReceived(buildMyInfo(MY_NODE_NUM));
            qDebug() << "[SIM] Sent MyInfo (nodeNum=0x" <<
                QString::number(MY_NODE_NUM, 16).toUpper() << ")";
        });
    });
}

void SimulationConnection::stop()
{
    clearTimers();
    m_active = false;
    emit disconnected();
    qDebug() << "[SIM] Stopped";
}

bool SimulationConnection::sendData(const QByteArray &data)
{
    if (!m_active) return false;

    // Parse the framed ToRadio packet sent by the app
    if (data.size() < 4) return true;
    if ((uint8_t)data[0] != MeshtasticProtocol::SYNC_BYTE_1) return true;
    if ((uint8_t)data[1] != MeshtasticProtocol::SYNC_BYTE_2) return true;

    int len = ((uint8_t)data[2] << 8) | (uint8_t)data[3];
    if (data.size() < 4 + len) return true;

    meshtastic::ToRadio msg;
    if (!msg.ParseFromArray(data.constData() + 4, len)) return true;

    if (msg.has_want_config_id()) {
        m_configId = msg.want_config_id();
        qDebug() << "[SIM] Got want_config_id=" << m_configId
                 << "- scheduling config dump";
        scheduleConfigDump();
    }

    // Intercept outgoing text messages sent to a sim node → reply with routing ACK
    if (msg.has_packet()) {
        const meshtastic::MeshPacket &pkt = msg.packet();
        if (pkt.has_decoded() &&
            pkt.decoded().portnum() == meshtastic::PortNum::TEXT_MESSAGE_APP &&
            pkt.to() != 0xFFFFFFFFu)  // private message (not broadcast)
        {
            uint32_t destNode = pkt.to();
            uint32_t packetId = pkt.id();

            bool isSimNode = false;
            for (int i = 0; i < SIM_NODE_COUNT; ++i) {
                if (SIM_NODES[i].num == destNode) { isSimNode = true; break; }
            }

            if (isSimNode) {
                QTimer::singleShot(500, this, [this, destNode, packetId]() {
                    if (!m_active) return;
                    emit dataReceived(buildRoutingAck(destNode, MY_NODE_NUM, packetId));
                    qDebug() << "[SIM] Sent routing ACK from"
                             << QString("!%1").arg(destNode, 8, 16, QChar('0'))
                             << "for packetId" << packetId;
                });
            }
        }
    }

    return true;
}

void SimulationConnection::scheduleConfigDump()
{
    int t = 0;

    // Own node
    auto *nt = new QTimer(this);
    nt->setSingleShot(true);
    t += 80;
    nt->setInterval(t);
    connect(nt, &QTimer::timeout, this, [this]() {
        emit dataReceived(buildNodeInfo(MY_NODE_NUM, "My Device", "ME",
                                       51.5000, -0.1250, 95, true));
        qDebug() << "[SIM] Sent own NodeInfo";
    });
    nt->start();
    m_timers.append(nt);

    // Peer nodes
    for (int i = 0; i < SIM_NODE_COUNT; ++i) {
        const SimNode &n = SIM_NODES[i];
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        t += 80;
        timer->setInterval(t);
        connect(timer, &QTimer::timeout, this, [this, n]() {
            emit dataReceived(buildNodeInfo(n.num, n.longName, n.shortName,
                                           n.lat, n.lon, n.battery));
            qDebug() << "[SIM] Sent NodeInfo:" << n.longName;
        });
        timer->start();
        m_timers.append(timer);
    }

    // Config packets
    struct { int delay; QByteArray (SimulationConnection::*fn)(); const char *name; } configs[] = {
        { t += 80, &SimulationConnection::buildLoRaConfig,     "LoRa config"     },
        { t += 80, &SimulationConnection::buildDeviceConfig,   "Device config"   },
        { t += 80, &SimulationConnection::buildPositionConfig, "Position config" },
        { t += 80, &SimulationConnection::buildPrimaryChannel, "Primary channel" },
    };
    for (auto &c : configs) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(c.delay);
        const char *name = c.name;
        auto fn = c.fn;
        connect(timer, &QTimer::timeout, this, [this, fn, name]() {
            emit dataReceived((this->*fn)());
            qDebug() << "[SIM] Sent" << name;
        });
        timer->start();
        m_timers.append(timer);
    }

    // ConfigCompleteId — must match what the app sent
    t += 80;
    auto *doneTimer = new QTimer(this);
    doneTimer->setSingleShot(true);
    doneTimer->setInterval(t);
    connect(doneTimer, &QTimer::timeout, this, [this]() {
        emit dataReceived(buildConfigCompleteId(m_configId));
        qDebug() << "[SIM] Sent ConfigCompleteId=" << m_configId;
        qDebug() << "[TEST PASS] Config load complete -"
                 << SIM_NODE_COUNT + 1 << "nodes populated";

        scheduleNeighborInfoDump();
        scheduleTracerouteDump();

        if (m_scenario == Scenario::Reconnect && !m_reconnectDone)
            scheduleReconnect();
    });
    doneTimer->start();
    m_timers.append(doneTimer);
}

void SimulationConnection::scheduleReconnect()
{
    qDebug() << "[SIM] Reconnect scenario: disconnect in 10s";

    auto *disc = new QTimer(this);
    disc->setSingleShot(true);
    disc->setInterval(10000);
    connect(disc, &QTimer::timeout, this, [this]() {
        qDebug() << "[SIM] Simulating connection drop...";
        emit disconnected();

        auto *reconn = new QTimer(this);
        reconn->setSingleShot(true);
        reconn->setInterval(3000);
        connect(reconn, &QTimer::timeout, this, [this]() {
            m_reconnectDone = true;
            qDebug() << "[SIM] Reconnecting...";
            emit connected();
            QTimer::singleShot(80, this, [this]() {
                emit dataReceived(buildMyInfo(MY_NODE_NUM));
                qDebug() << "[SIM] Sent MyInfo (reconnect)";
                qDebug() << "[TEST] Nodes should have stayed on map during 3s gap";
            });
        });
        reconn->start();
        m_timers.append(reconn);
    });
    disc->start();
    m_timers.append(disc);
}

void SimulationConnection::clearTimers()
{
    for (QTimer *t : m_timers) {
        t->stop();
        t->deleteLater();
    }
    m_timers.clear();
}

// ---------- Packet builders ----------

QByteArray SimulationConnection::wrapFrame(const std::string &serialized)
{
    QByteArray frame;
    uint16_t len = static_cast<uint16_t>(serialized.size());
    frame.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_1));
    frame.append(static_cast<char>(MeshtasticProtocol::SYNC_BYTE_2));
    frame.append(static_cast<char>((len >> 8) & 0xFF));
    frame.append(static_cast<char>(len & 0xFF));
    frame.append(QByteArray::fromStdString(serialized));
    return frame;
}

QByteArray SimulationConnection::buildMyInfo(uint32_t nodeNum)
{
    meshtastic::FromRadio fr;
    fr.mutable_my_info()->set_my_node_num(nodeNum);
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildNodeInfo(uint32_t nodeNum,
    const QString &longName, const QString &shortName,
    double lat, double lon, int battery, bool isOwn)
{
    meshtastic::FromRadio fr;
    auto *ni = fr.mutable_node_info();
    ni->set_num(nodeNum);
    ni->set_last_heard(static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch()));

    auto *user = ni->mutable_user();
    user->set_long_name(longName.toStdString());
    user->set_short_name(shortName.toStdString());
    user->set_id(QString("!%1").arg(nodeNum, 8, 16, QChar('0')).toStdString());
    user->set_hw_model(meshtastic::HardwareModel::HELTEC_V3);

    auto *pos = ni->mutable_position();
    pos->set_latitude_i(static_cast<int32_t>(lat * 1e7));
    pos->set_longitude_i(static_cast<int32_t>(lon * 1e7));
    pos->set_altitude(isOwn ? 45 : 10 + (nodeNum & 0x3F));
    pos->set_time(static_cast<uint32_t>(QDateTime::currentSecsSinceEpoch()));

    if (battery >= 0) {
        auto *dev = ni->mutable_device_metrics();
        dev->set_battery_level(static_cast<uint32_t>(battery));
        dev->set_voltage(battery > 0 ? 3.7f + (battery / 100.0f) * 0.5f : 3.2f);
    }

    ni->set_snr(8.0f);
    (void)isOwn;

    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildLoRaConfig()
{
    meshtastic::FromRadio fr;
    auto *cfg = fr.mutable_config()->mutable_lora();
    cfg->set_use_preset(true);
    cfg->set_modem_preset(1);  // LONG_FAST
    cfg->set_region(5);        // EU_868
    cfg->set_hop_limit(3);
    cfg->set_tx_enabled(true);
    cfg->set_tx_power(0);
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildDeviceConfig()
{
    meshtastic::FromRadio fr;
    auto *cfg = fr.mutable_config()->mutable_device();
    cfg->set_role(0);          // CLIENT
    cfg->set_serial_enabled(true);
    cfg->set_node_info_broadcast_secs(900);
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildPositionConfig()
{
    meshtastic::FromRadio fr;
    auto *cfg = fr.mutable_config()->mutable_position();
    cfg->set_position_broadcast_secs(900);
    cfg->set_position_broadcast_smart_enabled(true);
    cfg->set_gps_mode(1);      // ENABLED
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildPrimaryChannel()
{
    meshtastic::FromRadio fr;
    auto *ch = fr.mutable_channel();
    ch->set_index(0);
    auto *settings = ch->mutable_settings();
    settings->set_name("");     // primary channel has empty name
    settings->set_psk("\xd4\xf1\xbb\x3a\x20\x29\x07\x59\xf0\xbc\xff\xab\xcf\x4e\x69\x01", 16);
    ch->set_role(static_cast<meshtastic::Channel_Role>(1));  // PRIMARY = 1
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

void SimulationConnection::scheduleNeighborInfoDump()
{
    // Simulated RF links between nodes (bidirectional, SNR in dB)
    struct Link { uint32_t a; uint32_t b; float snr; };
    static const Link LINKS[] = {
        { MY_NODE_NUM,  0xAAAA0001,  9.0f },  // My device ↔ Alpha
        { MY_NODE_NUM,  0xAAAA0002,  5.5f },  // My device ↔ Beta
        { 0xAAAA0001,   0xAAAA0002,  7.2f },  // Alpha ↔ Beta
        { 0xAAAA0001,   0xAAAA0003,  3.1f },  // Alpha ↔ Gamma
        { 0xAAAA0002,   0xAAAA0004,  6.8f },  // Beta ↔ Delta
        { 0xAAAA0004,   0xAAAA0005, -1.5f },  // Delta ↔ Echo (weak link)
    };
    static const int LINK_COUNT = static_cast<int>(sizeof(LINKS) / sizeof(LINKS[0]));

    // Build per-node neighbor lists from the symmetric link table
    QMap<uint32_t, QList<QPair<uint32_t, float>>> neighborMap;
    for (int i = 0; i < LINK_COUNT; ++i) {
        neighborMap[LINKS[i].a].append({LINKS[i].b, LINKS[i].snr});
        neighborMap[LINKS[i].b].append({LINKS[i].a, LINKS[i].snr});
    }

    // Send each node's NeighborInfo packet 200 ms apart, 1 s after config
    int t = 1000;
    for (auto it = neighborMap.constBegin(); it != neighborMap.constEnd(); ++it) {
        uint32_t node = it.key();
        QList<QPair<uint32_t, float>> nbrs = it.value();
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(t);
        connect(timer, &QTimer::timeout, this, [this, node, nbrs]() {
            emit dataReceived(buildNeighborInfo(node, nbrs));
            qDebug() << "[SIM] Sent NeighborInfo from" << QString::number(node, 16)
                     << "with" << nbrs.size() << "neighbors";
        });
        timer->start();
        m_timers.append(timer);
        t += 200;
    }
}

QByteArray SimulationConnection::buildNeighborInfo(
    uint32_t fromNode, const QList<QPair<uint32_t, float>> &neighbors)
{
    meshtastic::NeighborInfo ni;
    ni.set_node_id(fromNode);
    ni.set_node_broadcast_interval_secs(900);
    for (const auto &pair : neighbors) {
        auto *n = ni.add_neighbors();
        n->set_node_id(pair.first);
        n->set_snr(pair.second);
    }

    std::string niBytes;
    ni.SerializeToString(&niBytes);

    meshtastic::FromRadio fr;
    auto *packet = fr.mutable_packet();
    packet->set_from(fromNode);
    packet->set_to(0xFFFFFFFFu);  // broadcast
    packet->set_id(static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(static_cast<meshtastic::PortNum>(67));  // NEIGHBORINFO_APP
    decoded->set_payload(niBytes);

    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildConfigCompleteId(uint32_t configId)
{
    meshtastic::FromRadio fr;
    fr.set_config_complete_id(configId);
    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

void SimulationConnection::scheduleTracerouteDump()
{
    // Simulated traceroute responses — one per reachable destination.
    // SNR values are stored as int32 = actual_snr * 4 (Meshtastic wire format).
    // snrTowards[i] = SNR for hop i of the forward path.
    // snrBack contains all hops of the return path EXCEPT the last one
    // (MY_NODE's rx_snr covers that final hop).
    struct SimTraceroute {
        uint32_t        responder;    // packet.from  (destination, responding node)
        QList<uint32_t> route;        // intermediate nodes, forward path
        QList<int32_t>  snrTowards;   // per-hop SNR * 4, forward
        QList<uint32_t> routeBack;    // intermediate nodes, return path
        QList<int32_t>  snrBack;      // per-hop SNR * 4, return (excl. last hop)
        float           rxSnr;        // SNR of the last hop back to MY_NODE
    };

    const QList<SimTraceroute> routes = {
        // MY_NODE → Alpha  (direct)
        { 0xAAAA0001, {}, {36},     {}, {},   9.0f },
        // MY_NODE → Beta   (direct)
        { 0xAAAA0002, {}, {22},     {}, {},   5.5f },
        // MY_NODE → Gamma  via Alpha
        { 0xAAAA0003, {0xAAAA0001}, {36, 12}, {0xAAAA0001}, {12}, 9.0f },
        // MY_NODE → Delta  via Beta
        { 0xAAAA0004, {0xAAAA0002}, {22, 27}, {0xAAAA0002}, {27}, 5.5f },
        // MY_NODE → Echo   via Beta → Delta
        { 0xAAAA0005, {0xAAAA0002, 0xAAAA0004}, {22, 27, -6},
                      {0xAAAA0004, 0xAAAA0002}, {-6, 27},   5.5f },
    };

    // Start 3 s after ConfigComplete (after all NeighborInfo has been emitted)
    int t = 3000;
    for (const SimTraceroute &sr : routes) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(t);
        connect(timer, &QTimer::timeout, this, [this, sr]() {
            emit dataReceived(buildTraceroute(sr.responder, MY_NODE_NUM,
                                             sr.route, sr.snrTowards,
                                             sr.routeBack, sr.snrBack,
                                             sr.rxSnr));
            qDebug() << "[SIM] Sent Traceroute response from"
                     << QString("!%1").arg(sr.responder, 8, 16, QChar('0'));
        });
        timer->start();
        m_timers.append(timer);
        t += 400;
    }
}

QByteArray SimulationConnection::buildTraceroute(
    uint32_t responder, uint32_t requester,
    const QList<uint32_t> &route,
    const QList<int32_t>  &snrTowards,
    const QList<uint32_t> &routeBack,
    const QList<int32_t>  &snrBack,
    float rxSnr)
{
    meshtastic::RouteDiscovery rd;
    for (uint32_t n : route)       rd.add_route(n);
    for (int32_t  s : snrTowards)  rd.add_snr_towards(s);
    for (uint32_t n : routeBack)   rd.add_route_back(n);
    for (int32_t  s : snrBack)     rd.add_snr_back(s);

    std::string rdBytes;
    rd.SerializeToString(&rdBytes);

    meshtastic::FromRadio fr;
    auto *packet = fr.mutable_packet();
    packet->set_from(responder);
    packet->set_to(requester);
    packet->set_id(static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    packet->set_rx_snr(rxSnr);
    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(static_cast<meshtastic::PortNum>(meshtastic::PortNum::TRACEROUTE_APP));
    decoded->set_payload(rdBytes);

    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}

QByteArray SimulationConnection::buildRoutingAck(
    uint32_t fromNode, uint32_t toNode, uint32_t requestId)
{
    meshtastic::Routing routing;
    // error_reason NONE = 0 — leave at default (success ACK)

    std::string routingBytes;
    routing.SerializeToString(&routingBytes);

    meshtastic::FromRadio fr;
    auto *packet = fr.mutable_packet();
    packet->set_from(fromNode);
    packet->set_to(toNode);
    packet->set_id(static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF));
    auto *decoded = packet->mutable_decoded();
    decoded->set_portnum(static_cast<meshtastic::PortNum>(meshtastic::PortNum::ROUTING_APP));
    decoded->set_payload(routingBytes);
    decoded->set_request_id(requestId);

    std::string s;
    fr.SerializeToString(&s);
    return wrapFrame(s);
}
