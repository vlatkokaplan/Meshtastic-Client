#include "TelemetryGraphWidget.h"
#include "NodeManager.h"
#include "MeshtasticProtocol.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <cmath>

// LineChartWidget implementation

LineChartWidget::LineChartWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void LineChartWidget::setData(const QList<QPointF> &points)
{
    m_data = points;
    if (m_autoYRange)
        calculateAutoRange();
    update();
}

void LineChartWidget::setYRange(float min, float max)
{
    m_yMin = min;
    m_yMax = max;
    m_autoYRange = false;
    update();
}

void LineChartWidget::clear()
{
    m_data.clear();
    update();
}

void LineChartWidget::calculateAutoRange()
{
    if (m_data.isEmpty())
    {
        m_yMin = 0;
        m_yMax = 100;
        return;
    }

    m_yMin = m_data.first().y();
    m_yMax = m_data.first().y();

    for (const auto &pt : m_data)
    {
        if (pt.y() < m_yMin) m_yMin = pt.y();
        if (pt.y() > m_yMax) m_yMax = pt.y();
    }

    // Add 10% padding
    float range = m_yMax - m_yMin;
    if (range < 0.001f) range = 1.0f; // Avoid zero range
    m_yMin -= range * 0.1f;
    m_yMax += range * 0.1f;
}

QPointF LineChartWidget::dataToScreen(const QPointF &data, const QRectF &chartArea)
{
    if (m_data.isEmpty())
        return chartArea.center();

    qint64 xMin = static_cast<qint64>(m_data.first().x());
    qint64 xMax = static_cast<qint64>(m_data.last().x());
    if (xMax <= xMin) xMax = xMin + 1;

    float xRatio = (data.x() - xMin) / float(xMax - xMin);
    float yRatio = (data.y() - m_yMin) / (m_yMax - m_yMin);

    float x = chartArea.left() + xRatio * chartArea.width();
    float y = chartArea.bottom() - yRatio * chartArea.height();

    return QPointF(x, y);
}

QString LineChartWidget::formatTime(qint64 timestamp)
{
    return QDateTime::fromSecsSinceEpoch(timestamp).toString("HH:mm");
}

QString LineChartWidget::formatValue(float value)
{
    if (std::abs(value) < 0.01f)
        return QString::number(value, 'f', 3);
    if (std::abs(value) < 10.0f)
        return QString::number(value, 'f', 2);
    if (std::abs(value) < 100.0f)
        return QString::number(value, 'f', 1);
    return QString::number(static_cast<int>(value));
}

void LineChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Colors
    QColor bgColor = palette().color(QPalette::Base);
    QColor textColor = palette().color(QPalette::Text);
    QColor gridColor = palette().color(QPalette::Mid);

    // Fill background
    painter.fillRect(rect(), bgColor);

    // Margins
    int leftMargin = 60;
    int rightMargin = 20;
    int topMargin = 30;
    int bottomMargin = 40;

    QRectF chartArea(leftMargin, topMargin, width() - leftMargin - rightMargin, height() - topMargin - bottomMargin);

    // Draw title
    if (!m_title.isEmpty())
    {
        painter.setPen(textColor);
        QFont titleFont = font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(QRectF(0, 5, width(), 20), Qt::AlignCenter, m_title);
        painter.setFont(font());
    }

    // Draw chart border
    painter.setPen(QPen(gridColor, 1));
    painter.drawRect(chartArea);

    // Draw grid lines and Y axis labels
    int numYLines = 5;
    for (int i = 0; i <= numYLines; i++)
    {
        float ratio = float(i) / numYLines;
        float y = chartArea.bottom() - ratio * chartArea.height();
        float value = m_yMin + ratio * (m_yMax - m_yMin);

        // Grid line
        painter.setPen(QPen(gridColor, 1, Qt::DotLine));
        painter.drawLine(QPointF(chartArea.left(), y), QPointF(chartArea.right(), y));

        // Label
        painter.setPen(textColor);
        QString label = formatValue(value);
        QRectF labelRect(0, y - 10, leftMargin - 5, 20);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Draw Y axis label
    if (!m_yLabel.isEmpty())
    {
        painter.save();
        painter.translate(12, chartArea.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-50, -10, 100, 20), Qt::AlignCenter, m_yLabel);
        painter.restore();
    }

    // Draw X axis time labels
    if (!m_data.isEmpty())
    {
        int numXLabels = qMin(6, m_data.size());
        for (int i = 0; i < numXLabels; i++)
        {
            int idx = (m_data.size() - 1) * i / qMax(1, numXLabels - 1);
            if (idx >= m_data.size()) idx = m_data.size() - 1;

            qint64 timestamp = static_cast<qint64>(m_data[idx].x());
            QPointF screenPt = dataToScreen(m_data[idx], chartArea);

            painter.setPen(textColor);
            QString timeStr = formatTime(timestamp);
            QRectF labelRect(screenPt.x() - 30, chartArea.bottom() + 5, 60, 20);
            painter.drawText(labelRect, Qt::AlignCenter, timeStr);

            // Tick mark
            painter.setPen(QPen(gridColor, 1));
            painter.drawLine(QPointF(screenPt.x(), chartArea.bottom()), QPointF(screenPt.x(), chartArea.bottom() + 3));
        }
    }

    // Draw data line
    if (m_data.size() >= 2)
    {
        QPainterPath path;
        QPointF firstPt = dataToScreen(m_data.first(), chartArea);
        path.moveTo(firstPt);

        for (int i = 1; i < m_data.size(); i++)
        {
            QPointF pt = dataToScreen(m_data[i], chartArea);
            path.lineTo(pt);
        }

        painter.setPen(QPen(m_lineColor, 2));
        painter.drawPath(path);

        // Draw points
        painter.setBrush(m_lineColor);
        painter.setPen(Qt::NoPen);
        for (const auto &data : m_data)
        {
            QPointF pt = dataToScreen(data, chartArea);
            painter.drawEllipse(pt, 3, 3);
        }
    }
    else if (m_data.size() == 1)
    {
        // Single point
        painter.setBrush(m_lineColor);
        painter.setPen(Qt::NoPen);
        QPointF pt = dataToScreen(m_data.first(), chartArea);
        painter.drawEllipse(pt, 5, 5);
    }
    else
    {
        // No data message
        painter.setPen(textColor);
        painter.drawText(chartArea, Qt::AlignCenter, "No data available");
    }
}

// TelemetryGraphWidget implementation

TelemetryGraphWidget::TelemetryGraphWidget(NodeManager *nodeManager, Database *database, QWidget *parent)
    : QWidget(parent), m_nodeManager(nodeManager), m_database(database)
{
    setupUI();

    connect(m_nodeManager, &NodeManager::nodesChanged, this, &TelemetryGraphWidget::refreshNodeList);

    refreshNodeList();
}

void TelemetryGraphWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Controls
    QHBoxLayout *controlsLayout = new QHBoxLayout;

    QLabel *nodeLabel = new QLabel("Node:");
    m_nodeCombo = new QComboBox;
    m_nodeCombo->setMinimumWidth(180);

    QLabel *metricLabel = new QLabel("Metric:");
    m_metricCombo = new QComboBox;
    m_metricCombo->addItem("Battery %", Battery);
    m_metricCombo->addItem("Voltage", Voltage);
    m_metricCombo->addItem("Temperature", Temperature);
    m_metricCombo->addItem("Humidity", Humidity);
    m_metricCombo->addItem("Pressure", Pressure);
    m_metricCombo->addItem("SNR", SNR);
    m_metricCombo->addItem("RSSI", RSSI);
    m_metricCombo->addItem("Channel Util", ChannelUtil);
    m_metricCombo->addItem("Air Util TX", AirUtilTx);

    QLabel *timeLabel = new QLabel("Time:");
    m_timeRangeCombo = new QComboBox;
    m_timeRangeCombo->addItem("1 hour", 1);
    m_timeRangeCombo->addItem("6 hours", 6);
    m_timeRangeCombo->addItem("24 hours", 24);
    m_timeRangeCombo->addItem("7 days", 168);
    m_timeRangeCombo->setCurrentIndex(2); // Default 24 hours

    m_refreshButton = new QPushButton("Refresh");

    controlsLayout->addWidget(nodeLabel);
    controlsLayout->addWidget(m_nodeCombo);
    controlsLayout->addSpacing(10);
    controlsLayout->addWidget(metricLabel);
    controlsLayout->addWidget(m_metricCombo);
    controlsLayout->addSpacing(10);
    controlsLayout->addWidget(timeLabel);
    controlsLayout->addWidget(m_timeRangeCombo);
    controlsLayout->addSpacing(10);
    controlsLayout->addWidget(m_refreshButton);
    controlsLayout->addStretch();

    mainLayout->addLayout(controlsLayout);

    // Chart
    QGroupBox *chartGroup = new QGroupBox("Telemetry History");
    QVBoxLayout *chartLayout = new QVBoxLayout(chartGroup);

    m_chart = new LineChartWidget;
    chartLayout->addWidget(m_chart);

    // Stats label
    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet("color: gray;");
    chartLayout->addWidget(m_statsLabel);

    mainLayout->addWidget(chartGroup, 1);

    // Connections
    connect(m_nodeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TelemetryGraphWidget::onNodeChanged);
    connect(m_metricCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TelemetryGraphWidget::onMetricChanged);
    connect(m_timeRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TelemetryGraphWidget::onTimeRangeChanged);
    connect(m_refreshButton, &QPushButton::clicked, this, &TelemetryGraphWidget::onRefresh);
}

