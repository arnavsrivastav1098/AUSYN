#include "live_visuals.h"
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QHideEvent>
#include <QLinearGradient>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Ausyn {
LiveActivityChart::LiveActivityChart(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("Recent CPU, memory and graphics activity"));
    setMouseTracking(true);
    arrival_ = new QVariantAnimation(this); arrival_->setDuration(480);
    arrival_->setStartValue(0.0); arrival_->setEndValue(1.0);
    connect(arrival_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { arrivalPhase_ = value.toReal(); if (isVisible() && !window()->isMinimized()) update(); });
}

void LiveActivityChart::addSnapshot(const SystemSnapshot& snapshot)
{
    if (!snapshot.capturedAt.isValid() || (!points_.isEmpty() && snapshot.capturedAt <= points_.last().at)) return;
    points_.append({snapshot.capturedAt, snapshot.processorUsagePercent,
                    snapshot.memoryUsagePercent, snapshot.graphicsUsagePercent});
    // Bound memory independently of the user's chosen sampling cadence.
    while (points_.size() > 120 || (!points_.isEmpty() && points_.first().at.secsTo(snapshot.capturedAt) > 600))
        points_.removeFirst();
    setAccessibleDescription(QStringLiteral("%1 actual readings over the last ten minutes. Missing values and large sampling gaps break the line.").arg(points_.size()));
    hovered_ = -1;
    if (isVisible() && !window()->isMinimized() && animationsEnabled_) arrival_->start();
    if (isVisible() && !window()->isMinimized()) update();
}

void LiveActivityChart::setDetailed(bool detailed) { detailed_ = detailed; update(); }
void LiveActivityChart::copyHistoryFrom(const LiveActivityChart& source) { points_ = source.points_; update(); }
void LiveActivityChart::setAnimationsEnabled(bool enabled) { animationsEnabled_ = enabled; if (!enabled) { arrival_->stop(); arrivalPhase_ = 1; update(); } }
bool LiveActivityChart::animating() const { return arrival_->state() == QAbstractAnimation::Running; }
void LiveActivityChart::hideEvent(QHideEvent* event) { arrival_->stop(); arrivalPhase_ = 1; QWidget::hideEvent(event); }
void LiveActivityChart::leaveEvent(QEvent* event) { hovered_ = -1; update(); QWidget::leaveEvent(event); }
void LiveActivityChart::mouseMoveEvent(QMouseEvent* event) {
    if (points_.isEmpty()) return;
    const qreal fraction = std::clamp((event->position().x() - 44) / std::max(1, width() - 60), 0.0, 1.0);
    const qint64 target = points_.first().at.toMSecsSinceEpoch() + static_cast<qint64>(fraction * points_.first().at.msecsTo(points_.last().at));
    int closest = 0; qint64 distance = std::numeric_limits<qint64>::max();
    for (qsizetype i = 0; i < points_.size(); ++i) {
        const auto delta = std::abs(points_[i].at.toMSecsSinceEpoch() - target);
        if (delta < distance) { distance = delta; closest = static_cast<int>(i); }
    }
    if (hovered_ != closest) { hovered_ = closest; update(); }
}

void LiveActivityChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool light = property("lightTheme").toBool();
    const QColor muted(light ? QStringLiteral("#59677b") : QStringLiteral("#93a0b5"));
    const QColor grid(light ? QStringLiteral("#e1e7ef") : QStringLiteral("#263244"));
    const std::array<QColor, 3> colors{QColor(QStringLiteral("#9592ff")), QColor(QStringLiteral("#30d8cb")), QColor(QStringLiteral("#f3b976"))};
    const std::array<QString, 3> names{QStringLiteral("CPU"), QStringLiteral("Memory"), QStringLiteral("Graphics")};
    QFont small = font(); small.setPixelSize(11); p.setFont(small);
    const QRectF plot(44, 51, std::max(1, width() - 60), std::max(1, height() - 86));
    QLinearGradient backdrop(plot.topLeft(), plot.bottomLeft());
    backdrop.setColorAt(0, QColor(light ? QStringLiteral("#edf1f9") : QStringLiteral("#172239")));
    backdrop.setColorAt(1, QColor(light ? QStringLiteral("#f9fbff") : QStringLiteral("#111a2b")));
    p.setPen(Qt::NoPen); p.setBrush(backdrop); p.drawRoundedRect(plot.adjusted(-6, -5, 6, 4), 10, 10);
    int legendIndex = 0;
    for (int i = 0; i < 3; ++i) {
        if (series_ >= 0 && series_ != i) continue;
        const int column = std::max(94, (width() - 44) / (series_ < 0 ? 3 : 1));
        const int left = 44 + legendIndex * column;
        p.setPen(Qt::NoPen); p.setBrush(colors[i]); p.drawEllipse(QPointF(left + 4, 13), 3, 3);
        p.setPen(muted); p.drawText(QRect(left + 15, 3, column - 20, 18), Qt::AlignVCenter, names[i]);
        const auto value = points_.isEmpty() ? std::optional<double>{} : i == 0 ? points_.last().cpu : i == 1 ? points_.last().memory : points_.last().gpu;
        QFont valueFont = small; valueFont.setPixelSize(16); valueFont.setWeight(QFont::DemiBold); p.setFont(valueFont); p.setPen(colors[i]);
        p.drawText(QRect(left + 15, 21, column - 20, 25), Qt::AlignVCenter, value && std::isfinite(*value) && *value >= 0 && *value <= 100 ? QStringLiteral("%1%" ).arg(*value, 0, 'f', 1) : QStringLiteral("Unavailable"));
        p.setFont(small);
        ++legendIndex;
    }
    for (int i = 0; i <= 4; ++i) {
        const qreal y = plot.top() + i * plot.height() / 4;
        p.setPen(QPen(grid, 1, Qt::DotLine)); p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(muted); p.drawText(QRectF(0, y - 8, 30, 16), Qt::AlignRight, QStringLiteral("%1%").arg(100 - i * 25));
    }
    if (points_.size() < 2) {
        p.setPen(muted);
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("Your live chart appears after two readings"));
        return;
    }
    const qint64 span = std::max<qint64>(1, points_.first().at.msecsTo(points_.last().at));
    for (int tick = 1; tick < 4; ++tick) {
        const qreal x = plot.left() + tick * plot.width() / 4;
        p.setPen(QPen(grid, 1, Qt::DotLine)); p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int series = 0; series < 3; ++series) {
        if (series_ >= 0 && series_ != series) continue;
        QPainterPath path;
        QPainterPath area;
        bool connected = false;
        qint64 previousAt = 0;
        QPointF segmentFirst, segmentLast;
        const auto finishSegment = [&] {
            if (connected) { area.lineTo(segmentLast.x(), plot.bottom()); area.lineTo(segmentFirst.x(), plot.bottom()); area.closeSubpath(); }
        };
        for (const Point& point : points_) {
            const auto value = series == 0 ? point.cpu : (series == 1 ? point.memory : point.gpu);
            const qint64 at = point.at.toMSecsSinceEpoch();
            if (!value || !std::isfinite(*value) || *value < 0 || *value > 100) { finishSegment(); connected = false; continue; }
            const QPointF position(plot.left() + static_cast<qreal>(points_.first().at.msecsTo(point.at)) / span * plot.width(),
                plot.bottom() - std::clamp(*value, 0.0, 100.0) / 100 * plot.height());
            if (connected && at - previousAt <= 30'000) { path.lineTo(position); area.lineTo(position); }
            else { finishSegment(); path.moveTo(position); area.moveTo(position); segmentFirst = position; }
            segmentLast = position;
            connected = true;
            previousAt = at;
        }
        finishSegment();
        QLinearGradient fill(plot.topLeft(), plot.bottomLeft()); QColor top = colors[series]; top.setAlpha(light ? 38 : 50); QColor bottom = colors[series]; bottom.setAlpha(2);
        fill.setColorAt(0, top); fill.setColorAt(1, bottom); p.setPen(Qt::NoPen); p.setBrush(fill); p.drawPath(area);
        QColor glow = colors[series]; glow.setAlpha(light ? 20 : 30); p.setBrush(Qt::NoBrush); p.setPen(QPen(glow, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.drawPath(path);
        p.setPen(QPen(colors[series], series == 0 ? 2.5 : 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush); p.drawPath(path);
        if (connected) {
            QColor halo = colors[series]; halo.setAlpha(static_cast<int>(65 * (1 - arrivalPhase_)) + 15);
            p.setPen(Qt::NoPen); p.setBrush(halo); p.drawEllipse(segmentLast, 6 + 5 * arrivalPhase_, 6 + 5 * arrivalPhase_);
            p.setBrush(colors[series]); p.drawEllipse(segmentLast, 3.5, 3.5);
        }
    }
    p.setPen(muted);
    p.drawText(QRectF(plot.left(), plot.bottom() + 7, plot.width(), 18), Qt::AlignLeft,
        detailed_ ? points_.first().at.toLocalTime().toString(QStringLiteral("h:mm:ss ap")) : QStringLiteral("Earlier"));
    p.drawText(QRectF(plot.left(), plot.bottom() + 7, plot.width(), 18), Qt::AlignRight,
        QStringLiteral("%1 readings · %2").arg(points_.size()).arg(points_.last().at.toLocalTime().toString(QStringLiteral("h:mm:ss ap"))));
    if (hovered_ >= 0 && hovered_ < points_.size() && plot.width() > 40) {
        const auto& point = points_[hovered_];
        const qreal x = plot.left() + static_cast<qreal>(points_.first().at.msecsTo(point.at)) / span * plot.width();
        p.setPen(QPen(muted, 1, Qt::DashLine)); p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const auto format = [](const std::optional<double>& value) { return value && std::isfinite(*value) && *value >= 0 && *value <= 100 ? QStringLiteral("%1%" ).arg(*value, 0, 'f', 1) : QStringLiteral("—"); };
        const QString text = QStringLiteral("%1  ·  CPU %2  RAM %3  GPU %4").arg(point.at.toLocalTime().toString(QStringLiteral("h:mm:ss")), format(point.cpu), format(point.memory), format(point.gpu));
        QFont hoverFont = small; hoverFont.setPixelSize(10); p.setFont(hoverFont);
        const qreal boxWidth = std::min(plot.width() - 8, static_cast<qreal>(p.fontMetrics().horizontalAdvance(text) + 20));
        const QRectF box(std::clamp(x - boxWidth / 2, plot.left() + 4, plot.right() - boxWidth - 4), plot.top() + 8, boxWidth, 28);
        p.setPen(QPen(grid, 1)); p.setBrush(QColor(light ? QStringLiteral("#ffffff") : QStringLiteral("#202d44"))); p.drawRoundedRect(box, 7, 7);
        p.setPen(QColor(light ? QStringLiteral("#253247") : QStringLiteral("#e1e7f1"))); p.drawText(box.adjusted(8, 0, -8, 0), Qt::AlignCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(boxWidth - 16)));
    }
}

ResourceRing::ResourceRing(QWidget* parent) : QWidget(parent)
{
    setFixedSize(132, 132);
    setAccessibleName(QStringLiteral("Measured performance index"));
    animation_ = new QVariantAnimation(this);
    animation_->setDuration(260);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        displayed_ = value.toReal(); update();
    });
}

