#ifndef APPSETTINGSTAB_H
#define APPSETTINGSTAB_H

#include <QWidget>
#include <QCheckBox>
#include <QWheelEvent>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

// A spin box that ignores the wheel unless it has focus. Inside a scroll area
// the wheel would otherwise change the value while the user is only scrolling
// past it - a silent settings change with no way to notice.
class NoScrollSpinBox : public QSpinBox
{
    Q_OBJECT
public:
    explicit NoScrollSpinBox(QWidget *parent = nullptr);
protected:
    void wheelEvent(QWheelEvent *event) override;
};

// Same for combo boxes.
class NoScrollComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit NoScrollComboBox(QWidget *parent = nullptr);
protected:
    void wheelEvent(QWheelEvent *event) override;
};

class AppSettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit AppSettingsTab(QWidget *parent = nullptr);

signals:
    void exportNodesRequested(const QString &format);  // "csv" or "json"
    void exportMessagesRequested(const QString &format);
    void clearNodeDatabaseRequested();
    void forgetRadioRequested();

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
    void onForgetRadio();
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
    QPushButton *m_forgetRadioBtn;
    QSpinBox *m_retentionDaysSpin;
    QSpinBox *m_packetRetentionSpin;

    void setupUI();
    void loadSettings();
    void applyTheme(bool dark);
};

#endif // APPSETTINGSTAB_H
