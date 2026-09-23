#include "RadioConfigTab.h"
#include "Theme.h"
#include "DeviceConfig.h"
#include "EnumCombo.h"

#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

RadioConfigTab::RadioConfigTab(DeviceConfig *config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    setupUI();

    connect(m_config, &DeviceConfig::loraConfigChanged,
            this, &RadioConfigTab::onConfigReceived);

    // Update UI if config already received
    if (m_config->hasLoRaConfig()) {
        updateUIFromConfig();
    }

    // Show timeout message if config not received after 5 seconds
    QTimer::singleShot(5000, this, [this]() {
        if (!m_config->hasLoRaConfig()) {
            m_statusLabel->setText("Config not available from device");
            m_statusLabel->setStyleSheet("color: orange;");
        }
    });
}

void RadioConfigTab::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Radio Settings group
    QGroupBox *radioGroup = new QGroupBox("LoRa Radio Settings");
    QFormLayout *radioLayout = new QFormLayout(radioGroup);

    // Region
    m_regionCombo = new QComboBox;
    populateEnumCombo(m_regionCombo, DeviceConfig::regionOptions(), true);
    radioLayout->addRow("Region:", m_regionCombo);

    // Modem Preset
    m_presetCombo = new QComboBox;
    populateEnumCombo(m_presetCombo, DeviceConfig::modemPresetOptions());
    radioLayout->addRow("Modem Preset:", m_presetCombo);

    // Hop Limit
    m_hopLimitSpin = new QSpinBox;
    m_hopLimitSpin->setRange(1, 7);
    m_hopLimitSpin->setValue(3);
    m_hopLimitSpin->setToolTip("Maximum number of hops for messages (1-7)");
    radioLayout->addRow("Hop Limit:", m_hopLimitSpin);

    // TX Power
    m_txPowerSpin = new QSpinBox;
    m_txPowerSpin->setRange(0, 30);
    m_txPowerSpin->setValue(0);
    m_txPowerSpin->setSuffix(" dBm");
    m_txPowerSpin->setSpecialValueText("Default");
    m_txPowerSpin->setToolTip("Transmit power in dBm (0 = device default)");
    radioLayout->addRow("TX Power:", m_txPowerSpin);

    // TX Enabled
    m_txEnabledCheck = new QCheckBox("Enable Transmit");
    m_txEnabledCheck->setChecked(true);
    m_txEnabledCheck->setToolTip("If disabled, device will only receive (listen-only mode)");
    radioLayout->addRow("", m_txEnabledCheck);

    mainLayout->addWidget(radioGroup);

    // Advanced Settings group
    QGroupBox *advGroup = new QGroupBox("Advanced Settings");
    QFormLayout *advLayout = new QFormLayout(advGroup);

    // Channel Number
    m_channelNumSpin = new QSpinBox;
    m_channelNumSpin->setRange(0, 100);
    m_channelNumSpin->setValue(0);
    m_channelNumSpin->setSpecialValueText("Auto");
    m_channelNumSpin->setToolTip("Frequency slot within the region (0 = auto)");
    advLayout->addRow("Channel Number:", m_channelNumSpin);

    // Frequency Offset
    m_freqOffsetSpin = new QDoubleSpinBox;
    m_freqOffsetSpin->setRange(-1000000.0, 1000000.0);
    m_freqOffsetSpin->setValue(0.0);
    m_freqOffsetSpin->setSuffix(" Hz");
    m_freqOffsetSpin->setDecimals(0);
    m_freqOffsetSpin->setToolTip("Fine frequency adjustment in Hz");
    advLayout->addRow("Frequency Offset:", m_freqOffsetSpin);

    // Override Duty Cycle
    m_overrideDutyCycleCheck = new QCheckBox("Override Duty Cycle Limit");
    m_overrideDutyCycleCheck->setToolTip("WARNING: May violate regulations in your region");
    advLayout->addRow("", m_overrideDutyCycleCheck);

    mainLayout->addWidget(advGroup);

    // Status and Save
    QHBoxLayout *bottomLayout = new QHBoxLayout;

    m_statusLabel = new QLabel("Waiting for device config...");
    m_statusLabel->setStyleSheet(Theme::statusLabelStyle());
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_saveButton = new QPushButton("Save to Device");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &RadioConfigTab::onSaveClicked);
    bottomLayout->addWidget(m_saveButton);

    mainLayout->addLayout(bottomLayout);
    mainLayout->addStretch();
}

void RadioConfigTab::notifySaved()
{
    m_statusLabel->setText("Saved \u2713");
    m_statusLabel->setStyleSheet("color: green;");
}

void RadioConfigTab::onConfigReceived()
{
    updateUIFromConfig();
    m_statusLabel->setText("Config received from device");
    m_statusLabel->setStyleSheet("color: green;");
    m_saveButton->setEnabled(true);
}

void RadioConfigTab::updateUIFromConfig()
{
    const auto &lora = m_config->loraConfig();

    selectEnumValue(m_regionCombo, DeviceConfig::regionOptions(), lora.region);
    selectEnumValue(m_presetCombo, DeviceConfig::modemPresetOptions(), lora.modemPreset);
    // With use_preset off the device runs custom bandwidth / spreading factor /
    // coding rate, and the preset is ignored. This tab does not edit those, so
    // leave the device on its custom settings rather than switching to a preset.
    m_presetCombo->setEnabled(lora.usePreset);
    m_presetCombo->setToolTip(lora.usePreset
        ? QString()
        : QString("The device uses custom modem settings (BW %1 kHz, SF %2, CR 4/%3); "
                  "the preset is ignored.").arg(lora.bandwidth).arg(lora.spreadFactor).arg(lora.codingRate));
    m_hopLimitSpin->setValue(lora.hopLimit);
    m_txPowerSpin->setValue(lora.txPower);
    m_txEnabledCheck->setChecked(lora.txEnabled);
    m_channelNumSpin->setValue(lora.channelNum);
    m_freqOffsetSpin->setValue(lora.frequencyOffset);
    m_overrideDutyCycleCheck->setChecked(lora.overrideDutyCycle);
}

void RadioConfigTab::onSaveClicked()
{
    // Start from the device's config so fields this tab doesn't show survive
    DeviceConfig::LoRaConfig lora = m_config->loraConfig();
    lora.region = enumComboValue(m_regionCombo);
    lora.modemPreset = enumComboValue(m_presetCombo);
    lora.hopLimit = m_hopLimitSpin->value();
    lora.txPower = m_txPowerSpin->value();
    lora.txEnabled = m_txEnabledCheck->isChecked();
    lora.channelNum = m_channelNumSpin->value();
    lora.frequencyOffset = m_freqOffsetSpin->value();
    lora.overrideDutyCycle = m_overrideDutyCycleCheck->isChecked();

    m_config->setLoRaConfig(lora);

    m_statusLabel->setText("Saving...");
    m_statusLabel->setStyleSheet("color: orange;");

    emit saveRequested();
}
