#include "Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPalette>

namespace Theme
{

namespace
{

// Light: warm-neutral greys so large white panels don't glare, with a blue
// accent that stays legible on both fills and text.
const Palette kLight = {
    /* window        */ QColor("#f4f5f7"),
    /* surface       */ QColor("#ffffff"),
    /* surfaceAlt    */ QColor("#fafbfc"),
    /* elevated      */ QColor("#ffffff"),
    /* border        */ QColor("#e3e6ea"),
    /* borderStrong  */ QColor("#c8ced6"),

    /* text          */ QColor("#1c2530"),
    /* textMuted     */ QColor("#6b7683"),
    /* textDisabled  */ QColor("#a8b1bb"),

    /* accent        */ QColor("#2f6fd0"),
    /* accentHover   */ QColor("#3b7ce0"),
    /* accentPressed */ QColor("#2559ad"),
    /* accentText    */ QColor("#ffffff"),

    /* success       */ QColor("#1f9254"),
    /* warning       */ QColor("#c77700"),
    /* danger        */ QColor("#cf3b3b"),
    /* info          */ QColor("#2f6fd0"),

    /* selection     */ QColor("#dfe9f9"),
    /* selectionText */ QColor("#1c2530"),
};

// Dark: lifted off pure black so the map and charts don't float, with the
// accent brightened to keep contrast against the darker surfaces.
const Palette kDark = {
    /* window        */ QColor("#16191d"),
    /* surface       */ QColor("#1d2126"),
    /* surfaceAlt    */ QColor("#22272d"),
    /* elevated      */ QColor("#23282e"),
    /* border        */ QColor("#31373f"),
    /* borderStrong  */ QColor("#454d57"),

    /* text          */ QColor("#e2e6ea"),
    /* textMuted     */ QColor("#939ca7"),
    /* textDisabled  */ QColor("#5d666f"),

    /* accent        */ QColor("#4d8ee6"),
    /* accentHover   */ QColor("#639ded"),
    /* accentPressed */ QColor("#3c78c9"),
    /* accentText    */ QColor("#ffffff"),

    /* success       */ QColor("#3dbd75"),
    /* warning       */ QColor("#e0a03c"),
    /* danger        */ QColor("#e56767"),
    /* info          */ QColor("#4d8ee6"),

    /* selection     */ QColor("#26405e"),
    /* selectionText */ QColor("#ffffff"),
};

bool g_dark = false;

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

// rgba() string for translucent fills in stylesheets
QString rgba(const QColor &c, double alpha)
{
    return QString("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha, 0, 'f', 2);
}

} // namespace

const Palette &paletteFor(bool dark) { return dark ? kDark : kLight; }
const Palette &palette() { return paletteFor(g_dark); }
bool isDark() { return g_dark; }

QString mutedLabelStyle(int fontSizePx)
{
    return QString("color: %1; font-size: %2px;")
        .arg(palette().textMuted.name())
        .arg(fontSizePx);
}

QString statusLabelStyle()
{
    return QString("color: %1;").arg(palette().textMuted.name());
}

QColor batteryColor(int percent, bool externalPower)
{
    const Palette &p = palette();
    if (externalPower)
        return p.info;
    if (percent <= 0)
        return p.textMuted;
    if (percent < 20)
        return p.danger;
    if (percent < 50)
        return p.warning;
    return p.success;
}

QColor signalColor(float snr)
{
    const Palette &p = palette();
    if (snr >= 5.0f)
        return p.success;
    if (snr >= 0.0f)
        return p.warning;
    return p.danger;
}

QIcon positionPin(const QColor &color)
{
    const int s = 12;
    QPixmap pm(s * 2, s * 2);           // 2x for crisp edges on hidpi
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // Teardrop pin: circle head over a short point
    QPainterPath path;
    path.addEllipse(QPointF(s, s * 0.85), s * 0.55, s * 0.55);
    path.moveTo(s - s * 0.33, s * 1.2);
    path.lineTo(s, s * 1.85);
    path.lineTo(s + s * 0.33, s * 1.2);
    path.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path.simplified());

