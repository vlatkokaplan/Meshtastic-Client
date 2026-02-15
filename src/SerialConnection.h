#ifndef SERIALCONNECTION_H
#define SERIALCONNECTION_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QByteArray>
#include <QQueue>
#include <QMutex>
#include <QTimer>

class SerialConnection : public QObject
{
    Q_OBJECT

public:
    explicit SerialConnection(QObject *parent = nullptr);
    ~SerialConnection();

    // Known Meshtastic device VID/PIDs
    static QList<QSerialPortInfo> detectMeshtasticDevices();
    static QList<QSerialPortInfo> availablePorts();
    static QString deviceDescription(const QSerialPortInfo &info);

    bool connectToPort(const QString &portName);
    void disconnectDevice();
    bool isConnected() const;
    QString connectedPortName() const;

    bool sendData(const QByteArray &data);

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);

private slots:
    void handleReadyRead();
    void handleError(QSerialPort::SerialPortError error);
    void attemptReconnect();

private:
    QSerialPort *m_serialPort;
    QByteArray m_readBuffer;
    QTimer *m_reconnectTimer;
    QString m_lastPortName;
    bool m_intentionalDisconnect;

    static const int BAUD_RATE = 115200;
    static const int RECONNECT_INTERVAL_MS = 3000;
};

#endif // SERIALCONNECTION_H