void TelemetryGraphWidget::refreshNodeList()
{
    uint32_t currentNode = m_nodeCombo->currentData().toUInt();

    m_nodeCombo->blockSignals(true);
    m_nodeCombo->clear();

    m_nodeCombo->addItem("Select a node...", 0);

    // Only show nodes that have telemetry history
    QList<uint32_t> nodesWithTelemetry;
    if (m_database)
        nodesWithTelemetry = m_database->getNodesWithTelemetry();

    if (nodesWithTelemetry.isEmpty())
    {
        m_nodeCombo->addItem("No telemetry data yet", 0);
        m_nodeCombo->blockSignals(false);
        return;
    }

    // Build list of nodes with names
    QList<QPair<uint32_t, QString>> nodeList;
    for (uint32_t nodeNum : nodesWithTelemetry)
    {
        QString name;
        if (m_nodeManager->hasNode(nodeNum))
        {
            NodeInfo node = m_nodeManager->getNode(nodeNum);
            name = node.shortName.isEmpty() ? node.longName : node.shortName;
        }
        if (name.isEmpty())
            name = MeshtasticProtocol::nodeIdToString(nodeNum);

        nodeList.append(qMakePair(nodeNum, name));
    }

    // Sort by name
    std::sort(nodeList.begin(), nodeList.end(), [](const auto &a, const auto &b) {
        return a.second.toLower() < b.second.toLower();
    });

    for (const auto &pair : nodeList)
    {
        QString display = QString("%1 (%2)").arg(pair.second, MeshtasticProtocol::nodeIdToString(pair.first));
        m_nodeCombo->addItem(display, pair.first);
    }

    // Restore selection
    int idx = m_nodeCombo->findData(currentNode);
    if (idx >= 0)
        m_nodeCombo->setCurrentIndex(idx);

    m_nodeCombo->blockSignals(false);
}

void TelemetryGraphWidget::onNodeChanged(int index)
{
    m_currentNode = m_nodeCombo->itemData(index).toUInt();
    updateChart();
}

void TelemetryGraphWidget::onMetricChanged(int index)
{
    m_currentMetric = static_cast<Metric>(m_metricCombo->itemData(index).toInt());
    updateChart();
}

void TelemetryGraphWidget::onTimeRangeChanged(int index)
{
    m_timeRangeHours = m_timeRangeCombo->itemData(index).toInt();
    updateChart();
}

void TelemetryGraphWidget::onRefresh()
{
    updateChart();
}

void TelemetryGraphWidget::onTelemetryReceived(uint32_t nodeNum)
{
    // Check if this node is already in our list
    bool nodeInList = false;
    for (int i = 0; i < m_nodeCombo->count(); i++)
    {
        if (m_nodeCombo->itemData(i).toUInt() == nodeNum)
        {
            nodeInList = true;
            break;
        }
    }

    // If new node with telemetry, refresh the list
    if (!nodeInList)
        refreshNodeList();

    // Update chart if viewing this node
    if (nodeNum == m_currentNode)
        updateChart();
}

