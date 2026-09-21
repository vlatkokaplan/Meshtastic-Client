#include "MeshAnalytics.h"

#include "Database.h"
#include "MeshtasticProtocol.h"
#include "NodeManager.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <functional>

namespace
{

// packets.timestamp is stored in milliseconds; every other table stores
// seconds. Getting this wrong silently yields an empty result rather than an
// error, so the two conversions are named rather than inlined.
qint64 toPacketStamp(const QDateTime &t) { return t.toMSecsSinceEpoch(); }
qint64 toRowStamp(const QDateTime &t) { return t.toSecsSinceEpoch(); }

} // namespace

MeshAnalytics::MeshAnalytics(Database *db, NodeManager *nodes)
    : m_db(db), m_nodes(nodes)
{
}

QString MeshAnalytics::nodeLabel(uint32_t nodeNum) const
{
    if (nodeNum == 0)
        return QStringLiteral("?");

    if (m_nodes && m_nodes->hasNode(nodeNum))
    {
        NodeInfo n = m_nodes->getNode(nodeNum);
        if (!n.longName.isEmpty())
            return n.longName;
        if (!n.shortName.isEmpty())
            return n.shortName;
    }
    return MeshtasticProtocol::nodeIdToString(nodeNum);
}

// ---------------------------------------------------------------- 16 --------

MeshAnalytics::DecodeStats MeshAnalytics::decodeStats(const QDateTime &since) const
{
    DecodeStats stats;
    if (!m_db || !m_db->isOpen())
        return stats;

    QSqlQuery q(m_db->connection());
    q.prepare("SELECT port_num, channel, COUNT(*) FROM packets "
              "WHERE timestamp >= ? GROUP BY port_num, channel");
    q.addBindValue(toPacketStamp(since));

    if (!q.exec())
    {
        qWarning() << "[Analytics] decodeStats failed:" << q.lastError().text();
        return stats;
    }

    QMap<int, int> decodedByPort;
    while (q.next())
    {
        const int port = q.value(0).toInt();
        const int channel = q.value(1).toInt();
        const int count = q.value(2).toInt();

        stats.total += count;
        // portnum 0 is UNKNOWN_APP: the payload never yielded a usable port,
        // which in practice means it was never decrypted.
        if (port == 0)
        {
            stats.undecoded += count;
            stats.undecodedByChannel[channel] += count;
        }
        else
        {
            stats.decoded += count;
            decodedByPort[port] += count;
        }
    }

    for (auto it = decodedByPort.constBegin(); it != decodedByPort.constEnd(); ++it)
    {
        PortCount pc;
        pc.portNum = it.key();
        pc.name = MeshtasticProtocol::portNumToString(
            static_cast<MeshtasticProtocol::PortNum>(it.key()));
        pc.count = it.value();
        stats.byPort.append(pc);
    }
    std::sort(stats.byPort.begin(), stats.byPort.end(),
              [](const PortCount &a, const PortCount &b) { return a.count > b.count; });

    return stats;
}

// ---------------------------------------------------------------- 17 --------

QVector<MeshAnalytics::AirtimeSample> MeshAnalytics::ownAirtime(uint32_t myNodeNum,
                                                               const QDateTime &since) const
{
    QVector<AirtimeSample> out;
    if (!m_db || !m_db->isOpen() || myNodeNum == 0)
        return out;

    QSqlQuery q(m_db->connection());
    q.prepare("SELECT timestamp, channel_util, air_util_tx FROM telemetry_history "
              "WHERE node_num = ? AND timestamp >= ? ORDER BY timestamp ASC");
    q.addBindValue(myNodeNum);
    q.addBindValue(toRowStamp(since));

    if (!q.exec())
    {
        qWarning() << "[Analytics] ownAirtime failed:" << q.lastError().text();
        return out;
    }

    while (q.next())
    {
        AirtimeSample s;
        s.when = QDateTime::fromSecsSinceEpoch(q.value(0).toLongLong());
        s.channelUtil = q.value(1).toDouble();
        s.airUtilTx = q.value(2).toDouble();
        out.append(s);
    }
    return out;
}