void ResourceRing::setAnimationsEnabled(bool enabled)
{
    animationsEnabled_ = enabled;
    if (!enabled) { animation_->stop(); displayed_ = value_.value_or(0); update(); }
}

void ResourceRing::setValue(std::optional<double> value, const QString& caption)
{
    if (value && !std::isfinite(*value)) value.reset();
    if (value) *value = std::clamp(*value, 0.0, 100.0);
    if (value == value_ && caption == caption_) return;
    value_ = value; caption_ = caption;
    setAccessibleDescription(value ? QStringLiteral("%1 out of 100, %2. A limited performance index, not a full PC health rating.").arg(*value, 0, 'f', 0).arg(caption) : caption);
    animation_->stop();
    if (animationsEnabled_ && isVisible() && value) {
        animation_->setStartValue(displayed_); animation_->setEndValue(*value); animation_->start();
    } else { displayed_ = value.value_or(0); update(); }
}

void ResourceRing::paintEvent(QPaintEvent*)
{
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    const bool light = property("lightTheme").toBool();
    const QRectF ring(9, 9, width() - 18, height() - 18);
    p.setPen(QPen(QColor(light ? QStringLiteral("#e1e7ef") : QStringLiteral("#263244")), 7));
    p.drawEllipse(ring);
    const QColor color(value_.value_or(100) < 50 ? QStringLiteral("#f0be73") : QStringLiteral("#9690ff"));
    p.setPen(QPen(color, 7, Qt::SolidLine, Qt::RoundCap));
    if (value_) p.drawArc(ring, 90 * 16, -static_cast<int>(displayed_ * 3.6 * 16));
    QFont large = font(); large.setPixelSize(34); large.setWeight(QFont::DemiBold); p.setFont(large);
    p.setPen(QColor(light ? QStringLiteral("#202a3b") : QStringLiteral("#f4f6fc")));
    p.drawText(QRect(10, 38, width() - 20, 43), Qt::AlignCenter,
        value_ ? QString::number(static_cast<int>(std::lround(displayed_))) : QStringLiteral("—"));
    QFont small = font(); small.setPixelSize(10); p.setFont(small);
    p.setPen(QColor(light ? QStringLiteral("#59677b") : QStringLiteral("#93a0b5")));
    p.drawText(QRect(10, 82, width() - 20, 22), Qt::AlignCenter, QStringLiteral("PERFORMANCE"));
}
} // namespace Ausyn
