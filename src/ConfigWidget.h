#ifndef CONFIGWIDGET_H
#define CONFIGWIDGET_H

#include <QWidget>
#include <QTabWidget>

class AppSettingsTab;
class RadioConfigTab;
class DeviceConfigTab;
class PositionConfigTab;
class ChannelsConfigTab;
class DeviceConfig;

class ConfigWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ConfigWidget(QWidget *parent = nullptr);

    DeviceConfig *deviceConfig() const { return m_deviceConfig; }
    AppSettingsTab *appSettingsTab() const { return m_appSettingsTab; }

signals:
    void saveLoRaConfig();
    void saveDeviceConfig();
    void savePositionConfig();
    void saveChannelConfig(int channelIndex);

private:
    QTabWidget *m_tabWidget;
    DeviceConfig *m_deviceConfig;

    // Tabs
    AppSettingsTab *m_appSettingsTab;
    RadioConfigTab *m_radioConfigTab;
    DeviceConfigTab *m_deviceConfigTab;
    PositionConfigTab *m_positionConfigTab;
    ChannelsConfigTab *m_channelsConfigTab;

    void setupUI();
};

#endif // CONFIGWIDGET_H
