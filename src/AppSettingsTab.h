#ifndef APPSETTINGSTAB_H
#define APPSETTINGSTAB_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

class AppSettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit AppSettingsTab(QWidget *parent = nullptr);

signals:
    void exportNodesRequested(const QString &format);  // "csv" or "json"
    void exportMessagesRequested(const QString &format);
    void clearNodeDatabaseRequested();

private slots:
    void onExportNodesCsv();
    void onExportNodesJson();
    void onExportMessagesCsv();
    void onExportMessagesJson();
    void onAutoConnectChanged(bool checked);
    void onShowOfflineNodesChanged(bool checked);
    void onHideNeverHeardChanged(bool checked);
    void onOfflineThresholdChanged(int value);
    void onNotificationsChanged(bool checked);
    void onSoundChanged(bool checked);
    void onTileServerChanged(int index);
    void onCustomTileServerChanged();
    void onHideLocalDevicePacketsChanged(bool checked);
    void onNodeBlinkEnabledChanged(bool checked);
    void onNodeBlinkDurationChanged(int value);
    void onDarkThemeChanged(bool checked);
    void onAutoPingResponseChanged(bool checked);
    void onShowPacketFlowLinesChanged(bool checked);
    void onSavePacketsToDbChanged(bool checked);
    void onPositionRefreshChanged(int index);
    void onClearNodeDatabase();
    void onRetentionDaysChanged(int value);
    void onPacketRetentionChanged(int value);

private:
    // Connection settings
    QCheckBox *m_autoConnectCheck;

    // Node display settings
    QCheckBox *m_showOfflineNodesCheck;
    QCheckBox *m_hideNeverHeardCheck;
    QSpinBox *m_offlineThresholdSpin;

    // Map settings
    QComboBox *m_tileServerCombo;
    QLineEdit *m_customTileServerEdit;
    QCheckBox *m_nodeBlinkCheck;
    QSpinBox *m_nodeBlinkDurationSpin;
    QCheckBox *m_showPacketFlowLinesCheck;
    QComboBox *m_positionRefreshCombo;

    // Message settings
    QCheckBox *m_autoPingResponseCheck;

    // Notification settings
    QCheckBox *m_notificationsCheck;
    QCheckBox *m_soundCheck;

    // Packet display settings
    QCheckBox *m_hideLocalDevicePacketsCheck;
    QCheckBox *m_savePacketsToDbCheck;

    // Appearance settings
    QCheckBox *m_darkThemeCheck;

    // Export buttons
    QPushButton *m_exportNodesCsvBtn;
    QPushButton *m_exportNodesJsonBtn;
    QPushButton *m_exportMessagesCsvBtn;
    QPushButton *m_exportMessagesJsonBtn;

    // Local database maintenance
    QPushButton *m_clearNodeDbBtn;
    QSpinBox *m_retentionDaysSpin;
    QSpinBox *m_packetRetentionSpin;

    void setupUI();
    void loadSettings();
    void applyTheme(bool dark);
};

#endif // APPSETTINGSTAB_H
