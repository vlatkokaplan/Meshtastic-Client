#include "TcpConnection.h"
#include <QDebug>

TcpConnection::TcpConnection(QObject *parent)
    : QObject(parent), m_socket(new QTcpSocket(this)), m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &TcpConnection::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpConnection::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TcpConnection::onSocketError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TcpConnection::attemptReconnect);

    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);
}

TcpConnection::~TcpConnection()
{
    disconnectDevice();
}

bool TcpConnection::connectToHost(const QString &host, quint16 port)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        disconnectDevice();
    }

    m_lastHost = host;
    m_lastPort = port;
    m_intentionalDisconnect = false;
    m_reconnectTimer->stop();

    qDebug() << "[TCP] Connecting to" << host << ":" << port;
    m_socket->connectToHost(host, port);

    return true; // Connection is async; result comes via connected()/errorOccurred()
}

void TcpConnection::disconnectDevice()
{
    m_intentionalDisconnect = true;
    bool wasReconnecting = m_reconnectTimer->isActive();
    m_reconnectTimer->stop();

    if (m_socket->state() != QAbstractSocket::UnconnectedState)
    {
        m_socket->abort();
        qDebug() << "[TCP] Disconnected from" << m_lastHost << ":" << m_lastPort;
        emit disconnected();
    }
    else if (wasReconnecting)
    {
        // Socket was already down (mid-reconnect loop) — still need to notify
        qDebug() << "[TCP] Cancelled reconnect to" << m_lastHost << ":" << m_lastPort;
        emit disconnected();
    }
}

bool TcpConnection::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString TcpConnection::connectedAddress() const
{
    if (isConnected())
    {
        return QString("%1:%2").arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort());
    }
    return QString();
}

bool TcpConnection::sendData(const QByteArray &data)
{
    if (!isConnected())
    {
        qWarning() << "[TCP] Cannot send data: not connected";
        return false;
    }

    qDebug() << "[TCP] Sending" << data.size() << "bytes";
    qint64 written = m_socket->write(data);
    if (written != data.size())
    {
        qWarning() << "[TCP] Failed to write all data:" << written << "of" << data.size();
        return false;
    }

    bool flushed = m_socket->flush();
    if (!flushed) {
        qWarning() << "[TCP] Socket flush failed";
    }
    return flushed;
}

void TcpConnection::onSocketConnected()
{
    qDebug() << "[TCP] Connected to" << m_lastHost << ":" << m_lastPort;
    m_reconnectTimer->stop();
    emit connected();
}

void TcpConnection::onSocketDisconnected()
{
    qDebug() << "[TCP] Socket disconnected";
    emit disconnected();

    if (!m_intentionalDisconnect && !m_lastHost.isEmpty())
    {
        qDebug() << "[TCP] Starting reconnection attempts...";
        m_reconnectTimer->start();
    }
}

void TcpConnection::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    if (!data.isEmpty())
    {
        emit dataReceived(data);
    }
}

void TcpConnection::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString = m_socket->errorString();
    qWarning() << "[TCP] Socket error:" << errorString;

    if (error == QAbstractSocket::RemoteHostClosedError ||
        error == QAbstractSocket::NetworkError ||
        error == QAbstractSocket::ConnectionRefusedError)
    {
        emit errorOccurred(errorString);

        if (!m_intentionalDisconnect && !m_lastHost.isEmpty())
        {
            qDebug() << "[TCP] Starting reconnection attempts...";
            m_reconnectTimer->start();
        }
    }
    else
    {
        emit errorOccurred(errorString);
    }
}

void TcpConnection::attemptReconnect()
{
    qDebug() << "[TCP] Attempting to reconnect to" << m_lastHost << ":" << m_lastPort;
    m_intentionalDisconnect = false;
    m_socket->connectToHost(m_lastHost, m_lastPort);
}
