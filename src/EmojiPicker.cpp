#include "EmojiPicker.h"

#include "AppSettings.h"
#include "Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace
{

// Enough to cover what people actually send, grouped so the grid reads at a
// glance rather than being one undifferentiated block.
const QStringList kApproval  = {"\U0001F44D", "❤️", "\U0001F525", "\U0001F389",
                                "\U0001F44F", "✅", "\U0001F4AF", "\U0001F64C"};
const QStringList kFeeling   = {"\U0001F602", "\U0001F604", "\U0001F609", "\U0001F914",
                                "\U0001F62E", "\U0001F622", "\U0001F621", "\U0001F60E"};
const QStringList kMesh      = {"\U0001F4E1", "\U0001F50B", "\U0001F5FA️", "\U0001F6A8",
                                "⚡", "\U0001F4CD", "\U0001F44B", "☕"};

constexpr int kRecentCount = 8;
const char *kRecentKey = "messages/recent_emoji";

QToolButton *makeButton(const QString &emoji, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(emoji);
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFixedSize(34, 34);

    // Name the emoji families explicitly; Qt does not reliably fall back to a
    // colour emoji font and the glyphs would draw as boxes.
    QFont f = b->font();
    QStringList families = f.families();
    if (families.isEmpty())
        families << f.family();
    families << "Noto Color Emoji" << "Apple Color Emoji" << "Segoe UI Emoji";
    f.setFamilies(families);
    f.setPointSizeF(f.pointSizeF() * 1.6);
    b->setFont(f);

    b->setStyleSheet(QString("QToolButton { border: none; border-radius: 6px; }"
                             "QToolButton:hover { background-color: %1; }")
                         .arg(Theme::palette().selection.name()));
    return b;
}

} // namespace

EmojiPicker::EmojiPicker(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QString("EmojiPicker { background-color: %1; border: 1px solid %2; "
                          "border-radius: %3px; }")
                      .arg(Theme::palette().surface.name(),
                           Theme::palette().border.name())
                      .arg(Theme::Radius::lg));
    build();
}

QStringList EmojiPicker::recentlyUsed()
{
    return AppSettings::instance()->value(kRecentKey, QString()).toString()
        .split('\u001f', Qt::SkipEmptyParts);
}

void EmojiPicker::noteUsed(const QString &emoji)
{
    QStringList recent = recentlyUsed();
    recent.removeAll(emoji);
    recent.prepend(emoji);
    while (recent.size() > kRecentCount)
        recent.removeLast();
    // Unit separator: emoji contain no control characters, so nothing collides
    AppSettings::instance()->setValue(kRecentKey, recent.join('\u001f'));
}

QWidget *EmojiPicker::makeRow(const QStringList &emoji)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    for (const QString &e : emoji)
    {
        QToolButton *b = makeButton(e, row);
        connect(b, &QToolButton::clicked, this, [this, e]() {
            noteUsed(e);
            emit picked(e);
            close();
        });
        layout->addWidget(b);
    }
    layout->addStretch();
    return row;
}

void EmojiPicker::build()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::Space::sm, Theme::Space::sm,
                               Theme::Space::sm, Theme::Space::sm);
    layout->setSpacing(Theme::Space::xs);

    auto caption = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setStyleSheet(QString("color: %1; font-size: 10px; letter-spacing: 1px;")
                             .arg(Theme::palette().textMuted.name()));
        return l;
    };

    const QStringList recent = recentlyUsed();
    if (!recent.isEmpty())
    {
        layout->addWidget(caption("RECENT"));
        m_recentRow = makeRow(recent);
        layout->addWidget(m_recentRow);
    }

    layout->addWidget(caption("REACTIONS"));
    layout->addWidget(makeRow(kApproval));
    layout->addWidget(makeRow(kFeeling));
    layout->addWidget(makeRow(kMesh));
}

void EmojiPicker::popupAt(const QPoint &globalPos)
{
    adjustSize();

    QPoint pos = globalPos;
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)
                              ? QGuiApplication::screenAt(globalPos)
                              : QGuiApplication::primaryScreen())
    {
        // Keep it on screen: a picker opened near an edge would otherwise be
        // partly unreachable.
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left(), pos.x(), avail.right() - width()));
        pos.setY(qBound(avail.top(), pos.y(), avail.bottom() - height()));
    }

    move(pos);
    show();
}