QList<MeshAnalytics::AirtimeNode> MeshAnalytics::airtimeByNode(const QDateTime &since) const
{
    QList<AirtimeNode> out;
    if (!m_db || !m_db->isOpen())
        return out;

    QSqlQuery q(m_db->connection());
    q.prepare("SELECT node_num, MAX(air_util_tx), AVG(air_util_tx), COUNT(*) "
              "FROM telemetry_history WHERE timestamp >= ? AND air_util_tx > 0 "
              "GROUP BY node_num");
    q.addBindValue(toRowStamp(since));

    if (!q.exec())
    {
        qWarning() << "[Analytics] airtimeByNode failed:" << q.lastError().text();
        return out;
    }

    while (q.next())
    {
        AirtimeNode a;
        a.nodeNum = q.value(0).toUInt();
        a.peakAirUtilTx = q.value(1).toDouble();
        a.avgAirUtilTx = q.value(2).toDouble();
        a.samples = q.value(3).toInt();
        a.name = nodeLabel(a.nodeNum);
        out.append(a);
    }

    std::sort(out.begin(), out.end(), [](const AirtimeNode &a, const AirtimeNode &b) {
        return a.peakAirUtilTx > b.peakAirUtilTx;
    });
    return out;
}

// ---------------------------------------------------------------- 18 --------

QSet<uint32_t> MeshAnalytics::findArticulationPoints(
    const QMap<uint32_t, QSet<uint32_t>> &adjacency)
{
    // Hopcroft-Tarjan. Iterative depth tracking via explicit recursion is fine
    // here: a mesh is hundreds of nodes, not millions.
    QSet<uint32_t> articulation;
    QMap<uint32_t, int> discovery, low;
    QSet<uint32_t> visited;
    int timer = 0;

    std::function<void(uint32_t, uint32_t, bool)> dfs =
        [&](uint32_t node, uint32_t parent, bool isRoot) {
            visited.insert(node);
            discovery[node] = low[node] = ++timer;
            int childCount = 0;

            for (uint32_t next : adjacency.value(node))
            {
                if (next == parent)
                    continue;

                if (visited.contains(next))
                {
                    low[node] = std::min(low[node], discovery[next]);
                    continue;
                }

                ++childCount;
                dfs(next, node, false);
                low[node] = std::min(low[node], low[next]);

                // A non-root node is an articulation point when some child's
                // subtree cannot reach above it.
                if (!isRoot && low[next] >= discovery[node])
                    articulation.insert(node);
            }

            // The root is an articulation point only with more than one child
            if (isRoot && childCount > 1)
                articulation.insert(node);
        };

    for (auto it = adjacency.constBegin(); it != adjacency.constEnd(); ++it)
    {
        if (!visited.contains(it.key()))
            dfs(it.key(), 0, true);
    }
    return articulation;
}

QList<QSet<uint32_t>> MeshAnalytics::findComponents(
    const QMap<uint32_t, QSet<uint32_t>> &adjacency)
{
    QList<QSet<uint32_t>> components;
    QSet<uint32_t> visited;

    for (auto it = adjacency.constBegin(); it != adjacency.constEnd(); ++it)
    {
        if (visited.contains(it.key()))
            continue;

        QSet<uint32_t> component;
        QList<uint32_t> stack{it.key()};
        while (!stack.isEmpty())
        {
            uint32_t node = stack.takeLast();
            if (visited.contains(node))
                continue;
            visited.insert(node);
            component.insert(node);
            for (uint32_t next : adjacency.value(node))
            {
                if (!visited.contains(next))
                    stack.append(next);
            }
        }
        components.append(component);
    }

    std::sort(components.begin(), components.end(),
              [](const QSet<uint32_t> &a, const QSet<uint32_t> &b) { return a.size() > b.size(); });
    return components;
}

