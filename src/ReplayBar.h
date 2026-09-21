#ifndef REPLAYBAR_H
#define REPLAYBAR_H

#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include "Database.h"

// Replays recorded mesh activity over a chosen window.
//
// Purely a reader: it walks rows already in the packets table and emits what it
// finds, so the map can show who was talking and when. It never sends anything,
// and it does not touch the live view's own state.
class ReplayBar : public QWidget
{
    Q_OBJECT

public:
    explicit ReplayBar(QWidget *parent = nullptr);

    void setDatabase(Database *db);

signals:
    // One recorded packet, as it happened. toNode is 0xFFFFFFFF for broadcasts.
    void packetReplayed(uint32_t fromNode, uint32_t toNode, int portNum);
    // Emitted when a replay starts or stops so the view can clear itself
    void replayStarted();
    void replayStopped();

private slots:
    void onPlayPause();
    void onLoad();
    void onScrub(int value);
    void onTick();

private:
    void setupUI();
    void stop();
    void applyPosition(qint64 newVirtualMs, bool emitPackets);
    QString describePosition() const;

    Database *m_db = nullptr;

    QList<Database::PacketRecord> m_packets;
    qint64 m_startMs = 0;
    qint64 m_endMs = 0;
    qint64 m_virtualMs = 0;
    int m_cursor = 0;          // index of the next packet to emit
    bool m_playing = false;
    static constexpr int kPacketLimit = 20000;
    bool m_truncated = false;

    QComboBox *m_windowCombo = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QPushButton *m_loadButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QSlider *m_scrub = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_timer = nullptr;
};

#endif // REPLAYBAR_H
