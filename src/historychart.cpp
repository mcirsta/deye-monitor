#include "historychart.h"
#include <QCursor>
#include <QDateTime>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <limits>

HistoryChart::HistoryChart(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void HistoryChart::setSamples(QVariantList samples) {
    m_samples = std::move(samples);
    emit samplesChanged();
    if (m_pointerActive)
        selectAt(m_pointerPosition, m_dragging);
    update();
}

void HistoryChart::setMinutes(int minutes) {
    minutes = qMax(1, minutes);
    if (m_minutes == minutes)
        return;
    m_minutes = minutes;
    emit minutesChanged();
    if (m_pointerActive)
        selectAt(m_pointerPosition, m_dragging);
    update();
}

QRectF HistoryChart::plotRect() const {
    return QRectF(48, 12, qMax(1., width() - 62), qMax(1., height() - 40));
}

void HistoryChart::clearCursor() {
    hideMouse(false);
    if (!m_cursorVisible)
        return;
    m_cursorVisible = false;
    m_cursorX = 0;
    m_cursorTime = 0;
    m_cursorSample.clear();
    emit cursorChanged();
    update();
}

void HistoryChart::hideMouse(bool hide) {
    if (m_mouseHidden == hide)
        return;
    m_mouseHidden = hide;
    if (hide)
        setCursor(QCursor(Qt::BlankCursor));
    else
        unsetCursor();
}

void HistoryChart::selectAt(QPointF position, bool dragging) {
    m_pointerPosition = position;
    m_pointerActive = true;
    const auto plot = plotRect();
    if (!dragging && !plot.contains(position)) {
        m_pointerActive = false;
        clearCursor();
        return;
    }
    hideMouse(true);
    const qint64 end = m_samples.isEmpty() ? QDateTime::currentMSecsSinceEpoch()
                                          : m_samples.last().toMap().value("time").toLongLong();
    const qint64 start = end - qint64(m_minutes) * 60000;
    const qreal x = qBound(plot.left(), position.x(), plot.right());
    const qint64 target = start + qint64((x - plot.left()) / plot.width() * (end - start));
    qint64 first = std::numeric_limits<qint64>::max();
    qint64 last = std::numeric_limits<qint64>::min();
    qint64 nearestDistance = std::numeric_limits<qint64>::max();
    QVariantMap nearest;
    for (const auto &value : m_samples) {
        const auto sample = value.toMap();
        const qint64 time = sample.value("time").toLongLong();
        if (time < start || time > end)
            continue;
        first = qMin(first, time);
        last = qMax(last, time);
        const qint64 distance = std::abs(time - target);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = sample;
        }
    }
    const auto sampleX = [&](qint64 time) {
        return plot.left() + qreal(time - start) / (end - start) * plot.width();
    };
    if (nearest.isEmpty() || x < sampleX(first) - 8 || x > sampleX(last) + 8)
        nearest.clear();
    if (!m_cursorVisible || !qFuzzyCompare(m_cursorX, x) || m_cursorTime != target ||
        m_cursorSample != nearest) {
        m_cursorVisible = true;
        m_cursorX = x;
        m_cursorTime = target;
        m_cursorSample = std::move(nearest);
        emit cursorChanged();
        update();
    }
}

void HistoryChart::hoverMoveEvent(QHoverEvent *event) {
    selectAt(event->position(), false);
    event->accept();
}

void HistoryChart::hoverLeaveEvent(QHoverEvent *event) {
    if (!m_dragging) {
        m_pointerActive = false;
        clearCursor();
    }
    event->accept();
}

void HistoryChart::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position())) {
        selectAt(event->position(), false);
        if (m_cursorVisible) {
            m_dragging = true;
            event->accept();
            return;
        }
    }
    event->ignore();
}

void HistoryChart::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        selectAt(event->position(), true);
        event->accept();
    } else
        event->ignore();
}

void HistoryChart::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging) {
        event->ignore();
        return;
    }
    m_dragging = false;
    selectAt(event->position(), false);
    event->accept();
}

