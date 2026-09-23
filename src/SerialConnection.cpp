#include "SerialConnection.h"
#include <QDebug>

// USB IDs of Meshtastic-capable boards, following the official Python
// client's supported_device.py plus a few common chips it does not list.
// Only used to sort and label ports in the connect dialog, so a false match
// costs little. ANY_PID matches every product of that vendor.
static constexpr quint16 ANY_PID = 0xFFFF;

struct DeviceId
{
    quint16 vid;
    quint16 pid;
    const char *name;
};

static const DeviceId KNOWN_DEVICES[] = {
    {0x1A86, 0x55D4, "CH9102"},        // T-Beam, T-Lora, Nano G1
    {0x1A86, 0x55D3, "CH343"},         // newer LILYGO / Heltec boards
    {0x1A86, 0x7523, "CH340"},         // RAK11200
    {0x10C4, 0xEA60, "CP2102"},        // Heltec, T-Lora, DIY
    {0x10C4, 0xEA70, "CP2105"},
    {0x0403, 0x6001, "FT232"},
    {0x0403, 0x6015, "FT231X"},
    {0x303A, 0x1001, "ESP32 native USB"}, // ESP32-S3/C3/C6, T-Deck
    {0x303A, 0x4001, "ESP32-S2"},
    {0x239A, ANY_PID, "nRF52840"},     // Adafruit bootloader: RAK4631, T-Echo, T114
    {0x2886, ANY_PID, "Seeed nRF52840"}, // XIAO, Wio Tracker, SenseCAP T1000-E
    {0x2E8A, ANY_PID, "RP2040"},       // RAK11310, Pico
    {0, 0, nullptr}};

static const DeviceId *matchDevice(quint16 vid, quint16 pid)
{
    for (const DeviceId *dev = KNOWN_DEVICES; dev->vid != 0; ++dev)
    {
        if (vid == dev->vid && (dev->pid == ANY_PID || pid == dev->pid))
            return dev;
    }
    return nullptr;
}

SerialConnection::SerialConnection(QObject *parent)
    : QObject(parent), m_serialPort(new QSerialPort(this)), m_reconnectTimer(new QTimer(this)), m_intentionalDisconnect(false)
{
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialConnection::handleReadyRead);
    connect(m_serialPort, &QSerialPort::errorOccurred, this, &SerialConnection::handleError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SerialConnection::attemptReconnect);

    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);
}

SerialConnection::~SerialConnection()
{
    disconnectDevice();
}

QList<QSerialPortInfo> SerialConnection::detectMeshtasticDevices()
{
    QList<QSerialPortInfo> meshtasticPorts;

    for (const QSerialPortInfo &portInfo : QSerialPortInfo::availablePorts())
    {
        if (!portInfo.hasVendorIdentifier())
            continue;
        if (const DeviceId *dev = matchDevice(portInfo.vendorIdentifier(), portInfo.productIdentifier()))
        {
            meshtasticPorts.append(portInfo);
            qDebug() << "Found Meshtastic device:" << dev->name
                     << "on" << portInfo.portName();
        }
    }

    return meshtasticPorts;
}

QList<QSerialPortInfo> SerialConnection::availablePorts()
{
    return QSerialPortInfo::availablePorts();
}

QString SerialConnection::deviceDescription(const QSerialPortInfo &info)
{
    if (info.hasVendorIdentifier())
    {
        if (const DeviceId *dev = matchDevice(info.vendorIdentifier(), info.productIdentifier()))
            return QString(dev->name);
    }

    return info.description();
}

bool SerialConnection::connectToPort(const QString &portName)
{
    if (m_serialPort->isOpen())
    {
        disconnectDevice();
    }

    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(BAUD_RATE);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort->open(QIODevice::ReadWrite))
    {
        m_lastPortName = portName;
        m_intentionalDisconnect = false;
        m_readBuffer.clear();
        m_reconnectTimer->stop();
        qDebug() << "Connected to" << portName;
        emit connected();
        return true;
    }
    else
    {
        QString error = m_serialPort->errorString();
        qWarning() << "Failed to connect to" << portName << ":" << error;
        emit errorOccurred(error);
        return false;
    }
}

void SerialConnection::disconnectDevice()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer->stop();

    if (m_serialPort->isOpen())
    {
        m_serialPort->close();
        qDebug() << "Disconnected from" << m_lastPortName;
        emit disconnected();
    }
}

bool SerialConnection::isConnected() const
{
    return m_serialPort->isOpen();
}

QString SerialConnection::connectedPortName() const
{
    if (m_serialPort->isOpen())
    {
        return m_serialPort->portName();
    }
    return QString();
}

bool SerialConnection::sendData(const QByteArray &data)
{
    if (!m_serialPort->isOpen())
    {
        qWarning() << "Cannot send data: not connected";
        return false;
    }

    qDebug() << "[Serial] Sending" << data.size() << "bytes";
    qint64 written = m_serialPort->write(data);
    if (written != data.size())
    {
        qWarning() << "Failed to write all data:" << written << "of" << data.size();
        return false;
    }

    // flush() returns false when the buffer was already empty, which is not an
    // error - the write above is what determines success.
    m_serialPort->flush();
    return true;
}

void SerialConnection::handleReadyRead()
{
    QByteArray newData = m_serialPort->readAll();
    if (!newData.isEmpty())
    {
        emit dataReceived(newData);
    }
}

void SerialConnection::handleError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
    {
        return;
    }

    QString errorString = m_serialPort->errorString();
    qWarning() << "Serial port error:" << errorString;

    if (error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError)
    {
        // Device was unplugged
        m_serialPort->close();
        emit disconnected();
        emit errorOccurred(errorString);

        if (!m_intentionalDisconnect && !m_lastPortName.isEmpty())
        {
            qDebug() << "Starting reconnection attempts...";
            m_reconnectTimer->start();
        }
    }
    else if (error != QSerialPort::NotOpenError)
    {
        emit errorOccurred(errorString);
    }
}

void SerialConnection::attemptReconnect()
{
    qDebug() << "Attempting to reconnect to" << m_lastPortName;

    // Check if the device is available again
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
    {
        if (info.portName() == m_lastPortName)
        {
            m_intentionalDisconnect = false;
            if (connectToPort(m_lastPortName))
            {
                return;
            }
        }
    }
}
