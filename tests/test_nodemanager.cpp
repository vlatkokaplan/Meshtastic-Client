#include <QtTest/QtTest>
#include <QSignalSpy>
#include "NodeManager.h"

class TestNodeManager : public QObject
{
    Q_OBJECT

private slots:
    void add_and_retrieve_node()
    {
        NodeManager nm;
        QVariantMap fields;
        fields["nodeNum"]  = 0x1111u;
        fields["longName"] = "Test Node";
        fields["shortName"]= "TEST";
        fields["latitude"] = 51.5;
        fields["longitude"]= -0.1;

        nm.updateNodeFromPacket(fields);

        QVERIFY(nm.hasNode(0x1111u));
        NodeInfo n = nm.getNode(0x1111u);
        QCOMPARE(n.longName, QString("Test Node"));
        QCOMPARE(n.hasPosition, true);
        QVERIFY(qAbs(n.latitude  - 51.5) < 1e-6);
        QVERIFY(qAbs(n.longitude - (-0.1)) < 1e-6);
    }

    void update_position()
    {
        NodeManager nm;
        nm.updateNodePosition(0x2222u, 48.8566, 2.3522, 35);

        QVERIFY(nm.hasNode(0x2222u));
        NodeInfo n = nm.getNode(0x2222u);
        QCOMPARE(n.altitude, 35);
        QVERIFY(n.hasPosition);
    }

    void zero_position_ignored()
    {
        NodeManager nm;
        nm.updateNodePosition(0x3333u, 0.0, 0.0, 0);
        // Node should not exist — zero coords are filtered
        QVERIFY(!nm.hasNode(0x3333u));
    }

    void clear_removes_all_nodes()
    {
        NodeManager nm;
        nm.updateNodePosition(0x4444u, 1.0, 2.0, 0);
        nm.updateNodePosition(0x5555u, 3.0, 4.0, 0);
        QCOMPARE(nm.allNodes().count(), 2);

        QSignalSpy spy(&nm, &NodeManager::nodesChanged);
        nm.clear();

        QCOMPARE(nm.allNodes().count(), 0);
        QCOMPARE(spy.count(), 1); // should emit exactly one nodesChanged
    }

    void nodesWithPosition_filters_correctly()
    {
        NodeManager nm;
        // Node with position
        nm.updateNodePosition(0xAAAAu, 10.0, 20.0, 0);
        // Node without position (via user packet only)
        QVariantMap fields;
        fields["nodeNum"]  = 0xBBBBu;
        fields["longName"] = "No GPS";
        nm.updateNodeFromPacket(fields);

        QList<NodeInfo> withPos = nm.nodesWithPosition();
        QCOMPARE(withPos.count(), 1);
        QCOMPARE(withPos[0].nodeNum, 0xAAAAu);
    }

    void update_telemetry()
    {
        NodeManager nm;
        nm.updateNodePosition(0xCCCCu, 5.0, 6.0, 0);

        QVariantMap tel;
        tel["batteryLevel"] = 75;
        tel["voltage"]      = 3.85;
        nm.updateNodeTelemetry(0xCCCCu, tel);

        NodeInfo n = nm.getNode(0xCCCCu);
        QCOMPARE(n.batteryLevel, 75);
        QVERIFY(qAbs(n.voltage - 3.85f) < 0.01f);
    }

    void external_power_battery_over_100()
    {
        NodeManager nm;
        nm.updateNodePosition(0xDDDDu, 1.0, 1.0, 0);

        QVariantMap tel;
        tel["batteryLevel"] = 120; // > 100 means external power in Meshtastic
        nm.updateNodeTelemetry(0xDDDDu, tel);

        NodeInfo n = nm.getNode(0xDDDDu);
        QCOMPARE(n.batteryLevel, 100); // capped at 100 for display
        QCOMPARE(n.isExternalPower, true);
    }

    void nodesChanged_debounced()
    {
        // Multiple rapid updates should coalesce into one nodesChanged emission
        NodeManager nm;
        QSignalSpy spy(&nm, &NodeManager::nodesChanged);

        for (int i = 0; i < 10; ++i) {
            nm.updateNodePosition(static_cast<uint32_t>(0xEEE0 + i),
                                  static_cast<double>(i), static_cast<double>(i), 0);
        }

        // Debounce is 100ms — wait for it to fire
        QTest::qWait(200);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestNodeManager)
#include "test_nodemanager.moc"
