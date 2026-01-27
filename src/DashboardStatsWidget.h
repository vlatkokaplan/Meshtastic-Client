#ifndef DASHBOARDSTATSWIDGET_H
#define DASHBOARDSTATSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QGridLayout>
#include <QVBoxLayout>

class NodeManager;
class DeviceConfig;

class DashboardStatsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardStatsWidget(NodeManager *nodeManager, DeviceConfig *deviceConfig,
                                   QWidget *parent = nullptr);

public slots:
    void setFirmwareVersion(const QString &version);

private slots:
    void onNodeUpdated(uint32_t nodeNum);
    void onNodesChanged();
    void onMyNodeNumChanged();
    void onLoraConfigChanged();
    void onDeviceConfigChanged();

private:
    void setupUI();
    void updateIdentity();
    void updateTelemetry();
    void updateConfig();
    void updateNodeCount();

    NodeManager *m_nodeManager;
    DeviceConfig *m_deviceConfig;
    QString m_firmwareVersion;

    // Identity section
    QLabel *m_nameLabel;
    QLabel *m_hwModelLabel;
    QLabel *m_nodeIdLabel;
    QLabel *m_fwVersionLabel;

    // Telemetry section
    QProgressBar *m_batteryBar;
    QLabel *m_batteryPctLabel;
    QLabel *m_voltageLabel;
    QProgressBar *m_chUtilBar;
    QLabel *m_chUtilLabel;
    QProgressBar *m_airTxBar;
    QLabel *m_airTxLabel;
    QLabel *m_envLabel;
    QLabel *m_envTitleLabel;
    QLabel *m_uptimeLabel;
    QLabel *m_uptimeTitleLabel;
    QLabel *m_signalLabel;
    QLabel *m_signalTitleLabel;

    // Config section
    QLabel *m_roleLabel;
    QLabel *m_regionPresetLabel;
    QLabel *m_hopsLabel;
    QLabel *m_nodeCountLabel;
};

#endif // DASHBOARDSTATSWIDGET_H
