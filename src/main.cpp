#include "historychart.h"
#include "monitor.h"
#include <QCommandLineParser>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <cstdio>
#include <memory>

static void json(const QVariantMap &data) {
    auto bytes = QJsonDocument::fromVariant(data).toJson(QJsonDocument::Indented);
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
}
int main(int argc, char **argv) {
    QCoreApplication::setOrganizationName("DeyeMonitor");
    QCoreApplication::setApplicationName("Deye Monitor");
    QCoreApplication::setApplicationVersion("2.0.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Local Deye energy monitor · C++ / Qt Quick");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(
        {{"snapshot", "Read one complete snapshot as JSON (no GUI)."},
         {"scan", "Discover local loggers as JSON (no GUI)."},
         {"probe", "Export a read-only diagnostic report, including unsupported devices (no GUI)."},
         {"probe-model", "Exact inverter model from its label (probe only).", "model"},
         {"probe-notes", "Reference display readings or operating conditions (probe only).", "notes"},
         {"probe-extended", "Also read bounded known-family ranges for an unsupported device."},
         {"include-identifiers", "Include device identifiers and wire frames in a probe report."},
         {"host", "Logger IPv4 address.", "address"},
         {"serial", "Logger serial number.", "number"},
         {"port", "Solarman TCP port.", "port"},
         {"profile", "Profile: auto, deye-am3 or deye-p3.", "profile"},
         {"no-settings", "Do not load or save settings."},
         {"demo", "Show clearly labelled sample data without connecting."},
         {"capture", "Save a screenshot of this application's window.", "file"},
         {"settings", "Open the Settings window."},
         {"quit-after", "Exit after this many milliseconds.", "milliseconds"}});
    QStringList arguments;
    for (int n = 0; n < argc; ++n)
        arguments.append(QString::fromLocal8Bit(argv[n]));
    auto usageError = [](const QString &message) {
        const auto bytes = ("error: " + message + "\nUse --help for available options.\n").toUtf8();
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
        return 2;
    };
    if (!parser.parse(arguments))
        return usageError(parser.errorText());
    if (!parser.positionalArguments().isEmpty())
        return usageError("Unexpected positional arguments");
    const bool probing = parser.isSet("probe");
    const bool cli = parser.isSet("snapshot") || parser.isSet("scan") || probing;
    if (int(parser.isSet("snapshot")) + int(parser.isSet("scan")) + int(probing) > 1)
        return usageError("Choose one of --snapshot, --scan or --probe");
    if (!probing && (parser.isSet("probe-model") || parser.isSet("probe-notes") ||
                     parser.isSet("probe-extended") || parser.isSet("include-identifiers")))
        return usageError("Probe options require --probe");
    if (cli && (parser.isSet("capture") || parser.isSet("settings")))
        return usageError("--capture and --settings require the desktop interface");
    if ((parser.isSet("scan") || probing) && parser.isSet("demo"))
        return usageError("--scan and --probe cannot be combined with --demo");
    if (parser.isSet("host")) {
        QHostAddress address(parser.value("host"));
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.toIPv4Address() == 0 ||
            address.toIPv4Address() == 0xffffffffu || address.isMulticast())
            return usageError("--host must be a logger IPv4 address");
    }
    if (parser.isSet("serial")) {
        bool valid = false;
        const auto serial = parser.value("serial").toULongLong(&valid);
        if (!valid || serial > 0xffffffffu ||
            !QRegularExpression("^[0-9]+$").match(parser.value("serial")).hasMatch())
            return usageError("--serial must be an unsigned 32-bit number (0 for automatic discovery)");
    }
    if (parser.isSet("port")) {
        bool valid = false;
        const int port = parser.value("port").toInt(&valid);
        if (!valid || port < 1 || port > 65535)
            return usageError("--port must be between 1 and 65535");
    }
    if (parser.isSet("profile") && parser.value("profile") != "auto") {
        Deye::Profiles profiles;
        if (!profiles.find(parser.value("profile")))
            return usageError("Unknown inverter profile: " + parser.value("profile"));
    }
    int quitAfterMs = 0;
    if (parser.isSet("quit-after")) {
        bool valid = false;
        quitAfterMs = parser.value("quit-after").toInt(&valid);
        if (!valid || quitAfterMs < 1)
            return usageError("--quit-after must be a positive number of milliseconds");
    }
    // Parse and validate before loading a GUI platform, so diagnostics also work without a display.
    std::unique_ptr<QCoreApplication> app;
    if (cli || parser.isSet("help") || parser.isSet("version")) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
