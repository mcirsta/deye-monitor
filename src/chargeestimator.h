#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace Deye {

// Fits the observed SOC trend without assuming a battery capacity. Invalid or
// stale observations are ignored; a gap longer than the window starts a new fit.
class ChargeEstimator {
public:
    explicit ChargeEstimator(qint64 windowMs = 45 * 60 * 1000);
    void observe(qint64 timestampMs, double soc);
    std::optional<qint64> timeToFullMs() const;
    void reset();

private:
    struct Sample {
        qint64 timestampMs;
        double soc;
    };
    std::optional<double> slopePerMs() const;
    qint64 m_windowMs;
    QVector<Sample> m_samples;
};

// Uses the desktop dashboard's existing wording and rounds to the nearest minute.
QString formatChargeEta(qint64 durationMs);

} // namespace Deye
