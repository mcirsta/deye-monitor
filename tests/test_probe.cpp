#include "diagnosticprobe.h"
#include "monitor.h"
#include "profiles.h"
#include "protocol.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <functional>
#include <memory>

using namespace Deye;
namespace {
constexpr quint32 loggerSerial = 3000000001u;
const QString inverterSerial = "9900000001";
const QString loggerMac = "02:00:00:00:00:01";
const QString privateUser = "private-probe-user";
const QString privatePassword = "private-probe-password";
const QString privateSsid = "private-probe-network";

Registers deviceIdentity(bool supported = true, QString serial = inverterSerial) {
    Registers values{{0, 2},      {1, 1},  {2, 513},
                     {16, 34464}, {17, 1}, {18, supported ? quint16(0x0301) : quint16(0)},
                     {20, 0},     {21, 0}, {22, 0}};
    const auto ascii = serial.toLatin1();
    for (int n = 0; n < 5; ++n)
        values[3 + n] = quint16(quint8(ascii[2 * n]) << 8 | quint8(ascii[2 * n + 1]));
    return values;
}

QByteArray wrap(QByteArray payload, int sequence) {
    QByteArray frame(11, 0);
    frame[0] = char(0xa5);
    qToLittleEndian<quint16>(quint16(payload.size()), frame.data() + 1);
    qToLittleEndian<quint16>(0x1510, frame.data() + 3);
    frame[5] = char(sequence);
    qToLittleEndian<quint32>(loggerSerial, frame.data() + 7);
    frame += payload;
    quint8 checksum = 0;
    for (int n = 1; n < frame.size(); ++n)
        checksum += quint8(frame[n]);
    frame += char(checksum);
    frame += char(0x15);
    return frame;
}

QByteArray rtuReply(QByteArray rtu, int sequence) {
    const auto crc = modbusCrc(rtu);
    rtu += char(crc);
    rtu += char(crc >> 8);
    QByteArray payload(14, 0);
    payload[0] = 2;
    payload[1] = 1;
    return wrap(payload + rtu, sequence);
}

struct Read {
    int start = 0, count = 0, sequence = 0;
    quint8 function = 0;
    QByteArray frame;
};

QByteArray registersReply(const Read &read, const Registers &values) {
    QByteArray rtu;
    rtu += char(1);
    rtu += char(3);
    rtu += char(read.count * 2);
    for (int n = 0; n < read.count; ++n) {
        const auto value = values.value(read.start + n);
        rtu += char(value >> 8);
        rtu += char(value);
    }
    return rtuReply(rtu, read.sequence);
}

QByteArray exceptionReply(const Read &read, int code = 2) {
    QByteArray rtu;
    rtu += char(1);
    rtu += char(0x83);
    rtu += char(code);
    return rtuReply(rtu, read.sequence);
}

QByteArray statusBody(QString firmware = "LSW3_32_TEST_FIRMWARE") {
    return QString("var cover_mid = \"%1\"; var cover_sta_mac = \"%2\"; "
                   "var webdata_sn = \"%3\"; var cover_ver = \"%4\"; "
                   "var webdata_msvn = \"B028-1726\"; var webdata_hsvn = \"0000-C37B\"; "
                   "var cover_ssid = \"%5\"; var wifi_password = \"%6\"; "
                   "var http_user = \"%7\"; var http_pass = \"%6\";")
        .arg(loggerSerial)
        .arg(loggerMac, inverterSerial, firmware, privateSsid, privatePassword, privateUser)
        .toUtf8();
}

struct Response {
    QByteArray frame;
    int delayMs = 0;
    bool disconnect = false;
};

class LoggerMock : public QObject {
public:
    QTcpServer modbus, http;
    Registers values = deviceIdentity();
    QList<Read> reads;
    QList<QByteArray> httpRequests;
    std::function<Response(const Read &, int)> readResponse;
    std::function<QPair<QByteArray, int>(int)> httpResponse;

