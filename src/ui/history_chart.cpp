#include "history_chart.h"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <utility>

namespace Ausyn {

HistoryChart::HistoryChart(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(280);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Selected-period processor and memory history chart"));
}

void HistoryChart::setHistory(QVector<HistoryPoint> points)
{
    points_ = std::move(points);
    update();
}

QSize HistoryChart::sizeHint() const
{
    return {760, 340};
}

void HistoryChart::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot(rect().adjusted(52, 38, -20, -35));
    if (plot.width() <= 1.0 || plot.height() <= 1.0) {
        return;
    }

    const QColor muted(QStringLiteral("#8390a5"));
    const QColor grid(QStringLiteral("#253142"));
    const QColor cpuColor(QStringLiteral("#a9a8ff"));
    const QColor memoryColor(QStringLiteral("#57d6bd"));

    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter.setPen(QPen(cpuColor, 2.5));
    painter.drawLine(QPointF(plot.left(), 17), QPointF(plot.left() + 18, 17));
    painter.setPen(muted);
    painter.drawText(QRectF(plot.left() + 24, 7, 120, 20), Qt::AlignVCenter, QStringLiteral("Processor"));
    painter.setPen(QPen(memoryColor, 2.5));
    painter.drawLine(QPointF(plot.left() + 118, 17), QPointF(plot.left() + 136, 17));
    painter.setPen(muted);
    painter.drawText(QRectF(plot.left() + 142, 7, 110, 20), Qt::AlignVCenter, QStringLiteral("Memory"));

    for (int tick = 0; tick <= 4; ++tick) {
        const double fraction = static_cast<double>(tick) / 4.0;
        const double y = plot.bottom() - fraction * plot.height();
        painter.setPen(QPen(grid, 1));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(muted);
        painter.drawText(QRectF(3, y - 9, 41, 18), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1%").arg(tick * 25));
    }
    painter.setPen(QPen(grid, 1));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.topLeft(), plot.bottomLeft());

    if (points_.isEmpty()) {
        painter.setPen(muted);
        painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("Your chart will fill in as Ausyn records real readings.\n"
                           "Samples are kept on this device for up to 30 days."));
        return;
    }

    qint64 firstMs = points_.first().capturedAt.toMSecsSinceEpoch();
    qint64 lastMs = points_.last().capturedAt.toMSecsSinceEpoch();
    if (lastMs <= firstMs) {
        firstMs -= 60'000;
        lastMs = points_.last().capturedAt.toMSecsSinceEpoch();
    }
    const double timeSpan = static_cast<double>(std::max<qint64>(lastMs - firstMs, 1));
    const auto xFor = [&plot, firstMs, timeSpan](const HistoryPoint& point) {
        const double elapsed = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - firstMs);
        return plot.left() + std::clamp(elapsed / timeSpan, 0.0, 1.0) * plot.width();
    };
    const auto drawSeries = [this, &painter, &plot, &xFor](const auto& valueForPoint, const QColor& color) {
        QPainterPath path;
        bool hasPoint = false;
        for (const HistoryPoint& point : std::as_const(points_)) {
            const auto value = valueForPoint(point);
            if (!value) {
                hasPoint = false;
                continue;
            }
            const double bounded = std::clamp(*value, 0.0, 100.0);
            const QPointF position(xFor(point), plot.bottom() - bounded / 100.0 * plot.height());
            if (!hasPoint) {
                path.moveTo(position);
                hasPoint = true;
            } else {
                path.lineTo(position);
            }
        }
        painter.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    };

    drawSeries([](const HistoryPoint& point) { return point.processorPercent; }, cpuColor);
    drawSeries([](const HistoryPoint& point) { return point.memoryPercent; }, memoryColor);

    painter.setPen(muted);
    const QString endpointFormat = timeSpan > 24.0 * 60.0 * 60.0 * 1000.0
        ? QStringLiteral("d MMM h:mm ap") : QStringLiteral("h:mm ap");
    painter.drawText(QRectF(plot.left(), plot.bottom() + 7, plot.width() / 2.0, 20),
        Qt::AlignLeft | Qt::AlignVCenter, points_.first().capturedAt.toString(endpointFormat));
    painter.drawText(QRectF(plot.center().x(), plot.bottom() + 7, plot.width() / 2.0, 20),
        Qt::AlignRight | Qt::AlignVCenter, points_.last().capturedAt.toString(endpointFormat));
}

} // namespace Ausyn
