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

private slots:
    void onExportNodesCsv();
    void onExportNodesJson();
    void onExportMessagesCsv();
    void onExportMessagesJson();
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
    void onDarkThemeChanged(bool checked);
    void onAutoPingResponseChanged(bool checked);
    void onShowPacketFlowLinesChanged(bool checked);

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
    QCheckBox *m_showPacketFlowLinesCheck;

    // Message settings
    QCheckBox *m_autoPingResponseCheck;

    // Notification settings
    QCheckBox *m_notificationsCheck;
    QCheckBox *m_soundCheck;

    // Packet display settings
    QCheckBox *m_hideLocalDevicePacketsCheck;

    // Appearance settings
    QCheckBox *m_darkThemeCheck;

    // Export buttons
    QPushButton *m_exportNodesCsvBtn;
    QPushButton *m_exportNodesJsonBtn;
    QPushButton *m_exportMessagesCsvBtn;
    QPushButton *m_exportMessagesJsonBtn;

    void setupUI();
    void loadSettings();
    void applyTheme(bool dark);
};

#endif // APPSETTINGSTAB_H
