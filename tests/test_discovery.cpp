#include "discovery.h"
#include "monitor.h"
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QPointer>
#include <QTcpServer>
#include <QUdpSocket>
#include <QtTest>
#include <functional>
#include <memory>

using namespace Deye;
namespace {
constexpr quint32 selectedSerial = 3000000001u;
constexpr quint32 replacementSerial = 3000000002u;
const QString selectedMac = "02:00:00:00:00:01";
const QString selectedInverter = "9900000001";
const QString broadcastHost = "127.0.0.1";
const QString fallbackHost = "127.0.0.2";
const QByteArray versionReply = "+ok=LSW3_32_5406_SS_04_00.00.00.0B\r\n";

QByteArray statusBody(quint32 serial = selectedSerial, QString mac = selectedMac,
                      QString inverter = selectedInverter) {
    return QString("var cover_mid = \"%1\"; var cover_sta_mac = \"%2\"; "
                   "var webdata_sn = \"%3\";")
        .arg(serial)
        .arg(mac, inverter)
        .toUtf8();
}

struct HttpResponse {
    QByteArray body = statusBody();
    int delayMs = 0;
};

class HttpMock : public QObject {
public:
    struct Request {
        QString host;
        QByteArray headers;
    };
    QTcpServer server;
    QList<Request> requests;
    std::function<HttpResponse(const QString &, int)> response;

    HttpMock() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto socket = server.nextPendingConnection();
                auto headers = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, headers, handled] {
                    *headers += socket->readAll();
                    if (*handled || !headers->contains("\r\n\r\n"))
                        return;
                    *handled = true;
                    const auto host = socket->localAddress().toString();
                    requests.append({host, *headers});
                    const auto reply = response ? response(host, requests.size()) : HttpResponse{};
                    QPointer<QTcpSocket> guarded(socket);
                    QTimer::singleShot(reply.delayMs, this, [guarded, reply] {
                        if (!guarded || guarded->state() != QAbstractSocket::ConnectedState)
                            return;
                        guarded->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
                                       QByteArray::number(reply.body.size()) +
                                       "\r\nConnection: close\r\n\r\n" + reply.body);
                        guarded->disconnectFromHost();
                    });
                });
            }
        });
    }
    bool listen() { return server.listen(QHostAddress::AnyIPv4); }
    quint16 port() const { return server.serverPort(); }
};

class TcpProbeMock : public QObject {
public:
    QTcpServer server;
    int connections = 0;

    TcpProbeMock() {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (server.hasPendingConnections()) {
                auto socket = server.nextPendingConnection();
                ++connections;
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                socket->disconnectFromHost();
            }
        });
    }
    bool listen() { return server.listen(QHostAddress(fallbackHost)); }
    quint16 port() const { return server.serverPort(); }
};

class UdpMock : public QObject {
public:
    QUdpSocket socket;
    QList<QNetworkDatagram> queries;
    std::function<void(const QNetworkDatagram &, int)> onQuery;

    UdpMock() {
        connect(&socket, &QUdpSocket::readyRead, this, [this] {
            while (socket.hasPendingDatagrams()) {
                const auto query = socket.receiveDatagram();
                queries.append(query);
                if (onQuery)
                    onQuery(query, queries.size());
            }
        });
    }
    bool listen() { return socket.bind(QHostAddress::LocalHost, 0); }
    quint16 port() const { return socket.localPort(); }
    void send(const QNetworkDatagram &query, QByteArray payload = versionReply, int delayMs = 0) {
        QTimer::singleShot(delayMs, this, [this, query, payload] {
            socket.writeDatagram(payload, query.senderAddress(), query.senderPort());
        });
    }
};

DiscoveryTargets targets(const UdpMock &udp, QStringList hosts = {fallbackHost}) {
    DiscoveryTargets result;
    result.broadcasts.append({QHostAddress::LocalHost, QHostAddress::LocalHost});
    result.hosts = std::move(hosts);
    result.versionPort = udp.port();
    return result;
}

