#include <QtTest/QtTest>
#include "MeshAnalytics.h"

// Builds a symmetric adjacency map from undirected edges
static QMap<uint32_t, QSet<uint32_t>> graph(const QList<QPair<uint32_t, uint32_t>> &edges,
                                            const QList<uint32_t> &isolated = {})
{
    QMap<uint32_t, QSet<uint32_t>> adj;
    for (const auto &e : edges) {
        adj[e.first].insert(e.second);
        adj[e.second].insert(e.first);
    }
    for (uint32_t n : isolated)
        adj[n];  // present with no neighbours
    return adj;
}

class TestAnalytics : public QObject
{
    Q_OBJECT

private slots:
    // ---- articulation points -------------------------------------------

    void empty_graph_has_no_articulation_points()
    {
        QVERIFY(MeshAnalytics::findArticulationPoints({}).isEmpty());
    }

    void path_graph_middle_is_articulation()
    {
        // A - B - C : losing B splits A from C
        auto adj = graph({{1, 2}, {2, 3}});
        auto ap = MeshAnalytics::findArticulationPoints(adj);
        QCOMPARE(ap.size(), 1);
        QVERIFY(ap.contains(2));
    }

    void triangle_has_no_articulation_points()
    {
        // Every node has a second path round the ring
        auto adj = graph({{1, 2}, {2, 3}, {3, 1}});
        QVERIFY(MeshAnalytics::findArticulationPoints(adj).isEmpty());
    }

    void star_centre_is_articulation()
    {
        // The classic single point of failure: one relay, three leaves
        auto adj = graph({{1, 2}, {1, 3}, {1, 4}});
        auto ap = MeshAnalytics::findArticulationPoints(adj);
        QCOMPARE(ap.size(), 1);
        QVERIFY(ap.contains(1));
    }

    void leaf_is_never_articulation()
    {
        auto adj = graph({{1, 2}, {1, 3}, {1, 4}});
        auto ap = MeshAnalytics::findArticulationPoints(adj);
        QVERIFY(!ap.contains(2));
        QVERIFY(!ap.contains(3));
        QVERIFY(!ap.contains(4));
    }

    void bridge_between_two_rings()
    {
        // Two triangles joined by a single edge 3-4: both endpoints of the
        // bridge are articulation points, the other four nodes are not.
        auto adj = graph({{1, 2}, {2, 3}, {3, 1},
                          {4, 5}, {5, 6}, {6, 4},
                          {3, 4}});
        auto ap = MeshAnalytics::findArticulationPoints(adj);
        QVERIFY(ap.contains(3));
        QVERIFY(ap.contains(4));
        QCOMPARE(ap.size(), 2);
    }

    void articulation_found_in_every_component()
    {
        // Two disjoint paths - the middle of each is an articulation point
        auto adj = graph({{1, 2}, {2, 3}, {10, 11}, {11, 12}});
        auto ap = MeshAnalytics::findArticulationPoints(adj);
        QVERIFY(ap.contains(2));
        QVERIFY(ap.contains(11));
        QCOMPARE(ap.size(), 2);
    }

    // ---- components ------------------------------------------------------

    void components_split_a_partitioned_graph()
    {
        auto adj = graph({{1, 2}, {2, 3}, {10, 11}});
        auto comps = MeshAnalytics::findComponents(adj);
        QCOMPARE(comps.size(), 2);
        QCOMPARE(comps[0].size(), 3);   // largest first
        QCOMPARE(comps[1].size(), 2);
        QVERIFY(comps[0].contains(1));
        QVERIFY(comps[1].contains(10));
    }

    void connected_graph_is_one_component()
    {
        auto adj = graph({{1, 2}, {2, 3}, {3, 1}});
        auto comps = MeshAnalytics::findComponents(adj);
        QCOMPARE(comps.size(), 1);
        QCOMPARE(comps[0].size(), 3);
    }

    void isolated_node_is_its_own_component()
    {
        auto adj = graph({{1, 2}}, {99});
        auto comps = MeshAnalytics::findComponents(adj);
        QCOMPARE(comps.size(), 2);
        QCOMPARE(comps[1].size(), 1);
        QVERIFY(comps[1].contains(99));
    }

    void isolated_node_is_not_articulation()
    {
        auto adj = graph({{1, 2}}, {99});
        QVERIFY(!MeshAnalytics::findArticulationPoints(adj).contains(99));
    }
};

QTEST_MAIN(TestAnalytics)
#include "test_analytics.moc"
