#include "ConfigWidget.h"
#include "AppSettingsTab.h"
#include "RadioConfigTab.h"
#include "DeviceConfigTab.h"
#include "PositionConfigTab.h"
#include "ChannelsConfigTab.h"
#include "DeviceConfig.h"

#include <QVBoxLayout>
#include <QScrollArea>

ConfigWidget::ConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    m_deviceConfig = new DeviceConfig(this);
    setupUI();
}

// Each config tab stacks its group boxes in a plain QVBoxLayout, so the tab's
// minimum height is the sum of everything it contains - App Settings alone is
// over 1400px. That minimum propagates all the way up to the main window, and a
// window whose minimum size exceeds the screen cannot be maximised at all (the
// window manager drops the maximise button). Wrapping each tab in a scroll area
// keeps its minimum height small and makes tall tabs usable on short screens.
static QWidget *wrapInScrollArea(QWidget *content)
{
    QScrollArea *area = new QScrollArea;
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    return area;
}

void ConfigWidget::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget;
    m_tabWidget->setTabPosition(QTabWidget::West);  // Tabs on left side

    // App Settings tab (local settings)
    m_appSettingsTab = new AppSettingsTab;
    m_tabWidget->addTab(wrapInScrollArea(m_appSettingsTab), "App Settings");

    // Device config tabs
    m_radioConfigTab = new RadioConfigTab(m_deviceConfig);
    m_tabWidget->addTab(wrapInScrollArea(m_radioConfigTab), "Radio");

    m_deviceConfigTab = new DeviceConfigTab(m_deviceConfig);
    m_tabWidget->addTab(wrapInScrollArea(m_deviceConfigTab), "Device");

    m_positionConfigTab = new PositionConfigTab(m_deviceConfig);
    m_tabWidget->addTab(wrapInScrollArea(m_positionConfigTab), "Position");

    m_channelsConfigTab = new ChannelsConfigTab(m_deviceConfig);
    m_tabWidget->addTab(wrapInScrollArea(m_channelsConfigTab), "Channels");

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

void ConfigWidget::notifyLoRaSaved()     { m_radioConfigTab->notifySaved(); }
void ConfigWidget::notifyDeviceSaved()   { m_deviceConfigTab->notifySaved(); }
void ConfigWidget::notifyPositionSaved() { m_positionConfigTab->notifySaved(); }
void ConfigWidget::notifyChannelSaved()  { m_channelsConfigTab->notifySaved(); }
