#ifndef DEVICECONFIGTAB_H
#define DEVICECONFIGTAB_H

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class DeviceConfig;

class DeviceConfigTab : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceConfigTab(DeviceConfig *config, QWidget *parent = nullptr);

signals:
    void configChanged();
    void saveRequested();

private slots:
    void onConfigReceived();
    void onSaveClicked();

private:
    DeviceConfig *m_config;

    // UI elements
    QComboBox *m_roleCombo;
    QSpinBox *m_nodeInfoIntervalSpin;
    QCheckBox *m_serialEnabledCheck;
    QCheckBox *m_debugLogCheck;
    QCheckBox *m_ledHeartbeatCheck;
    QCheckBox *m_doubleTapCheck;
    QCheckBox *m_disableTripleClickCheck;
    QLineEdit *m_timezoneEdit;
    QPushButton *m_saveButton;
    QLabel *m_statusLabel;

    void setupUI();
    void updateUIFromConfig();
};

#endif // DEVICECONFIGTAB_H
