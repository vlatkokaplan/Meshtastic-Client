#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>

// Single source of truth for the application's visual design.
//
// Everything that draws should pull its colours from here rather than
// hardcoding hex values, so light and dark stay consistent and a palette
// change happens in one place.
namespace Theme
{

// Semantic colour roles. Named for what they mean, not what they look like.
struct Palette
{
    QColor window;        // app background
    QColor surface;       // panels, tables, cards
    QColor surfaceAlt;    // alternating rows, subtle fills
    QColor elevated;      // toolbars, headers
    QColor border;        // hairlines and outlines
    QColor borderStrong;  // focused / emphasised outlines

    QColor text;          // primary text
    QColor textMuted;     // secondary text, captions
    QColor textDisabled;

    QColor accent;        // primary action, selection, focus ring
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;    // text drawn on top of accent

    QColor success;       // good signal, healthy battery, online
    QColor warning;       // degraded
    QColor danger;        // failure, low battery
    QColor info;

    QColor selection;     // row selection background
    QColor selectionText;
};

// Spacing scale (px). Use these instead of arbitrary numbers.
namespace Space
{
constexpr int xs = 4;
constexpr int sm = 6;
constexpr int md = 10;
constexpr int lg = 16;
constexpr int xl = 24;
}

namespace Radius
{
constexpr int sm = 4;
constexpr int md = 6;
constexpr int lg = 10;
}

// The palette for the active mode. Set by apply().
const Palette &palette();
bool isDark();

// Palette for a specific mode, regardless of what's active.
const Palette &paletteFor(bool dark);

// Build the application stylesheet for a mode.
QString styleSheet(bool dark);

// Apply the stylesheet and matching QPalette to the whole application, and
// record the mode so palette() reflects it.
void apply(bool dark);

// Ready-made stylesheets for the label roles that repeat across every tab, so
// widgets stop hardcoding "color: gray" (which is unreadable in dark mode).
QString mutedLabelStyle(int fontSizePx = 11);
QString statusLabelStyle();

// Battery level -> colour, shared by the node table, device panel and map.
QColor batteryColor(int percent, bool externalPower = false);

// SNR -> colour on the same success/warning/danger scale.
QColor signalColor(float snr);

} // namespace Theme

#endif // THEME_H
