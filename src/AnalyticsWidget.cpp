#include "AnalyticsWidget.h"

#include "Database.h"
#include "DeviceConfig.h"
#include "NodeManager.h"
#include "Theme.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------

ProportionBar::ProportionBar(QWidget *parent)
    : QWidget(parent), m_fill(Theme::palette().accent)
{
    setFixedHeight(14);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ProportionBar::setValue(double value, double max, double limit)
{
    m_value = value;
    m_max = (max > 0.0) ? max : 1.0;
    m_limit = limit;
    update();
}

void ProportionBar::setFillColor(const QColor &c)
{
    m_fill = c;
    update();
}

void ProportionBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto &pal = Theme::palette();
    QRectF track(0, 2, width(), height() - 4);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::isDark() ? pal.borderStrong : pal.border);
    p.drawRoundedRect(track, 4, 4);

    double frac = qBound(0.0, m_value / m_max, 1.0);
    if (frac > 0.0)
    {
        QRectF fill = track;
        fill.setWidth(track.width() * frac);
        p.setBrush(m_fill);
        p.drawRoundedRect(fill, 4, 4);
    }

    // Regulatory ceiling or similar reference line
    if (m_limit > 0.0 && m_limit <= m_max)
    {
        double x = track.width() * (m_limit / m_max);
        p.setPen(QPen(pal.danger, 2));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
    }
}

// ---------------------------------------------------------------------------

AnalyticsWidget::AnalyticsWidget(NodeManager *nodes, DeviceConfig *config, QWidget *parent)
    : QWidget(parent), m_nodes(nodes), m_config(config)
{
    setupUI();

    // Analysis is read-only over local rows, so refreshing costs nothing but a
    // few queries - but there is no reason to do it while the tab is hidden.
    m_autoRefresh = new QTimer(this);
    m_autoRefresh->setInterval(30000);
    connect(m_autoRefresh, &QTimer::timeout, this, [this]() {
        if (isVisible())
            refresh();
    });
    m_autoRefresh->start();
}

void AnalyticsWidget::setDatabase(Database *db)
{
    m_db = db;
    refresh();
}

QDateTime AnalyticsWidget::windowStart() const
{
    const QDateTime now = QDateTime::currentDateTime();
    switch (m_windowCombo ? m_windowCombo->currentIndex() : 2)
    {
    case 0: return now.addSecs(-3600);        // 1 hour
    case 1: return now.addSecs(-6 * 3600);    // 6 hours
    case 2: return now.addDays(-1);           // 24 hours
    case 3: return now.addDays(-7);           // 7 days
    default: return QDateTime::fromSecsSinceEpoch(0);  // everything stored
    }
}

double AnalyticsWidget::dutyCycleLimitPercent() const
{
    // Regions differ; these are the common ceilings. Unknown regions fall back
    // to the strictest common value rather than implying more headroom than
    // the operator actually has.
    if (!m_config)
        return 10.0;

    switch (m_config->loraConfig().region)
    {
    case 1:  return 100.0;  // US - no duty cycle, dwell time instead
    case 2:  return 10.0;   // EU_433
    case 3:  return 10.0;   // EU_868
    case 4:  return 100.0;  // CN
    case 5:  return 100.0;  // JP
    case 6:  return 100.0;  // ANZ
    default: return 10.0;
    }
}

