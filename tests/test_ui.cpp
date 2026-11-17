#include "historychart.h"
#include "monitor.h"
#include <QCursor>
#include <QNetworkDatagram>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QUdpSocket>
#include <QtEndian>
#include <QtTest>
#include <functional>
#include <limits>
#include <memory>

static QByteArray unsupportedIdentityReply(quint8 sequence) {
    QByteArray rtu = QByteArray::fromHex("010332");
    const QByteArray serial = "9900000001";
    for (int address = 0; address < 25; ++address) {
        rtu += address >= 3 && address <= 7 ? serial[(address - 3) * 2] : char(0);
        rtu += address >= 3 && address <= 7 ? serial[(address - 3) * 2 + 1] : char(0);
    }
    const auto crc = Deye::modbusCrc(rtu);
    rtu += char(crc);
    rtu += char(crc >> 8);
    QByteArray payload(14, 0);
    payload[0] = 2;
    payload[1] = 1;
    payload += rtu;
    QByteArray frame(11, 0);
    frame[0] = char(0xa5);
    qToLittleEndian<quint16>(quint16(payload.size()), frame.data() + 1);
    qToLittleEndian<quint16>(0x1510, frame.data() + 3);
    qToLittleEndian<quint32>(3000000001u, frame.data() + 7);
    frame[5] = char(sequence);
    frame += payload;
    quint8 checksum = 0;
    for (int n = 1; n < frame.size(); ++n)
        checksum += quint8(frame[n]);
    frame += char(checksum);
    frame += char(0x15);
    return frame;
}

