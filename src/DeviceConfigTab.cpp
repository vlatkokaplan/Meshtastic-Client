#include "DeviceConfigTab.h"
#include "Theme.h"
#include "DeviceConfig.h"
#include "EnumCombo.h"

#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>

DeviceConfigTab::DeviceConfigTab(DeviceConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setupUI();

    connect(m_config, &DeviceConfig::deviceConfigChanged,
            this, &DeviceConfigTab::onConfigReceived);
    connect(m_config, &DeviceConfig::securityConfigChanged,
            this, &DeviceConfigTab::updateUIFromConfig);

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
    populateEnumCombo(m_roleCombo, DeviceConfig::deviceRoleOptions());
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

    // Both live in SecurityConfig on current firmware (the DeviceConfig
    // fields of the same name are deprecated and ignored). Disabled until the
    // device has sent its security config, since saving needs it as a base.
    m_serialEnabledCheck = new QCheckBox("Enable Serial API");
    m_serialEnabledCheck->setToolTip("Allow clients to connect over the USB/serial port");
    m_serialEnabledCheck->setEnabled(false);
    serialLayout->addWidget(m_serialEnabledCheck);

    m_debugLogApiCheck = new QCheckBox("Send Debug Logs to Clients");
    m_debugLogApiCheck->setToolTip("Stream the device's debug log to connected apps");
    m_debugLogApiCheck->setEnabled(false);
    serialLayout->addWidget(m_debugLogApiCheck);

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

    // Device Actions group
    QGroupBox *actionsGroup = new QGroupBox("Device Actions");
    QHBoxLayout *actionsLayout = new QHBoxLayout(actionsGroup);

    m_rebootButton = new QPushButton("Reboot Device");
    m_rebootButton->setToolTip("Reboot the device (5 second delay)");
    connect(m_rebootButton, &QPushButton::clicked, this, &DeviceConfigTab::onRebootClicked);
    actionsLayout->addWidget(m_rebootButton);

    m_factoryResetButton = new QPushButton("Factory Reset");
    m_factoryResetButton->setToolTip("Reset device to factory defaults (WARNING: erases all settings!)");
    m_factoryResetButton->setProperty("danger", true);  // styled by Theme
    connect(m_factoryResetButton, &QPushButton::clicked, this, &DeviceConfigTab::onFactoryResetClicked);
    actionsLayout->addWidget(m_factoryResetButton);

    actionsLayout->addStretch();
    mainLayout->addWidget(actionsGroup);

    // Status and Save
    QHBoxLayout *bottomLayout = new QHBoxLayout;

    m_statusLabel = new QLabel("Waiting for device config...");
    m_statusLabel->setStyleSheet(Theme::statusLabelStyle());
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_saveButton = new QPushButton("Save to Device");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &DeviceConfigTab::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    mainLayout->addLayout(bottomLayout);
    mainLayout->addStretch();
}

void DeviceConfigTab::notifySaved()
{
    m_statusLabel->setText("Saved \u2713");
    m_statusLabel->setStyleSheet("color: green;");
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

    selectEnumValue(m_roleCombo, DeviceConfig::deviceRoleOptions(), device.role);
    m_nodeInfoIntervalSpin->setValue(device.nodeInfoBroadcastSecs);
    const auto security = m_config->securityConfig();
    m_serialEnabledCheck->setEnabled(m_config->hasSecurityConfig());
    m_debugLogApiCheck->setEnabled(m_config->hasSecurityConfig());
    m_serialEnabledCheck->setChecked(security.serialEnabled);
    m_debugLogApiCheck->setChecked(security.debugLogApiEnabled);
    m_ledHeartbeatCheck->setChecked(device.ledHeartbeatDisabled);
    m_doubleTapCheck->setChecked(device.doubleTapAsButtonPress);
    m_disableTripleClickCheck->setChecked(device.disableTripleClick);
    m_timezoneEdit->setText(device.tzdef);
}

void DeviceConfigTab::onSaveClicked()
{
    // Start from the device's config so fields this tab doesn't show survive
    DeviceConfig::DeviceSettings device = m_config->deviceConfig();
    device.role = enumComboValue(m_roleCombo);
    device.nodeInfoBroadcastSecs = m_nodeInfoIntervalSpin->value();
    device.ledHeartbeatDisabled = m_ledHeartbeatCheck->isChecked();
    device.doubleTapAsButtonPress = m_doubleTapCheck->isChecked();
    device.disableTripleClick = m_disableTripleClickCheck->isChecked();
    device.tzdef = m_timezoneEdit->text();

    // Read every widget before storing anything: each set* emits a change
    // signal that refreshes this tab from the stored config.
    DeviceConfig::SecuritySettings security = m_config->securityConfig();
    security.serialEnabled = m_serialEnabledCheck->isChecked();
    security.debugLogApiEnabled = m_debugLogApiCheck->isChecked();

    m_config->setDeviceConfig(device);
    if (m_config->hasSecurityConfig())
        m_config->setSecurityConfig(security);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    emit saveRequested();
}

void DeviceConfigTab::onRebootClicked()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Reboot Device",
        "Are you sure you want to reboot the device?\n\n"
        "The device will restart in 5 seconds.",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        m_statusLabel->setText("Rebooting device...");
        m_statusLabel->setStyleSheet("color: orange;");
        emit rebootRequested();
    }
}

void DeviceConfigTab::onFactoryResetClicked()
{
    QMessageBox box(QMessageBox::Warning, "Factory Reset",
                    "Reset the device to factory defaults?", QMessageBox::NoButton, this);
    box.setInformativeText(
        "All settings, channels and the node list are erased, and the device "
        "reboots. Its encryption keys are regenerated too, so other nodes will "
        "see it as a new node and earlier direct messages cannot be decrypted.\n\n"
        "This cannot be undone.");
    QPushButton *settings = box.addButton("Reset Settings", QMessageBox::DestructiveRole);
    settings->setToolTip("Keep Bluetooth pairings with phones");
    QPushButton *everything = box.addButton("Reset Everything", QMessageBox::DestructiveRole);
    everything->setToolTip("Also forget Bluetooth pairings");
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.exec();

    if (box.clickedButton() != settings && box.clickedButton() != everything)
        return;

    m_statusLabel->setText("Factory reset sent...");
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(Theme::palette().danger.name()));
    emit factoryResetRequested(box.clickedButton() == everything);
}