void AnalyticsWidget::setupUI()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    // ---- toolbar ----
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(Theme::Space::md, Theme::Space::md, Theme::Space::md, 0);

    auto *windowLabel = new QLabel("Window:");
    windowLabel->setStyleSheet(Theme::mutedLabelStyle(12));
    bar->addWidget(windowLabel);

    m_windowCombo = new QComboBox;
    m_windowCombo->addItems({"Last hour", "Last 6 hours", "Last 24 hours",
                             "Last 7 days", "Everything stored"});
    m_windowCombo->setCurrentIndex(2);
    connect(m_windowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnalyticsWidget::onWindowChanged);
    bar->addWidget(m_windowCombo);

    m_refreshButton = new QPushButton("Refresh");
    connect(m_refreshButton, &QPushButton::clicked, this, &AnalyticsWidget::refresh);
    bar->addWidget(m_refreshButton);

    bar->addStretch();

    m_updatedLabel = new QLabel;
    m_updatedLabel->setStyleSheet(Theme::mutedLabelStyle(11));
    bar->addWidget(m_updatedLabel);
    outer->addLayout(bar);

    // ---- scrollable body ----
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(Theme::Space::md, Theme::Space::md,
                               Theme::Space::md, Theme::Space::md);
    layout->setSpacing(Theme::Space::md);

    auto caption = [](const QString &text) {
        auto *l = new QLabel(text);
        l->setWordWrap(true);
        l->setStyleSheet(Theme::mutedLabelStyle(11));
        return l;
    };

    // ---- 16: decode success ----
    auto *decodeGroup = new QGroupBox("Decode Success");
    auto *decodeLayout = new QVBoxLayout(decodeGroup);
    decodeLayout->setSpacing(Theme::Space::sm);

    m_decodeHeadline = new QLabel("-");
    m_decodeHeadline->setStyleSheet(
        QString("font-size: 15px; font-weight: 600; color: %1;").arg(Theme::palette().text.name()));
    decodeLayout->addWidget(m_decodeHeadline);

    m_decodeBar = new ProportionBar;
    m_decodeBar->setFillColor(Theme::palette().success);
    decodeLayout->addWidget(m_decodeBar);

    m_decodeByPort = new QLabel("-");
    m_decodeByPort->setWordWrap(true);
    decodeLayout->addWidget(m_decodeByPort);

    m_decodeByChannel = new QLabel;
    m_decodeByChannel->setWordWrap(true);
    m_decodeByChannel->setStyleSheet(Theme::mutedLabelStyle(11));
    decodeLayout->addWidget(m_decodeByChannel);

    decodeLayout->addWidget(caption(
        "Over-the-air mesh packets only; device housekeeping frames are excluded. "
        "A packet that never yielded a port number was not decrypted. On a shared "
        "mesh most of those are other people's traffic on channels you hold no key "
        "for, which is expected and healthy. A channel hash you have configured "
        "appearing here means your key for it is wrong."));
    layout->addWidget(decodeGroup);

    // ---- 17: airtime ----
    auto *airGroup = new QGroupBox("Airtime");
    auto *airLayout = new QVBoxLayout(airGroup);
    airLayout->setSpacing(Theme::Space::sm);

    m_airtimeHeadline = new QLabel("-");
    m_airtimeHeadline->setStyleSheet(
        QString("font-size: 15px; font-weight: 600; color: %1;").arg(Theme::palette().text.name()));
    airLayout->addWidget(m_airtimeHeadline);

    m_airtimeBar = new ProportionBar;
    airLayout->addWidget(m_airtimeBar);

    m_airtimeTable = new QTableWidget;
    m_airtimeTable->setColumnCount(4);
    m_airtimeTable->setHorizontalHeaderLabels({"Node", "Peak TX", "Average TX", "Samples"});
    m_airtimeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_airtimeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_airtimeTable->verticalHeader()->setVisible(false);
    m_airtimeTable->setShowGrid(false);
    m_airtimeTable->setAlternatingRowColors(true);
    m_airtimeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_airtimeTable->setFrameShape(QFrame::NoFrame);
    m_airtimeTable->setMinimumHeight(140);
    airLayout->addWidget(m_airtimeTable);

    airLayout->addWidget(caption(
        "Transmit airtime each node reports for itself. The red line is the duty "
        "cycle ceiling for your region. One node sitting near the line is the "
        "usual reason a mesh feels congested."));
    layout->addWidget(airGroup);

    // ---- 18: reachability ----
    auto *reachGroup = new QGroupBox("Reachability");
    auto *reachLayout = new QVBoxLayout(reachGroup);
    reachLayout->setSpacing(Theme::Space::sm);

    m_reachHeadline = new QLabel("-");
    m_reachHeadline->setStyleSheet(
        QString("font-size: 15px; font-weight: 600; color: %1;").arg(Theme::palette().text.name()));
    reachLayout->addWidget(m_reachHeadline);

    m_reachDetail = new QLabel;
    m_reachDetail->setWordWrap(true);
    reachLayout->addWidget(m_reachDetail);

    m_articulationTable = new QTableWidget;
    m_articulationTable->setColumnCount(2);
    m_articulationTable->setHorizontalHeaderLabels({"Single point of failure", "Links"});
    m_articulationTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_articulationTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_articulationTable->verticalHeader()->setVisible(false);
    m_articulationTable->setShowGrid(false);
    m_articulationTable->setAlternatingRowColors(true);
    m_articulationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_articulationTable->setFrameShape(QFrame::NoFrame);
    m_articulationTable->setMinimumHeight(120);
    reachLayout->addWidget(m_articulationTable);

    reachLayout->addWidget(caption(
        "The mesh as a graph, built from hops in stored traceroutes and from "
        "NeighborInfo broadcasts. A single point of failure is a node whose loss "
        "would split the mesh into pieces that can no longer reach each other."));
    layout->addWidget(reachGroup);

    // ---- 19: route churn ----
    auto *churnGroup = new QGroupBox("Route Stability");
    auto *churnLayout = new QVBoxLayout(churnGroup);
    churnLayout->setSpacing(Theme::Space::sm);

    m_churnTable = new QTableWidget;
    m_churnTable->setColumnCount(5);
    m_churnTable->setHorizontalHeaderLabels(
        {"Destination", "Traceroutes", "Distinct routes", "Churn", "Most recent route"});
    m_churnTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_churnTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_churnTable->verticalHeader()->setVisible(false);
    m_churnTable->setShowGrid(false);
    m_churnTable->setAlternatingRowColors(true);
    m_churnTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_churnTable->setFrameShape(QFrame::NoFrame);
    m_churnTable->setMinimumHeight(160);
    churnLayout->addWidget(m_churnTable);

    churnLayout->addWidget(caption(
        "How many different paths each destination has been reached by. A "
        "destination with many distinct routes is being reached over an unstable "
        "link; one with a single route is settled. Only traceroutes you have "
        "already run appear here - this view never sends any."));
    layout->addWidget(churnGroup);

    layout->addStretch();
    scroll->setWidget(body);
    outer->addWidget(scroll);
}

