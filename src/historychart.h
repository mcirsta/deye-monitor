#pragma once
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantMap>

class HistoryChart : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(int minutes READ minutes WRITE setMinutes NOTIFY minutesChanged)
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY cursorChanged)
    Q_PROPERTY(qreal cursorX READ cursorX NOTIFY cursorChanged)
    Q_PROPERTY(qint64 cursorTime READ cursorTime NOTIFY cursorChanged)
    Q_PROPERTY(QVariantMap cursorSample READ cursorSample NOTIFY cursorChanged)
public:
    explicit HistoryChart(QQuickItem *parent = nullptr);
    QVariantList samples() const { return m_samples; }
    int minutes() const { return m_minutes; }
    bool cursorVisible() const { return m_cursorVisible; }
    qreal cursorX() const { return m_cursorX; }
    qint64 cursorTime() const { return m_cursorTime; }
    QVariantMap cursorSample() const { return m_cursorSample; }
    void setSamples(QVariantList samples);
    void setMinutes(int minutes);
    void paint(QPainter *) override;
protected:
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
signals:
    void samplesChanged();
    void minutesChanged();
    void cursorChanged();

private:
    QRectF plotRect() const;
    void selectAt(QPointF position, bool dragging);
    void clearCursor();
    void hideMouse(bool hide);
    QVariantList m_samples;
    QVariantMap m_cursorSample;
    int m_minutes = 15;
    QPointF m_pointerPosition;
    qreal m_cursorX = 0;
    qint64 m_cursorTime = 0;
    bool m_cursorVisible = false;
    bool m_pointerActive = false;
    bool m_dragging = false;
    bool m_mouseHidden = false;
};