void TelemetryGraphWidget::updateChart()
{
    if (m_currentNode == 0 || !m_database)
    {
        m_chart->clear();
        m_statsLabel->setText("Select a node to view telemetry history");
        return;
    }

    QList<Database::TelemetryRecord> records = m_database->loadTelemetryHistory(m_currentNode, m_timeRangeHours);

    if (records.isEmpty())
    {
        m_chart->clear();
        m_statsLabel->setText("No telemetry history for this node in the selected time range");
        return;
    }

    // Convert to chart data
    QList<QPointF> points;
    float sum = 0.0f;
    float minVal = std::numeric_limits<float>::max();
    float maxVal = std::numeric_limits<float>::lowest();

    for (const auto &rec : records)
    {
        float value = getMetricValue(rec, m_currentMetric);
        qint64 timestamp = rec.timestamp.toSecsSinceEpoch();
        points.append(QPointF(timestamp, value));

        sum += value;
        if (value < minVal) minVal = value;
        if (value > maxVal) maxVal = value;
    }

    float avg = sum / records.size();

    m_chart->setLineColor(getMetricColor(m_currentMetric));
    m_chart->setYLabel(getMetricUnit(m_currentMetric));
    m_chart->setTitle(getMetricName(m_currentMetric));
    m_chart->setData(points);

    // Update stats
    QString stats = QString("Points: %1 | Min: %2 | Max: %3 | Avg: %4 %5")
        .arg(records.size())
        .arg(minVal, 0, 'f', 1)
        .arg(maxVal, 0, 'f', 1)
        .arg(avg, 0, 'f', 1)
        .arg(getMetricUnit(m_currentMetric));
    m_statsLabel->setText(stats);
}

float TelemetryGraphWidget::getMetricValue(const Database::TelemetryRecord &rec, Metric metric)
{
    switch (metric)
    {
    case Temperature:   return rec.temperature;
    case Humidity:      return rec.humidity;
    case Pressure:      return rec.pressure;
    case Battery:       return rec.batteryLevel;
    case Voltage:       return rec.voltage;
    case SNR:           return rec.snr;
    case RSSI:          return rec.rssi;
    case ChannelUtil:   return rec.channelUtil;
    case AirUtilTx:     return rec.airUtilTx;
    }
    return 0.0f;
}

QString TelemetryGraphWidget::getMetricName(Metric metric)
{
    switch (metric)
    {
    case Temperature:   return "Temperature";
    case Humidity:      return "Humidity";
    case Pressure:      return "Barometric Pressure";
    case Battery:       return "Battery Level";
    case Voltage:       return "Voltage";
    case SNR:           return "Signal-to-Noise Ratio";
    case RSSI:          return "RSSI";
    case ChannelUtil:   return "Channel Utilization";
    case AirUtilTx:     return "Air Util TX";
    }
    return "Unknown";
}

QString TelemetryGraphWidget::getMetricUnit(Metric metric)
{
    switch (metric)
    {
    case Temperature:   return "\u00B0C";
    case Humidity:      return "%";
    case Pressure:      return "hPa";
    case Battery:       return "%";
    case Voltage:       return "V";
    case SNR:           return "dB";
    case RSSI:          return "dBm";
    case ChannelUtil:   return "%";
    case AirUtilTx:     return "%";
    }
    return "";
}

QColor TelemetryGraphWidget::getMetricColor(Metric metric)
{
    switch (metric)
    {
    case Temperature:   return QColor(220, 60, 60);    // Red
    case Humidity:      return QColor(60, 160, 220);   // Blue
    case Pressure:      return QColor(140, 100, 180);  // Purple
    case Battery:       return QColor(80, 180, 80);    // Green
    case Voltage:       return QColor(220, 180, 60);   // Yellow
    case SNR:           return QColor(60, 180, 180);   // Cyan
    case RSSI:          return QColor(180, 100, 60);   // Orange
    case ChannelUtil:   return QColor(100, 100, 200);  // Indigo
    case AirUtilTx:     return QColor(200, 100, 150);  // Pink
    }
    return QColor(0, 120, 212);
}
