#include "ReplayBar.h"

#include "Theme.h"

#include <QDateTime>
#include <QHBoxLayout>

namespace
{
// Wall-clock milliseconds between ticks. Fine enough to look continuous
// without redrawing the map more often than it can keep up with.
constexpr int kTickMs = 100;
}

ReplayBar::ReplayBar(QWidget *parent) : QWidget(parent)
{
    setupUI();

    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &ReplayBar::onTick);
}

void ReplayBar::setupUI()
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(Theme::Space::md, Theme::Space::sm,
                               Theme::Space::md, Theme::Space::sm);
    layout->setSpacing(Theme::Space::sm);

    auto *label = new QLabel("Replay:");
    label->setStyleSheet(Theme::mutedLabelStyle(12));
    layout->addWidget(label);

    m_windowCombo = new QComboBox;
    m_windowCombo->addItems({"Last hour", "Last 6 hours", "Last 24 hours", "Last 7 days"});
    m_windowCombo->setCurrentIndex(0);
    layout->addWidget(m_windowCombo);

    m_loadButton = new QPushButton("Load");
    connect(m_loadButton, &QPushButton::clicked, this, &ReplayBar::onLoad);
    layout->addWidget(m_loadButton);

    m_playButton = new QPushButton("Play");
    m_playButton->setProperty("accent", true);
    m_playButton->setEnabled(false);
    connect(m_playButton, &QPushButton::clicked, this, &ReplayBar::onPlayPause);
    layout->addWidget(m_playButton);

    m_speedCombo = new QComboBox;
    m_speedCombo->addItem("60x", 60);
    m_speedCombo->addItem("300x", 300);
    m_speedCombo->addItem("1800x", 1800);
    m_speedCombo->setCurrentIndex(1);
    m_speedCombo->setToolTip("How much recorded time passes per second of playback");
    layout->addWidget(m_speedCombo);

    m_scrub = new QSlider(Qt::Horizontal);
    m_scrub->setRange(0, 1000);
    m_scrub->setEnabled(false);
    connect(m_scrub, &QSlider::valueChanged, this, &ReplayBar::onScrub);
    layout->addWidget(m_scrub, 1);

    m_status = new QLabel("No replay loaded");
    m_status->setStyleSheet(Theme::mutedLabelStyle(11));
    m_status->setMinimumWidth(240);
    layout->addWidget(m_status);
}

void ReplayBar::setDatabase(Database *db)
{
    m_db = db;
    stop();
    m_packets.clear();
    m_cursor = 0;
    m_scrub->setEnabled(false);
    m_playButton->setEnabled(false);
    m_status->setText(db ? "No replay loaded" : "No database open");
}

void ReplayBar::onLoad()
{
    stop();

    if (!m_db || !m_db->isOpen())
    {
        m_status->setText("No database open");
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime from;
    switch (m_windowCombo->currentIndex())
    {
    case 0: from = now.addSecs(-3600); break;
    case 1: from = now.addSecs(-6 * 3600); break;
    case 2: from = now.addDays(-1); break;
    default: from = now.addDays(-7); break;
    }

    m_startMs = from.toMSecsSinceEpoch();
    m_endMs = now.toMSecsSinceEpoch();
    m_packets = m_db->loadPacketsInRange(m_startMs, m_endMs);
    m_cursor = 0;
    m_virtualMs = m_startMs;

    if (m_packets.isEmpty())
    {
        m_status->setText("No recorded mesh packets in this window");
        m_scrub->setEnabled(false);
        m_playButton->setEnabled(false);
        return;
    }

    // Tighten the range to the data actually present, so a quiet start does not
    // leave the user watching an empty timeline.
    m_startMs = m_packets.first().timestamp;
    m_endMs = m_packets.last().timestamp;
    m_virtualMs = m_startMs;

    {
        QSignalBlocker block(m_scrub);
        m_scrub->setValue(0);
    }
    m_scrub->setEnabled(true);
    m_playButton->setEnabled(true);
    m_status->setText(QString("%1 packets loaded  ·  %2")
                          .arg(m_packets.size())
                          .arg(describePosition()));
}

void ReplayBar::onPlayPause()
{
    if (m_packets.isEmpty())
        return;

    if (m_playing)
    {
        stop();
        return;
    }

    // Starting from the end would show nothing, so wrap round
    if (m_virtualMs >= m_endMs)
    {
        m_virtualMs = m_startMs;
        m_cursor = 0;
        QSignalBlocker block(m_scrub);
        m_scrub->setValue(0);
    }

    m_playing = true;
    m_playButton->setText("Pause");
    emit replayStarted();
    m_timer->start();
}

void ReplayBar::stop()
{
    if (m_timer)
        m_timer->stop();
    if (!m_playing)
        return;
    m_playing = false;
    if (m_playButton)
        m_playButton->setText("Play");
    emit replayStopped();
}

void ReplayBar::onScrub(int value)
{
    if (m_packets.isEmpty() || m_endMs <= m_startMs)
        return;

    const qint64 target = m_startMs + (m_endMs - m_startMs) * value / 1000;

    // Jumping should not replay everything in between, so move the cursor
    // silently and carry on from there.
    m_virtualMs = target;
    m_cursor = 0;
    while (m_cursor < m_packets.size() && m_packets[m_cursor].timestamp < target)
        ++m_cursor;

    m_status->setText(QString("%1 packets loaded  ·  %2")
                          .arg(m_packets.size())
                          .arg(describePosition()));
}

void ReplayBar::onTick()
{
    if (m_packets.isEmpty())
    {
        stop();
        return;
    }

    const int speed = m_speedCombo->currentData().toInt();
    applyPosition(m_virtualMs + static_cast<qint64>(kTickMs) * speed, true);

    if (m_virtualMs >= m_endMs)
    {
        stop();
        m_status->setText(QString("%1 packets  ·  replay finished").arg(m_packets.size()));
    }
}

void ReplayBar::applyPosition(qint64 newVirtualMs, bool emitPackets)
{
    m_virtualMs = qMin(newVirtualMs, m_endMs);

    if (emitPackets)
    {
        while (m_cursor < m_packets.size() && m_packets[m_cursor].timestamp <= m_virtualMs)
        {
            const auto &p = m_packets[m_cursor];
            emit packetReplayed(p.fromNode, p.toNode, p.portNum);
            ++m_cursor;
        }
    }

    if (m_endMs > m_startMs)
    {
        QSignalBlocker block(m_scrub);
        m_scrub->setValue(static_cast<int>(1000 * (m_virtualMs - m_startMs)
                                           / (m_endMs - m_startMs)));
    }

    m_status->setText(QString("%1 / %2  ·  %3")
                          .arg(m_cursor)
                          .arg(m_packets.size())
                          .arg(describePosition()));
}

QString ReplayBar::describePosition() const
{
    return QDateTime::fromMSecsSinceEpoch(m_virtualMs).toString("MMM d  HH:mm:ss");
}