void AnalyticsWidget::onWindowChanged(int)
{
    refresh();
}

void AnalyticsWidget::refresh()
{
    if (!m_db || !m_db->isOpen())
    {
        m_updatedLabel->setText("No database open");
        return;
    }

    const QDateTime since = windowStart();
    // One instance for the whole refresh; it only holds pointers, but four
    // separate ones also meant reading the traceroute table twice.
    MeshAnalytics analytics(m_db, m_nodes);
    updateDecode(since, analytics);
    updateAirtime(since, analytics);
    updateReachability(since, analytics);
    updateChurn(since, analytics);

    m_updatedLabel->setText("Updated " + QDateTime::currentDateTime().toString("HH:mm:ss"));
}

void AnalyticsWidget::updateDecode(const QDateTime &since, MeshAnalytics &analytics)
{
    const auto stats = analytics.decodeStats(since);

    if (!stats.hasData())
    {
        m_decodeHeadline->setText("No packets recorded in this window");
        m_decodeBar->setValue(0, 1);
        m_decodeByPort->setText(QString());
        m_decodeByChannel->setText(
            "Packet capture is off, or no mesh packets arrived in this window. "
            "Enable \"Save packets to database\" in App Settings to collect this.");
        return;
    }

    m_decodeHeadline->setText(QString("%1 of %2 mesh packets decoded  (%3%)")
                                  .arg(stats.decoded)
                                  .arg(stats.total)
                                  .arg(stats.decodedPercent(), 0, 'f', 1));
    m_decodeBar->setValue(stats.decoded, stats.total);
    m_decodeBar->setFillColor(stats.decodedPercent() >= 50.0 ? Theme::palette().success
                                                             : Theme::palette().warning);

    QStringList ports;
    for (const auto &pc : stats.byPort)
        ports << QString("%1 %2").arg(pc.name).arg(pc.count);
    m_decodeByPort->setText(ports.isEmpty() ? QStringLiteral("Nothing decoded.")
                                            : "Decoded: " + ports.join("  ·  "));

    QStringList channels;
    for (auto it = stats.undecodedByChannel.constBegin();
         it != stats.undecodedByChannel.constEnd(); ++it)
    {
        channels << QString("hash %1: %2").arg(it.key()).arg(it.value());
    }
    m_decodeByChannel->setText(channels.isEmpty()
                                   ? QString()
                                   : QString("%1 undecoded, by channel hash - %2")
                                         .arg(stats.undecoded)
                                         .arg(channels.join("  ·  ")));
}

void AnalyticsWidget::updateAirtime(const QDateTime &since, MeshAnalytics &analytics)
{
    const double limit = dutyCycleLimitPercent();
    const uint32_t myNode = m_nodes ? m_nodes->myNodeNum() : 0;

    const auto samples = analytics.ownAirtime(myNode, since);
    double peak = 0.0;
    for (const auto &s : samples)
        peak = std::max(peak, s.airUtilTx);

    if (samples.isEmpty())
    {
        m_airtimeHeadline->setText("No airtime telemetry for your node in this window");
        m_airtimeBar->setValue(0, std::max(limit, 1.0), limit);
    }
    else
    {
        m_airtimeHeadline->setText(QString("Your node peaked at %1% transmit airtime  "
                                           "(ceiling %2%)")
                                       .arg(peak, 0, 'f', 2)
                                       .arg(limit, 0, 'f', 0));
        m_airtimeBar->setValue(peak, std::max(limit, peak), limit);
        m_airtimeBar->setFillColor(peak >= limit ? Theme::palette().danger
                                   : peak >= limit * 0.5 ? Theme::palette().warning
                                                         : Theme::palette().success);
    }

    const auto nodes = analytics.airtimeByNode(since);
    m_airtimeTable->setRowCount(0);
    int row = 0;
    for (const auto &n : nodes)
    {
        m_airtimeTable->insertRow(row);
        m_airtimeTable->setItem(row, 0, new QTableWidgetItem(n.name));

        auto *peakItem = new QTableWidgetItem(QString("%1%").arg(n.peakAirUtilTx, 0, 'f', 2));
        peakItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (n.peakAirUtilTx >= limit)
            peakItem->setForeground(QBrush(Theme::palette().danger));
        else if (n.peakAirUtilTx >= limit * 0.5)
            peakItem->setForeground(QBrush(Theme::palette().warning));
        m_airtimeTable->setItem(row, 1, peakItem);

        auto *avgItem = new QTableWidgetItem(QString("%1%").arg(n.avgAirUtilTx, 0, 'f', 2));
        avgItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_airtimeTable->setItem(row, 2, avgItem);

        auto *sampleItem = new QTableWidgetItem(QString::number(n.samples));
        sampleItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_airtimeTable->setItem(row, 3, sampleItem);
        row++;
    }
}

