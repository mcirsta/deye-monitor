#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QtTest>

class CliTest : public QObject {
    Q_OBJECT
private:
    struct Result {
        int code;
        QProcess::ExitStatus status;
        QByteArray output, errors;
    };
    Result run(QStringList arguments) {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("DISPLAY");
        environment.remove("WAYLAND_DISPLAY");
        environment.insert("QT_QPA_PLATFORM", "no-such-display-platform");
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(DEYE_EXECUTABLE), arguments);
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
            return {-1, QProcess::CrashExit, process.readAllStandardOutput(), process.readAllStandardError()};
        }
        return {process.exitCode(), process.exitStatus(), process.readAllStandardOutput(),
                process.readAllStandardError()};
    }
private slots:
    void headlessHelpAndVersion() {
        for (const auto &option : {"--help", "--version"}) {
            const auto result = run({option, "--no-settings"});
            QCOMPARE(result.status, QProcess::NormalExit);
            QCOMPARE(result.code, 0);
            QVERIFY(!result.output.isEmpty());
            QVERIFY(!result.errors.contains("platform plugin"));
        }
    }
    void invalidArguments_data() {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("unknown option") << QStringList{"--unknown"};
        QTest::newRow("positional argument") << QStringList{"unexpected"};
        QTest::newRow("invalid host") << QStringList{"--host", "bad-host"};
        QTest::newRow("zero host") << QStringList{"--host", "0.0.0.0"};
        QTest::newRow("multicast host") << QStringList{"--host", "224.0.0.1"};
        QTest::newRow("negative port") << QStringList{"--port", "-1"};
        QTest::newRow("large port") << QStringList{"--port", "65536"};
        QTest::newRow("overflow serial") << QStringList{"--serial", "4294967296"};
        QTest::newRow("negative serial") << QStringList{"--serial", "-1"};
        QTest::newRow("bad profile") << QStringList{"--profile", "not-a-profile"};
        QTest::newRow("bad deadline") << QStringList{"--quit-after", "0"};
        QTest::newRow("conflicting modes") << QStringList{"--scan", "--snapshot"};
        QTest::newRow("capture without GUI") << QStringList{"--snapshot", "--capture", "x.png"};
        QTest::newRow("demo network scan") << QStringList{"--scan", "--demo"};
        QTest::newRow("demo probe") << QStringList{"--probe", "--demo"};
        QTest::newRow("conflicting probe modes") << QStringList{"--probe", "--snapshot"};
        QTest::newRow("probe-only options") << QStringList{"--probe-model", "SUN-UNKNOWN"};
    }
    void invalidArguments() {
        QFETCH(QStringList, arguments);
        arguments.append("--no-settings");
        const auto result = run(arguments);
        QCOMPARE(result.status, QProcess::NormalExit);
        QCOMPARE(result.code, 2);
        QVERIFY(result.errors.startsWith("error:"));
        QVERIFY(!result.errors.contains("platform plugin"));
    }
    void embeddedDemoSnapshot() {
        const auto result = run({"--snapshot", "--demo", "--no-settings", "--profile", "deye-am3"});
        QCOMPARE(result.status, QProcess::NormalExit);
        QCOMPARE(result.code, 0);
        QJsonParseError error;
        const auto report = QJsonDocument::fromJson(result.output, &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(report["profile"].toString(), QString("deye-am3"));
        QCOMPARE(report["values"].toObject()["bat_soc"].toDouble(), 68.0);
        QCOMPARE(report["states"].toObject()["device_state"].toString(), QString("Normal"));
        QVERIFY(!report.contains("settings"));
        QVERIFY(!result.output.contains("http_pass"));
        QVERIFY(result.errors.isEmpty());
    }
    void probeWithoutAddressExportsFailure() {
        const auto result = run({"--probe", "--no-settings", "--probe-model", "SUN-UNKNOWN"});
        QCOMPARE(result.status, QProcess::NormalExit);
        QCOMPARE(result.code, 1);
        QJsonParseError error;
        const auto report = QJsonDocument::fromJson(result.output, &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(report["schema_version"].toInt(), 1);
        QCOMPARE(report["outcome"].toString(), QString("error"));
        QVERIFY(!report["errors"].toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(CliTest)
#include "test_cli.moc"
