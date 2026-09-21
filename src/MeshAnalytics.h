#ifndef MESHANALYTICS_H
#define MESHANALYTICS_H

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>

class Database;
class NodeManager;

// Read-only analysis over data the radio has already delivered.
//
// Nothing here transmits. Every figure is derived from rows already in the
// database or from NodeManager's in-memory state, so running any of it costs no
// airtime - which is the whole point: a mesh analyst tool must not itself
// become traffic on the mesh it is measuring.
class MeshAnalytics
{
public:
    MeshAnalytics(Database *db, NodeManager *nodes);

    // ---- 16: decode success -------------------------------------------------
    // How much of what we heard we could actually read. Packets the radio
    // handed us with no usable portnum are either encrypted for channels we
    // hold no key for (expected on a public mesh) or our own channels failing
    // to decrypt (a real problem). Splitting the two is the point.
    struct PortCount
    {
        int portNum = 0;
        QString name;
        int count = 0;
    };
    struct DecodeStats
    {
        int total = 0;
        int decoded = 0;
        int undecoded = 0;
        QList<PortCount> byPort;          // decoded traffic, busiest first
        QMap<int, int> undecodedByChannel; // channel/hash -> count
        bool hasData() const { return total > 0; }
        double decodedPercent() const { return total ? 100.0 * decoded / total : 0.0; }
    };
    DecodeStats decodeStats(const QDateTime &since) const;

    // ---- 17: airtime --------------------------------------------------------
    // Channel utilisation and transmit airtime as reported by the nodes
    // themselves. EU_868 allows a 10% duty cycle; a single busy router is the
    // usual reason a mesh feels congested.
    struct AirtimeSample
    {
        QDateTime when;
        double channelUtil = 0.0;  // percent
        double airUtilTx = 0.0;    // percent
    };
    struct AirtimeNode
    {
        uint32_t nodeNum = 0;
        QString name;
        double peakAirUtilTx = 0.0;
        double avgAirUtilTx = 0.0;
        int samples = 0;
    };
    QVector<AirtimeSample> ownAirtime(uint32_t myNodeNum, const QDateTime &since) const;
    QList<AirtimeNode> airtimeByNode(const QDateTime &since) const;

    // ---- 18: reachability ---------------------------------------------------
    // The mesh as a graph, and the nodes holding it together. An articulation
    // point is one whose removal splits the graph: losing it partitions the
    // mesh. Built from stored traceroute hops and NeighborInfo, never by
    // sending anything.
    struct Link
    {
        uint32_t a = 0;
        uint32_t b = 0;
        double snr = 0.0;
        int observations = 0;
    };
    struct Topology
    {
        QList<Link> links;
        QSet<uint32_t> nodes;
        QSet<uint32_t> articulationPoints;
        QList<QSet<uint32_t>> components;  // largest first
        bool hasData() const { return !links.isEmpty(); }
    };
    Topology topology(const QDateTime &since) const;

    // ---- 19: route churn ----------------------------------------------------
    // How settled the path to a destination is. A route that keeps changing is
    // an unreliable link; a stable one needs no attention.
    struct RouteChurn
    {
        uint32_t destination = 0;
        QString name;
        int observations = 0;
        int distinctRoutes = 0;
        QString lastRoute;       // "a > b > c", names where known
        double churnPercent = 0.0;  // distinct / observations
    };
    QList<RouteChurn> routeChurn(const QDateTime &since) const;

    // ---- shared -------------------------------------------------------------
    QString nodeLabel(uint32_t nodeNum) const;

    // Articulation points of an arbitrary undirected graph (Hopcroft-Tarjan).
    // Exposed for testing; `adjacency` must be symmetric.
    static QSet<uint32_t> findArticulationPoints(const QMap<uint32_t, QSet<uint32_t>> &adjacency);

    // Connected components, largest first. Exposed for testing.
    static QList<QSet<uint32_t>> findComponents(const QMap<uint32_t, QSet<uint32_t>> &adjacency);

    // Parses a route as Database::saveTraceroute() stores it: hex node ids,
    // optionally '!'-prefixed, joined with ';'. Exposed so the storage format
    // is pinned by a test - reading it with the wrong separator or radix
    // produces a wrong graph silently rather than failing.
    static QList<uint32_t> parseStoredRoute(const QString &stored);

private:
    Database *m_db = nullptr;
    NodeManager *m_nodes = nullptr;
};

#endif // MESHANALYTICS_H