#ifdef Q_OS_LINUX
        qputenv("QT_WAYLAND_DECORATION", "bradient");
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "wayland");
#endif
        app = std::make_unique<QGuiApplication>(argc, argv);
    }
    if (parser.isSet("help"))
        parser.showHelp();
    if (parser.isSet("version"))
        parser.showVersion();
    Deye::Monitor monitor(!parser.isSet("no-settings") && !parser.isSet("demo"));
    auto settings = monitor.settings();
    for (auto key : {"host", "serial", "port", "profile"})
        if (parser.isSet(key))
            settings[QString(key) == "host" ? "ip" : key] = parser.value(key);
    if (parser.isSet("serial") && parser.value("serial") != monitor.settings().value("serial").toString()) {
        settings.remove("logger_mac");
        settings.remove("inverter_serial");
    }
    if (cli) {
        if (parser.isSet("demo")) {
            monitor.setDemo();
            json(monitor.report());
            return 0;
        }
        if (probing) {
            auto probe = monitor.diagnosticProbe();
            QObject::connect(probe, &Deye::DiagnosticProbe::finished, app.get(), [&, probe] {
                const auto report = probe->report(parser.isSet("include-identifiers"));
                const auto outcome = report.value("outcome").toString();
                json(report);
                app->exit(outcome == "error" || outcome == "cancelled" ? 1 : 0);
            });
            QTimer::singleShot(0, &monitor, [&] {
                monitor.applySettings(settings, false);
                monitor.startProbe(parser.value("probe-model"), parser.value("probe-notes"),
                                   parser.isSet("probe-extended"));
            });
        } else if (parser.isSet("scan")) {
            QObject::connect(&monitor, &Deye::Monitor::scanFinished, app.get(),
                             [&](QVariantList results, double elapsed, QString error) {
                                 json({{"loggers", results}, {"elapsed", elapsed}, {"error", error}});
                                 app->exit(error.isEmpty() ? 0 : 1);
                             });
            QTimer::singleShot(0, &monitor, [&] { monitor.scanSettings(settings); });
        } else {
            QObject::connect(&monitor, &Deye::Monitor::snapshotReady, app.get(), [&](QVariantMap report) {
                json(report);
                app->exit(0);
            });
            QObject::connect(&monitor, &Deye::Monitor::connectionError, app.get(),
                             [&](QString error, bool fatal) {
                                 if (fatal) {
                                     json({{"error", error}});
                                     app->exit(1);
                                 }
                             });
            QTimer::singleShot(0, &monitor, [&] {
                monitor.applySettings(settings, false);
                monitor.start();
            });
        }
        QTimer::singleShot(probing ? 60000 : 20000, app.get(), [&] {
            if (probing && monitor.diagnosticProbe()->running()) {
                monitor.cancelProbe();
                return;
            }
            json({{"error", "Operation timed out"}});
            app->exit(1);
        });
        if (quitAfterMs)
            QTimer::singleShot(quitAfterMs, app.get(), [&] {
                if (probing && monitor.diagnosticProbe()->running()) {
                    monitor.cancelProbe();
                    return;
                }
                json({{"error", "Operation cancelled by --quit-after"}});
                app->exit(1);
            });
        return app->exec();
    }
    QGuiApplication::setDesktopFileName("com.deye.monitor");
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/com.deye.monitor.svg")));
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<HistoryChart>("Deye", 1, 0, "HistoryChart");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("monitor", &monitor);
    if (parser.isSet("demo"))
        monitor.setDemo();
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, app.get(), [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (parser.isSet("settings"))
        QTimer::singleShot(100, app.get(),
                           [&] { QMetaObject::invokeMethod(engine.rootObjects().first(), "openSettings"); });
    if (parser.isSet("capture"))
        QTimer::singleShot(1800, app.get(), [&] {
            auto root = engine.rootObjects().first();
            auto window = parser.isSet("settings") ? root->findChild<QQuickWindow *>("settingsWindow")
                                                   : qobject_cast<QQuickWindow *>(root);
            if (!window || !window->grabWindow().save(parser.value("capture")))
                qWarning("Could not save application screenshot");
        });
    if (quitAfterMs)
        QTimer::singleShot(quitAfterMs, app.get(), &QCoreApplication::quit);
    if (!parser.isSet("demo"))
        QTimer::singleShot(0, &monitor, [&] {
            if (parser.isSet("host") || parser.isSet("serial") || parser.isSet("profile") ||
                parser.isSet("port"))
                monitor.applySettings(settings, false);
            monitor.start();
        });
    return app->exec();
}
