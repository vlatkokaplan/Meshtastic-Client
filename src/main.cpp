#include <QApplication>
#include <QStyleFactory>
#include <QIcon>
#include <QFontDatabase>
#include <QFont>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    // Ensure proper window decorations on Wayland
    // If decorations are missing, run with: QT_QPA_PLATFORM=xcb ./meshtastic-client
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        // Prefer X11 for better compatibility with window decorations
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    QApplication app(argc, argv);

    app.setApplicationName("Meshtastic Client");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Meshtastic");

    // Set application icon
    app.setWindowIcon(QIcon(":/icon.svg"));

    // Enable emoji support - use a font that supports color emojis
    // Noto Color Emoji or Segoe UI Emoji are good choices
    QFont defaultFont = app.font();
    QStringList families = defaultFont.families();
    // Add emoji fonts as fallbacks
    families << "Noto Color Emoji" << "Segoe UI Emoji" << "Apple Color Emoji" << "EmojiOne";
    defaultFont.setFamilies(families);
    app.setFont(defaultFont);

    // Don't force Fusion - use native style for proper Ubuntu look
    // Users can override with: QT_STYLE_OVERRIDE=Fusion ./meshtastic-client

    MainWindow window;
    window.show();

    return app.exec();
}
