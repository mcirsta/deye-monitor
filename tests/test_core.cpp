#include "discovery.h"
#include "monitor.h"
#include "profiles.h"
#include "protocol.h"
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <limits>

using namespace Deye;
static Registers identity() {
    // Synthetic device identifiers retain the layout of a validated AM3 reply.
    return {{0, 2},      {1, 1},      {2, 513}, {3, 0x3939},  {4, 0x3030}, {5, 0x3030}, {6, 0x3030},
            {7, 0x3031}, {16, 34464}, {17, 1},  {18, 0x0301}, {20, 255},   {21, 0},     {22, 6666}};
}
static QByteArray wrap(QByteArray payload, int sequence = 0xb8, int control = 0x1510) {
    QByteArray f(11, 0);
    f[0] = char(0xa5);
    qToLittleEndian<quint16>(payload.size(), f.data() + 1);
    qToLittleEndian<quint16>(control, f.data() + 3);
    f[5] = char(sequence);
    qToLittleEndian<quint32>(3000000001u, f.data() + 7);
    f += payload;
    quint8 sum = 0;
    for (int n = 1; n < f.size(); ++n)
        sum += quint8(f[n]);
    f += char(sum);
    f += char(0x15);
    return f;
}
static QByteArray reply(QList<quint16> words, int sequence, bool padded = false) {
    QByteArray rtu;
    rtu += char(1);
    rtu += char(3);
    rtu += char(words.size() * 2);
    for (auto word : words) {
        rtu += char(word >> 8);
        rtu += char(word);
    }
    quint16 crc = modbusCrc(rtu);
    rtu += char(crc);
    rtu += char(crc >> 8);
    if (padded)
        rtu += QByteArray(2, 0);
    QByteArray payload(14, 0);
    payload[0] = 2;
    payload[1] = 1;
    return wrap(payload + rtu, sequence);
}
class CoreTest : public QObject {
    Q_OBJECT
private slots:
    void capturedRequest() {
        QCOMPARE(readRequest(3000000001u, 0xb8, 1, 7).toHex(),
                 QByteArray("a517001045b800015ed0b202000000000000000000000000000001030001000755c83015"));
        QVERIFY(readRequest(1, 0, 0, 33).isEmpty());
        QVERIFY(readRequest(1, 0, 65535, 2).isEmpty());
        QVERIFY(readRequest(0, 0, 1, 1).isEmpty());
        QVERIFY(readRequest(1, 0, std::numeric_limits<int>::max(), 32).isEmpty());
    }
    void capturedReply() {
        auto f = QByteArray::fromHex(
            "a521001015b80b015ed0b20201aca12201f2000000d147af6901030e00010201393930303030303030315216f015");
        QString error;
        Registers r;
        QVERIFY2(validateFrame(f, 3000000001u, error), qPrintable(error));
        QVERIFY2(decodeReply(f, 1, 7, r, error), qPrintable(error));
        QCOMPARE(Profiles::serial(r), QString("9900000001"));
        QCOMPARE(r[2], quint16(513));
        QVERIFY(!validateFrame(f, 99, error));
        auto corruptedIdentity = f;
        corruptedIdentity[7] = char(corruptedIdentity[7] ^ 1);
        QVERIFY(!validateFrame(corruptedIdentity, 3000000001u, error));
        QCOMPARE(error, QString("Solarman checksum mismatch"));
        f[25] = char(f[25] ^ 1);
        QVERIFY(!validateFrame(f, 3000000001u, error));
    }
    void responseValidation() {
        QString error;
        Registers r;
        QVERIFY(!decodeReply({}, 0, 1, r, error));
        QVERIFY(!decodeReply(reply({1}, 42), -1, 1, r, error));
        QVERIFY(decodeReply(reply({5271, 66}, 42, true), 183, 2, r, error));
        QCOMPARE(r[183], quint16(5271));
        QVERIFY(!decodeReply(reply({1}, 42), 0, 2, r, error));
        auto f = reply({1}, 42);
        f[f.size() - 4] = char(f[f.size() - 4] ^ 1);
        QVERIFY(!decodeReply(f, 0, 1, r, error));
    }
    void profiles() {
        Profiles profiles;
        Device d;
        QString error;
        auto r = identity();
        auto p = profiles.select(r, "auto", d, error);
        QVERIFY2(p, qPrintable(error));
        QCOMPARE(p->id, QString("deye-am3"));
        QCOMPARE(d.ratedPower, 10000.);
        QCOMPARE(d.serial, QString("9900000001"));
        r[20] = 54464;
        r[21] = 1;
        r[22] = 0x0203;
        p = profiles.select(r, "auto", d, error);
        QVERIFY(!p);
        QVERIFY(error.contains("ambiguous"));
        r[16] = 0;
        r[17] = 0;
        p = profiles.select(r, "auto", d, error);
        QVERIFY(p);
        QCOMPARE(p->id, QString("deye-p3"));
        r[22] = 0;
        QVERIFY(!profiles.select(r, "auto", d, error));
        QVERIFY(error.contains("Unsupported"));
    }
    void actualScales() {
        Profiles profiles;
        Device d;
        QString error;
        auto p = profiles.select(identity(), "auto", d, error);
        QVERIFY(p);
        Registers r{{178, 38},   {183, 5271}, {184, 66},     {190, 453},  {191, 86},   {182, 1210},
                    {90, 1100},  {85, 65535}, {86, 1},       {204, 200},  {210, 110},  {211, 220},
                    {217, 20},   {218, 50},   {219, 35},     {220, 4600}, {221, 5200}, {222, 4750},
                    {312, 5760}, {314, 82},   {315, 150},    {316, 66},   {317, 5280}, {318, 65531},
                    {319, 1180}, {154, 2300}, {280, 0x2000}, {284, 0},    {285, 0},    {286, 0},
                    {287, 2650}, {288, 1850}};
        QMap<QString, double> decoded;
        for (const auto &m : p->metrics)
            if (auto n = m.decode(r))
                decoded[m.key] = *n;
        QCOMPARE(decoded["load_power"], 380.);
        QCOMPARE(decoded["bat_v"], 52.71);
        QCOMPARE(decoded["bat_i"], 8.6);
        QCOMPARE(decoded["bat_power"], 453.);
        QCOMPARE(decoded["bat_temp"], 21.);
        QCOMPARE(decoded["bat_cfg_capacity"], 200.);
        QCOMPARE(decoded["bat_cfg_shutdown_soc"], 20.);
        QCOMPARE(decoded["bat_cfg_restart_soc"], 50.);
        QCOMPARE(decoded["bat_cfg_low_soc"], 35.);
        QCOMPARE(decoded["bat_cfg_low_v"], 47.5);
        QCOMPARE(decoded["bms_charge_i_limit"], 82.);
        QCOMPARE(decoded["bms_soc"], 66.);
        QCOMPARE(decoded["bms_i"], -5.);
        QCOMPARE(decoded["bms_temp"], 18.);
        QCOMPARE(decoded["ac_cfg_output_voltage_code"], 0.);
        QCOMPARE(decoded["inv_output_v"], 230.);
        QCOMPARE(decoded["ac_cfg_grid_high_v"], 265.);
        QCOMPARE(decoded["ac_cfg_grid_low_v"], 185.);
        r[280] = 0x2005;
        for (const auto &m : p->metrics)
            if (m.key == "ac_cfg_output_voltage_code")
                QCOMPARE(m.decode(r).value(), 5.);
        r[191] = 65516;
        for (const auto &m : p->metrics)
            if (m.key == "bat_i")
                QCOMPARE(m.decode(r).value(), -2.);
    }
    void solarEnergyCounters() {
        Profiles profiles;
        const auto p = profiles.find("deye-am3");
        QVERIFY(p);
        // General energy / load must never stand in for the dedicated PV counters.
        const Registers r{{60, 16}, {63, 300}, {64, 0}, {84, 16}, {108, 157}, {96, 54321}, {97, 1}};
        const QMap<QString, double> expected{
            {"e_today_pv", 15.7}, {"e_total_pv", 11985.7}, {"e_today_load", 1.6}};
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const auto m = std::find_if(p->metrics.cbegin(), p->metrics.cend(),
                                        [&](const auto &m) { return m.key == it.key(); });
            QVERIFY(m != p->metrics.cend());
            const auto value = m->decode(r);
            QVERIFY(value);
            QCOMPARE(*value, it.value());
            if (it.key() != "e_today_load")
                QVERIFY(!m->decode({{60, 16}, {63, 300}, {64, 0}, {84, 16}}));
        }
    }
    void statusParser() {
        auto l = Discovery::parseStatus(
            "192.168.1.2",
            R"(var cover_mid = "3000000001"; var webdata_sn = "9900000001"; var cover_sta_mac = "02:00:00:00:00:01";)");
        QVERIFY(l);
        QCOMPARE(l->serial, 3000000001u);
        QCOMPARE(l->inverterSerial, QString("9900000001"));
        QVERIFY(!Discovery::parseStatus("x", "<html>Another device</html>"));
    }
    void singleHostDiscoveryDoesNotProbeAgain() {
        QTcpServer http, telemetry;
        QVERIFY(http.listen(QHostAddress::LocalHost));
        QVERIFY(telemetry.listen(QHostAddress::LocalHost));
        QSignalSpy telemetryConnections(&telemetry, &QTcpServer::newConnection);
        int requests = 0;
        connect(&http, &QTcpServer::newConnection, this, [&] {
            auto socket = http.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                if (!socket->readAll().contains("GET /status.html"))
                    return;
                ++requests;
                const QByteArray body = R"(var cover_mid = "3000000001"; var webdata_sn = "9900000001";)";
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        ScanOptions options;
        options.knownIp = "127.0.0.1";
        options.port = telemetry.serverPort();
        options.statusPort = http.serverPort();
        Discovery discovery;
        QSignalSpy finished(&discovery, &Discovery::finished);
        discovery.start(options, true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
        QCOMPARE(requests, 1);
        QCOMPARE(telemetryConnections.count(), 0);
        QCOMPARE(qvariant_cast<QList<Logger>>(finished.first()[0]).size(), 1);
        QVERIFY(finished.first()[1].toString().isEmpty());
    }
    void streamFragmentation() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Transport client;
        QSignalSpy ready(&client, &Transport::registersReady), failed(&client, &Transport::failed),
            heartbeat(&client, &Transport::heartbeat);
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [socket, request] {
                *request += socket->readAll();
                if (request->size() < 36)
                    return;
                int seq = quint8((*request)[5]);
                auto data = wrap(QByteArray(1, 0), seq, 0x4710) + reply({1, 2}, (seq + 1) & 255) +
                            reply({42, 65535}, seq);
                socket->write(data.left(9));
                QTimer::singleShot(15, socket, [socket, data] { socket->write(data.mid(9)); });
            });
        });
        client.configure("127.0.0.1", server.serverPort(), 3000000001u);
        client.read(100, 2);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 1500);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(heartbeat.count(), 1);
        auto r = qvariant_cast<Registers>(ready.first().first());
        QCOMPARE(r.value(100), quint16(42));
        QCOMPARE(r.value(101), quint16(65535));
    }
    void monitorSnapshot() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        Registers data = identity();
        data.insert({{59, 2},    {60, 16},   {63, 300},   {64, 0},     {84, 16},  {96, 54321},   {97, 1},
                     {108, 157}, {169, 1},   {178, 38},   {183, 5271}, {184, 66}, {154, 2300},   {190, 453},
                     {191, 86},  {204, 200}, {217, 20},   {218, 50},   {219, 35}, {280, 0x2000}, {284, 0},
                     {285, 0},   {286, 0},   {287, 2650}, {288, 1850}, {316, 66}, {318, 65531}});
        int reads = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, buffer] {
                *buffer += socket->readAll();
                while (buffer->size() >= 36) {
                    auto request = buffer->left(36);
                    buffer->remove(0, 36);
                    QCOMPARE(quint8(request[27]), quint8(3));
                    int start = qFromBigEndian<quint16>(request.constData() + 28),
                        count = qFromBigEndian<quint16>(request.constData() + 30);
                    QVERIFY(count <= 32);
                    QList<quint16> words;
                    for (int n = 0; n < count; ++n)
                        words.append(data.value(start + n));
                    if (reads == 0)
                        socket->write(wrap(QByteArray(1, 0), quint8(request[5]), 0x4710));
                    socket->write(reply(words, quint8(request[5])));
                    ++reads;
                }
            });
        });
        Monitor monitor(false);
        QSignalSpy snapshot(&monitor, &Monitor::snapshotReady), failed(&monitor, &Monitor::connectionError);
        monitor.applySettings({{"ip", "127.0.0.1"},
                               {"port", server.serverPort()},
                               {"serial", "3000000001"},
                               {"profile", "auto"}});
        QTRY_COMPARE_WITH_TIMEOUT(snapshot.count(), 1, 2000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(reads, 11);
        QCOMPARE(monitor.report().value("heartbeats").toInt(), 1);
        QVERIFY(monitor.report().value("heartbeat_now").toBool());
        QVERIFY(!monitor.report().value("last_heartbeat").toString().isEmpty());
        QVERIFY(monitor.connected());
        QCOMPARE(monitor.values().value("load_power").toDouble(), 380.);
        QCOMPARE(monitor.values().value("e_today_pv").toDouble(), 15.7);
        QCOMPARE(monitor.values().value("e_total_pv").toDouble(), 11985.7);
        QCOMPARE(monitor.values().value("e_today_load").toDouble(), 1.6);
        QCOMPARE(monitor.values().value("bat_i").toDouble(), 8.6);
        QCOMPARE(monitor.values().value("bat_cfg_low_soc").toDouble(), 35.);
        QCOMPARE(monitor.values().value("bms_i").toDouble(), -5.);
        QCOMPARE(monitor.values().value("ac_cfg_output_voltage_code").toInt(), 0);
        QCOMPARE(monitor.values().value("ac_cfg_grid_high_v").toDouble(), 265.);
        const auto groups = monitor.detailGroups();
        QVERIFY(groups.size() >= 2);
        const auto battery = groups[0].toMap();
        const auto advanced = groups[1].toMap();
        QCOMPARE(battery.value("title").toString(), QString("Battery"));
        QCOMPARE(advanced.value("title").toString(), QString("Advanced battery settings"));
        const auto batteryRows = battery.value("rows").toList();
        const auto advancedRows = advanced.value("rows").toList();
        QVERIFY(std::any_of(batteryRows.cbegin(), batteryRows.cend(), [](const QVariant &row) {
            return row.toMap().value("label").toString() == "Shutdown SOC";
        }));
        QVERIFY(std::none_of(advancedRows.cbegin(), advancedRows.cend(), [](const QVariant &row) {
            return row.toMap().value("label").toString() == "Shutdown SOC";
        }));
        QVERIFY(advanced.value("advanced").toBool());
        auto groupByTitle = [&groups](const QString &title) -> QVariantMap {
            for (const auto &group : groups)
                if (group.toMap().value("title").toString() == title)
                    return group.toMap();
            return {};
        };
        const auto acOutput = groupByTitle("AC output settings");
        const auto gridLimits = groupByTitle("Grid voltage limits");
        QVERIFY(!acOutput.isEmpty());
        QVERIFY(!gridLimits.isEmpty());
        auto rowValue = [](const QVariantMap &group, const QString &label) -> QVariant {
            for (const auto &row : group.value("rows").toList())
                if (row.toMap().value("label").toString() == label)
                    return row.toMap().value("value");
            return {};
        };
        QCOMPARE(rowValue(acOutput, "Configured output voltage").toString(), QString("230 V"));
        QCOMPARE(rowValue(acOutput, "Measured inverter output").toDouble(), 230.);
        QCOMPARE(rowValue(acOutput, "Grid frequency setting").toString(), QString("50 Hz"));
        QCOMPARE(rowValue(acOutput, "Grid type").toString(), QString("Single Phase"));
        QCOMPARE(rowValue(acOutput, "Grid mode").toString(), QString("General Standard"));
        QCOMPARE(rowValue(gridLimits, "High grid-voltage limit").toDouble(), 265.);
        QCOMPARE(rowValue(gridLimits, "Low grid-voltage limit").toDouble(), 185.);
        QVERIFY(gridLimits.value("note").toString().contains("not inverter output"));
        QCOMPARE(monitor.profile(), QString("deye-am3"));
        const auto heartbeatTime = monitor.report().value("last_heartbeat").toString();
        monitor.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(snapshot.count(), 2, 2000);
        QCOMPARE(monitor.report().value("heartbeats").toInt(), 1);
        QVERIFY(!monitor.report().value("heartbeat_now").toBool());
        QCOMPARE(monitor.report().value("last_heartbeat").toString(), heartbeatTime);
        monitor.togglePaused();
    }
    void revalidatesIdentityAfterReconnect_data() {
        QTest::addColumn<bool>("paused");
        QTest::newRow("pause and resume") << true;
        QTest::newRow("idle disconnect") << false;
    }
    void revalidatesIdentityAfterReconnect() {
        QFETCH(bool, paused);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto data = identity();
        QList<int> requestedStarts;
        QPointer<QTcpSocket> lastSocket;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            lastSocket = socket;
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, buffer] {
                *buffer += socket->readAll();
                while (buffer->size() >= 36) {
                    const auto request = buffer->left(36);
                    buffer->remove(0, 36);
                    const int start = qFromBigEndian<quint16>(request.constData() + 28);
                    const int count = qFromBigEndian<quint16>(request.constData() + 30);
                    requestedStarts.append(start);
                    QList<quint16> words;
                    for (int n = 0; n < count; ++n)
                        words.append(data.value(start + n));
                    socket->write(reply(words, quint8(request[5])));
                }
            });
        });
        Monitor monitor(false);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotReady), errors(&monitor, &Monitor::connectionError);
        monitor.applySettings({{"ip", "127.0.0.1"}, {"port", server.serverPort()}, {"serial", "3000000001"}});
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.count(), 1, 2000);
        requestedStarts.clear();
        data[3] = 0x3838;
        if (paused) {
            monitor.togglePaused();
            monitor.togglePaused();
        } else {
            QVERIFY(lastSocket);
            lastSocket->disconnectFromHost();
            QTRY_VERIFY(lastSocket->state() == QAbstractSocket::UnconnectedState);
            QTest::qWait(30);
            monitor.refresh();
        }
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 2000);
        QVERIFY(errors.first()[1].toBool());
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(requestedStarts, QList<int>{0});
        QVERIFY(!monitor.connected());
    }
    void changingLoggerRemovesSavedIdentityPins() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString previousFile =
            QSettings(QSettings::IniFormat, QSettings::UserScope, "DeyeMonitor", "DeyeMonitor").fileName();
        const QString previousRoot = QFileInfo(QFileInfo(previousFile).absolutePath()).absolutePath();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        const auto restorePath = qScopeGuard(
            [&] { QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, previousRoot); });
        {
            Monitor monitor(true);
            monitor.applySettings({{"ip", ""},
                                   {"serial", "3000000001"},
                                   {"logger_mac", "02:00:00:00:00:01"},
                                   {"inverter_serial", "9900000001"}},
                                  false);
            monitor.applySettings({{"ip", ""}, {"serial", "1234567890"}}, false);
        }
        Monitor reloaded(true);
        QCOMPARE(reloaded.settings().value("serial").toString(), QString("1234567890"));
        QVERIFY(!reloaded.settings().contains("logger_mac"));
        QVERIFY(!reloaded.settings().contains("inverter_serial"));
    }
    void identityStops_data() {
        QTest::addColumn<bool>("changedSerial");
        QTest::newRow("unsupported profile") << false;
        QTest::newRow("changed inverter") << true;
    }
    void identityStops() {
        QFETCH(bool, changedSerial);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto data = identity();
        if (!changedSerial)
            data[18] = 0;
        int reads = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, buffer] {
                *buffer += socket->readAll();
                if (buffer->size() < 36)
                    return;
                ++reads;
                QList<quint16> words;
                for (int n = 0; n < 25; ++n)
                    words.append(data.value(n));
                socket->write(reply(words, quint8((*buffer)[5])));
                buffer->clear();
            });
        });
        Monitor monitor(false);
        QSignalSpy errors(&monitor, &Monitor::connectionError);
        monitor.applySettings({{"ip", "127.0.0.1"},
                               {"serial", "3000000001"},
                               {"port", server.serverPort()},
                               {"inverter_serial", changedSerial ? "9999999999" : ""}});
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 1500);
        QVERIFY(errors.first()[1].toBool());
        QVERIFY(!monitor.connected());
        QVERIFY(!monitor.busy());
        QVERIFY(monitor.history().isEmpty());
        QVERIFY(monitor.probeOffered());
        QVERIFY(monitor.diagnosticProbe()->ready());
        QVERIFY(!monitor.diagnosticProbe()->running());
        QVERIFY(!monitor.diagnosticProbe()->report().value("raw_registers").toMap().isEmpty());
        QTest::qWait(60);
        QCOMPARE(reads, 1);
    }
    void pauseDuringDiscovery() {
        Monitor monitor(false);
        monitor.applySettings({{"ip", "127.0.0.1"}});
        QVERIFY(monitor.busy());
        monitor.togglePaused();
        QVERIFY(monitor.paused());
        QVERIFY(!monitor.busy());
        monitor.togglePaused();
        QVERIFY(!monitor.paused());
        QVERIFY(monitor.busy());
        monitor.togglePaused();
        QVERIFY(!monitor.busy());
    }
    void pauseWhenConnectingStopsDiscovery() {
        Monitor monitor(false);
        monitor.applySettings({{"ip", "127.0.0.1"}}, false);
        QSignalSpy errors(&monitor, &Monitor::connectionError);
        connect(&monitor, &Monitor::changed, &monitor, [&] {
            if (monitor.status().startsWith("Connecting to ") && !monitor.paused())
                monitor.togglePaused();
        });
        monitor.start();
        QVERIFY(monitor.paused());
        QVERIFY(!monitor.busy());
        QTest::qWait(100);
        QCOMPARE(errors.count(), 0);
    }
    void startupFallsBackAfterOneAttempt_data() {
        QTest::addColumn<bool>("unanswered");
        QTest::newRow("connection refused") << false;
        QTest::newRow("unanswered request") << true;
    }
    void startupFallsBackAfterOneAttempt() {
        QFETCH(bool, unanswered);
        QTcpServer portReservation;
        QVERIFY(portReservation.listen(QHostAddress::LocalHost));
        const auto port = portReservation.serverPort();
        if (!unanswered)
            portReservation.close();
        QSignalSpy connections(&portReservation, &QTcpServer::newConnection);

        Monitor monitor(false, nullptr, DiscoveryTargets{});
        monitor.applySettings({{"ip", "127.0.0.1"}, {"port", port}, {"serial", "3000000001"}}, false);
        QSignalSpy errors(&monitor, &Monitor::connectionError);
        QElapsedTimer elapsed;
        bool scanDue = false;
        connect(&monitor, &Monitor::changed, &monitor, [&] {
            if (monitor.status() == "Reconnecting · finding your logger") {
                scanDue = true;
                monitor.togglePaused();
            }
        });
        elapsed.start();
        monitor.start();
        QTRY_VERIFY_WITH_TIMEOUT(scanDue, unanswered ? 2800 : 1000);
        QVERIFY(elapsed.elapsed() < (unanswered ? 2800 : 1000));
        if (unanswered)
            QVERIFY(elapsed.elapsed() >= 1800);
        QCOMPARE(connections.count(), unanswered ? 1 : 0);
        QVERIFY(monitor.paused());
        QCOMPARE(errors.count(), 1);
        // A stale saved address is handled by discovery before offering diagnostics.
        QVERIFY(!monitor.probeOffered());
        QVERIFY(!monitor.diagnosticProbe()->ready());
    }
    void startupDiscoveryFailureOffersOnce_data() {
        QTest::addColumn<bool>("automaticSerial");
        QTest::newRow("known logger") << false;
        QTest::newRow("HTTP lookup failed") << true;
    }
    void startupDiscoveryFailureOffersOnce() {
        QFETCH(bool, automaticSerial);
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost));
        const auto port = reservation.serverPort();
        reservation.close();
        Monitor monitor(false, nullptr, DiscoveryTargets{});
        monitor.applySettings({{"ip", "127.0.0.1"},
                               {"port", port},
                               {"diagnostic_status_port", port},
                               {"serial", automaticSerial ? "" : "3000000001"}},
                              false);
        QSignalSpy errors(&monitor, &Monitor::connectionError);
        monitor.start();
        QTRY_VERIFY_WITH_TIMEOUT(monitor.probeOffered(), 1000);
        QVERIFY(monitor.diagnosticProbe()->ready());
        QVERIFY(!monitor.diagnosticProbe()->running());
        const auto firstReport = monitor.diagnosticProbe()->report();
        const auto errorCount = errors.count();
        monitor.dismissProbeOffer();
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() > errorCount, 1500);
        QVERIFY(!monitor.probeOffered());
        QCOMPARE(monitor.diagnosticProbe()->report(), firstReport);
        monitor.togglePaused();
    }
    void startupKeepsVerifiedLoggerDuringSlowSnapshot() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const auto data = identity();
        int reads = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, buffer] {
                *buffer += socket->readAll();
                while (buffer->size() >= 36) {
                    const auto request = buffer->left(36);
                    buffer->remove(0, 36);
                    const int start = qFromBigEndian<quint16>(request.constData() + 28);
                    const int count = qFromBigEndian<quint16>(request.constData() + 30);
                    QList<quint16> words;
                    for (int n = 0; n < count; ++n)
                        words.append(data.value(start + n));
                    const auto frame = reply(words, quint8(request[5]));
                    QTimer::singleShot(reads++ == 0 ? 0 : 700, socket,
                                       [socket, frame] { socket->write(frame); });
                }
            });
        });
        Monitor monitor(false);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotReady);
        bool scanDue = false;
        connect(&monitor, &Monitor::changed, &monitor, [&] {
            if (monitor.status() == "Reconnecting · finding your logger") {
                scanDue = true;
                monitor.togglePaused();
            }
        });
        monitor.applySettings({{"ip", "127.0.0.1"}, {"port", server.serverPort()}, {"serial", "3000000001"}},
                              false);
        QElapsedTimer elapsed;
        elapsed.start();
        monitor.start();
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.count(), 1, 10000);
        QVERIFY(elapsed.elapsed() >= 5000);
        QVERIFY(!scanDue);
        QVERIFY(monitor.connected());
    }
};
QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
