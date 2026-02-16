#include "BluetoothConnection.h"
#include <QDebug>

// Meshtastic BLE service and characteristic UUIDs
const QBluetoothUuid BluetoothConnection::SERVICE_UUID(
    QUuid("6ba1b218-15a8-461f-9fa8-5dcae273eafd"));
const QBluetoothUuid BluetoothConnection::FROMRADIO_UUID(
    QUuid("2c55e69e-4993-11ed-b878-0242ac120002"));
const QBluetoothUuid BluetoothConnection::TORADIO_UUID(
    QUuid("f75c76d2-129e-4dad-a1dd-7866124401e7"));
const QBluetoothUuid BluetoothConnection::FROMNUM_UUID(
    QUuid("ed9da18c-a800-4f66-a670-aa7547e34453"));

BluetoothConnection::BluetoothConnection(QObject *parent)
    : QObject(parent)
    , m_discoveryAgent(nullptr) // Lazy-initialized on first scan to avoid slow startup
    , m_controller(nullptr)
    , m_service(nullptr)
    , m_reconnectTimer(new QTimer(this))
    , m_intentionalDisconnect(false)
    , m_connected(false)
{
    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);
    connect(m_reconnectTimer, &QTimer::timeout, this, &BluetoothConnection::attemptReconnect);
}

void BluetoothConnection::ensureDiscoveryAgent()
{
    if (m_discoveryAgent)
        return;

    qDebug() << "[BT] Initializing Bluetooth discovery agent...";
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    m_discoveryAgent->setLowEnergyDiscoveryTimeout(15000); // 15 second scan

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BluetoothConnection::onDeviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BluetoothConnection::onScanFinished);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &BluetoothConnection::onScanError);
    qDebug() << "[BT] Discovery agent ready";
}

BluetoothConnection::~BluetoothConnection()
{
    disconnectDevice();
}

void BluetoothConnection::startScan()
{
    ensureDiscoveryAgent();
    if (m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
    qDebug() << "[BT] Starting BLE scan...";
    m_discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void BluetoothConnection::stopScan()
{
    if (m_discoveryAgent && m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
}

void BluetoothConnection::connectToDevice(const QBluetoothDeviceInfo &deviceInfo)
{
    if (m_controller) {
        disconnectDevice();
    }

    m_lastDevice = deviceInfo;
    m_intentionalDisconnect = false;
    m_reconnectTimer->stop();
    m_deviceName = deviceInfo.name();

    qDebug() << "[BT] Connecting to" << deviceInfo.name() << deviceInfo.address().toString();

    m_controller = QLowEnergyController::createCentral(deviceInfo, this);
    if (!m_controller) {
        qWarning() << "[BT] Failed to create BLE controller";
        emit errorOccurred("Failed to create BLE controller");
        return;
    }

    connect(m_controller, &QLowEnergyController::connected,
            this, &BluetoothConnection::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BluetoothConnection::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred,
            this, &BluetoothConnection::onControllerError);
    connect(m_controller, &QLowEnergyController::serviceDiscovered,
            this, &BluetoothConnection::onServiceDiscovered);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BluetoothConnection::onServiceDiscoveryFinished);

    m_controller->connectToDevice();
}

void BluetoothConnection::disconnectDevice()
{
    bool wasConnected = m_connected;
    m_intentionalDisconnect = true;
    m_reconnectTimer->stop();
    m_connected = false;

    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;
    }

    if (wasConnected)
        emit disconnected();
}

bool BluetoothConnection::isConnected() const
{
    return m_connected;
}

bool BluetoothConnection::sendData(const QByteArray &data)
{
    if (!m_connected || !m_service || !m_toRadioChar.isValid()) {
        qWarning() << "[BT] Cannot send data: not connected or ToRadio not available";
        return false;
    }

    qDebug() << "[BT] Sending" << data.size() << "bytes";

    // BLE has an MTU limit; send framed data
    // Meshtastic BLE protocol: write the framed packet directly to ToRadio
    m_service->writeCharacteristic(m_toRadioChar, data,
                                    QLowEnergyService::WriteWithResponse);
    return true;
}

QString BluetoothConnection::connectedDeviceName() const
{
    if (m_connected)
        return m_deviceName;
    return QString();
}

void BluetoothConnection::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    // Only report BLE devices that advertise the Meshtastic service UUID
    if (info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
        QList<QBluetoothUuid> serviceUuids = info.serviceUuids();
        bool hasMeshtasticService = serviceUuids.contains(SERVICE_UUID);

        // Also check by name pattern as some devices don't advertise service UUIDs
        bool nameMatch = info.name().startsWith("Meshtastic", Qt::CaseInsensitive);

        if (hasMeshtasticService || nameMatch) {
            QString name = info.name().isEmpty() ? "Unknown" : info.name();
            QString address = info.address().toString();
            if (address.isEmpty()) {
                // On macOS, address may be empty; use device UUID instead
                address = info.deviceUuid().toString();
            }
            qDebug() << "[BT] Found Meshtastic device:" << name << address;
            emit deviceDiscovered(name, address, info);
        }
    }
}

void BluetoothConnection::onScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error);
    qWarning() << "[BT] Scan error:" << m_discoveryAgent->errorString();
    emit errorOccurred("BLE scan error: " + m_discoveryAgent->errorString());
    emit scanFinished();
}

