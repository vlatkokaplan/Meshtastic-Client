#ifndef EMOJIPICKER_H
#define EMOJIPICKER_H

#include <QFrame>
#include <QStringList>

// A small popup for picking a reaction.
//
// Reacting used to mean right-click, open a submenu, choose from six fixed
// emoji. This shows a grid, remembers what you actually use, and closes on the
// first click - a reaction should cost one gesture, not three.
class EmojiPicker : public QFrame
{
    Q_OBJECT

public:
    explicit EmojiPicker(QWidget *parent = nullptr);

    // Shows the picker near `globalPos`, kept on screen.
    void popupAt(const QPoint &globalPos);

    // Records a use so it surfaces in the recent row next time.
    static void noteUsed(const QString &emoji);
    static QStringList recentlyUsed();

signals:
    void picked(const QString &emoji);

private:
    void build();
    QWidget *makeRow(const QStringList &emoji);

    QWidget *m_recentRow = nullptr;
};

#endif // EMOJIPICKER_H