ScanOptions options(const HttpMock &http, const TcpProbeMock &tcp) {
    ScanOptions result;
    result.knownIp = "127.0.0.99";
    result.serial = selectedSerial;
    result.mac = selectedMac;
    result.inverterSerial = selectedInverter;
    result.port = tcp.port();
    result.statusPort = http.port();
    return result;
}

QList<Logger> found(const QSignalSpy &finished) {
    return finished.isEmpty() ? QList<Logger>{} : qvariant_cast<QList<Logger>>(finished.first()[0]);
}

bool sawStage(const QSignalSpy &stages, Discovery::Stage stage) {
    for (const auto &arguments : stages)
        if (qvariant_cast<Discovery::Stage>(arguments.first()) == stage)
            return true;
    return false;
}
} // namespace

class DiscoveryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<QList<Logger>>();
        qRegisterMetaType<Discovery::Stage>();
    }

    void broadcastVerifiesSavedIdentityWithoutTcpScan() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        udp.onQuery = [&](const QNetworkDatagram &query, int) { udp.send(query); };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1200);
        QCOMPARE(udp.queries.size(), 1);
        QCOMPARE(udp.queries.first().data(), QByteArray("AT+YZVER", 8));
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(http.requests.first().host, broadcastHost);
        QVERIFY(http.requests.first().headers.startsWith("GET /status.html HTTP/1.1\r\n"));
        QCOMPARE(tcp.connections, 0);
        QVERIFY(sawStage(stages, Discovery::Stage::Broadcast));
        QVERIFY(!sawStage(stages, Discovery::Stage::NetworkScan));
        QCOMPARE(found(finished).size(), 1);
        QCOMPARE(found(finished).first().ip, broadcastHost);
        QCOMPARE(found(finished).first().serial, selectedSerial);
        QVERIFY(finished.first()[1].toString().isEmpty());
        QVERIFY(!discovery.running());
    }

    void settingsReceiveResultsBeforeTheScanFinishes() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        udp.onQuery = [&](const QNetworkDatagram &query, int) { udp.send(query); };
        Monitor monitor(false, nullptr, targets(udp));
        QSignalSpy finished(&monitor, &Monitor::scanFinished);
        monitor.scanSettings(
            {{"ip", "127.0.0.99"}, {"port", tcp.port()}, {"diagnostic_status_port", http.port()}});
        QTRY_COMPARE_WITH_TIMEOUT(monitor.scanResults().size(), 1, 800);
        QVERIFY(monitor.scanning());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(monitor.scanResults().first().toMap().value("serial").toString(),
                 QString::number(selectedSerial));
        QCOMPARE(tcp.connections, 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(monitor.scanResults().size(), 1);
        QCOMPARE(tcp.connections, 0);
    }

    void cancelOnImmediateResultStopsTheScan() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        udp.onQuery = [&](const QNetworkDatagram &query, int) { udp.send(query); };
        Discovery discovery(targets(udp));
        QSignalSpy results(&discovery, &Discovery::loggerFound);
        QSignalSpy finished(&discovery, &Discovery::finished);
        connect(&discovery, &Discovery::loggerFound, &discovery, &Discovery::cancel);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(results.count(), 1, 800);
        QVERIFY(!discovery.running());
        QTest::qWait(100);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(tcp.connections, 0);
    }

    void malformedOrWrongPortRepliesFallBack() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QUdpSocket wrongPort;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        QVERIFY(wrongPort.bind(QHostAddress::LocalHost, 0));
        udp.onQuery = [&](const QNetworkDatagram &query, int) {
            udp.send(query, "AT+YZVER\r\n");
            udp.send(query, "+ok\r\n");
            udp.send(query, versionReply + "unexpected second line\r\n");
            udp.send(query, QByteArray("\0+ok=LSW3", 10));
            wrongPort.writeDatagram(versionReply, query.senderAddress(), query.senderPort());
        };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3500);
        QVERIFY(sawStage(stages, Discovery::Stage::NetworkScan));
        QCOMPARE(tcp.connections, 1);
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(http.requests.first().host, fallbackHost);
        QCOMPARE(found(finished).size(), 1);
        QCOMPARE(found(finished).first().ip, fallbackHost);
    }

    void wrongIdentityFallsBack_data() {
        QTest::addColumn<QByteArray>("wrongBody");
        QTest::newRow("logger serial") << statusBody(replacementSerial);
        QTest::newRow("logger MAC") << statusBody(selectedSerial, "00:11:22:33:44:55");
        QTest::newRow("inverter serial") << statusBody(selectedSerial, selectedMac, "9999999999");
    }
    void wrongIdentityFallsBack() {
        QFETCH(QByteArray, wrongBody);
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        http.response = [&](const QString &host, int) {
            return HttpResponse{host == broadcastHost ? wrongBody : statusBody(), 0};
        };
        udp.onQuery = [&](const QNetworkDatagram &query, int) { udp.send(query); };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(http.requests.size(), 1, 800);
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3500);
        QVERIFY(sawStage(stages, Discovery::Stage::NetworkScan));
        QCOMPARE(tcp.connections, 1);
        QCOMPARE(http.requests.size(), 2);
        QCOMPARE(found(finished).size(), 1);
        QCOMPARE(found(finished).first().ip, fallbackHost);
        QCOMPARE(found(finished).first().serial, selectedSerial);
    }

    void unansweredBroadcastUsesControlledTcpFallback() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        QElapsedTimer elapsed;
        elapsed.start();
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(udp.queries.size(), 1, 800);
        QCOMPARE(udp.queries.first().data(), QByteArray("AT+YZVER", 8));
        QCOMPARE(tcp.connections, 0);
        QCOMPARE(http.requests.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3500);
        QVERIFY(elapsed.elapsed() >= 1400);
        QVERIFY(sawStage(stages, Discovery::Stage::NetworkScan));
        QCOMPARE(tcp.connections, 1);
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(http.requests.first().host, fallbackHost);
        QCOMPARE(found(finished).size(), 1);
    }

    void pendingStatusVerificationDelaysFallback() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        http.response = [](const QString &, int) { return HttpResponse{statusBody(), 1000}; };
        udp.onQuery = [&](const QNetworkDatagram &query, int) { udp.send(query, versionReply, 1100); };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(http.requests.size(), 1, 1500);
        QTest::qWait(550);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(tcp.connections, 0);
        QVERIFY(!sawStage(stages, Discovery::Stage::NetworkScan));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
        QCOMPARE(tcp.connections, 0);
        QCOMPARE(found(finished).size(), 1);
        QCOMPARE(found(finished).first().ip, broadcastHost);
    }

    void duplicateRepliesVerifySenderOnlyOnce() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        http.response = [](const QString &, int) { return HttpResponse{statusBody(), 250}; };
        udp.onQuery = [&](const QNetworkDatagram &query, int) {
            udp.send(query);
            udp.send(query, versionReply, 25);
            udp.send(query, versionReply, 50);
        };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        discovery.start(options(http, tcp));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1200);
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(tcp.connections, 0);
        QCOMPARE(found(finished).size(), 1);
    }

    void cancelledRoundCannotCompleteRestart() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        http.response = [](const QString &host, int) {
            return host == broadcastHost ? HttpResponse{statusBody(), 1000}
                                         : HttpResponse{statusBody(replacementSerial), 0};
        };
        udp.onQuery = [&](const QNetworkDatagram &query, int number) {
            if (number == 1)
                udp.send(query);
        };
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        auto config = options(http, tcp);
        discovery.start(config);
        QTRY_COMPARE_WITH_TIMEOUT(http.requests.size(), 1, 800);
        discovery.cancel();
        QVERIFY(!discovery.running());
        udp.send(udp.queries.first());
        QTest::qWait(100);
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(finished.count(), 0);

        config.serial = replacementSerial;
        discovery.start(config);
        QTRY_COMPARE_WITH_TIMEOUT(udp.queries.size(), 2, 800);
        QTest::qWait(1050);
        QCOMPARE(finished.count(), 0);
        QCOMPARE(http.requests.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2500);
        QVERIFY(sawStage(stages, Discovery::Stage::NetworkScan));
        QCOMPARE(tcp.connections, 1);
        QCOMPARE(http.requests.size(), 2);
        QCOMPARE(found(finished).size(), 1);
        QCOMPARE(found(finished).first().serial, replacementSerial);
        QCOMPARE(found(finished).first().ip, fallbackHost);
    }

    void cancelBeforeStageIo_data() {
        QTest::addColumn<Discovery::Stage>("cancelAt");
        QTest::newRow("saved host") << Discovery::Stage::SavedHost;
        QTest::newRow("broadcast") << Discovery::Stage::Broadcast;
        QTest::newRow("TCP scan") << Discovery::Stage::NetworkScan;
    }
    void cancelBeforeStageIo() {
        QFETCH(Discovery::Stage, cancelAt);
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        bool cancelled = false;
        connect(&discovery, &Discovery::stageChanged, &discovery, [&](Discovery::Stage stage) {
            if (stage == cancelAt) {
                cancelled = true;
                discovery.cancel();
            }
        });
        auto config = options(http, tcp);
        config.knownIp = broadcastHost;
        discovery.start(config, cancelAt == Discovery::Stage::SavedHost);
        QTRY_VERIFY_WITH_TIMEOUT(cancelled, 3000);
        QTest::qWait(80);
        QVERIFY(!discovery.running());
        QCOMPARE(finished.count(), 0);
        QCOMPARE(http.requests.size(), 0);
        QCOMPARE(tcp.connections, 0);
        QCOMPARE(udp.queries.size(), cancelAt == Discovery::Stage::NetworkScan ? 1 : 0);
    }

    void restartFromStageSignalDoesNotSendOldBroadcast() {
        HttpMock http;
        TcpProbeMock tcp;
        UdpMock udp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        QVERIFY(udp.listen());
        Discovery discovery(targets(udp));
        QSignalSpy finished(&discovery, &Discovery::finished);
        QSignalSpy stages(&discovery, &Discovery::stageChanged);
        auto config = options(http, tcp);
        config.knownIp = broadcastHost;
        bool restarted = false;
        connect(&discovery, &Discovery::stageChanged, &discovery, [&](Discovery::Stage stage) {
            if (stage == Discovery::Stage::Broadcast && !restarted) {
                restarted = true;
                discovery.start(config, true);
            }
        });
        discovery.start(config);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1200);
        QVERIFY(restarted);
        QVERIFY(sawStage(stages, Discovery::Stage::SavedHost));
        QCOMPARE(udp.queries.size(), 0);
        QCOMPARE(tcp.connections, 0);
        QCOMPARE(http.requests.size(), 1);
        QCOMPARE(found(finished).size(), 1);
    }

    void emptyInjectedTargetsNeverExpandToLocalInterfaces() {
        HttpMock http;
        TcpProbeMock tcp;
        QVERIFY(http.listen());
        QVERIFY(tcp.listen());
        Discovery discovery(DiscoveryTargets{});
        QSignalSpy finished(&discovery, &Discovery::finished);
        auto config = options(http, tcp);
        config.knownIp = broadcastHost;
        discovery.start(config);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3500);
        QVERIFY(found(finished).isEmpty());
        QVERIFY(!finished.first()[1].toString().isEmpty());
        QCOMPARE(http.requests.size(), 0);
        QCOMPARE(tcp.connections, 0);
    }
};

QTEST_GUILESS_MAIN(DiscoveryTest)
#include "test_discovery.moc"
