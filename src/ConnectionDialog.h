#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QBluetoothDeviceInfo>

class BluetoothConnection;

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    enum class ConnectionType {
        None,
        Serial,
        Tcp,
        Bluetooth
    };

    struct ConnectionResult {
        ConnectionType type = ConnectionType::None;
        QString serialPort;
        QString tcpHost;
        quint16 tcpPort = 4403;
        QBluetoothDeviceInfo btDevice;
    };

    explicit ConnectionDialog(BluetoothConnection *bluetooth, QWidget *parent = nullptr);
    ~ConnectionDialog();

    ConnectionResult result() const;

public slots:
    void onBtDeviceDiscovered(const QString &name, const QString &address,
                              const QBluetoothDeviceInfo &info);
    void onBtScanFinished();

private slots:
    void refreshSerialPorts();
    void startBtScan();
    void onConnectClicked();

private:
    void setupSerialTab();
    void setupTcpTab();
    void setupBluetoothTab();

    BluetoothConnection *m_bluetooth;
    ConnectionResult m_result;

    QTabWidget *m_tabWidget;

    // Serial tab
    QComboBox *m_serialPortCombo;

    // TCP tab
    QLineEdit *m_tcpHostEdit;

    // Bluetooth tab
    QPushButton *m_btScanButton;
    QListWidget *m_btDeviceList;
    QLabel *m_btStatusLabel;
};

#endif // CONNECTIONDIALOG_H
