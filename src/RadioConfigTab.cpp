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

    // Modem: a named preset, or custom bandwidth / spreading factor / coding rate
    m_usePresetCheck = new QCheckBox("Use modem preset");
    m_usePresetCheck->setChecked(true);
    m_usePresetCheck->setToolTip("Presets are what almost every mesh uses. Turn off only "
                                 "to match a mesh that runs custom modem settings.");
    radioLayout->addRow("", m_usePresetCheck);

    m_presetCombo = new QComboBox;
    populateEnumCombo(m_presetCombo, DeviceConfig::modemPresetOptions());
    radioLayout->addRow("Modem Preset:", m_presetCombo);

    m_bandwidthCombo = new QComboBox;
    for (const auto &bw : DeviceConfig::bandwidthOptions())
        m_bandwidthCombo->addItem(bw.label, bw.code);
    radioLayout->addRow("Bandwidth:", m_bandwidthCombo);

    m_spreadFactorSpin = new QSpinBox;
    m_spreadFactorSpin->setRange(5, 12);  // firmware LORA_SF_MIN..MAX
    m_spreadFactorSpin->setValue(11);
    m_spreadFactorSpin->setToolTip("Higher reaches further but takes longer on air");
    radioLayout->addRow("Spreading Factor:", m_spreadFactorSpin);

    m_codingRateCombo = new QComboBox;
    for (int cr = 5; cr <= 8; ++cr)
        m_codingRateCombo->addItem(QString("4/%1").arg(cr), cr);
    radioLayout->addRow("Coding Rate:", m_codingRateCombo);

    m_customWarning = new QLabel(
        "Custom settings only reach nodes using exactly the same bandwidth, "
        "spreading factor and coding rate. An unnamed primary channel is also "
        "renamed \"Custom\", which changes its hash.");
    m_customWarning->setWordWrap(true);
    m_customWarning->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    m_customWarning->setStyleSheet(QString("color: %1;").arg(Theme::palette().warning.name()));
    // Spans both columns: a word-wrapped label in the field column gets
    // its height from the wrong width and clips the last line
    radioLayout->addRow(m_customWarning);

    connect(m_usePresetCheck, &QCheckBox::toggled, this, &RadioConfigTab::onUsePresetToggled);
    // While on a preset, keep the (disabled) custom fields showing what that
    // preset means, so switching to custom starts from the same settings
    auto trackPreset = [this]() {
        if (m_usePresetCheck->isChecked())
            fillCustomFromPreset();
    };
    connect(m_presetCombo, &QComboBox::currentIndexChanged, this, trackPreset);
    connect(m_regionCombo, &QComboBox::currentIndexChanged, this, trackPreset);
    onUsePresetToggled(true);

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

    // In preset mode the device usually reports 0 for BW/SF/CR; show what the
    // preset actually uses, so switching to custom starts from there.
    if (lora.usePreset || lora.bandwidth == 0) {
        fillCustomFromPreset();
    } else {
        selectBandwidth(lora.bandwidth);
        m_spreadFactorSpin->setValue(lora.spreadFactor);
        const int cr = m_codingRateCombo->findData(lora.codingRate);
        m_codingRateCombo->setCurrentIndex(cr >= 0 ? cr : 0);
    }
    const QSignalBlocker block(m_usePresetCheck);
    m_usePresetCheck->setChecked(lora.usePreset);
    onUsePresetToggled(lora.usePreset);
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
    lora.usePreset = m_usePresetCheck->isChecked();
    if (!lora.usePreset) {
        lora.bandwidth = m_bandwidthCombo->currentData().toInt();
        lora.spreadFactor = m_spreadFactorSpin->value();
        lora.codingRate = m_codingRateCombo->currentData().toInt();
    }
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

void RadioConfigTab::onUsePresetToggled(bool usePreset)
{
    m_presetCombo->setEnabled(usePreset);
    m_bandwidthCombo->setEnabled(!usePreset);
    m_spreadFactorSpin->setEnabled(!usePreset);
    m_codingRateCombo->setEnabled(!usePreset);
    m_customWarning->setVisible(!usePreset);
    m_customWarning->updateGeometry();  // re-measure the wrapped height once shown
}

void RadioConfigTab::fillCustomFromPreset()
{
    const bool wideLora = enumComboValue(m_regionCombo) == DeviceConfig::REGION_LORA_24;
    int bw = 0, sf = 0, cr = 0;
    DeviceConfig::presetModemParams(enumComboValue(m_presetCombo), wideLora, bw, sf, cr);
    selectBandwidth(bw);
    m_spreadFactorSpin->setValue(sf);
    m_codingRateCombo->setCurrentIndex(m_codingRateCombo->findData(cr));
}

// Selects `code`, adding it if the device uses a width this list lacks, so
// saving never silently changes it.
void RadioConfigTab::selectBandwidth(int code)
{
    int idx = m_bandwidthCombo->findData(code);
    if (idx < 0) {
        m_bandwidthCombo->addItem(QString("%1 kHz").arg(code), code);
        idx = m_bandwidthCombo->count() - 1;
    }
    m_bandwidthCombo->setCurrentIndex(idx);
}