MeshAnalytics::Topology MeshAnalytics::topology(const QDateTime &since) const
{
    Topology topo;
    if (!m_db || !m_db->isOpen())
        return topo;

    // Adjacency plus running SNR mean per undirected link
    QMap<uint32_t, QSet<uint32_t>> adjacency;
    QMap<QPair<uint32_t, uint32_t>, QPair<double, int>> linkStats;

    auto addLink = [&](uint32_t a, uint32_t b, double snr) {
        if (a == 0 || b == 0 || a == b)
            return;
        adjacency[a].insert(b);
        adjacency[b].insert(a);
        auto key = qMakePair(std::min(a, b), std::max(a, b));
        auto &entry = linkStats[key];
        entry.first += snr;
        entry.second += 1;
    };

    // Consecutive hops in a stored traceroute are, by definition, nodes that
    // could hear each other.
    QSqlQuery tr(m_db->connection());
    tr.prepare("SELECT from_node, to_node, route_to, snr_to FROM traceroutes "
               "WHERE timestamp >= ? AND is_response = 1");
    tr.addBindValue(toRowStamp(since));
    if (tr.exec())
    {
        while (tr.next())
        {
            const uint32_t from = tr.value(0).toUInt();
            const uint32_t to = tr.value(1).toUInt();
            const QStringList hops = tr.value(2).toString().split(',', Qt::SkipEmptyParts);
            const QStringList snrs = tr.value(3).toString().split(',', Qt::SkipEmptyParts);

            QList<uint32_t> path;
            path.append(from);
            for (const QString &h : hops)
            {
                bool ok = false;
                uint32_t n = h.trimmed().toUInt(&ok);
                if (ok && n != 0)
                    path.append(n);
            }
            path.append(to);

            for (int i = 0; i + 1 < path.size(); ++i)
            {
                double snr = (i < snrs.size()) ? snrs[i].trimmed().toDouble() : 0.0;
                addLink(path[i], path[i + 1], snr);
            }
        }
    }
    else
    {
        qWarning() << "[Analytics] topology traceroute query failed:" << tr.lastError().text();
    }

    // NeighborInfo, where a mesh publishes it, is the cheapest topology source
    // there is - the nodes broadcast it without us asking.
    QSqlQuery nb(m_db->connection());
    nb.prepare("SELECT node_num, neighbor_node, snr FROM neighbor_info WHERE timestamp >= ?");
    nb.addBindValue(toRowStamp(since));
    if (nb.exec())
    {
        while (nb.next())
            addLink(nb.value(0).toUInt(), nb.value(1).toUInt(), nb.value(2).toDouble());
    }

    for (auto it = linkStats.constBegin(); it != linkStats.constEnd(); ++it)
    {
        Link l;
        l.a = it.key().first;
        l.b = it.key().second;
        l.observations = it.value().second;
        l.snr = it.value().second ? it.value().first / it.value().second : 0.0;
        topo.links.append(l);
    }

    for (auto it = adjacency.constBegin(); it != adjacency.constEnd(); ++it)
        topo.nodes.insert(it.key());

    topo.articulationPoints = findArticulationPoints(adjacency);
    topo.components = findComponents(adjacency);
    return topo;
}

// ---------------------------------------------------------------- 19 --------

QList<MeshAnalytics::RouteChurn> MeshAnalytics::routeChurn(const QDateTime &since) const
{
    QList<RouteChurn> out;
    if (!m_db || !m_db->isOpen())
        return out;

    QSqlQuery q(m_db->connection());
    q.prepare("SELECT to_node, route_to, timestamp FROM traceroutes "
              "WHERE timestamp >= ? AND is_response = 1 ORDER BY timestamp ASC");
    q.addBindValue(toRowStamp(since));

    if (!q.exec())
    {
        qWarning() << "[Analytics] routeChurn failed:" << q.lastError().text();
        return out;
    }

    struct Acc
    {
        int observations = 0;
        QSet<QString> routes;
        QString last;
    };
    QMap<uint32_t, Acc> byDest;

    while (q.next())
    {
        const uint32_t dest = q.value(0).toUInt();
        if (dest == 0)
            continue;
        const QString route = q.value(1).toString().trimmed();

        Acc &acc = byDest[dest];
        acc.observations++;
        acc.routes.insert(route);
        acc.last = route;
    }

    for (auto it = byDest.constBegin(); it != byDest.constEnd(); ++it)
    {
        RouteChurn rc;
        rc.destination = it.key();
        rc.name = nodeLabel(it.key());
        rc.observations = it.value().observations;
        rc.distinctRoutes = it.value().routes.size();
        rc.churnPercent = rc.observations ? 100.0 * rc.distinctRoutes / rc.observations : 0.0;

        QStringList pretty;
        for (const QString &hop : it.value().last.split(',', Qt::SkipEmptyParts))
        {
            bool ok = false;
            uint32_t n = hop.trimmed().toUInt(&ok);
            pretty << (ok ? nodeLabel(n) : hop.trimmed());
        }
        rc.lastRoute = pretty.isEmpty() ? QStringLiteral("direct") : pretty.join(" > ");
        out.append(rc);
    }

    // Least settled first - those are the ones worth looking at
    std::sort(out.begin(), out.end(), [](const RouteChurn &a, const RouteChurn &b) {
        if (a.distinctRoutes != b.distinctRoutes)
            return a.distinctRoutes > b.distinctRoutes;
        return a.observations > b.observations;
    });
    return out;
}
