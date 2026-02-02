#ifndef SIGNALSCANNERWIDGET_H
#define SIGNALSCANNERWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QList>
#include <QDateTime>

class NodeManager;

// Data structure for a single signal measurement
struct SignalMeasurement
{
    QDateTime timestamp;
    uint32_t nodeNum;
    float snr;
    int rssi;
    int hopsAway;
};

// Table model for signal history
class SignalHistoryModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        ColTime,
        ColNode,
        ColSNR,
        ColRSSI,
        ColHops,
        ColQuality,
        ColCount
    };

    explicit SignalHistoryModel(NodeManager *nodeManager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addMeasurement(const SignalMeasurement &measurement);
    void clear();
    void setTargetNode(uint32_t nodeNum);
    uint32_t targetNode() const { return m_targetNode; }

    // Statistics
    float averageSNR() const;
    int averageRSSI() const;
    int measurementCount() const { return m_measurements.size(); }

private:
    QList<SignalMeasurement> m_measurements;
    NodeManager *m_nodeManager;
    uint32_t m_targetNode = 0;
    static const int MAX_MEASUREMENTS = 500;

    QString formatNodeName(uint32_t nodeNum) const;
    QString qualityFromSignal(float snr, int rssi) const;
    QColor qualityColor(float snr, int rssi) const;
};

// Main Signal Scanner widget
class SignalScannerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SignalScannerWidget(NodeManager *nodeManager, QWidget *parent = nullptr);

public slots:
    void onNodeSignalUpdated(uint32_t nodeNum);
    void refreshNodeList();

private slots:
    void onTargetNodeChanged(int index);
    void onClearHistory();
    void onScanAllToggled(bool checked);
    void updateStats();

private:
    NodeManager *m_nodeManager;
    SignalHistoryModel *m_model;

    // UI components
    QComboBox *m_nodeCombo;
    QPushButton *m_clearButton;
    QPushButton *m_scanAllButton;
    QTableView *m_tableView;

    // Stats display
    QLabel *m_currentSnrLabel;
    QLabel *m_currentRssiLabel;
    QLabel *m_currentHopsLabel;
    QLabel *m_currentQualityLabel;
    QProgressBar *m_signalBar;
    QLabel *m_avgSnrLabel;
    QLabel *m_avgRssiLabel;
    QLabel *m_countLabel;
    QLabel *m_lastHopWarningLabel;

    bool m_scanAll = false;

    void setupUI();
    void updateCurrentSignal(uint32_t nodeNum);
    QString qualityFromSignal(float snr, int rssi) const;
    int signalPercentage(float snr, int rssi) const;
};

#endif // SIGNALSCANNERWIDGET_H
