#ifndef TELEMETRYGRAPHWIDGET_H
#define TELEMETRYGRAPHWIDGET_H

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include "Database.h"

class NodeManager;

// Simple line chart widget using QPainter
class LineChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LineChartWidget(QWidget *parent = nullptr);

    void setData(const QList<QPointF> &points); // x = timestamp (secs since epoch), y = value
    void setYRange(float min, float max);
    void setAutoYRange(bool enabled) { m_autoYRange = enabled; }
    void setLineColor(const QColor &color) { m_lineColor = color; update(); }
    void setYLabel(const QString &label) { m_yLabel = label; update(); }
    void setTitle(const QString &title) { m_title = title; update(); }
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPointF> m_data;
    float m_yMin = 0.0f;
    float m_yMax = 100.0f;
    bool m_autoYRange = true;
    QColor m_lineColor = QColor(0, 120, 212);
    QString m_yLabel;
    QString m_title;

    void calculateAutoRange();
    QPointF dataToScreen(const QPointF &data, const QRectF &chartArea);
    QString formatTime(qint64 timestamp);
    QString formatValue(float value);
};

// Main telemetry graph widget
class TelemetryGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryGraphWidget(NodeManager *nodeManager, Database *database, QWidget *parent = nullptr);

    enum Metric
    {
        Temperature,
        Humidity,
        Pressure,
        Battery,
        Voltage,
        SNR,
        RSSI,
        ChannelUtil,
        AirUtilTx
    };

public slots:
    void refreshNodeList();
    void onTelemetryReceived(uint32_t nodeNum);

private slots:
    void onNodeChanged(int index);
    void onMetricChanged(int index);
    void onTimeRangeChanged(int index);
    void onRefresh();

private:
    NodeManager *m_nodeManager;
    Database *m_database;

    QComboBox *m_nodeCombo;
    QComboBox *m_metricCombo;
    QComboBox *m_timeRangeCombo;
    QPushButton *m_refreshButton;
    LineChartWidget *m_chart;
    QLabel *m_statsLabel;

    uint32_t m_currentNode = 0;
    Metric m_currentMetric = Battery;
    int m_timeRangeHours = 24;

    void setupUI();
    void updateChart();
    float getMetricValue(const Database::TelemetryRecord &rec, Metric metric);
    QString getMetricName(Metric metric);
    QString getMetricUnit(Metric metric);
    QColor getMetricColor(Metric metric);
};

#endif // TELEMETRYGRAPHWIDGET_H
