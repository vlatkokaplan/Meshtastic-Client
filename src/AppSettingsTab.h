#ifndef APPSETTINGSTAB_H
#define APPSETTINGSTAB_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>

class AppSettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit AppSettingsTab(QWidget *parent = nullptr);

private slots:
    void onAutoConnectChanged(bool checked);
    void onShowOfflineNodesChanged(bool checked);
    void onOfflineThresholdChanged(int value);
    void onNotificationsChanged(bool checked);
    void onSoundChanged(bool checked);
    void onTileServerChanged(int index);
    void onCustomTileServerChanged();
    void onHideLocalDevicePacketsChanged(bool checked);
    void onNodeBlinkEnabledChanged(bool checked);
    void onNodeBlinkDurationChanged(int value);

private:
    // Connection settings
    QCheckBox *m_autoConnectCheck;

    // Node display settings
    QCheckBox *m_showOfflineNodesCheck;
    QSpinBox *m_offlineThresholdSpin;

    // Map settings
    QComboBox *m_tileServerCombo;
    QLineEdit *m_customTileServerEdit;
    QCheckBox *m_nodeBlinkCheck;
    QSpinBox *m_nodeBlinkDurationSpin;

    // Notification settings
    QCheckBox *m_notificationsCheck;
    QCheckBox *m_soundCheck;

    // Packet display settings
    QCheckBox *m_hideLocalDevicePacketsCheck;

    void setupUI();
    void loadSettings();
};

#endif // APPSETTINGSTAB_H