    // hollow centre so it reads as a pin, not a blob
    p.setBrush(palette().surface);
    p.drawEllipse(QPointF(s, s * 0.85), s * 0.2, s * 0.2);
    p.end();

    return QIcon(pm);
}

QIcon batteryPip(int percent, bool externalPower)
{
    const int w = 26, h = 12;
    QPixmap pm(w * 2, h * 2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor fill = batteryColor(percent, externalPower);
    QRectF body(1, 1, w * 2 - 6, h * 2 - 2);

    p.setPen(QPen(fill, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 3, 3);

    // terminal nub
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(body.right() + 1, h * 0.6, 3, h * 0.8), 1, 1);

    // charge level, or a full bar when running on external power
    double frac = externalPower ? 1.0 : qBound(0, percent, 100) / 100.0;
    if (frac > 0.0)
    {
        QRectF inner = body.adjusted(3, 3, -3, -3);
        inner.setWidth(inner.width() * frac);
        p.drawRoundedRect(inner, 1, 1);
    }
    p.end();

    return QIcon(pm);
}

QString styleSheet(bool dark)
{
    const Palette &p = paletteFor(dark);

    return QString(R"(
/* ---------- base ---------- */
QWidget {
    background-color: %{window};
    color: %{text};
}
QMainWindow, QDialog {
    background-color: %{window};
}
QToolTip {
    background-color: %{elevated};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{rsm}px;
    padding: %{xs}px %{sm}px;
}

/* ---------- tabs: flat, with an accent underline on the active tab ---------- */
QTabWidget::pane {
    border: none;
    border-top: 1px solid %{border};
    background-color: %{window};
}
QTabBar {
    background: transparent;
    qproperty-drawBase: 0;
}
QTabBar::tab {
    background: transparent;
    color: %{textMuted};
    padding: %{sm}px %{lg}px;
    margin-right: 2px;
    border: none;
    border-bottom: 2px solid transparent;
    font-weight: 500;
}
QTabBar::tab:hover {
    color: %{text};
    background-color: %{surfaceAlt};
}
QTabBar::tab:selected {
    color: %{accent};
    border-bottom: 2px solid %{accent};
}

/* ---------- toolbar ---------- */
QToolBar {
    background-color: %{elevated};
    border: none;
    border-bottom: 1px solid %{border};
    padding: %{sm}px %{md}px;
    spacing: %{sm}px;
}
QToolBar::separator {
    background-color: %{border};
    width: 1px;
    margin: %{xs}px %{sm}px;
}

/* ---------- buttons ---------- */
QPushButton {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{borderStrong};
    border-radius: %{rsm}px;
    padding: %{sm}px %{lg}px;
    font-weight: 500;
    min-height: 16px;
}
QPushButton:hover {
    background-color: %{surfaceAlt};
    border-color: %{accent};
}
QPushButton:pressed {
    background-color: %{selection};
}
QPushButton:disabled {
    color: %{textDisabled};
    border-color: %{border};
    background-color: %{window};
}
/* Primary action: set property("accent", true) on the button */
QPushButton[accent="true"] {
    background-color: %{accent};
    color: %{accentText};
    border: 1px solid %{accent};
}
QPushButton[accent="true"]:hover {
    background-color: %{accentHover};
    border-color: %{accentHover};
}
QPushButton[accent="true"]:pressed {
    background-color: %{accentPressed};
}
QPushButton[accent="true"]:disabled {
    background-color: %{border};
    border-color: %{border};
    color: %{textDisabled};
}
/* Destructive action: set property("danger", true) */
QPushButton[danger="true"] {
    color: %{danger};
    border-color: %{danger};
    background-color: transparent;
}
QPushButton[danger="true"]:hover {
    background-color: %{dangerWash};
}

/* ---------- inputs ---------- */
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{borderStrong};
    border-radius: %{rsm}px;
    padding: %{sm}px %{md}px;
    selection-background-color: %{accent};
    selection-color: %{accentText};
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid %{accent};
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: %{textDisabled};
    background-color: %{window};
}
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox QAbstractItemView {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border};
    selection-background-color: %{selection};
    selection-color: %{selectionText};
    outline: none;
}

/* ---------- item views ---------- */
QTableWidget, QTableView, QListWidget, QListView, QTreeWidget, QTreeView {
    background-color: %{surface};
    alternate-background-color: %{surfaceAlt};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{rmd}px;
    gridline-color: transparent;
    outline: none;
    selection-background-color: %{selection};
    selection-color: %{selectionText};
}
QTableWidget::item, QTableView::item,
QListWidget::item, QTreeWidget::item, QTreeView::item {
    padding: %{sm}px %{xs}px;
    border: none;
}
QTableWidget::item:hover, QListWidget::item:hover, QTreeWidget::item:hover {
    background-color: %{hoverWash};
}
QTableWidget::item:selected, QTableView::item:selected,
QListWidget::item:selected, QTreeWidget::item:selected, QTreeView::item:selected {
    background-color: %{selection};
    color: %{selectionText};
}
QHeaderView {
    background-color: transparent;
}
QHeaderView::section {
    background-color: %{elevated};
    color: %{textMuted};
    border: none;
    border-bottom: 1px solid %{border};
    padding: %{sm}px %{xs}px;
    font-weight: 600;
}
QHeaderView::section:hover {
    color: %{text};
}

/* ---------- group boxes ---------- */
QGroupBox {
    background-color: %{surface};
    border: 1px solid %{border};
    border-radius: %{rmd}px;
    margin-top: %{lg}px;
    padding: %{lg}px %{md}px %{md}px %{md}px;
    font-weight: 600;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: %{md}px;
    top: %{xs}px;
    padding: 0 %{xs}px;
    color: %{textMuted};
}

/* ---------- progress ---------- */
QProgressBar {
    background-color: %{surfaceAlt};
    border: 1px solid %{border};
    border-radius: %{rsm}px;
    height: 8px;
    text-align: center;
    color: %{text};
}
QProgressBar::chunk {
    background-color: %{accent};
    border-radius: 3px;
}

/* ---------- checkboxes ---------- */
QCheckBox, QRadioButton {
    color: %{text};
    spacing: %{sm}px;
    padding: 2px 0;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px;
    height: 15px;
}
QCheckBox::indicator:unchecked {
    border: 1px solid %{borderStrong};
    border-radius: 3px;
    background-color: %{surface};
}
QCheckBox::indicator:checked {
    border: 1px solid %{accent};
    border-radius: 3px;
    background-color: %{accent};
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover {
    border-color: %{accent};
}

/* ---------- scrollbars: thin and unobtrusive ---------- */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background-color: %{scrollHandle};
    border-radius: 5px;
    min-height: 28px;
}
QScrollBar::handle:vertical:hover {
    background-color: %{scrollHandleHover};
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background-color: %{scrollHandle};
    border-radius: 5px;
    min-width: 28px;
}
QScrollBar::handle:horizontal:hover {
    background-color: %{scrollHandleHover};
}
QScrollBar::add-line, QScrollBar::sub-line {
    height: 0; width: 0; border: none; background: none;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

/* ---------- menus ---------- */
QMenuBar {
    background-color: %{elevated};
    color: %{text};
    border-bottom: 1px solid %{border};
}
QMenuBar::item {
    padding: %{sm}px %{md}px;
    background: transparent;
}
QMenuBar::item:selected {
    background-color: %{selection};
    border-radius: %{rsm}px;
}
QMenu {
    background-color: %{surface};
    color: %{text};
    border: 1px solid %{border};
    border-radius: %{rmd}px;
    padding: %{xs}px;
}
QMenu::item {
    padding: %{sm}px %{lg}px;
    border-radius: %{rsm}px;
}
QMenu::item:selected {
    background-color: %{selection};
    color: %{selectionText};
}
QMenu::item:disabled {
    color: %{textDisabled};
}
QMenu::separator {
    height: 1px;
    background-color: %{border};
    margin: %{xs}px %{sm}px;
}

/* ---------- misc chrome ---------- */
QStatusBar {
    background-color: %{elevated};
    color: %{textMuted};
    border-top: 1px solid %{border};
}
QStatusBar::item {
    border: none;
}
QSplitter::handle {
    background-color: %{border};
}
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical   { height: 1px; }
QSplitter::handle:hover {
    background-color: %{accent};
}
)")
        .replace("%{window}", hex(p.window))
        .replace("%{surfaceAlt}", hex(p.surfaceAlt))
        .replace("%{surface}", hex(p.surface))
        .replace("%{elevated}", hex(p.elevated))
        .replace("%{borderStrong}", hex(p.borderStrong))
        .replace("%{border}", hex(p.border))
        .replace("%{textMuted}", hex(p.textMuted))
        .replace("%{textDisabled}", hex(p.textDisabled))
        .replace("%{text}", hex(p.text))
        .replace("%{accentHover}", hex(p.accentHover))
        .replace("%{accentPressed}", hex(p.accentPressed))
        .replace("%{accentText}", hex(p.accentText))
        .replace("%{accent}", hex(p.accent))
        .replace("%{danger}", hex(p.danger))
        .replace("%{dangerWash}", rgba(p.danger, 0.12))
        .replace("%{selectionText}", hex(p.selectionText))
        .replace("%{selection}", hex(p.selection))
        .replace("%{hoverWash}", rgba(p.accent, dark ? 0.10 : 0.06))
        .replace("%{scrollHandleHover}", hex(p.textMuted))
        .replace("%{scrollHandle}", hex(p.borderStrong))
        .replace("%{rsm}", QString::number(Radius::sm))
        .replace("%{rmd}", QString::number(Radius::md))
        .replace("%{xs}", QString::number(Space::xs))
        .replace("%{sm}", QString::number(Space::sm))
        .replace("%{md}", QString::number(Space::md))
        .replace("%{lg}", QString::number(Space::lg));
}

void apply(bool dark)
{
    g_dark = dark;
    const Palette &p = paletteFor(dark);

    // Qt draws some things (QWebEngineView chrome, native dialogs, item view
    // fallbacks) from QPalette rather than the stylesheet, so set both.
    QPalette qp;
    qp.setColor(QPalette::Window, p.window);
    qp.setColor(QPalette::WindowText, p.text);
    qp.setColor(QPalette::Base, p.surface);
    qp.setColor(QPalette::AlternateBase, p.surfaceAlt);
    qp.setColor(QPalette::Text, p.text);
    qp.setColor(QPalette::PlaceholderText, p.textMuted);
    qp.setColor(QPalette::Button, p.surface);
    qp.setColor(QPalette::ButtonText, p.text);
    qp.setColor(QPalette::Highlight, p.accent);
    qp.setColor(QPalette::HighlightedText, p.accentText);
    qp.setColor(QPalette::ToolTipBase, p.elevated);
    qp.setColor(QPalette::ToolTipText, p.text);
    qp.setColor(QPalette::Link, p.accent);
    qp.setColor(QPalette::Disabled, QPalette::Text, p.textDisabled);
    qp.setColor(QPalette::Disabled, QPalette::ButtonText, p.textDisabled);
    qp.setColor(QPalette::Disabled, QPalette::WindowText, p.textDisabled);
    qApp->setPalette(qp);

    qApp->setStyleSheet(styleSheet(dark));
}

} // namespace Theme
