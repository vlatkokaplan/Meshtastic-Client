#include <QtTest/QtTest>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "Database.h"
#include "NodeManager.h"

// Exercises the real Database against a real SQLite file.
//
// The rest of the suite links DatabaseStub.cpp, which replaces every method
// with an empty stub, so none of this SQL was covered. Three bugs shipped from
// here: DELETE FROM nodes failing a foreign key while reporting success, the
// traceroutes table never being created for an existing database, and packets
// storing milliseconds while every other table stores seconds. Each has a test
// below.
class TestDatabase : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_counter = 0;

    // A fresh database per test; Database keys its connection on a UUID, so
    // several can be open at once without colliding.
    QString newPath()
    {
        return m_dir.filePath(QString("t%1.db").arg(++m_counter));
    }

    static NodeInfo makeNode(uint32_t num, const QString &name = "Node")
    {
        NodeInfo n;
        n.nodeNum = num;
        n.nodeId = QString("!%1").arg(num, 8, 16, QChar('0'));
        n.longName = name;
        n.shortName = name.left(4);
        n.lastHeard = QDateTime::currentDateTime();
        return n;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    // ---- schema ---------------------------------------------------------

    void fresh_database_has_every_table()
    {
        Database db;
        QVERIFY(db.open(newPath()));

        QStringList tables;
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT name FROM sqlite_master WHERE type='table'"));
        while (q.next())
            tables << q.value(0).toString();

        for (const QString &expected : {"nodes", "messages", "traceroutes",
                                        "telemetry_history", "position_history",
                                        "packets", "neighbor_info"})
        {
            QVERIFY2(tables.contains(expected),
                     qPrintable(QString("missing table: %1").arg(expected)));
        }
    }

    void missing_table_is_recreated_on_open()
    {
        // The shipped bug: createTables() only ran for brand new databases and
        // migrateSchema() only ever added columns, so a table introduced later
        // never appeared while the version still read as current.
        const QString path = newPath();
        {
            Database db;
            QVERIFY(db.open(path));
        }

        {
            QSqlDatabase raw = QSqlDatabase::addDatabase("QSQLITE", "drop_conn");
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            QSqlQuery q(raw);
            QVERIFY(q.exec("DROP TABLE traceroutes"));
            raw.close();
        }
        QSqlDatabase::removeDatabase("drop_conn");

        Database db;
        QVERIFY(db.open(path));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT COUNT(*) FROM sqlite_master "
                       "WHERE type='table' AND name='traceroutes'"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    void write_ahead_logging_is_enabled()
    {
        Database db;
        QVERIFY(db.open(newPath()));
        QSqlQuery q(db.connection());
        QVERIFY(q.exec("PRAGMA journal_mode"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString().toLower(), QString("wal"));
    }

    // ---- nodes ----------------------------------------------------------

    void node_round_trips()
    {
        Database db;
        QVERIFY(db.open(newPath()));
        QVERIFY(db.saveNode(makeNode(0xb29c7344, "Zemun Dunav")));

        auto nodes = db.loadAllNodes();
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes[0].nodeNum, 0xb29c7344u);
        QCOMPARE(nodes[0].longName, QString("Zemun Dunav"));
        QCOMPARE(db.nodeCount(), 1);
    }

    void node_number_above_int_max_survives()
    {
        // Node ids use the full uint32 range; anything that round-trips them
        // through a signed int loses roughly half of them.
        Database db;
        QVERIFY(db.open(newPath()));
        const uint32_t big = 0xf8bc4672;   // > INT_MAX
        QVERIFY(db.saveNode(makeNode(big, "Big")));

        auto nodes = db.loadAllNodes();
        QCOMPARE(nodes.size(), 1);
        QCOMPARE(nodes[0].nodeNum, big);
    }

    void zero_last_heard_loads_as_invalid()
    {
        Database db;
        QVERIFY(db.open(newPath()));
        NodeInfo n = makeNode(0x1234);
        n.lastHeard = QDateTime();          // never heard from
        QVERIFY(db.saveNode(n));

        auto nodes = db.loadAllNodes();
        QCOMPARE(nodes.size(), 1);
        QVERIFY(!nodes[0].lastHeard.isValid());
    }

    void delete_all_nodes_succeeds_with_dependent_rows()
    {
        // The shipped bug: messages, telemetry_history and position_history all
        // carry a foreign key on nodes(node_num) with no ON DELETE CASCADE, and
        // open() enables foreign_keys, so DELETE FROM nodes failed outright for
        // any node with history - and the failure was reported as success.
        Database db;
        QVERIFY(db.open(newPath()));

        const uint32_t node = 0xb29c7344;
        QVERIFY(db.saveNode(makeNode(node)));

        Database::PositionRecord pos;
        pos.nodeNum = node;
        pos.latitude = 44.8;
        pos.longitude = 20.4;
        pos.timestamp = QDateTime::currentDateTime();
        QVERIFY(db.savePosition(pos));

        Database::TelemetryRecord tel;
        tel.nodeNum = node;
        tel.timestamp = QDateTime::currentDateTime();
        tel.batteryLevel = 80;
        QVERIFY(db.saveTelemetryRecord(tel));

        QCOMPARE(db.nodeCount(), 1);
        QVERIFY2(db.deleteAllNodes(), "deleteAllNodes must not fail on dependent rows");
        QCOMPARE(db.nodeCount(), 0);
    }

    void clearing_nodes_keeps_history()
    {
        // Cascading the delete would take the user's messages with it, so the
        // history is deliberately left in place to re-associate by node_num.
        Database db;
        QVERIFY(db.open(newPath()));

        const uint32_t node = 0x55aa55aa;
        QVERIFY(db.saveNode(makeNode(node)));

        Database::PositionRecord pos;
        pos.nodeNum = node;
        pos.latitude = 1.0;
        pos.longitude = 2.0;
        pos.timestamp = QDateTime::currentDateTime();
        QVERIFY(db.savePosition(pos));

        QVERIFY(db.deleteAllNodes());

        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT COUNT(*) FROM position_history"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);
    }

    // ---- timestamp units -------------------------------------------------

    void packets_store_milliseconds_and_others_store_seconds()
    {
        // The two units live side by side; mixing them up yields an empty
        // result rather than an error, which is how it goes unnoticed.
        Database db;
        QVERIFY(db.open(newPath()));

        const uint32_t node = 0x2222;
        QVERIFY(db.saveNode(makeNode(node)));

        const QDateTime now = QDateTime::currentDateTime();

        Database::PacketRecord pkt;
        pkt.timestamp = now.toMSecsSinceEpoch();
        pkt.packetType = 1;
        pkt.fromNode = node;
        pkt.portNum = 3;
        QVERIFY(db.savePacket(pkt));

        Database::TelemetryRecord tel;
        tel.nodeNum = node;
        tel.timestamp = now;
        QVERIFY(db.saveTelemetryRecord(tel));

        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT (SELECT timestamp FROM packets LIMIT 1), "
                       "       (SELECT timestamp FROM telemetry_history LIMIT 1)"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toLongLong(), now.toMSecsSinceEpoch());
        QCOMPARE(q.value(1).toLongLong(), now.toSecsSinceEpoch());
    }

    // ---- analytics-facing queries ----------------------------------------

    void packet_range_returns_oldest_first_but_truncates_newest()
    {
        Database db;
        QVERIFY(db.open(newPath()));

        const qint64 base = QDateTime::currentDateTime().toMSecsSinceEpoch();
        for (int i = 0; i < 10; ++i)
        {
            Database::PacketRecord p;
            p.timestamp = base + i * 1000;
            p.packetType = 1;
            p.fromNode = 0x1000 + i;
            p.portNum = 1;
            QVERIFY(db.savePacket(p));
        }

        auto all = db.loadPacketsInRange(base, base + 100000);
        QCOMPARE(all.size(), 10);
        QVERIFY2(all.first().timestamp < all.last().timestamp, "must be oldest-first");

        // Hitting the cap has to keep the most recent packets, not the oldest,
        // otherwise a replay silently covers only the start of the window.
        auto capped = db.loadPacketsInRange(base, base + 100000, 3);
        QCOMPARE(capped.size(), 3);
        QCOMPARE(capped.last().timestamp, base + 9000);
        QVERIFY2(capped.first().timestamp > base, "cap must drop the oldest, not the newest");
    }

    void packet_range_excludes_device_frames()
    {
        // packet_type 1 is a real mesh packet; the table also holds device-to-app
        // frames, which are not mesh activity.
        Database db;
        QVERIFY(db.open(newPath()));
        const qint64 base = QDateTime::currentDateTime().toMSecsSinceEpoch();

        Database::PacketRecord mesh;
        mesh.timestamp = base;
        mesh.packetType = 1;
        mesh.portNum = 1;
        QVERIFY(db.savePacket(mesh));

        Database::PacketRecord frame;
        frame.timestamp = base + 1;
        frame.packetType = 3;   // NodeInfo, a device frame
        QVERIFY(db.savePacket(frame));

        QCOMPARE(db.loadPacketsInRange(base - 1000, base + 1000).size(), 1);
    }

    void position_track_is_ordered_oldest_first()
    {
        Database db;
        QVERIFY(db.open(newPath()));

        const uint32_t node = 0x3333;
        QVERIFY(db.saveNode(makeNode(node)));

        const qint64 base = QDateTime::currentDateTime().toSecsSinceEpoch() - 500;
        for (int i = 0; i < 5; ++i)
        {
            Database::PositionRecord p;
            p.nodeNum = node;
            p.latitude = 44.0 + i;
            p.longitude = 20.0;
            p.timestamp = QDateTime::fromSecsSinceEpoch(base + i * 10);
            QVERIFY(db.savePosition(p));
        }

        auto track = db.loadPositionTrack(node, 0);
        QCOMPARE(track.size(), 5);
        QVERIFY(track.first().timestamp < track.last().timestamp);
        QVERIFY(qAbs(track.first().latitude - 44.0) < 1e-9);

        // A window that starts after every fix yields nothing
        QVERIFY(db.loadPositionTrack(node, base + 10000).isEmpty());
    }

    // ---- traceroutes -----------------------------------------------------

    void traceroute_round_trips_with_semicolon_separated_hops()
    {
        // MeshAnalytics parses these back out; the separator and the hex form
        // are the contract between the two.
        Database db;
        QVERIFY(db.open(newPath()));

        Database::Traceroute tr;
        tr.fromNode = 0x1111;
        tr.toNode = 0x2222;
        tr.routeTo = QStringList{"b29c7344", "f8bc4672"};
        tr.snrTo = QStringList{"5.5", "6.8"};
        tr.timestamp = QDateTime::currentDateTime();
        tr.isResponse = true;
        QVERIFY(db.saveTraceroute(tr));

        QSqlQuery q(db.connection());
        QVERIFY(q.exec("SELECT route_to FROM traceroutes LIMIT 1"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QString("b29c7344;f8bc4672"));

        auto loaded = db.loadTraceroutes(10, 0);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded[0].routeTo.size(), 2);
        QCOMPARE(loaded[0].routeTo[0], QString("b29c7344"));
    }

    // ---- lifecycle -------------------------------------------------------

    // NB: must not end in "_data" - QTest would take it for a data provider
    void reopening_preserves_stored_nodes()
    {
        const QString path = newPath();
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(db.saveNode(makeNode(0xabcd, "Persisted")));
        }
        Database db;
        QVERIFY(db.open(path));
        QCOMPARE(db.nodeCount(), 1);
        QCOMPARE(db.loadAllNodes()[0].longName, QString("Persisted"));
    }

    void operations_on_a_closed_database_fail_quietly()
    {
        Database db;   // never opened
        QVERIFY(!db.isOpen());
        QVERIFY(!db.saveNode(makeNode(1)));
        QVERIFY(db.loadAllNodes().isEmpty());
        QCOMPARE(db.nodeCount(), 0);
        QVERIFY(!db.deleteAllNodes());
        QVERIFY(db.loadPacketsInRange(0, 1).isEmpty());
        QVERIFY(db.loadPositionTrack(1, 0).isEmpty());
    }

};

QTEST_MAIN(TestDatabase)
#include "test_database.moc"
