#include "ConnectionDialog.h"
#include "BluetoothConnection.h"
#include "SerialConnection.h"
#include "AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

ConnectionDialog::ConnectionDialog(BluetoothConnection *bluetooth, QWidget *parent)
    : QDialog(parent), m_bluetooth(bluetooth)
{
    setWindowTitle("Connect to Device");
    setMinimumSize(450, 300);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_tabWidget = new QTabWidget;
    mainLayout->addWidget(m_tabWidget);

    setupSerialTab();
    setupTcpTab();
    setupBluetoothTab();

    // Connect / Cancel buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox;
    QPushButton *connectBtn = buttonBox->addButton("Connect", QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::onConnectClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Pre-populate
    refreshSerialPorts();
}

ConnectionDialog::~ConnectionDialog()
{
    m_bluetooth->stopScan();
}

ConnectionDialog::ConnectionResult ConnectionDialog::result() const
{
    return m_result;
}

void ConnectionDialog::setupSerialTab()
{
    QWidget *tab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *topRow = new QHBoxLayout;
    QLabel *label = new QLabel("Serial Port:");
    topRow->addWidget(label);
    topRow->addStretch();
    QPushButton *refreshBtn = new QPushButton("Refresh");
    connect(refreshBtn, &QPushButton::clicked, this, &ConnectionDialog::refreshSerialPorts);
    topRow->addWidget(refreshBtn);
    layout->addLayout(topRow);

    m_serialPortCombo = new QComboBox;
    m_serialPortCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_serialPortCombo);

    layout->addStretch();

    m_tabWidget->addTab(tab, "Serial");
}

void ConnectionDialog::setupTcpTab()
{
    QWidget *tab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QLabel *label = new QLabel("Host address:");
    layout->addWidget(label);

    m_tcpHostEdit = new QLineEdit;
    m_tcpHostEdit->setPlaceholderText("192.168.1.x:4403");
    layout->addWidget(m_tcpHostEdit);

    // Restore last host
    QString lastHost = AppSettings::instance()->lastTcpHost();
    if (!lastHost.isEmpty())
        m_tcpHostEdit->setText(lastHost);

    QLabel *hint = new QLabel("Default port is 4403 if not specified.");
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    layout->addStretch();

    m_tabWidget->addTab(tab, "TCP");
}

void ConnectionDialog::setupBluetoothTab()
{
    QWidget *tab = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *topRow = new QHBoxLayout;
    m_btScanButton = new QPushButton("Scan for Devices");
    connect(m_btScanButton, &QPushButton::clicked, this, &ConnectionDialog::startBtScan);
    topRow->addWidget(m_btScanButton);

    m_btStatusLabel = new QLabel;
    topRow->addWidget(m_btStatusLabel);
    topRow->addStretch();
    layout->addLayout(topRow);

    m_btDeviceList = new QListWidget;
    m_btDeviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_btDeviceList);

    m_tabWidget->addTab(tab, "Bluetooth");
}

void ConnectionDialog::refreshSerialPorts()
{
    m_serialPortCombo->clear();

    // Detected Meshtastic devices first
    QList<QSerialPortInfo> meshtasticPorts = SerialConnection::detectMeshtasticDevices();
    for (const QSerialPortInfo &info : meshtasticPorts)
    {
        QString label = QString("%1 - %2 [Meshtastic]")
                            .arg(info.portName())
                            .arg(SerialConnection::deviceDescription(info));
        m_serialPortCombo->addItem(label, info.portName());
    }

    // Other ports
    QList<QSerialPortInfo> allPorts = SerialConnection::availablePorts();
    for (const QSerialPortInfo &info : allPorts)
    {
        bool isMeshtastic = false;
        for (const QSerialPortInfo &mesh : meshtasticPorts)
        {
            if (mesh.portName() == info.portName())
            {
                isMeshtastic = true;
                break;
            }
        }
        if (isMeshtastic)
            continue;

        QString label = QString("%1 - %2")
                            .arg(info.portName())
                            .arg(SerialConnection::deviceDescription(info));
        m_serialPortCombo->addItem(label, info.portName());
    }

    if (m_serialPortCombo->count() == 0)
    {
        m_serialPortCombo->addItem("No ports found", QString());
    }
}

void ConnectionDialog::startBtScan()
{
    m_btDeviceList->clear();
    m_btScanButton->setEnabled(false);
    m_btScanButton->setText("Scanning...");
    m_btStatusLabel->setText("Searching for devices...");
    m_bluetooth->startScan();
}

void ConnectionDialog::onBtDeviceDiscovered(const QString &name, const QString &address,
                                             const QBluetoothDeviceInfo &info)
{
    // Check for duplicates
    for (int i = 0; i < m_btDeviceList->count(); i++)
    {
        if (m_btDeviceList->item(i)->data(Qt::UserRole + 1).toString() == address)
            return;
    }

    QListWidgetItem *item = new QListWidgetItem(QString("%1 (%2)").arg(name, address));
    item->setData(Qt::UserRole, QVariant::fromValue(info));
    item->setData(Qt::UserRole + 1, address);
    m_btDeviceList->addItem(item);
    m_btStatusLabel->setText(QString("Found %1 device(s)").arg(m_btDeviceList->count()));
}

void ConnectionDialog::onBtScanFinished()
{
    m_btScanButton->setEnabled(true);
    m_btScanButton->setText("Scan for Devices");
    if (m_btDeviceList->count() == 0)
        m_btStatusLabel->setText("No devices found");
    else
        m_btStatusLabel->setText(QString("Found %1 device(s)").arg(m_btDeviceList->count()));
}

void ConnectionDialog::onConnectClicked()
{
    int tabIndex = m_tabWidget->currentIndex();

    if (tabIndex == 0) // Serial
    {
        QString portName = m_serialPortCombo->currentData().toString();
        if (portName.isEmpty())
        {
            QMessageBox::warning(this, "Error", "No serial port selected");
            return;
        }
        m_result.type = ConnectionType::Serial;
        m_result.serialPort = portName;
    }
    else if (tabIndex == 1) // TCP
    {
        QString input = m_tcpHostEdit->text().trimmed();
        if (input.isEmpty())
        {
            QMessageBox::warning(this, "Error", "Enter a host address");
            return;
        }

        QString host;
        quint16 port = 4403;

        // Parse "host:port" or just "host"
        int colonIdx = input.lastIndexOf(':');
        if (colonIdx > 0)
        {
            bool ok;
            quint16 parsedPort = input.mid(colonIdx + 1).toUShort(&ok);
            if (ok)
            {
                host = input.left(colonIdx);
                port = parsedPort;
            }
            else
            {
                host = input;
            }
        }
        else
        {
            host = input;
        }

        m_result.type = ConnectionType::Tcp;
        m_result.tcpHost = host;
        m_result.tcpPort = port;
    }
    else if (tabIndex == 2) // Bluetooth
    {
        QListWidgetItem *selected = m_btDeviceList->currentItem();
        if (!selected)
        {
            QMessageBox::warning(this, "Error", "No Bluetooth device selected. Scan first.");
            return;
        }

        QBluetoothDeviceInfo deviceInfo = selected->data(Qt::UserRole).value<QBluetoothDeviceInfo>();
        if (!deviceInfo.isValid())
        {
            QMessageBox::warning(this, "Error", "Invalid device selection");
            return;
        }

        m_result.type = ConnectionType::Bluetooth;
        m_result.btDevice = deviceInfo;
    }

    accept();
}