    LoggerMock() {
        connect(&modbus, &QTcpServer::newConnection, this, [this] {
            while (modbus.hasPendingConnections()) {
                auto socket = modbus.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    while (buffer->size() >= 36) {
                        const auto frame = buffer->left(36);
                        buffer->remove(0, 36);
                        Read read{qFromBigEndian<quint16>(frame.constData() + 28),
                                  qFromBigEndian<quint16>(frame.constData() + 30), quint8(frame[5]),
                                  quint8(frame[27]), frame};
                        reads.append(read);
                        const auto reply = readResponse ? readResponse(read, reads.size())
                                                        : Response{registersReply(read, values), 0, false};
                        QPointer<QTcpSocket> guarded(socket);
                        QTimer::singleShot(reply.delayMs, this, [guarded, reply] {
                            if (!guarded || guarded->state() != QAbstractSocket::ConnectedState)
                                return;
                            if (!reply.frame.isEmpty())
                                guarded->write(reply.frame);
                            if (reply.disconnect)
                                guarded->disconnectFromHost();
                        });
                    }
                });
            }
        });
        connect(&http, &QTcpServer::newConnection, this, [this] {
            while (http.hasPendingConnections()) {
                auto socket = http.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    if (*handled || !buffer->contains("\r\n\r\n"))
                        return;
                    *handled = true;
                    httpRequests.append(*buffer);
                    const auto reply =
                        httpResponse ? httpResponse(httpRequests.size()) : qMakePair(statusBody(), 0);
                    QPointer<QTcpSocket> guarded(socket);
                    QTimer::singleShot(reply.second, this, [guarded, reply] {
                        if (!guarded || guarded->state() != QAbstractSocket::ConnectedState)
                            return;
                        guarded->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                                       QByteArray::number(reply.first.size()) +
                                       "\r\nConnection: close\r\n\r\n" + reply.first);
                        guarded->disconnectFromHost();
                    });
                });
            }
        });
    }
    bool listen() { return modbus.listen(QHostAddress::LocalHost) && http.listen(QHostAddress::LocalHost); }
    QVariantMap settings() const {
        return {{"ip", "127.0.0.1"},
                {"port", modbus.serverPort()},
                {"serial", QString::number(loggerSerial)},
                {"inverter_serial", inverterSerial},
                {"logger_mac", loggerMac},
                {"profile", "auto"},
                {"http_user", privateUser},
                {"http_pass", privatePassword},
                {"diagnostic_status_port", http.serverPort()}};
    }
};

QByteArray serialized(const QVariantMap &report) {
    return QJsonDocument::fromVariant(report).toJson(QJsonDocument::Compact);
}

bool hasError(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if ((it.key() == "error" && !it.value().toString().isEmpty()) ||
                (it.key() == "errors" && !it.value().toList().isEmpty()) || hasError(it.value()))
                return true;
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto &child : value.toList())
            if (hasError(child))
                return true;
    }
    return false;
}

bool containsKey(const QVariant &value, const QString &key) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        if (map.contains(key))
            return true;
        for (const auto &child : map)
            if (containsKey(child, key))
                return true;
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto &child : value.toList())
            if (containsKey(child, key))
                return true;
    }
    return false;
}

bool containsIdentityRegisters(const QVariant &value) {
    if (value.metaType().id() == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (it.key() == "raw_registers" || it.key() == "registers") {
                const auto registers = it.value().toMap();
                for (int address = 3; address <= 7; ++address)
                    if (registers.contains(QString::number(address)))
                        return true;
            }
            if (containsIdentityRegisters(it.value()))
                return true;
        }
    } else if (value.metaType().id() == QMetaType::QVariantList) {
        for (const auto &child : value.toList())
            if (containsIdentityRegisters(child))
                return true;
    }
    return false;
}
} // namespace

