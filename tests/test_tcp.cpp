#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include "TcpConnection.h"

class TestTcp : public QObject
{
    Q_OBJECT

private slots:
    void reconnect_backs_off_to_30_seconds()
    {
        QCOMPARE(TcpConnection::reconnectDelayMs(0), 3000);
        QCOMPARE(TcpConnection::reconnectDelayMs(1), 6000);
        QCOMPARE(TcpConnection::reconnectDelayMs(2), 12000);
        QCOMPARE(TcpConnection::reconnectDelayMs(3), 24000);
        QCOMPARE(TcpConnection::reconnectDelayMs(4), 30000);
        QCOMPARE(TcpConnection::reconnectDelayMs(50), 30000);
    }

    void dropped_link_reports_reconnecting_and_comes_back()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        TcpConnection conn;
        QSignalSpy connected(&conn, &TcpConnection::connected);
        conn.connectToHost("127.0.0.1", server.serverPort());
        QVERIFY(connected.wait(3000));

        // MainWindow keeps nodes and the database only if isReconnecting()
        // is already true when disconnected() arrives
        bool reconnectingWhenAnnounced = false;
        connect(&conn, &TcpConnection::disconnected, this, [&]() {
            reconnectingWhenAnnounced = conn.isReconnecting();
        });

        QSignalSpy disconnected(&conn, &TcpConnection::disconnected);
        conn.dropAndReconnect();  // no socket error on this path
        QCOMPARE(disconnected.count(), 1);
        QVERIFY(reconnectingWhenAnnounced);

        // First retry after 3 s, and the server is still there
        QVERIFY(connected.wait(6000));
        QCOMPARE(connected.count(), 2);
        QVERIFY(!conn.isReconnecting());
        conn.disconnectDevice();
    }

    void intentional_disconnect_does_not_reconnect()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));

        TcpConnection conn;
        QSignalSpy connected(&conn, &TcpConnection::connected);
        conn.connectToHost("127.0.0.1", server.serverPort());
        QVERIFY(connected.wait(3000));

        conn.disconnectDevice();
        QVERIFY(!conn.isReconnecting());
    }
};

QTEST_MAIN(TestTcp)
#include "test_tcp.moc"
