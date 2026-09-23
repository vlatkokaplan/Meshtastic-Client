#ifndef RADIOCONFIGTAB_H
#define RADIOCONFIGTAB_H

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>

class DeviceConfig;

class RadioConfigTab : public QWidget
{
    Q_OBJECT

public:
    explicit RadioConfigTab(DeviceConfig *config, QWidget *parent = nullptr);

public slots:
    void notifySaved();

signals:
    void configChanged();
    void saveRequested();

private slots:
    void onConfigReceived();
    void onSaveClicked();

private:
    DeviceConfig *m_config;

    // UI elements
    QComboBox *m_regionCombo;
    QComboBox *m_presetCombo;
    QCheckBox *m_usePresetCheck;
    QComboBox *m_bandwidthCombo;   // data: firmware bandwidth code
    QSpinBox *m_spreadFactorSpin;
    QComboBox *m_codingRateCombo;  // data: 5-8, i.e. 4/5 - 4/8
    QLabel *m_customWarning;
    QSpinBox *m_hopLimitSpin;
    QSpinBox *m_txPowerSpin;
    QCheckBox *m_txEnabledCheck;
    QSpinBox *m_channelNumSpin;
    QCheckBox *m_overrideDutyCycleCheck;
    QDoubleSpinBox *m_freqOffsetSpin;
    QPushButton *m_saveButton;
    QLabel *m_statusLabel;

    void setupUI();
    void updateUIFromConfig();
    void onUsePresetToggled(bool usePreset);
    void fillCustomFromPreset();
    void selectBandwidth(int code);
};

#endif // RADIOCONFIGTAB_H