void HistoryChart::paint(QPainter *p) {
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plotRect();
    const QColor labelColor("#778780");
    QFont font = p->font();
    font.setPixelSize(13);
    p->setFont(font);
    QList<QVariantMap> samples;
    qint64 end = m_samples.isEmpty() ? QDateTime::currentMSecsSinceEpoch()
                                     : m_samples.last().toMap().value("time").toLongLong();
    qint64 start = end - qint64(m_minutes) * 60000;
    double low = 0, high = 1000;
    for (const auto &v : m_samples) {
        auto s = v.toMap();
        if (s.value("time").toLongLong() < start)
            continue;
        samples.append(s);
        for (auto key : {"pv", "load", "battery", "grid"}) {
            if (!s.value(key).isValid())
                continue;
            double n = s.value(key).toDouble();
            low = qMin(low, n);
            high = qMax(high, n);
        }
    }
    double step = std::pow(10., std::floor(std::log10((high - low) / 4)));
    double normalized = (high - low) / 4 / step;
    step *= normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10;
    high = std::ceil(high / step) * step;
    low = std::floor(low / step) * step;
    if (high == low)
        high += step;
    auto y = [&](double n) { return plot.bottom() - (n - low) / (high - low) * plot.height(); };
    p->setPen(QColor("#e8eeeb"));
    for (double n = low; n <= high + step * .1; n += step) {
        p->drawLine(QPointF(plot.left(), y(n)), QPointF(plot.right(), y(n)));
        p->setPen(labelColor);
        p->drawText(QRectF(0, y(n) - 9, 40, 18), Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(n / 1000, 'g', 3));
        p->setPen(QColor("#e8eeeb"));
    }
    p->setPen(labelColor);
    p->drawText(QRectF(0, 0, 24, 18), Qt::AlignCenter, "kW");
    for (int n = 0; n <= 3; ++n) {
        qint64 t = start + (end - start) * n / 3;
        double x = plot.left() + plot.width() * n / 3;
        const double labelX = qMax(0., qMin(x - 24, width() - 48));
        p->drawText(QRectF(labelX, plot.bottom() + 9, 48, 18), Qt::AlignCenter,
                    QDateTime::fromMSecsSinceEpoch(t).toString("HH:mm"));
    }
    const QStringList keys = {"pv", "load", "battery", "grid"},
                      colors = {"#e9a23b", "#5485c0", "#21a482", "#9679bc"};
    if (samples.isEmpty()) {
        p->drawText(plot, Qt::AlignCenter, "Live history appears after the first reading");
    } else {
        p->save();
        p->setClipRect(plot.adjusted(-2, -2, 2, 2));
        for (int index = 0; index < keys.size(); ++index) {
            QPainterPath path;
            bool active = false;
            for (const auto &s : samples) {
                if (!s.value(keys[index]).isValid()) {
                    active = false;
                    continue;
                }
                QPointF point(plot.left() +
                                  double(s.value("time").toLongLong() - start) / (end - start) * plot.width(),
                              y(s.value(keys[index]).toDouble()));
                if (!active) {
                    path.moveTo(point);
                    active = true;
                } else
                    path.lineTo(point);
            }
            p->setPen(QPen(QColor(colors[index]), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(path);
            if (samples.size() == 1) {
                const auto &s = samples.first();
                if (s.value(keys[index]).isValid())
                    p->drawEllipse(QPointF(plot.right(), y(s.value(keys[index]).toDouble())), 2.5, 2.5);
            }
        }
        p->restore();
    }
    if (!m_cursorVisible)
        return;

    const qreal cursorX = m_cursorX;
    p->setPen(QPen(QColor("#6c8178"), 1, Qt::DashLine));
    p->drawLine(QPointF(cursorX, plot.top()), QPointF(cursorX, plot.bottom()));
    const qint64 sampleTime = m_cursorSample.value("time").toLongLong();
    const qreal sampleX = plot.left() + qreal(sampleTime - start) / (end - start) * plot.width();
    for (int index = 0; index < keys.size(); ++index) {
        if (!m_cursorSample.value(keys[index]).isValid())
            continue;
        p->setPen(QPen(QColor(colors[index]), 2));
        p->setBrush(Qt::white);
        p->drawEllipse(QPointF(sampleX, y(m_cursorSample.value(keys[index]).toDouble())), 4, 4);
    }

    constexpr qreal cardWidth = 294;
    constexpr qreal cardHeight = 78;
    qreal cardX = cursorX + 12;
    if (cardX + cardWidth > width() - 4)
        cardX = cursorX - cardWidth - 12;
    cardX = qBound(4., cardX, qMax(4., width() - cardWidth - 4));
    const QRectF card(cardX, 2, cardWidth, cardHeight);
    p->setBrush(QColor("#ffffff"));
    p->setPen(QPen(QColor("#d4e3dc"), 1));
    p->drawRoundedRect(card, 8, 8);
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    p->setFont(font);
    p->setPen(QColor("#183d37"));
    p->drawText(QRectF(card.left() + 11, card.top() + 5, 100, 19),
                Qt::AlignLeft | Qt::AlignVCenter,
                QDateTime::fromMSecsSinceEpoch(m_cursorTime).toString("HH:mm:ss"));
    font.setPixelSize(11);
    font.setWeight(QFont::Normal);
    p->setFont(font);
    p->setPen(labelColor);
    p->drawText(QRectF(card.right() - 120, card.top() + 5, 108, 19),
                Qt::AlignRight | Qt::AlignVCenter,
                m_cursorSample.isEmpty() ? "No reading" :
                    "Sample " + QDateTime::fromMSecsSinceEpoch(sampleTime).toString("HH:mm:ss"));
    p->setPen(QColor("#e9f0ec"));
    p->drawLine(QPointF(card.left() + 11, card.top() + 30),
                QPointF(card.right() - 11, card.top() + 30));

    const QStringList labels = {"Solar", "Home", "Battery", "Grid"};
    font.setPixelSize(12);
    p->setFont(font);
    for (int index = 0; index < keys.size(); ++index) {
        const qreal cellX = card.left() + 11 + (index % 2) * 137;
        const qreal cellY = card.top() + 36 + (index / 2) * 19;
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(colors[index]));
        p->drawEllipse(QPointF(cellX + 4, cellY + 8), 3, 3);
        p->setPen(QColor("#61786e"));
        p->drawText(QRectF(cellX + 13, cellY, 49, 16),
                    Qt::AlignLeft | Qt::AlignVCenter, labels[index]);
        const auto value = m_cursorSample.value(keys[index]);
        p->setPen(QColor("#183d37"));
        p->drawText(QRectF(cellX + 62, cellY, 68, 16),
                    Qt::AlignRight | Qt::AlignVCenter,
                    value.isValid() ? QString::number(value.toDouble() / 1000, 'f', 2) + " kW" : "—");
    }
}
