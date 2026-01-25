#include "DeviceConfigTab.h"
#include "DeviceConfig.h"

#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

DeviceConfigTab::DeviceConfigTab(DeviceConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setupUI();

    connect(m_config, &DeviceConfig::deviceConfigChanged,
            this, &DeviceConfigTab::onConfigReceived);

    if (m_config->hasDeviceConfig()) {
        updateUIFromConfig();
    }

    // Show timeout message if config not received after 5 seconds
    QTimer::singleShot(5000, this, [this]() {
        if (!m_config->hasDeviceConfig()) {
            m_statusLabel->setText("Config not available from device");
            m_statusLabel->setStyleSheet("color: orange;");
        }
    });
}

void DeviceConfigTab::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Device Role group
    QGroupBox *roleGroup = new QGroupBox("Device Role");
    QFormLayout *roleLayout = new QFormLayout(roleGroup);

    m_roleCombo = new QComboBox;
    m_roleCombo->addItems(DeviceConfig::deviceRoleNames());
    m_roleCombo->setToolTip(
        "Client: Normal node that can send/receive messages\n"
        "Client Mute: Receives but doesn't rebroadcast\n"
        "Router: Optimized for routing, minimal power use\n"
        "Router Client: Router that also acts as a client\n"
        "Repeater: Only rebroadcasts, no user interaction\n"
        "Tracker: GPS tracker mode\n"
        "Sensor: Environmental sensor node"
    );
    roleLayout->addRow("Role:", m_roleCombo);

    m_nodeInfoIntervalSpin = new QSpinBox;
    m_nodeInfoIntervalSpin->setRange(60, 86400);
    m_nodeInfoIntervalSpin->setValue(900);
    m_nodeInfoIntervalSpin->setSuffix(" seconds");
    m_nodeInfoIntervalSpin->setToolTip("How often to broadcast node info (name, hardware, etc.)");
    roleLayout->addRow("Node Info Interval:", m_nodeInfoIntervalSpin);

    mainLayout->addWidget(roleGroup);

    // Serial/Debug group
    QGroupBox *serialGroup = new QGroupBox("Serial & Debug");
    QVBoxLayout *serialLayout = new QVBoxLayout(serialGroup);

    m_serialEnabledCheck = new QCheckBox("Enable Serial Output");
    m_serialEnabledCheck->setToolTip("Enable serial port output for debugging/API");
    serialLayout->addWidget(m_serialEnabledCheck);

    m_debugLogCheck = new QCheckBox("Enable Debug Logging");
    m_debugLogCheck->setToolTip("Enable verbose debug logging to serial");
    serialLayout->addWidget(m_debugLogCheck);

    m_ledHeartbeatCheck = new QCheckBox("Disable LED Heartbeat");
    m_ledHeartbeatCheck->setToolTip("Disable the LED heartbeat blink");
    serialLayout->addWidget(m_ledHeartbeatCheck);

    mainLayout->addWidget(serialGroup);

    // Button Behavior group
    QGroupBox *buttonGroup = new QGroupBox("Button Behavior");
    QVBoxLayout *buttonLayout = new QVBoxLayout(buttonGroup);

    m_doubleTapCheck = new QCheckBox("Double Tap as Button Press");
    m_doubleTapCheck->setToolTip("Treat accelerometer double-tap as button press");
    buttonLayout->addWidget(m_doubleTapCheck);

    m_disableTripleClickCheck = new QCheckBox("Disable Triple Click");
    m_disableTripleClickCheck->setToolTip("Disable triple-click to enter admin mode");
    buttonLayout->addWidget(m_disableTripleClickCheck);

    mainLayout->addWidget(buttonGroup);

    // Timezone group
    QGroupBox *tzGroup = new QGroupBox("Time Settings");
    QFormLayout *tzLayout = new QFormLayout(tzGroup);

    m_timezoneEdit = new QLineEdit;
    m_timezoneEdit->setPlaceholderText("e.g., EST5EDT,M3.2.0,M11.1.0");
    m_timezoneEdit->setToolTip("POSIX timezone definition string");
    tzLayout->addRow("Timezone:", m_timezoneEdit);

    mainLayout->addWidget(tzGroup);

    // Status and Save
    QHBoxLayout *bottomLayout = new QHBoxLayout;

    m_statusLabel = new QLabel("Waiting for device config...");
    m_statusLabel->setStyleSheet("color: gray;");
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_saveButton = new QPushButton("Save to Device");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &DeviceConfigTab::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    mainLayout->addLayout(bottomLayout);
    mainLayout->addStretch();
}

void DeviceConfigTab::onConfigReceived()
{
    updateUIFromConfig();
    m_statusLabel->setText("Config received from device");
    m_statusLabel->setStyleSheet("color: green;");
    m_saveButton->setEnabled(true);
}

void DeviceConfigTab::updateUIFromConfig()
{
    const auto &device = m_config->deviceConfig();

    m_roleCombo->setCurrentIndex(device.role);
    m_nodeInfoIntervalSpin->setValue(device.nodeInfoBroadcastSecs);
    m_serialEnabledCheck->setChecked(device.serialEnabled);
    m_debugLogCheck->setChecked(device.debugLogEnabled);
    m_ledHeartbeatCheck->setChecked(device.ledHeartbeatDisabled);
    m_doubleTapCheck->setChecked(device.doubleTapAsButtonPress);
    m_disableTripleClickCheck->setChecked(device.disableTripleClick);
    m_timezoneEdit->setText(device.tzdef);
}

void DeviceConfigTab::onSaveClicked()
{
    DeviceConfig::DeviceSettings device;
    device.role = m_roleCombo->currentIndex();
    device.nodeInfoBroadcastSecs = m_nodeInfoIntervalSpin->value();
    device.serialEnabled = m_serialEnabledCheck->isChecked();
    device.debugLogEnabled = m_debugLogCheck->isChecked();
    device.ledHeartbeatDisabled = m_ledHeartbeatCheck->isChecked();
    device.doubleTapAsButtonPress = m_doubleTapCheck->isChecked();
    device.disableTripleClick = m_disableTripleClickCheck->isChecked();
    device.tzdef = m_timezoneEdit->text();

    m_config->setDeviceConfig(device);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    emit saveRequested();
}