void AnalyticsWidget::updateReachability(const QDateTime &since, MeshAnalytics &analytics)
{
    const auto topo = analytics.topology(since);

    m_articulationTable->setRowCount(0);

    if (!topo.hasData())
    {
        m_reachHeadline->setText("No link data in this window");
        m_reachDetail->setText(
            "Links are learned from traceroute responses and from NeighborInfo "
            "broadcasts. Run a traceroute from the node list, or widen the window.");
        return;
    }

    m_reachHeadline->setText(QString("%1 nodes  ·  %2 links  ·  %3 %4")
                                 .arg(topo.nodes.size())
                                 .arg(topo.links.size())
                                 .arg(topo.components.size())
                                 .arg(topo.components.size() == 1 ? "connected group"
                                                                  : "separate groups"));

    QString detail;
    if (topo.components.size() > 1)
    {
        QStringList sizes;
        for (const auto &c : topo.components)
            sizes << QString::number(c.size());
        detail = QString("The observed mesh is not fully connected: groups of %1 nodes. ")
                     .arg(sizes.join(", "));
    }
    if (topo.articulationPoints.isEmpty())
        detail += "No single node's loss would split the mesh.";
    else
        detail += QString("%1 node%2 would split the mesh if lost.")
                      .arg(topo.articulationPoints.size())
                      .arg(topo.articulationPoints.size() == 1 ? "" : "s");
    m_reachDetail->setText(detail);

    // Degree per node, so the table can show how much each one carries
    QMap<uint32_t, int> degree;
    for (const auto &l : topo.links)
    {
        degree[l.a]++;
        degree[l.b]++;
    }

    QList<uint32_t> points = topo.articulationPoints.values();
    std::sort(points.begin(), points.end(), [&](uint32_t a, uint32_t b) {
        return degree.value(a) > degree.value(b);
    });

    int row = 0;
    for (uint32_t node : points)
    {
        m_articulationTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(analytics.nodeLabel(node));
        nameItem->setForeground(QBrush(Theme::palette().warning));
        m_articulationTable->setItem(row, 0, nameItem);

        auto *degItem = new QTableWidgetItem(QString::number(degree.value(node)));
        degItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_articulationTable->setItem(row, 1, degItem);
        row++;
    }
}

void AnalyticsWidget::updateChurn(const QDateTime &since, MeshAnalytics &analytics)
{
    const auto churn = analytics.routeChurn(since);

    m_churnTable->setRowCount(0);
    int row = 0;
    for (const auto &c : churn)
    {
        m_churnTable->insertRow(row);
        m_churnTable->setItem(row, 0, new QTableWidgetItem(c.name));

        auto *obs = new QTableWidgetItem(QString::number(c.observations));
        obs->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_churnTable->setItem(row, 1, obs);

        auto *distinct = new QTableWidgetItem(QString::number(c.distinctRoutes));
        distinct->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_churnTable->setItem(row, 2, distinct);

        auto *pct = new QTableWidgetItem(QString("%1%").arg(c.churnPercent, 0, 'f', 0));
        pct->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // A single observation is not evidence of stability, so only colour
        // once there are enough samples to mean something.
        if (c.observations >= 3)
        {
            pct->setForeground(QBrush(c.churnPercent >= 75.0   ? Theme::palette().danger
                                      : c.churnPercent >= 40.0 ? Theme::palette().warning
                                                               : Theme::palette().success));
        }
        else
        {
            pct->setForeground(QBrush(Theme::palette().textMuted));
            pct->setToolTip("Too few traceroutes to judge stability");
        }
        m_churnTable->setItem(row, 3, pct);

        m_churnTable->setItem(row, 4, new QTableWidgetItem(c.lastRoute));
        row++;
    }
}