class ProbeTest : public QObject {
    Q_OBJECT
private slots:
    void existingFailureEvidenceIsReadyWithoutNetworkIo() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        auto registers = deviceIdentity(false);
        for (int address = 0; address < 25; ++address)
            if (!registers.contains(address))
                registers[address] = 0;
        const Read request{0, 25, 42, 3, readRequest(loggerSerial, 42, 0, 25)};
        const QVariantList wire{
            QVariantMap{{"direction", "tx"},
                        {"timestamp", "2026-11-06T12:00:00.000Z"},
                        {"hex", QString::fromLatin1(request.frame.toHex())}},
            QVariantMap{{"direction", "rx"},
                        {"timestamp", "2026-11-06T12:00:00.050Z"},
                        {"hex", QString::fromLatin1(registersReply(request, registers).toHex())}}};
        const auto error = QString("Unsupported inverter at 127.0.0.1; logger %1; inverter %2; MAC %3")
                               .arg(loggerSerial)
                               .arg(inverterSerial, loggerMac);
        DiagnosticProbe probe;
        QSignalSpy changed(&probe, &DiagnosticProbe::changed);
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.captureFailure(logger.settings(), registers, wire, error);
        QVERIFY(probe.ready());
        QVERIFY(!probe.running());
        QVERIFY(changed.count() > 0);
        QCOMPARE(finished.count(), 0);
        const auto full = probe.report(true);
        QCOMPARE(full.value("outcome").toString(), QString("partial"));
        QCOMPARE(full.value("wire").toList(), wire);
        const auto raw = full.value("raw_registers").toMap();
        QCOMPARE(raw.size(), 25);
        for (int address = 0; address < 25; ++address)
            QCOMPARE(raw.value(QString::number(address)).toUInt(), quint32(registers.value(address)));
        QVERIFY(hasError(full));
        QVERIFY(serialized(full).contains(error.toUtf8()));
        QVERIFY(!full.value("raw_blocks").toList().isEmpty());
        QVERIFY(containsIdentityRegisters(full));
        QVERIFY(!full.contains("decoded"));
        const auto anonymous = probe.report();
        QVERIFY(!containsIdentityRegisters(anonymous));
        QVERIFY(!containsKey(anonymous, "wire"));
        const auto anonymousJson = serialized(anonymous);
        for (const auto &identifier :
             {QString("127.0.0.1"), QString::number(loggerSerial), inverterSerial, loggerMac})
            QVERIFY2(!anonymousJson.contains(identifier.toUtf8()), qPrintable(identifier));
        QTest::qWait(80);
        QCOMPARE(logger.httpRequests.size(), 0);
        QCOMPARE(logger.reads.size(), 0);
        QCOMPARE(finished.count(), 0);
    }

    void existingFailureCanBeAnnotatedAndSavedWithoutRerun() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        const auto registers = deviceIdentity(false);
        const QVariantList wire{
            QVariantMap{{"direction", "tx"},
                        {"timestamp", "2026-11-06T12:00:00.000Z"},
                        {"hex", QString::fromLatin1(readRequest(loggerSerial, 42, 0, 25).toHex())}}};
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.captureFailure(logger.settings(), registers, wire, "Unsupported inverter signature");
        const auto original = probe.report(true);
        QSignalSpy changed(&probe, &DiagnosticProbe::changed);
        const auto notes = QString("Observed serial %1 at 127.0.0.1").arg(inverterSerial);
        probe.setDetails("SUN-UNSUPPORTED-EU", notes);
        QVERIFY(changed.count() > 0);
        QCOMPARE(probe.property("exactModel").toString(), QString("SUN-UNSUPPORTED-EU"));
        QCOMPARE(probe.property("notes").toString(), notes);
        const auto annotated = probe.report(true);
        QCOMPARE(annotated.value("exact_model").toString(), QString("SUN-UNSUPPORTED-EU"));
        QCOMPARE(annotated.value("notes").toString(), notes);
        for (const auto &key : {"raw_registers", "raw_blocks", "wire", "errors", "outcome"})
            QCOMPARE(annotated.value(key), original.value(key));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto fileName = directory.filePath("existing-failure.json");
        QVERIFY(probe.save(QUrl::fromLocalFile(fileName)));
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
        QCOMPARE(saved.value("exact_model").toString(), QString("SUN-UNSUPPORTED-EU"));
        QVERIFY(saved.value("notes").toString().contains("[redacted]"));
        QVERIFY(!serialized(saved).contains(inverterSerial.toUtf8()));
        QVERIFY(!containsKey(saved, "wire"));
        QVERIFY(!containsIdentityRegisters(saved));
        QCOMPARE(logger.httpRequests.size(), 0);
        QCOMPARE(logger.reads.size(), 0);
        QCOMPARE(finished.count(), 0);
    }

    void automaticSerialAndExactWire() {
        LoggerMock logger;
        logger.values = deviceIdentity(false);
        QVERIFY(logger.listen());
        auto settings = logger.settings();
        settings["serial"] = "0";
        settings["http_pass"] = "1234";
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(settings, "Unknown model", "Password 1234 must be hidden");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(probe.report().value("outcome").toString(), QString("success"));
        QCOMPARE(logger.reads.size(), 1);
        const auto full = probe.report(true);
        QVERIFY(!full.value("notes").toString().contains("1234"));
        const auto wire = full.value("wire").toList();
        QVERIFY(wire.size() >= 2);
        QCOMPARE(wire.first().toMap().value("hex").toString().toLatin1(), logger.reads.first().frame.toHex());
    }
    void unspecifiedAddressNeverStartsNetworkIo() {
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start({{"ip", "0.0.0.0"}, {"serial", "3000000001"}}, "Unknown model", "");
        QCOMPARE(finished.count(), 1);
        QVERIFY(probe.ready());
        QVERIFY(!probe.running());
        QCOMPARE(probe.completed(), 0);
        QCOMPARE(probe.report().value("outcome").toString(), QString("error"));
    }
    void unknownIdentityCanBeExported() {
        LoggerMock logger;
        logger.values = deviceIdentity(false);
        QVERIFY(logger.listen());
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        QSignalSpy exported(&probe, &DiagnosticProbe::exportFinished);
        probe.start(logger.settings(), "Unknown SUN-MOCK-EU", "Unsupported-family test");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
        QVERIFY(probe.ready());
        QVERIFY(!probe.running());
        QVERIFY(probe.completed() > 0);
        QVERIFY(probe.completed() <= probe.total());
        QCOMPARE(logger.reads.size(), 1);
        QCOMPARE(logger.reads.first().start, 0);
        QCOMPARE(logger.reads.first().count, 25);
        QCOMPARE(logger.reads.first().function, quint8(3));
        QCOMPARE(logger.httpRequests.size(), 1);
        const auto report = serialized(probe.report());
        QVERIFY(report.contains("Unknown SUN-MOCK-EU"));
        QVERIFY(report.contains("Unsupported-family test"));
        QCOMPARE(probe.report().value("profile_detection").toMap().value("status").toString(),
                 QString("unsupported"));
        QVERIFY(!probe.report().contains("decoded"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto fileName = directory.filePath("unsupported.json");
        QVERIFY(probe.save(QUrl::fromLocalFile(fileName)));
        QCOMPARE(exported.count(), 1);
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const auto saved = QJsonDocument::fromJson(file.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(saved.isObject());
        QVERIFY(!saved.object().isEmpty());
    }

    void supportedCaptureUsesKnownReadPlan() {
        LoggerMock logger;
        logger.values.insert({{59, 2},
                              {183, 5271},
                              {184, 66},
                              {178, 38},
                              {190, 453},
                              {186, 1200},
                              {187, 2400},
                              {188, 0},
                              {244, 2}});
        QVERIFY(logger.listen());
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "SUN-10K-SG02LP1-EU-AM3", "Known-profile test");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(probe.ready());
        QVERIFY(logger.reads.size() > 1);
        QCOMPARE(logger.httpRequests.size(), 1);
        Profiles profiles;
        const auto profile = profiles.find("deye-am3");
        QVERIFY(profile);
        for (const auto &block : profile->blocks) {
            bool present = false;
            for (const auto &read : logger.reads)
                present |= read.start == block.first && read.count == block.second;
            QVERIFY2(present, qPrintable(QString("Missing block %1+%2").arg(block.first).arg(block.second)));
        }
        for (const auto &read : logger.reads) {
            QCOMPARE(read.function, quint8(3));
            QVERIFY(read.count >= 1 && read.count <= 32);
        }
        const auto full = serialized(probe.report(true));
        QVERIFY(full.contains("deye-am3"));
        QVERIFY(full.contains("load_power"));
        QVERIFY(full.contains("bat_soc"));
        const auto decoded = probe.report(true).value("decoded").toMap().value("values").toMap();
        QCOMPARE(decoded.value("bat_soc").toDouble(), 66.);
        QCOMPARE(decoded.value("load_power").toDouble(), 380.);
    }

    void anonymousReportRemovesIdentifiersFromEveryRepresentation() {
        LoggerMock logger;
        logger.values = deviceIdentity(false);
        QVERIFY(logger.listen());
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        const auto exactModel = QString("Model at 127.0.0.1, logger %1").arg(loggerSerial);
        const auto notes =
            QString("Inverter %1; MAC %2; URL http://127.0.0.1/status.html").arg(inverterSerial, loggerMac);
        probe.start(logger.settings(), exactModel, notes);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
        const auto complete = probe.report(true);
        const auto anonymous = probe.report();
        const auto fullJson = serialized(complete);
        const auto anonymousJson = serialized(anonymous);
        for (const auto &identifier :
             {QString("127.0.0.1"), QString::number(loggerSerial), inverterSerial, loggerMac}) {
            QVERIFY2(fullJson.contains(identifier.toUtf8()), qPrintable(identifier));
            QVERIFY2(!anonymousJson.contains(identifier.toUtf8()), qPrintable(identifier));
        }
        QVERIFY(anonymous.value("exact_model").toString().contains("[redacted]"));
        QVERIFY(anonymous.value("notes").toString().contains("[redacted]"));
        const auto anonymousLogger = anonymous.value("logger").toMap();
        QVERIFY(!anonymousLogger.contains("ip"));
        QVERIFY(!anonymousLogger.contains("serial"));
        QVERIFY(!anonymousLogger.contains("mac"));
        QVERIFY(!anonymous.value("identity").toMap().contains("inverter_serial"));
        QVERIFY(containsIdentityRegisters(complete));
        QVERIFY(!containsIdentityRegisters(anonymous));
        QVERIFY(containsKey(complete, "wire"));
        QVERIFY(!containsKey(anonymous, "wire"));
        QVERIFY(!complete.value("wire").toList().isEmpty());
        QVERIFY(fullJson.contains(logger.reads.first().frame.toHex()));

        const auto httpFields = complete.value("http_status").toMap().value("fields").toMap();
        QCOMPARE(httpFields.value("cover_ver").toString(), QString("LSW3_32_TEST_FIRMWARE"));
        QCOMPARE(httpFields.value("webdata_msvn").toString(), QString("B028-1726"));
        for (const auto &secret : {privateUser, privatePassword, privateSsid}) {
            QVERIFY(!fullJson.contains(secret.toUtf8()));
            QVERIFY(!anonymousJson.contains(secret.toUtf8()));
        }
        QVERIFY(!containsKey(complete, "cover_ssid"));
        QVERIFY(!containsKey(complete, "http_pass"));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto anonymousFile = directory.filePath("anonymous.json");
        const auto completeFile = directory.filePath("with-identifiers.json");
        QVERIFY(probe.save(QUrl::fromLocalFile(anonymousFile)));
        QVERIFY(probe.save(QUrl::fromLocalFile(completeFile), true));
        QFile anonymousSaved(anonymousFile), completeSaved(completeFile);
        QVERIFY(anonymousSaved.open(QIODevice::ReadOnly));
        QVERIFY(completeSaved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(anonymousSaved.readAll()).toVariant().toMap(), anonymous);
        QCOMPARE(QJsonDocument::fromJson(completeSaved.readAll()).toVariant().toMap(), complete);
        QSignalSpy failedExport(&probe, &DiagnosticProbe::exportFailed);
        QVERIFY(!probe.save(QUrl("https://example.invalid/report.json")));
        QCOMPARE(failedExport.count(), 1);
    }

    void readFailuresLeaveExportablePartialEvidence_data() {
        QTest::addColumn<bool>("exception");
        QTest::newRow("Modbus exception") << true;
        QTest::newRow("transport disconnect") << false;
    }
    void readFailuresLeaveExportablePartialEvidence() {
        QFETCH(bool, exception);
        LoggerMock logger;
        QVERIFY(logger.listen());
        bool rejected = false;
        logger.readResponse = [&](const Read &read, int) {
            if (read.start != 0 && !rejected) {
                rejected = true;
                return exception ? Response{exceptionReply(read), 0, false} : Response{{}, 0, true};
            }
            return Response{registersReply(read, logger.values), 0, false};
        };
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "SUN-10K-SG02LP1-EU-AM3", "Read failure test");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(rejected);
        QVERIFY(probe.ready());
        QVERIFY(logger.reads.size() > 2);
        const auto report = probe.report(true);
        QCOMPARE(report.value("outcome").toString(), QString("partial"));
        QVERIFY(hasError(report));
        const auto blocks = report.value("raw_blocks").toList();
        bool failed = false, succeededAfterFailure = false;
        for (const auto &value : blocks) {
            const auto block = value.toMap();
            if (!block.value("ok").toBool() && !block.value("error").toString().isEmpty())
                failed = true;
            else if (failed && block.value("ok").toBool())
                succeededAfterFailure = true;
        }
        QVERIFY(failed);
        QVERIFY(succeededAfterFailure);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(probe.save(QUrl::fromLocalFile(directory.filePath("partial.json"))));
    }

    void rejectedIdentityStillLeavesStatusAndErrorEvidence() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        logger.readResponse = [](const Read &read, int) { return Response{exceptionReply(read), 0, false}; };
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "Unknown model", "Identity exception");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
        QVERIFY(probe.ready());
        QCOMPARE(logger.reads.size(), 1);
        QCOMPARE(logger.httpRequests.size(), 1);
        QVERIFY(hasError(probe.report(true)));
        QVERIFY(!probe.report(true).value("raw_blocks").toList().isEmpty());
        QVERIFY(!probe.report(true).contains("decoded"));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(probe.save(QUrl::fromLocalFile(directory.filePath("identity-error.json"))));
    }

    void changedInitialIdentityStopsAdditionalReads() {
        LoggerMock logger;
        logger.values = deviceIdentity(true, "9999999999");
        QVERIFY(logger.listen());
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "Identity mismatch", "Stop before reference reads", true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
        QVERIFY(probe.ready());
        QCOMPARE(logger.reads.size(), 1);
        QCOMPARE(logger.reads.first().start, 0);
        QVERIFY(hasError(probe.report(true)));
        QVERIFY(!probe.report(true).contains("decoded"));
        QTest::qWait(80);
        QCOMPARE(logger.reads.size(), 1);
    }

    void extendedUnsupportedProbeContinuesAfterRejectedReferenceBlock() {
        LoggerMock logger;
        logger.values = deviceIdentity(false);
        QVERIFY(logger.listen());
        bool rejected = false;
        logger.readResponse = [&](const Read &read, int) {
            if (read.start != 0 && !rejected) {
                rejected = true;
                return Response{exceptionReply(read), 0, false};
            }
            return Response{registersReply(read, logger.values), 0, false};
        };
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        QElapsedTimer elapsed;
        elapsed.start();
        probe.start(logger.settings(), "Unknown model", "Bounded extended capture", true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(probe.ready());
        QVERIFY(rejected);
        QVERIFY(logger.reads.size() > 3);
        QVERIFY(logger.reads.size() <= 24);
        QVERIFY(elapsed.elapsed() < 45000);
        QCOMPARE(logger.reads.first().start, 0);
        QCOMPARE(logger.reads.first().count, 25);
        QCOMPARE(logger.reads.last().start, 0);
        QCOMPARE(logger.reads.last().count, 25);
        Profiles profiles;
        for (const auto &read : logger.reads) {
            QCOMPARE(read.function, quint8(3));
            QVERIFY(read.count >= 1 && read.count <= 32);
            bool allowed = read.start == 0 && read.count == 25;
            for (const auto &profile : profiles.all())
                for (const auto &block : profile.blocks)
                    allowed |= read.start == block.first && read.count == block.second;
            QVERIFY2(allowed, qPrintable(QString("Unexpected read %1+%2").arg(read.start).arg(read.count)));
        }
        const auto report = probe.report(true);
        QCOMPARE(report.value("outcome").toString(), QString("partial"));
        QCOMPARE(report.value("profile_detection").toMap().value("status").toString(),
                 QString("unsupported"));
        QVERIFY(report.value("identity").toMap().value("verified_at_end").toBool());
        QVERIFY(!report.contains("decoded"));
        QVERIFY(hasError(report));
        const auto lastBlock = report.value("raw_blocks").toList().last().toMap();
        QCOMPARE(lastBlock.value("purpose").toString(), QString("identity_verification"));
    }

    void changedFinalIdentityPreventsDecodedMetrics() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        int identityReads = 0;
        logger.readResponse = [&](const Read &read, int) {
            if (read.start == 0 && ++identityReads > 1)
                return Response{registersReply(read, deviceIdentity(true, "9999999999")), 0, false};
            return Response{registersReply(read, logger.values), 0, false};
        };
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "SUN-10K-SG02LP1-EU-AM3", "Final identity mismatch", true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(probe.ready());
        QVERIFY(identityReads >= 2);
        QCOMPARE(logger.reads.last().start, 0);
        const auto full = probe.report(true);
        QVERIFY(!full.value("identity").toMap().value("verified_at_end").toBool());
        QVERIFY(!full.contains("decoded"));
        QVERIFY(hasError(full));
        QVERIFY(!serialized(probe.report()).contains("9999999999"));
    }

    void cancelledCaptureCannotOverwriteRestart_data() {
        QTest::addColumn<bool>("delayedHttp");
        QTest::newRow("stale HTTP callback") << true;
        QTest::newRow("stale register callback") << false;
    }
    void cancelledCaptureCannotOverwriteRestart() {
        QFETCH(bool, delayedHttp);
        LoggerMock logger;
        logger.values = deviceIdentity(false);
        QVERIFY(logger.listen());
        logger.httpResponse = [&](int number) {
            return qMakePair(statusBody(number == 1 ? "OLD_CALLBACK_FIRMWARE" : "NEW_CAPTURE_FIRMWARE"),
                             number == 1 && delayedHttp ? 900 : 0);
        };
        logger.readResponse = [&](const Read &read, int number) {
            return Response{registersReply(read, logger.values), number == 1 && !delayedHttp ? 900 : 0,
                            false};
        };
        DiagnosticProbe probe;
        QSignalSpy finished(&probe, &DiagnosticProbe::finished);
        probe.start(logger.settings(), "OLD_CAPTURE_MODEL", "OLD_CAPTURE_NOTES");
        if (delayedHttp)
            QTRY_COMPARE_WITH_TIMEOUT(logger.httpRequests.size(), 1, 800);
        else
            QTRY_COMPARE_WITH_TIMEOUT(logger.reads.size(), 1, 1200);
        probe.cancel();
        QVERIFY(!probe.running());
        const auto cancelledFinishes = finished.count();
        probe.start(logger.settings(), "NEW_CAPTURE_MODEL", "NEW_CAPTURE_NOTES");
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), cancelledFinishes + 1, 8000);
        QVERIFY(probe.ready());
        const auto newReport = serialized(probe.report(true));
        QVERIFY(newReport.contains("NEW_CAPTURE_MODEL"));
        QVERIFY(newReport.contains("NEW_CAPTURE_NOTES"));
        QVERIFY(newReport.contains("NEW_CAPTURE_FIRMWARE"));
        QVERIFY(!newReport.contains("OLD_CAPTURE_MODEL"));
        QVERIFY(!newReport.contains("OLD_CAPTURE_NOTES"));
        QVERIFY(!newReport.contains("OLD_CALLBACK_FIRMWARE"));
        QTest::qWait(1000);
        QCOMPARE(finished.count(), cancelledFinishes + 1);
        QCOMPARE(serialized(probe.report(true)), newReport);
        QCOMPARE(logger.httpRequests.size(), 2);
    }

    void reportOffersEndAfterSuccessfulStartup_data() {
        QTest::addColumn<bool>("dismissed");
        QTest::newRow("offer still visible") << false;
        QTest::newRow("offer dismissed") << true;
    }
    void reportOffersEndAfterSuccessfulStartup() {
        QFETCH(bool, dismissed);
        LoggerMock logger;
        QVERIFY(logger.listen());
        bool rejectRead = true;
        logger.readResponse = [&](const Read &read, int) {
            return Response{rejectRead && read.start != 0 ? exceptionReply(read)
                                                          : registersReply(read, logger.values)};
        };
        Monitor monitor(false, nullptr, DiscoveryTargets{});
        QSignalSpy errors(&monitor, &Monitor::connectionError);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotReady);
        monitor.applySettings(logger.settings());
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 2000);
        QVERIFY(monitor.probeOffered());
        QVERIFY(monitor.diagnosticProbe()->ready());
        QVERIFY(!monitor.diagnosticProbe()->report().value("raw_registers").toMap().isEmpty());
        if (dismissed)
            monitor.dismissProbeOffer();
        monitor.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 2, 2000);
        QCOMPARE(monitor.probeOffered(), !dismissed);

        rejectRead = false;
        monitor.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.count(), 1, 2000);
        QVERIFY(monitor.connected());
        QVERIFY(!monitor.probeOffered());

        // Even a rejected register after a successful startup does not reopen diagnostics.
        rejectRead = true;
        monitor.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 3, 2000);
        QVERIFY(!monitor.probeOffered());
        monitor.togglePaused();
        monitor.togglePaused();
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 4, 2000);
        QVERIFY(!monitor.probeOffered());

        // Selecting a device begins a new startup validation.
        monitor.applySettings(logger.settings());
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 5, 2000);
        QVERIFY(monitor.probeOffered());
    }

    void monitorPausesForCaptureThenResumesWithoutLosingHistory() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        logger.httpResponse = [](int) { return qMakePair(statusBody(), 1200); };
        Monitor monitor(false);
        auto config = logger.settings();
        config["interval_sec"] = 1;
        monitor.applySettings(config, false);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotReady);
        auto probe = monitor.diagnosticProbe();
        QSignalSpy finished(probe, &DiagnosticProbe::finished);
        monitor.start();
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.count(), 1, 3000);
        QVERIFY(monitor.connected());
        const auto firstHistory = monitor.history();
        QCOMPARE(firstHistory.size(), 1);

        monitor.startProbe("SUN-10K-SG02LP1-EU-AM3", "Pause and resume integration");
        QVERIFY(monitor.paused());
        QVERIFY(probe->running());
        QTest::qWait(1050);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(monitor.history(), firstHistory);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(probe->ready());
        QTRY_VERIFY_WITH_TIMEOUT(snapshots.count() >= 2, 3000);
        QVERIFY(!monitor.paused());
        QVERIFY(monitor.connected());
        QVERIFY(monitor.history().size() >= 2);
        QCOMPARE(monitor.history().first(), firstHistory.first());
        monitor.togglePaused();
    }

    void monitorUserPauseSurvivesCapture() {
        LoggerMock logger;
        QVERIFY(logger.listen());
        Monitor monitor(false);
        auto config = logger.settings();
        config["interval_sec"] = 1;
        monitor.applySettings(config, false);
        QSignalSpy snapshots(&monitor, &Monitor::snapshotReady);
        auto probe = monitor.diagnosticProbe();
        QSignalSpy finished(probe, &DiagnosticProbe::finished);
        monitor.start();
        QTRY_COMPARE_WITH_TIMEOUT(snapshots.count(), 1, 3000);
        monitor.togglePaused();
        QVERIFY(monitor.paused());
        const auto firstHistory = monitor.history();

        monitor.startProbe("SUN-10K-SG02LP1-EU-AM3", "Preserve deliberate user pause");
        QVERIFY(monitor.paused());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(probe->ready());
        QVERIFY(monitor.paused());
        QTest::qWait(1100);
        QCOMPARE(snapshots.count(), 1);
        QCOMPARE(monitor.history(), firstHistory);
        QVERIFY(monitor.paused());
    }
};

QTEST_GUILESS_MAIN(ProbeTest)
#include "test_probe.moc"
