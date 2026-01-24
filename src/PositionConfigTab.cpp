#include "PositionConfigTab.h"
#include "DeviceConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

PositionConfigTab::PositionConfigTab(DeviceConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setupUI();

    connect(m_config, &DeviceConfig::positionConfigChanged,
            this, &PositionConfigTab::onConfigReceived);

    if (m_config->hasPositionConfig()) {
        updateUIFromConfig();
    }
}

void PositionConfigTab::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // GPS Settings group
    QGroupBox *gpsGroup = new QGroupBox("GPS Settings");
    QFormLayout *gpsLayout = new QFormLayout(gpsGroup);

    m_gpsModeCombo = new QComboBox;
    m_gpsModeCombo->addItems(DeviceConfig::gpsModeNames());
    m_gpsModeCombo->setToolTip("GPS mode: Disabled, Enabled, or Not Present");
    gpsLayout->addRow("GPS Mode:", m_gpsModeCombo);

    m_gpsUpdateIntervalSpin = new QSpinBox;
    m_gpsUpdateIntervalSpin->setRange(0, 86400);
    m_gpsUpdateIntervalSpin->setValue(120);
    m_gpsUpdateIntervalSpin->setSuffix(" seconds");
    m_gpsUpdateIntervalSpin->setToolTip("How often to poll the GPS for position updates");
    gpsLayout->addRow("GPS Update Interval:", m_gpsUpdateIntervalSpin);

    m_gpsAttemptTimeSpin = new QSpinBox;
    m_gpsAttemptTimeSpin->setRange(0, 600);
    m_gpsAttemptTimeSpin->setValue(120);
    m_gpsAttemptTimeSpin->setSuffix(" seconds");
    m_gpsAttemptTimeSpin->setToolTip("Maximum time to wait for GPS fix");
    gpsLayout->addRow("GPS Attempt Time:", m_gpsAttemptTimeSpin);

    m_fixedPositionCheck = new QCheckBox("Use Fixed Position");
    m_fixedPositionCheck->setToolTip("Use a manually set fixed position instead of GPS");
    gpsLayout->addRow("", m_fixedPositionCheck);

    mainLayout->addWidget(gpsGroup);

    // Broadcast Settings group
    QGroupBox *broadcastGroup = new QGroupBox("Position Broadcast");
    QFormLayout *broadcastLayout = new QFormLayout(broadcastGroup);

    m_broadcastIntervalSpin = new QSpinBox;
    m_broadcastIntervalSpin->setRange(0, 86400);
    m_broadcastIntervalSpin->setValue(900);
    m_broadcastIntervalSpin->setSuffix(" seconds");
    m_broadcastIntervalSpin->setSpecialValueText("Disabled");
    m_broadcastIntervalSpin->setToolTip("How often to broadcast position (0 = disabled)");
    broadcastLayout->addRow("Broadcast Interval:", m_broadcastIntervalSpin);

    mainLayout->addWidget(broadcastGroup);

    // Smart Position group
    QGroupBox *smartGroup = new QGroupBox("Smart Position");
    QVBoxLayout *smartMainLayout = new QVBoxLayout(smartGroup);

    m_smartPositionCheck = new QCheckBox("Enable Smart Position Broadcast");
    m_smartPositionCheck->setToolTip("Only broadcast position when movement is detected");
    connect(m_smartPositionCheck, &QCheckBox::toggled,
            this, &PositionConfigTab::onSmartPositionToggled);
    smartMainLayout->addWidget(m_smartPositionCheck);

    QFormLayout *smartLayout = new QFormLayout;

    m_smartMinDistanceSpin = new QSpinBox;
    m_smartMinDistanceSpin->setRange(0, 10000);
    m_smartMinDistanceSpin->setValue(100);
    m_smartMinDistanceSpin->setSuffix(" meters");
    m_smartMinDistanceSpin->setToolTip("Minimum distance moved before broadcasting");
    smartLayout->addRow("Min Distance:", m_smartMinDistanceSpin);

    m_smartMinIntervalSpin = new QSpinBox;
    m_smartMinIntervalSpin->setRange(0, 3600);
    m_smartMinIntervalSpin->setValue(30);
    m_smartMinIntervalSpin->setSuffix(" seconds");
    m_smartMinIntervalSpin->setToolTip("Minimum time between smart broadcasts");
    smartLayout->addRow("Min Interval:", m_smartMinIntervalSpin);

    smartMainLayout->addLayout(smartLayout);
    mainLayout->addWidget(smartGroup);

    // Status and Save
    QHBoxLayout *bottomLayout = new QHBoxLayout;

    m_statusLabel = new QLabel("Waiting for device config...");
    m_statusLabel->setStyleSheet("color: gray;");
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_saveButton = new QPushButton("Save to Device");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &PositionConfigTab::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    mainLayout->addLayout(bottomLayout);
    mainLayout->addStretch();
}

void PositionConfigTab::onConfigReceived()
{
    updateUIFromConfig();
    m_statusLabel->setText("Config received from device");
    m_statusLabel->setStyleSheet("color: green;");
    m_saveButton->setEnabled(true);
}

void PositionConfigTab::updateUIFromConfig()
{
    const auto &pos = m_config->positionConfig();

    m_gpsModeCombo->setCurrentIndex(pos.gpsMode);
    m_gpsUpdateIntervalSpin->setValue(pos.gpsUpdateInterval);
    m_gpsAttemptTimeSpin->setValue(pos.gpsAttemptTime);
    m_fixedPositionCheck->setChecked(pos.fixedPosition);
    m_broadcastIntervalSpin->setValue(pos.positionBroadcastSecs);
    m_smartPositionCheck->setChecked(pos.smartPositionEnabled);
    m_smartMinDistanceSpin->setValue(pos.broadcastSmartMinDistance);
    m_smartMinIntervalSpin->setValue(pos.broadcastSmartMinIntervalSecs);

    onSmartPositionToggled(pos.smartPositionEnabled);
}

void PositionConfigTab::onSmartPositionToggled(bool checked)
{
    m_smartMinDistanceSpin->setEnabled(checked);
    m_smartMinIntervalSpin->setEnabled(checked);
}

void PositionConfigTab::onSaveClicked()
{
    DeviceConfig::PositionSettings pos;
    pos.gpsMode = m_gpsModeCombo->currentIndex();
    pos.gpsUpdateInterval = m_gpsUpdateIntervalSpin->value();
    pos.gpsAttemptTime = m_gpsAttemptTimeSpin->value();
    pos.fixedPosition = m_fixedPositionCheck->isChecked();
    pos.positionBroadcastSecs = m_broadcastIntervalSpin->value();
    pos.smartPositionEnabled = m_smartPositionCheck->isChecked();
    pos.broadcastSmartMinDistance = m_smartMinDistanceSpin->value();
    pos.broadcastSmartMinIntervalSecs = m_smartMinIntervalSpin->value();
    pos.gpsEnabled = (pos.gpsMode == 1);  // Mode 1 = Enabled

    m_config->setPositionConfig(pos);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    emit saveRequested();
}
