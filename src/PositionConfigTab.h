#ifndef POSITIONCONFIGTAB_H
#define POSITIONCONFIGTAB_H

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>

class DeviceConfig;

class PositionConfigTab : public QWidget
{
    Q_OBJECT

public:
    explicit PositionConfigTab(DeviceConfig *config, QWidget *parent = nullptr);

signals:
    void configChanged();
    void saveRequested();

private slots:
    void onConfigReceived();
    void onSaveClicked();
    void onSmartPositionToggled(bool checked);

private:
    DeviceConfig *m_config;

    // UI elements
    QComboBox *m_gpsModeCombo;
    QSpinBox *m_broadcastIntervalSpin;
    QCheckBox *m_smartPositionCheck;
    QSpinBox *m_smartMinDistanceSpin;
    QSpinBox *m_smartMinIntervalSpin;
    QCheckBox *m_fixedPositionCheck;
    QSpinBox *m_gpsUpdateIntervalSpin;
    QSpinBox *m_gpsAttemptTimeSpin;
    QPushButton *m_saveButton;
    QLabel *m_statusLabel;

    void setupUI();
    void updateUIFromConfig();
};

#endif // POSITIONCONFIGTAB_H
