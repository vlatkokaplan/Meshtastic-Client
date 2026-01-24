#include "ConfigWidget.h"
#include "AppSettingsTab.h"
#include "RadioConfigTab.h"
#include "DeviceConfigTab.h"
#include "PositionConfigTab.h"
#include "ChannelsConfigTab.h"
#include "DeviceConfig.h"

#include <QVBoxLayout>

ConfigWidget::ConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    m_deviceConfig = new DeviceConfig(this);
    setupUI();
}

void ConfigWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget;
    m_tabWidget->setTabPosition(QTabWidget::West);  // Tabs on left side

    // App Settings tab (local settings)
    m_appSettingsTab = new AppSettingsTab;
    m_tabWidget->addTab(m_appSettingsTab, "App Settings");

    // Device config tabs
    m_radioConfigTab = new RadioConfigTab(m_deviceConfig);
    m_tabWidget->addTab(m_radioConfigTab, "Radio");

    m_deviceConfigTab = new DeviceConfigTab(m_deviceConfig);
    m_tabWidget->addTab(m_deviceConfigTab, "Device");

    m_positionConfigTab = new PositionConfigTab(m_deviceConfig);
    m_tabWidget->addTab(m_positionConfigTab, "Position");

    m_channelsConfigTab = new ChannelsConfigTab(m_deviceConfig);
    m_tabWidget->addTab(m_channelsConfigTab, "Channels");

    // Connect save signals
    connect(m_radioConfigTab, &RadioConfigTab::saveRequested,
            this, &ConfigWidget::saveLoRaConfig);
    connect(m_deviceConfigTab, &DeviceConfigTab::saveRequested,
            this, &ConfigWidget::saveDeviceConfig);
    connect(m_positionConfigTab, &PositionConfigTab::saveRequested,
            this, &ConfigWidget::savePositionConfig);
    connect(m_channelsConfigTab, &ChannelsConfigTab::saveRequested,
            this, &ConfigWidget::saveChannelConfig);

    layout->addWidget(m_tabWidget);
}
