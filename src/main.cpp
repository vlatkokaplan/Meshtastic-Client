#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLoggingCategory>
#include "MainWindow.h"

// Custom message handler for debug logging
static bool debugEnabled = false;

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Always show warnings, critical, and fatal messages
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
    {
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
        return;
    }

    // Only show debug messages if --debug flag is set
    if (type == QtDebugMsg && debugEnabled)
    {
        fprintf(stdout, "[DEBUG] %s\n", msg.toLocal8Bit().constData());
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Meshtastic Client");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Meshtastic");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Meshtastic mesh network client");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption debugOption(QStringList() << "d" << "debug",
                                   "Enable debug logging to console");
    parser.addOption(debugOption);

    parser.process(app);

    // Set debug mode based on command line flag
    debugEnabled = parser.isSet(debugOption);
    qInstallMessageHandler(customMessageHandler);

    if (debugEnabled)
    {
        qDebug() << "Debug logging enabled";
    }

    app.setWindowIcon(QIcon(":/icon.svg"));

    MainWindow window;
    window.show();

    return app.exec();
}