class UiTest : public QObject {
    Q_OBJECT
private slots:
    void settingsAndNavigation() {
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<HistoryChart>("Deye", 1, 0, "HistoryChart");
        QTcpServer discoveryHttp;
        QUdpSocket discoveryUdp;
        QVERIFY(discoveryHttp.listen(QHostAddress::LocalHost));
        QVERIFY(discoveryUdp.bind(QHostAddress::LocalHost, 0));
        connect(&discoveryUdp, &QUdpSocket::readyRead, this, [&] {
            while (discoveryUdp.hasPendingDatagrams()) {
                const auto query = discoveryUdp.receiveDatagram();
                discoveryUdp.writeDatagram("+ok=LSW3_TEST\r\n", query.senderAddress(), query.senderPort());
            }
        });
        connect(&discoveryHttp, &QTcpServer::newConnection, this, [&] {
            auto socket = discoveryHttp.nextPendingConnection();
            auto headers = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [socket, headers] {
                *headers += socket->readAll();
                if (!headers->contains("\r\n\r\n"))
                    return;
                const QByteArray body = "var cover_mid = \"3000000001\"; "
                                        "var cover_sta_mac = \"02:00:00:00:00:01\"; "
                                        "var webdata_sn = \"9900000001\";";
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        Deye::DiscoveryTargets discoveryTargets;
        discoveryTargets.broadcasts.append({QHostAddress::LocalHost, QHostAddress::LocalHost});
        discoveryTargets.versionPort = discoveryUdp.localPort();
        Deye::Monitor monitor(false, nullptr, discoveryTargets);
        monitor.setDemo();
        connect(&monitor, &Deye::Monitor::connectionError, &monitor, [&] {
            if (!monitor.paused())
                monitor.togglePaused();
        });
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("monitor", &monitor);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(warnings.count(), 0);
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem =
            [&](QQuickItem *item, const QString &name) -> QQuickItem * {
            if (item->objectName() == name)
                return item;
            for (auto child : item->childItems())
                if (auto found = findItem(child, name))
                    return found;
            return nullptr;
        };
        auto solarProduction = findItem(window->contentItem(), "solarProductionCard");
        auto homeConsumption = findItem(window->contentItem(), "homeConsumptionCard");
        QVERIFY(solarProduction && homeConsumption);
        QCOMPARE(solarProduction->property("note").toString(), QString("18.4 kWh generated today"));
        QCOMPARE(homeConsumption->property("note").toString(), QString("12.6 kWh consumed today"));
        for (const auto &invalid :
             QVariantList{QVariant{}, QString("15.7"), -1.0, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
            QVariant note;
            QVERIFY(QMetaObject::invokeMethod(window, "energyNote", Q_RETURN_ARG(QVariant, note),
                                              Q_ARG(QVariant, invalid),
                                              Q_ARG(QVariant, QVariant("generated today"))));
            QVERIFY(note.toString().isEmpty());
        }
        QVariant zeroNote;
        QVERIFY(QMetaObject::invokeMethod(window, "energyNote", Q_RETURN_ARG(QVariant, zeroNote),
                                          Q_ARG(QVariant, QVariant(0.0)),
                                          Q_ARG(QVariant, QVariant("generated today"))));
        QCOMPARE(zeroNote.toString(), QString("0.0 kWh generated today"));
        auto animatedFlow = findItem(window->contentItem(), "energyFlowView");
        QVERIFY(animatedFlow);
        auto solarDot = findItem(animatedFlow, "pv_total_pFlowDot");
        QVERIFY(solarDot);
        QTRY_VERIFY(solarDot->property("motionRunning").toBool());
        window->setVisibility(QWindow::Minimized);
        QTRY_VERIFY(!animatedFlow->property("animationsEnabled").toBool());
        QTRY_VERIFY(!solarDot->property("motionRunning").toBool());
        window->setVisibility(QWindow::Windowed);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(solarDot->property("motionRunning").toBool());
        QSignalSpy solarXChanges(solarDot, &QQuickItem::xChanged);
        const auto firstFlowFrame = window->grabWindow();
        QVERIFY(!firstFlowFrame.isNull());
        solarXChanges.clear();
        QTest::qWait(250);
        const auto secondFlowFrame = window->grabWindow();
        QVERIFY(!secondFlowFrame.isNull());
        QVERIFY(firstFlowFrame != secondFlowFrame);
        QVERIFY(solarXChanges.count() < 6);
        auto batteryPower = findItem(window->contentItem(), "batteryPowerCard");
        auto batteryPowerIcon = findItem(window->contentItem(), "batteryPowerIcon");
        auto batteryFlowValue = findItem(window->contentItem(), "bat_powerFlowValue");
        auto batteryState = findItem(window->contentItem(), "batteryStateText");
        QVERIFY(batteryPower && batteryPowerIcon && batteryFlowValue && batteryState);
        QVERIFY(batteryPowerIcon->isVisible());
        QCOMPARE(batteryPowerIcon->property("name").toString(), QString("battery"));
        QCOMPARE(batteryPower->property("title").toString(), QString("Charging battery"));
        QCOMPARE(batteryPower->property("note").toString(), QString("Power into battery"));
        QCOMPARE(batteryPower->property("titleColor").value<QColor>(),
                 batteryPower->property("valueColor").value<QColor>());
        QCOMPARE(batteryFlowValue->property("color").value<QColor>(),
                 batteryPower->property("valueColor").value<QColor>());
        QCOMPARE(batteryState->property("text").toString(), QString("Charging"));
        QVERIFY(!findItem(window->contentItem(), "batteryFlowStatus"));
        QVERIFY(!findItem(window->contentItem(), "batteryFlowDirection"));
        auto historyPanel = findItem(window->contentItem(), "overviewHistoryPanel");
        QVERIFY(historyPanel);
        auto chart = findItem(historyPanel, "powerHistoryChart");
        QVERIFY(chart);
        const auto lastHistoryTime = monitor.history().last().toMap().value("time").toLongLong();
        QTest::mouseMove(window,
                         chart->mapToScene(QPointF(chart->width() - 16, chart->height() / 2)).toPoint());
        QTRY_VERIFY(chart->property("cursorVisible").toBool());
        QCOMPARE(chart->property("cursorSample").toMap().value("time").toLongLong(), lastHistoryTime);
        QCOMPARE(chart->cursor().shape(), Qt::BlankCursor);
        const auto rightCursorX = chart->property("cursorX").toReal();
        QTest::mouseMove(
            window,
            chart->mapToScene(QPointF(48 + (chart->width() - 62) / 2, chart->height() / 2)).toPoint());
        QTRY_VERIFY(chart->property("cursorSample").toMap().value("time").toLongLong() < lastHistoryTime);
        QVERIFY(chart->property("cursorX").toReal() < rightCursorX - 50);
        const auto middlePoint =
            chart->mapToScene(QPointF(48 + (chart->width() - 62) / 2, chart->height() / 2)).toPoint();
        const auto lastPoint = chart->mapToScene(QPointF(chart->width() - 16, chart->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, middlePoint);
        QTest::mouseMove(window, lastPoint);
        QTRY_COMPARE(chart->property("cursorSample").toMap().value("time").toLongLong(), lastHistoryTime);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, lastPoint);
        QVERIFY(historyPanel->setProperty("minutes", 60));
        QTest::mouseMove(window, middlePoint);
        QTRY_VERIFY(chart->property("cursorVisible").toBool());
        QTRY_VERIFY(chart->property("cursorSample").toMap().isEmpty());
        QVERIFY(chart->property("cursorTime").toLongLong() > 0);
        QVERIFY(historyPanel->setProperty("minutes", 15));
        QTest::mouseMove(window, chart->mapToScene(QPointF(2, 2)).toPoint());
        QTRY_VERIFY(!chart->property("cursorVisible").toBool());
        QCOMPARE(chart->cursor().shape(), Qt::ArrowCursor);
        window->setProperty("page", 1);
        QTest::qWait(30);
        QVERIFY(!animatedFlow->property("animationsEnabled").toBool());
        QVERIFY(!solarDot->property("motionRunning").toBool());
        QCOMPARE(warnings.count(), 0);
        auto battery = findItem(window->contentItem(), "batteryCard");
        auto advanced = findItem(window->contentItem(), "advancedBatterySettingsCard");
        auto acOutput = findItem(window->contentItem(), "acOutputSettingsCard");
        auto gridLimits = findItem(window->contentItem(), "gridVoltageLimitsCard");
        QVERIFY(battery && advanced);
        QVERIFY(acOutput && gridLimits);
        QVERIFY(battery->isVisible());
        QVERIFY(advanced->isVisible());
        QVERIFY(acOutput->isVisible());
        QVERIFY(gridLimits->isVisible());
        QVERIFY(advanced->x() > battery->x());
        QCOMPARE(advanced->y(), battery->y());
        QVERIFY(!findItem(window->contentItem(), "advancedBatteryToggle"));
        window->setProperty("page", 2);
        QTest::qWait(30);
        QCOMPARE(warnings.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(window, "openSettings"));
        auto settings = window->findChild<QQuickWindow *>("settingsWindow");
        QVERIFY(settings);
        QTRY_VERIFY(settings->isVisible());
        auto ip = settings->findChild<QQuickItem *>("loggerIp");
        auto scan = settings->findChild<QQuickItem *>("scanButton");
        auto save = settings->findChild<QQuickItem *>("saveSettings");
        auto cancel = settings->findChild<QQuickItem *>("cancelSettings");
        QVERIFY(ip && scan && save && cancel);
        QVERIFY(scan->isVisible());
        QCOMPARE(scan->property("text").toString(), QString("Scan network"));
        monitor.scanSettings({{"diagnostic_status_port", discoveryHttp.serverPort()}});
        QTRY_COMPARE_WITH_TIMEOUT(ip->property("text").toString(), QString("127.0.0.1"), 800);
        QVERIFY(monitor.scanning());
        QVERIFY(save->isEnabled());
        // Cancelling from Save & connect must preserve edits made after the first result appeared.
        QVERIFY(ip->setProperty("text", "127.0.0.99"));
        monitor.cancelScan();
        QCOMPARE(ip->property("text").toString(), QString("127.0.0.99"));
        auto probeButton = settings->findChild<QQuickItem *>("probeSettingsButton");
        auto probeWindow = window->findChild<QQuickWindow *>("diagnosticProbeWindow");
        QVERIFY(!probeButton);
        QVERIFY(probeWindow);
        QVERIFY(QMetaObject::invokeMethod(probeWindow, "open"));
        QVERIFY(!probeWindow->isVisible());
        auto probeModel = findItem(probeWindow->contentItem(), "probeExactModel");
        auto probeStart = findItem(probeWindow->contentItem(), "probeStartButton");
        auto probeExport = findItem(probeWindow->contentItem(), "probeExportButton");
        auto includeIdentifiers = findItem(probeWindow->contentItem(), "probeIncludeIdentifiers");
        QVERIFY(probeModel && probeStart && probeExport && includeIdentifiers);
        QVERIFY(!monitor.diagnosticProbe()->running());
        QVERIFY(!includeIdentifiers->property("checked").toBool());
        QVERIFY(probeModel->setProperty("text", "SUN-UNKNOWN"));
        QVERIFY(!probeStart->isEnabled());
        QVERIFY(!probeExport->isEnabled());
        probeWindow->close();
        QTRY_VERIFY(!probeWindow->isVisible());
        QVERIFY(ip->setProperty("text", ""));
        QVERIFY(!save->isEnabled());
        QVERIFY(ip->setProperty("text", "127.0.0.1"));
        QVERIFY(save->isEnabled());
        QCOMPARE(monitor.host(), QString("Demo preview"));
        QTest::mouseClick(settings, Qt::LeftButton, Qt::NoModifier,
                          cancel->mapToScene(QPointF(cancel->width() / 2, cancel->height() / 2)).toPoint());
        QTRY_VERIFY(!settings->isVisible());
        QVERIFY(monitor.demo());
        QCOMPARE(warnings.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(window, "openSettings"));
        QVERIFY(ip->setProperty("text", "127.0.0.1"));
        QTest::mouseClick(settings, Qt::LeftButton, Qt::NoModifier,
                          save->mapToScene(QPointF(save->width() / 2, save->height() / 2)).toPoint());
        QTRY_VERIFY(!settings->isVisible());
        QVERIFY(!monitor.demo());
        QCOMPARE(monitor.host(), QString("127.0.0.1"));
        QCOMPARE(monitor.settings().value("profile").toString(), QString("auto"));
        QVERIFY(solarProduction->property("note").toString().isEmpty());
        QVERIFY(homeConsumption->property("note").toString().isEmpty());
        auto sessionHistory = findItem(window->contentItem(), "sessionHistoryPanel");
        QVERIFY(sessionHistory);
        auto emptyChart = findItem(sessionHistory, "powerHistoryChart");
        QVERIFY(emptyChart);
        QTest::mouseMove(
            window,
            emptyChart->mapToScene(QPointF(emptyChart->width() / 2, emptyChart->height() / 2)).toPoint());
        QTRY_VERIFY(emptyChart->property("cursorVisible").toBool());
        QVERIFY(emptyChart->property("cursorSample").toMap().isEmpty());
        QCOMPARE(emptyChart->cursor().shape(), Qt::BlankCursor);
        if (!monitor.paused())
            monitor.togglePaused();
        window->resize(1080, 800);
        window->setProperty("page", 0);
        QTest::qWait(30);
        QVERIFY(batteryPowerIcon->isVisible());
        QVERIFY(batteryPowerIcon->mapToItem(batteryPower, QPointF(batteryPowerIcon->width(), 0)).x() <
                batteryPower->width());
        auto verifyFlowGeometry = [&] {
            auto flow = findItem(window->contentItem(), "energyFlowView");
            auto diagram = findItem(window->contentItem(), "powerFlowDiagram");
            auto mode = findItem(window->contentItem(), "powerFlowMode");
            QVERIFY(flow && diagram && mode);
            QVERIFY(diagram->width() <= 900);
            for (const auto &key : {"pv_total_p", "grid_power", "bat_power", "load_power"}) {
                const QString name = QString::fromLatin1(key);
                auto icon = findItem(window->contentItem(), name + "FlowIcon");
                auto label = findItem(window->contentItem(), name + "FlowLabel");
                auto value = findItem(window->contentItem(), name + "FlowValue");
                QVERIFY(icon && label && value);
                const auto iconTop = icon->mapToItem(diagram, QPointF()).y();
                const auto labelTop = label->mapToItem(diagram, QPointF()).y();
                const auto valueTop = value->mapToItem(diagram, QPointF()).y();
                const auto row =
                    (name == "pv_total_p" || name == "grid_power") ? "topFraction" : "bottomFraction";
                const auto connectorY = diagram->height() * diagram->property(row).toReal();
                QVERIFY(qAbs(iconTop + icon->height() / 2 - connectorY) < 1);
                QVERIFY(labelTop >= iconTop + icon->height());
                QVERIFY(valueTop >= labelTop + label->height());
                QVERIFY(value->mapToItem(flow, QPointF(0, value->height())).y() <
                        mode->mapToItem(flow, QPointF()).y());
            }
        };
        verifyFlowGeometry();
        window->resize(2000, 900);
        QTest::qWait(30);
        verifyFlowGeometry();
        window->resize(2000, 1400);
        QTest::qWait(30);
        verifyFlowGeometry();
        auto expandedFlow = findItem(window->contentItem(), "energyFlowView");
        QVERIFY(expandedFlow);
        QVERIFY(expandedFlow->height() > 500);
        QVERIFY(expandedFlow->setProperty("active", true));
        QVERIFY(expandedFlow->setProperty("animationsEnabled", true));
        QVERIFY(expandedFlow->setProperty("values", QVariantMap{{"pv_total_p", 1000.0}}));
        QTRY_VERIFY(solarDot->property("motionRunning").toBool());
        QVERIFY(expandedFlow->setProperty("values", QVariantMap{}));
        QTRY_VERIFY(!solarDot->property("motionRunning").toBool());
        QTcpServer unsupportedLogger;
        QVERIFY(unsupportedLogger.listen(QHostAddress::LocalHost));
        int unsupportedReads = 0;
        connect(&unsupportedLogger, &QTcpServer::newConnection, this, [&] {
            auto socket = unsupportedLogger.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket, request] {
                *request += socket->readAll();
                if (request->size() >= 36) {
                    ++unsupportedReads;
                    socket->write(unsupportedIdentityReply(quint8((*request)[5])));
                    request->clear();
                }
            });
        });
        monitor.applySettings(
            {{"ip", "127.0.0.1"}, {"port", unsupportedLogger.serverPort()}, {"serial", "3000000001"}});
        QTRY_VERIFY(monitor.probeOffered());
        auto offer = findItem(window->contentItem(), "probeOfferButton");
        QVERIFY(offer && offer->isVisible());
        QCOMPARE(unsupportedReads, 1);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          offer->mapToScene(QPointF(offer->width() / 2, offer->height() / 2)).toPoint());
        QTRY_VERIFY(probeWindow->isVisible());
        QVERIFY(!settings->isVisible());
        QCOMPARE(probeWindow->transientParent(), window);
        QVERIFY(monitor.diagnosticProbe()->ready());
        QVERIFY(!monitor.diagnosticProbe()->running());
        QVERIFY(probeModel->setProperty("text", "SUN-NEW-MODEL"));
        QVERIFY(probeExport->isEnabled());
        QCOMPARE(unsupportedReads, 1);
        probeWindow->close();
        QCOMPARE(warnings.count(), 0);
    }
};
QTEST_MAIN(UiTest)
#include "test_ui.moc"
