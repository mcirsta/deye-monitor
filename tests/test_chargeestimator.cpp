#include "chargeestimator.h"
#include <QtTest>
#include <limits>

using namespace Deye;
namespace {
constexpr qint64 second = 1000;
constexpr qint64 minute = 60 * second;
constexpr qint64 hour = 60 * minute;
constexpr qint64 base = 1700000000000;

void feed(ChargeEstimator &estimator, qint64 start, qint64 step, int count, double startSoc, double change) {
    for (int i = 0; i < count; ++i)
        estimator.observe(start + i * step, startSoc + i * change);
}
} // namespace

class ChargeEstimatorTest : public QObject {
    Q_OBJECT
private slots:
    void chargingRamp() {
        ChargeEstimator estimator;
        feed(estimator, base, 30 * second, 21, 50, .5);
        const auto eta = estimator.timeToFullMs();
        QVERIFY(eta);
        QVERIFY(*eta >= 39 * minute && *eta <= 41 * minute);
    }
    void dischargingAndFull() {
        ChargeEstimator estimator;
        feed(estimator, base, 30 * second, 21, 80, -.5);
        QVERIFY(!estimator.timeToFullMs());
        estimator.reset();
        feed(estimator, base, 30 * second, 21, 90, .5);
        QVERIFY(!estimator.timeToFullMs());
    }
    void enoughSignalRequired() {
        ChargeEstimator estimator;
        estimator.observe(base, 50);
        estimator.observe(base + minute, 52);
        QVERIFY(!estimator.timeToFullMs());

        estimator.reset();
        feed(estimator, base, 5 * second, 5, 50, .5);
        QVERIFY(!estimator.timeToFullMs());

        estimator.reset();
        for (int i = 0; i < 20; ++i)
            estimator.observe(base + i * 30 * second, i < 10 ? 70 : 71);
        QVERIFY(!estimator.timeToFullMs());
    }
    void quantizedSoc() {
        ChargeEstimator estimator;
        for (int i = 0; i <= 90; ++i)
            estimator.observe(base + i * 30 * second, 70 + i / 15);
        const auto eta = estimator.timeToFullMs();
        QVERIFY(eta);
        QVERIFY(*eta > 2 * hour && *eta < 4 * hour);
    }
    void staleAndInvalidSamplesIgnored() {
        ChargeEstimator estimator;
        feed(estimator, base, 30 * second, 21, 50, .5);
        const auto expected = estimator.timeToFullMs();
        QVERIFY(expected);
        estimator.observe(base + 10 * minute, 95);
        estimator.observe(base - minute, 90);
        estimator.observe(base + 11 * minute, std::numeric_limits<double>::quiet_NaN());
        estimator.observe(base + 11 * minute, std::numeric_limits<double>::infinity());
        estimator.observe(base + 11 * minute, -1);
        estimator.observe(base + 11 * minute, 101);
        QCOMPARE(estimator.timeToFullMs(), expected);
    }
    void longGapAndExpiredHistory() {
        ChargeEstimator estimator(10 * minute);
        feed(estimator, base, 30 * second, 21, 50, .5);
        QVERIFY(estimator.timeToFullMs());
        estimator.observe(base + hour, 80);
        QVERIFY(!estimator.timeToFullMs());

        estimator.reset();
        feed(estimator, base, minute, 11, 50, 1);
        QVERIFY(estimator.timeToFullMs());
        feed(estimator, base + 11 * minute, minute, 11, 60, 0);
        QVERIFY(!estimator.timeToFullMs());
    }
    void maximumEta() {
        ChargeEstimator estimator(4 * hour);
        feed(estimator, base, hour, 3, 50, 1);
        const auto boundary = estimator.timeToFullMs();
        QVERIFY(boundary);
        QCOMPARE(*boundary, 48 * hour);
        estimator.reset();
        feed(estimator, base, hour, 3, 30, 1);
        QVERIFY(!estimator.timeToFullMs());
    }
    void nonpositiveWindowUsesDefault() {
        ChargeEstimator estimator(0);
        for (int i = 0; i <= 90; ++i)
            estimator.observe(base + i * 30 * second, 70 + i / 15);
        QVERIFY(estimator.timeToFullMs());
    }
    void formatEta_data() {
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<QString>("label");
        QTest::newRow("no duration") << qint64(0) << QString();
        QTest::newRow("subminute") << 30 * second << QString("<1 min to full");
        QTest::newRow("minutes") << 45 * minute << QString("45 min to full");
        QTest::newRow("hours") << 83 * minute << QString("1 h 23 min to full");
        QTest::newRow("whole hour") << 2 * hour << QString("2 h 0 min to full");
        QTest::newRow("round down") << 89 * second << QString("1 min to full");
        QTest::newRow("round up") << 90 * second << QString("2 min to full");
    }
    void formatEta() {
        QFETCH(qint64, durationMs);
        QFETCH(QString, label);
        QCOMPARE(formatChargeEta(durationMs), label);
    }
};

QTEST_GUILESS_MAIN(ChargeEstimatorTest)
#include "test_chargeestimator.moc"
