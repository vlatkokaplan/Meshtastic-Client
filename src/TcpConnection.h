#ifndef TCPCONNECTION_H
#define TCPCONNECTION_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

class TcpConnection : public QObject
{
    Q_OBJECT

public:
    explicit TcpConnection(QObject *parent = nullptr);
    ~TcpConnection();

    bool connectToHost(const QString &host, quint16 port = 4403);
    void disconnectDevice();
    bool isConnected() const;
    QString connectedAddress() const;

    bool sendData(const QByteArray &data);
    bool isReconnecting() const { return m_reconnectTimer->isActive(); }

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void attemptReconnect();

private:
    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QString m_lastHost;
    quint16 m_lastPort = 4403;
    bool m_intentionalDisconnect = false;

    static const int RECONNECT_INTERVAL_MS = 3000;
};

#endif // TCPCONNECTION_H