void BluetoothConnection::onScanFinished()
{
    qDebug() << "[BT] Scan finished";
    emit scanFinished();
}

void BluetoothConnection::onControllerConnected()
{
    qDebug() << "[BT] Controller connected, discovering services...";
    m_controller->discoverServices();
}

void BluetoothConnection::onControllerDisconnected()
{
    qDebug() << "[BT] Controller disconnected";
    m_connected = false;

    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }

    emit disconnected();

    if (!m_intentionalDisconnect) {
        qDebug() << "[BT] Starting reconnection attempts...";
        m_reconnectTimer->start();
    }
}

void BluetoothConnection::onControllerError(QLowEnergyController::Error error)
{
    Q_UNUSED(error);
    QString errorStr = m_controller ? m_controller->errorString() : "Unknown error";
    qWarning() << "[BT] Controller error:" << errorStr;
    emit errorOccurred("BLE error: " + errorStr);
}

void BluetoothConnection::onServiceDiscovered(const QBluetoothUuid &serviceUuid)
{
    qDebug() << "[BT] Service discovered:" << serviceUuid.toString();
}

void BluetoothConnection::onServiceDiscoveryFinished()
{
    qDebug() << "[BT] Service discovery finished";

    // Clean up old service if any (prevents duplicate signal connections)
    if (m_service) {
        delete m_service;
        m_service = nullptr;
    }

    // Look for Meshtastic service
    m_service = m_controller->createServiceObject(SERVICE_UUID, this);
    if (!m_service) {
        qWarning() << "[BT] Meshtastic service not found on device";
        emit errorOccurred("Meshtastic BLE service not found");
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &BluetoothConnection::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &BluetoothConnection::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicRead,
            this, &BluetoothConnection::onCharacteristicRead);

    m_service->discoverDetails();
}

void BluetoothConnection::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state == QLowEnergyService::RemoteServiceDiscovered) {
        qDebug() << "[BT] Service details discovered";

        // Get characteristics
        m_fromRadioChar = m_service->characteristic(FROMRADIO_UUID);
        m_toRadioChar = m_service->characteristic(TORADIO_UUID);
        m_fromNumChar = m_service->characteristic(FROMNUM_UUID);

        if (!m_fromRadioChar.isValid() || !m_toRadioChar.isValid()) {
            qWarning() << "[BT] Required characteristics not found";
            emit errorOccurred("Required BLE characteristics not found");
            return;
        }

        qDebug() << "[BT] FromRadio valid:" << m_fromRadioChar.isValid()
                 << "ToRadio valid:" << m_toRadioChar.isValid()
                 << "FromNum valid:" << m_fromNumChar.isValid();

        // Enable notifications on FromNum (signals new data available)
        enableNotifications();

        m_connected = true;
        emit connected();

        // Start reading initial data
        readFromRadio();
    }
}

void BluetoothConnection::enableNotifications()
{
    if (m_fromNumChar.isValid()) {
        // Enable notifications for FromNum characteristic
        QLowEnergyDescriptor notification = m_fromNumChar.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QLowEnergyCharacteristic::CCCDEnableNotification);
            qDebug() << "[BT] Enabled notifications on FromNum";
        }
    }

    if (m_fromRadioChar.isValid()) {
        // Also enable notifications on FromRadio if supported
        QLowEnergyDescriptor notification = m_fromRadioChar.descriptor(
            QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
        if (notification.isValid()) {
            m_service->writeDescriptor(notification, QLowEnergyCharacteristic::CCCDEnableNotification);
            qDebug() << "[BT] Enabled notifications on FromRadio";
        }
    }
}

void BluetoothConnection::onCharacteristicChanged(const QLowEnergyCharacteristic &ch,
                                                     const QByteArray &value)
{
    if (ch.uuid() == FROMNUM_UUID) {
        // FromNum changed → new data available, read FromRadio
        qDebug() << "[BT] FromNum notification, reading FromRadio...";
        readFromRadio();
    } else if (ch.uuid() == FROMRADIO_UUID) {
        // Direct notification with data
        if (!value.isEmpty()) {
            emit dataReceived(value);
        }
    }
}

void BluetoothConnection::onCharacteristicRead(const QLowEnergyCharacteristic &ch,
                                                  const QByteArray &value)
{
    if (ch.uuid() == FROMRADIO_UUID && !value.isEmpty()) {
        // Frame the data like serial/TCP: 0x94 0xC3 + length(2 bytes MSB) + payload
        QByteArray framed;
        framed.append(static_cast<char>(0x94));
        framed.append(static_cast<char>(0xC3));
        uint16_t len = static_cast<uint16_t>(value.size());
        framed.append(static_cast<char>((len >> 8) & 0xFF));
        framed.append(static_cast<char>(len & 0xFF));
        framed.append(value);

        emit dataReceived(framed);

        // Continue reading until empty (there may be more queued data)
        readFromRadio();
    }
}

void BluetoothConnection::readFromRadio()
{
    if (m_service && m_fromRadioChar.isValid()) {
        m_service->readCharacteristic(m_fromRadioChar);
    }
}

void BluetoothConnection::attemptReconnect()
{
    if (m_lastDevice.isValid()) {
        qDebug() << "[BT] Attempting reconnect to" << m_lastDevice.name();
        m_intentionalDisconnect = false;
        connectToDevice(m_lastDevice);
    }
}
