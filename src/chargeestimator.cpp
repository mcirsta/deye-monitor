#include "chargeestimator.h"
#include <cmath>

namespace Deye {
namespace {
constexpr qint64 defaultWindowMs = 45 * 60 * 1000;
constexpr qint64 minimumSpanMs = 60 * 1000;
constexpr qint64 maximumEtaMs = 48LL * 60 * 60 * 1000;
constexpr double minimumSocMovement = 2.0;

// Callers establish chronological order before using this difference. Unsigned
// subtraction also handles timestamps on opposite sides of the Unix epoch.
quint64 elapsedMs(qint64 later, qint64 earlier) { return quint64(later) - quint64(earlier); }
} // namespace

ChargeEstimator::ChargeEstimator(qint64 windowMs) : m_windowMs(windowMs > 0 ? windowMs : defaultWindowMs) {}

void ChargeEstimator::observe(qint64 timestampMs, double soc) {
    if (!std::isfinite(soc) || soc < 0 || soc > 100)
        return;
    if (!m_samples.isEmpty()) {
        const auto lastTimestamp = m_samples.last().timestampMs;
        if (timestampMs <= lastTimestamp)
            return;
        if (elapsedMs(timestampMs, lastTimestamp) > quint64(m_windowMs))
            m_samples.clear();
    }
    m_samples.append({timestampMs, soc});
    qsizetype expired = 0;
    while (expired < m_samples.size() &&
           elapsedMs(timestampMs, m_samples[expired].timestampMs) > quint64(m_windowMs))
        ++expired;
    if (expired)
        m_samples.remove(0, expired);
}

void ChargeEstimator::reset() { m_samples.clear(); }

std::optional<double> ChargeEstimator::slopePerMs() const {
    if (m_samples.size() < 3 ||
        elapsedMs(m_samples.last().timestampMs, m_samples.first().timestampMs) < minimumSpanMs ||
        std::abs(m_samples.last().soc - m_samples.first().soc) < minimumSocMovement)
        return {};

    // Center the observations before fitting so absolute epoch timestamps never
    // enter the regression and long observation windows retain useful precision.
    double meanX = 0, meanY = 0;
    const auto firstTimestamp = m_samples.first().timestampMs;
    for (const auto &sample : m_samples) {
        meanX += double(elapsedMs(sample.timestampMs, firstTimestamp));
        meanY += sample.soc;
    }
    meanX /= double(m_samples.size());
    meanY /= double(m_samples.size());
    double covariance = 0, variance = 0;
    for (const auto &sample : m_samples) {
        const double x = double(elapsedMs(sample.timestampMs, firstTimestamp)) - meanX;
        covariance += x * (sample.soc - meanY);
        variance += x * x;
    }
    if (variance <= 0)
        return {};
    const double slope = covariance / variance;
    return std::isfinite(slope) ? std::optional<double>(slope) : std::nullopt;
}

std::optional<qint64> ChargeEstimator::timeToFullMs() const {
    const auto slope = slopePerMs();
    if (!slope || *slope <= 0)
        return {};
    const double remaining = 100 - m_samples.last().soc;
    if (remaining <= 0)
        return {};
    const double durationMs = remaining / *slope;
    if (!std::isfinite(durationMs) || durationMs <= 0 || durationMs > maximumEtaMs)
        return {};
    return qint64(durationMs);
}

QString formatChargeEta(qint64 durationMs) {
    if (durationMs <= 0)
        return {};
    if (durationMs < 60000)
        return "<1 min to full";
    const qint64 minutes = durationMs / 60000 + (durationMs % 60000 >= 30000 ? 1 : 0);
    return minutes < 60 ? QString("%1 min to full").arg(minutes)
                        : QString("%1 h %2 min to full").arg(minutes / 60).arg(minutes % 60);
}

} // namespace Deye
