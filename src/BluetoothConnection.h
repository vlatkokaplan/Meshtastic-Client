#ifndef BLUETOOTHCONNECTION_H
#define BLUETOOTHCONNECTION_H

#include <QObject>
#include <QTimer>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>

class BluetoothConnection : public QObject
{
    Q_OBJECT

public:
    explicit BluetoothConnection(QObject *parent = nullptr);
    ~BluetoothConnection();

    void startScan();
    void stopScan();
    void connectToDevice(const QBluetoothDeviceInfo &deviceInfo);
    void disconnectDevice();
    bool isConnected() const;
    bool sendData(const QByteArray &data);
    QString connectedDeviceName() const;

signals:
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &error);
    void deviceDiscovered(const QString &name, const QString &address, const QBluetoothDeviceInfo &info);
    void scanFinished();

private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onScanFinished();
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void onServiceDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic &ch, const QByteArray &value);
    void onCharacteristicRead(const QLowEnergyCharacteristic &ch, const QByteArray &value);
    void attemptReconnect();

private:
    // Meshtastic BLE UUIDs
    static const QBluetoothUuid SERVICE_UUID;
    static const QBluetoothUuid FROMRADIO_UUID;
    static const QBluetoothUuid TORADIO_UUID;
    static const QBluetoothUuid FROMNUM_UUID;

    QBluetoothDeviceDiscoveryAgent *m_discoveryAgent; // Lazy-initialized
    QLowEnergyController *m_controller;
    QLowEnergyService *m_service;

    QLowEnergyCharacteristic m_fromRadioChar;
    QLowEnergyCharacteristic m_toRadioChar;
    QLowEnergyCharacteristic m_fromNumChar;

    QTimer *m_reconnectTimer;
    QBluetoothDeviceInfo m_lastDevice;
    bool m_intentionalDisconnect;
    bool m_connected;
    QString m_deviceName;

    void ensureDiscoveryAgent();
    void enableNotifications();
    void readFromRadio();

    static const int RECONNECT_INTERVAL_MS = 3000;
};

#endif // BLUETOOTHCONNECTION_H
