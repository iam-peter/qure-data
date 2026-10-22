#include "PatchDocument.hpp"
#include "Session.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>

#include <memory>

// The Qure QML module lives in a static library; pull in its plugin.
Q_IMPORT_QML_PLUGIN(QurePlugin)

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("QureData"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Qure Data"));
    QGuiApplication::setOrganizationName(QStringLiteral("QureData"));
    QGuiApplication::setApplicationVersion(QStringLiteral(QURE_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    // Distance-field text scales cleanly when the canvas zooms.
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A visual programming environment for audio, compatible with Pd patches."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dark(QStringLiteral("dark"), QStringLiteral("Use the dark theme."));
    QCommandLineOption light(QStringLiteral("light"), QStringLiteral("Use the light theme."));
    QCommandLineOption noAudio(QStringLiteral("no-audio"),
                               QStringLiteral("Never open an audio device (the engine runs silently on a timer)."));
    parser.addOption(dark);
    parser.addOption(light);
    parser.addOption(noAudio);
    QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                  QStringLiteral("Save every window to <prefix>-N.png after <delay> ms and quit (for tests)."),
                                  QStringLiteral("prefix"));
    QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Delay for --screenshot in ms."),
                             QStringLiteral("ms"), QStringLiteral("1500"));
    QCommandLineOption interval_(QStringLiteral("interval"), QStringLiteral("Take a screenshot every <ms>."),
                                 QStringLiteral("ms"), QStringLiteral("0"));
    QCommandLineOption count(QStringLiteral("count"), QStringLiteral("Number of screenshots with --interval."),
                             QStringLiteral("n"), QStringLiteral("10"));
    parser.addOption(screenshot);
    parser.addOption(delay);
    parser.addOption(interval_);
    parser.addOption(count);
    parser.addPositionalArgument(QStringLiteral("patches"), QStringLiteral("Pd patches to open."), QStringLiteral("[file.pd...]"));
    parser.process(app);

    qure::ui::Session session(nullptr, parser.isSet(noAudio) || qEnvironmentVariableIsSet("QURE_NO_AUDIO")
                                          ? qure::ui::Session::Audio::Silent
                                          : qure::ui::Session::Audio::Device);
    // Follow the desktop unless told otherwise.
    const bool systemDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    session.setDarkTheme(parser.isSet(dark) || (systemDark && !parser.isSet(light)));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Qure", "Main");

    for (const auto& file : parser.positionalArguments())
        if (auto* doc = session.openPatch(QUrl::fromLocalFile(file)))
            session.openCanvas(doc->root());

    if (parser.isSet(screenshot)) {
        // Test hook: grab every visible window to <prefix>-<shot>-<window>.png
        // every <interval> ms (or once after <delay> ms), quit after the last.
        const QString prefix = parser.value(screenshot);
        const int interval = parser.value(interval_).toInt();
        const int shots = interval > 0 ? parser.value(count).toInt() : 1;
        auto* timer = new QTimer(&app);
        auto shot = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, &app, [=] {
            int n = 0;
            for (QWindow* w : QGuiApplication::topLevelWindows())
                if (auto* qw = qobject_cast<QQuickWindow*>(w); qw && qw->isVisible())
                    qw->grabWindow().save(QStringLiteral("%1-%2-%3.png").arg(prefix).arg(*shot).arg(n++));
            if (++*shot >= shots)
                QCoreApplication::quit();
            else
                timer->start(interval);
        });
        timer->setSingleShot(true);
        timer->start(parser.value(delay).toInt());
    }

    const int result = QGuiApplication::exec();
    // Windows go first, so no view outlives its document.
    engine.clearComponentCache();
    return result;
}
