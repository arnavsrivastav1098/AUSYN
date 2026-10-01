#include "prediction_page.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

namespace Ausyn {
namespace {
QFrame* makePanel(QWidget* parent) {
    auto* f = new QFrame(parent); f->setObjectName(QStringLiteral("panel")); return f;
}
void setStatusBadgeStyle(QLabel* badge, const QColor& foreground, const QColor& background, const QColor& border) {
    if (!badge) return;
    badge->setStyleSheet(QStringLiteral("color:%1;background:%2;border:1px solid %3;border-radius:8px;padding:6px 9px;font-size:10px;font-weight:700;letter-spacing:1px")
        .arg(foreground.name(), background.name(), border.name()));
}
class StorageChart final : public QWidget {
public:
    explicit StorageChart(QWidget* parent = nullptr, bool compact = false)
        : QWidget(parent), compact_(compact) {
        setMinimumHeight(compact_ ? 112 : 250);
        setMaximumHeight(compact_ ? 130 : QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, compact_ ? QSizePolicy::Fixed : QSizePolicy::Expanding);
        setAccessibleName(QStringLiteral("Daily average free space history"));
    }
    void setPoints(QVector<StorageTrendPoint> value) { points_ = std::move(value); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF plot = compact_ ? QRectF(rect().adjusted(42, 8, -10, -24))
                                     : QRectF(rect().adjusted(52, 14, -16, -34));
        const QColor muted(QStringLiteral("#8390a5")), grid(QStringLiteral("#253142")), line(QStringLiteral("#a9a8ff"));
        p.setFont(QFont(QStringLiteral("Segoe UI"), compact_ ? 8 : 9));
        const QList<int> ticks = compact_ ? QList<int>{0,50,100} : QList<int>{0,25,50,75,100};
        for (int tick : ticks) {
            const double y = plot.bottom() - tick / 100.0 * plot.height();
            p.setPen(QPen(grid, 1)); p.drawLine(QPointF(plot.left(),y), QPointF(plot.right(),y));
            p.setPen(muted); p.drawText(QRectF(0,y-8,34,16),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("%1%").arg(tick));
        }
        const double threshold = plot.bottom() - 0.1 * plot.height();
        p.setPen(QPen(QColor(QStringLiteral("#f0be73")),1.4,Qt::DashLine));
        p.drawLine(QPointF(plot.left(),threshold),QPointF(plot.right(),threshold));
        if (!compact_) {
            p.setPen(muted); p.drawText(QRectF(plot.right()-85,threshold-19,85,16),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("10% attention"));
        }
        if (points_.isEmpty()) {
            p.drawText(plot,Qt::AlignCenter|Qt::TextWordWrap, compact_
                ? QStringLiteral("Collecting daily drive history…")
                : QStringLiteral("Daily drive history appears after at least three days of local samples."));
            return;
        }
        const qint64 first=points_.first().capturedAt.toMSecsSinceEpoch();
        const qint64 last=points_.last().capturedAt.toMSecsSinceEpoch();
        const double span=static_cast<double>(std::max<qint64>(last-first,86'400'000));
        QPainterPath path;
        for (qsizetype i=0;i<points_.size();++i) {
            const auto& point=points_.at(i);
            const double x=plot.left()+static_cast<double>(point.capturedAt.toMSecsSinceEpoch()-first)/span*plot.width();
            const double y=plot.bottom()-std::clamp(point.freePercent,0.0,100.0)/100.0*plot.height();
            if (i==0) path.moveTo(x,y); else path.lineTo(x,y);
        }
        p.setPen(QPen(line,2.5,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin)); p.drawPath(path);
        for (const auto& point:points_) {
            const double x=plot.left()+static_cast<double>(point.capturedAt.toMSecsSinceEpoch()-first)/span*plot.width();
            const double y=plot.bottom()-std::clamp(point.freePercent,0.0,100.0)/100.0*plot.height();
            p.setPen(Qt::NoPen); p.setBrush(line); p.drawEllipse(QPointF(x,y),3,3);
        }
        p.setPen(muted);
        p.drawText(QRectF(plot.left(),plot.bottom()+3,plot.width()/2,18),Qt::AlignLeft|Qt::AlignVCenter,points_.first().capturedAt.toString(QStringLiteral("d MMM")));
        p.drawText(QRectF(plot.center().x(),plot.bottom()+3,plot.width()/2,18),Qt::AlignRight|Qt::AlignVCenter,points_.last().capturedAt.toString(QStringLiteral("d MMM")));
    }
private:
    QVector<StorageTrendPoint> points_;
    bool compact_ = false;
};

class MemoryChart final : public QWidget {
public:
    explicit MemoryChart(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(150);
        setMaximumHeight(175);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(QStringLiteral("Recent five-minute average memory use and conditional outlook"));
    }
    void setForecast(const MemoryPressureForecast& forecast) {
        forecast_ = forecast;
        if (forecast_.observations.isEmpty()) {
            setAccessibleDescription(QStringLiteral("No well-sampled memory history is available yet."));
        } else {
            QString description=QStringLiteral("Chart of %1 five-minute average memory readings; latest average %2 percent.")
                .arg(forecast_.observations.size()).arg(forecast_.observations.last().averagePercent,0,'f',1);
            description+=forecast_.hasEstimate
                ? QStringLiteral(" The dashed projection estimates reaching 90 percent in about %1 minutes.").arg(static_cast<int>(std::lround(forecast_.minutesUntil90Percent)))
                : QStringLiteral(" The dashed horizontal line marks the 90 percent attention threshold; no time estimate is available.");
            setAccessibleDescription(description);
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot(rect().adjusted(42, 10, -12, -28));
        const QColor muted(QStringLiteral("#8390a5"));
        const QColor grid(QStringLiteral("#253142"));
        const QColor line(QStringLiteral("#a9a8ff"));
        const QColor threshold(QStringLiteral("#f0be73"));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        for (const int tick : {0, 50, 90, 100}) {
            const double y = plot.bottom() - static_cast<double>(tick) / 100.0 * plot.height();
            painter.setPen(QPen(grid, 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(tick == 90 ? threshold : muted);
            painter.drawText(QRectF(0, y - 8, 34, 16), Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("%1%").arg(tick));
        }
        if (forecast_.observations.isEmpty()) {
            painter.setPen(muted);
            painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap,
                             QStringLiteral("Collecting well-sampled five-minute memory averages…"));
            return;
        }

        const auto& points = forecast_.observations;
        const qint64 firstMs = points.first().capturedAt.toMSecsSinceEpoch();
        const qint64 lastMs = points.last().capturedAt.toMSecsSinceEpoch();
        const double observedMinutes = std::max(1.0, static_cast<double>(lastMs - firstMs) / 60'000.0);
        const double projectedSpan = forecast_.hasEstimate
            ? observedMinutes + forecast_.minutesUntil90Percent : std::max(20.0, observedMinutes);
        const auto xForMinutes = [&plot, projectedSpan](double minutes) {
            return plot.left() + std::clamp(minutes / projectedSpan, 0.0, 1.0) * plot.width();
        };
        const auto yForPercent = [&plot](double value) {
            return plot.bottom() - std::clamp(value, 0.0, 100.0) / 100.0 * plot.height();
        };
        const double thresholdY = yForPercent(90.0);
        painter.setPen(QPen(threshold, 1.3, Qt::DashLine));
        painter.drawLine(QPointF(plot.left(), thresholdY), QPointF(plot.right(), thresholdY));
        painter.setPen(threshold);
        painter.drawText(QRectF(plot.right() - 116, thresholdY - 18, 116, 15), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("90% attention"));

        QPainterPath historyPath;
        for (qsizetype i = 0; i < points.size(); ++i) {
            const double minutes = static_cast<double>(points.at(i).capturedAt.toMSecsSinceEpoch() - firstMs) / 60'000.0;
            const QPointF point(xForMinutes(minutes), yForPercent(points.at(i).averagePercent));
            if (i == 0) historyPath.moveTo(point);
            else historyPath.lineTo(point);
        }
        painter.setPen(QPen(line, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(historyPath);
        painter.setBrush(line);
        painter.setPen(Qt::NoPen);
        for (const MemoryTrendPoint& point : points) {
            const double minutes = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - firstMs) / 60'000.0;
            painter.drawEllipse(QPointF(xForMinutes(minutes), yForPercent(point.averagePercent)), 3.0, 3.0);
        }
        if (forecast_.hasEstimate) {
            const double latestMinutes = observedMinutes;
            const QPointF latest(xForMinutes(latestMinutes), yForPercent(forecast_.currentPercent));
            const QPointF target(xForMinutes(latestMinutes + forecast_.minutesUntil90Percent), thresholdY);
            painter.setPen(QPen(threshold, 2.0, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(latest, target);
            painter.setBrush(threshold);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(target, 4.0, 4.0);
        }
        painter.setPen(muted);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 4, plot.width() / 2.0, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Older readings"));
        painter.drawText(QRectF(plot.center().x(), plot.bottom() + 4, plot.width() / 2.0, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         forecast_.hasEstimate ? QStringLiteral("Now · projected") : QStringLiteral("Latest reading"));
    }
private:
    MemoryPressureForecast forecast_;
};

class BatteryChart final : public QWidget {
public:
    explicit BatteryChart(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(138);
        setMaximumHeight(160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(QStringLiteral("Recent battery charge readings and conditional outlook"));
    }
    void setForecast(const BatteryForecast& forecast) {
        forecast_ = forecast;
        if (forecast_.observations.isEmpty()) {
            setAccessibleDescription(QStringLiteral("No recent battery charge history is available."));
        } else {
            QString description=QStringLiteral("Chart of %1 battery readings; latest charge %2 percent.")
                .arg(forecast_.observations.size()).arg(forecast_.observations.last().chargePercent,0,'f',0);
            description+=forecast_.hasEstimate
                ? QStringLiteral(" The dashed projection estimates reaching 15 percent in about %1 minutes.").arg(static_cast<int>(std::lround(forecast_.minutesUntil15Percent)))
                : QStringLiteral(" The dashed horizontal line marks the 15 percent low-charge level; no time estimate is available.");
            setAccessibleDescription(description);
        }
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot(rect().adjusted(42, 9, -12, -27));
        const QColor muted(QStringLiteral("#8390a5"));
        const QColor grid(QStringLiteral("#253142"));
        const QColor line(QStringLiteral("#79c9bd"));
        const QColor threshold(QStringLiteral("#f0be73"));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        for (const int tick : {0, 15, 50, 100}) {
            const double y = plot.bottom() - static_cast<double>(tick) / 100.0 * plot.height();
            painter.setPen(QPen(grid, 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(tick == 15 ? threshold : muted);
            painter.drawText(QRectF(0, y - 8, 34, 16), Qt::AlignRight | Qt::AlignVCenter,
                             QStringLiteral("%1%").arg(tick));
        }
        if (forecast_.observations.isEmpty()) {
            painter.setPen(muted);
            painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap,
                             QStringLiteral("Battery history appears while unplugged and sampling charge."));
            return;
        }
        const auto& points = forecast_.observations;
        const qint64 firstMs = points.first().capturedAt.toMSecsSinceEpoch();
        const qint64 lastMs = points.last().capturedAt.toMSecsSinceEpoch();
        const double observedMinutes = std::max(1.0, static_cast<double>(lastMs - firstMs) / 60'000.0);
        const double projectedSpan = forecast_.hasEstimate
            ? observedMinutes + forecast_.minutesUntil15Percent : std::max(20.0, observedMinutes);
        const auto xForMinutes = [&plot, projectedSpan](double minutes) {
            return plot.left() + std::clamp(minutes / projectedSpan, 0.0, 1.0) * plot.width();
        };
        const auto yForPercent = [&plot](double value) {
            return plot.bottom() - std::clamp(value, 0.0, 100.0) / 100.0 * plot.height();
        };
        const double thresholdY = yForPercent(15.0);
        painter.setPen(QPen(threshold, 1.3, Qt::DashLine));
        painter.drawLine(QPointF(plot.left(), thresholdY), QPointF(plot.right(), thresholdY));
        painter.setPen(threshold);
        painter.drawText(QRectF(plot.right() - 116, thresholdY - 18, 116, 15), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("15% low-charge level"));
        QPainterPath historyPath;
        for (qsizetype i = 0; i < points.size(); ++i) {
            const double minutes = static_cast<double>(points.at(i).capturedAt.toMSecsSinceEpoch() - firstMs) / 60'000.0;
            const QPointF point(xForMinutes(minutes), yForPercent(points.at(i).chargePercent));
            if (i == 0) historyPath.moveTo(point); else historyPath.lineTo(point);
        }
        painter.setPen(QPen(line, 2.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(historyPath);
        painter.setBrush(line); painter.setPen(Qt::NoPen);
        for (const BatteryTrendPoint& point : points) {
            const double minutes = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - firstMs) / 60'000.0;
            painter.drawEllipse(QPointF(xForMinutes(minutes), yForPercent(point.chargePercent)), 2.8, 2.8);
        }
        if (forecast_.hasEstimate) {
            const QPointF latest(xForMinutes(observedMinutes), yForPercent(points.last().chargePercent));
            const QPointF target(xForMinutes(observedMinutes + forecast_.minutesUntil15Percent), thresholdY);
            painter.setPen(QPen(threshold, 2.0, Qt::DashLine, Qt::RoundCap));
            painter.drawLine(latest, target);
            painter.setBrush(threshold); painter.setPen(Qt::NoPen); painter.drawEllipse(target, 4.0, 4.0);
        }
        painter.setPen(muted);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 3, plot.width() / 2.0, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, points.first().capturedAt.toLocalTime().toString(QStringLiteral("h:mm AP")));
        painter.drawText(QRectF(plot.center().x(), plot.bottom() + 3, plot.width() / 2.0, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         forecast_.hasEstimate
                             ? QStringLiteral("%1 · projected").arg(points.last().capturedAt.toLocalTime().toString(QStringLiteral("h:mm AP")))
                             : points.last().capturedAt.toLocalTime().toString(QStringLiteral("h:mm AP")));
    }
private:
    BatteryForecast forecast_;
};

QFrame* unavailableCard(const QString& title,const QString& message,QWidget* parent,
                        const QString& bodyObjectName = {}) {
    auto* card=makePanel(parent); auto* layout=new QVBoxLayout(card);
    layout->setContentsMargins(18,16,18,16);
    auto* heading=new QLabel(title,card); heading->setObjectName(QStringLiteral("panelTitle"));
    auto* badge=new QLabel(QStringLiteral("NOT MEASURED"),card);
    badge->setStyleSheet(QStringLiteral("color:#e6b76f;font-size:10px;font-weight:700;letter-spacing:1px"));
    auto* body=new QLabel(message,card); body->setObjectName(bodyObjectName.isEmpty() ? QStringLiteral("subtle") : bodyObjectName); body->setWordWrap(true);
    layout->addWidget(heading); layout->addWidget(badge); layout->addWidget(body); return card;
}
}
PredictionPage::PredictionPage(QWidget* parent):QWidget(parent) {
    auto* outer=new QVBoxLayout(this); outer->setContentsMargins(30,24,30,24); outer->setSpacing(14);
    auto* eyebrow=new QLabel(QStringLiteral("PREDICTIONS · HISTORICAL EVIDENCE"),this); eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* title=new QLabel(QStringLiteral("What may happen next"),this); title->setObjectName(QStringLiteral("heroTitle"));
    auto* intro=new QLabel(QStringLiteral("Trend estimates use daily local history. Ausyn shows no forecast when evidence is too short, stale, or irregular."),this);
    intro->setObjectName(QStringLiteral("heroBody")); intro->setWordWrap(true);
    outer->addWidget(eyebrow); outer->addWidget(title); outer->addWidget(intro);
    auto* scroll=new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content=new QWidget(scroll); auto* contentLayout=new QVBoxLayout(content);
    contentLayout->setContentsMargins(0,0,8,0); contentLayout->setSpacing(14);
    auto* forecast=makePanel(content); auto* forecastLayout=new QVBoxLayout(forecast);
    forecastLayout->setContentsMargins(20,17,20,17);
    auto* header=new QHBoxLayout;
    auto* h=new QLabel(QStringLiteral("System drive outlook"),forecast); h->setObjectName(QStringLiteral("panelTitle"));
    auto* confidence=new QLabel(QStringLiteral("COLLECTING HISTORY"),forecast); confidence->setObjectName(QStringLiteral("forecastConfidence"));
    confidence->setStyleSheet(QStringLiteral("color:#a9a8ff;background:#20253a;border:1px solid #363957;border-radius:8px;padding:6px 9px;font-size:10px;font-weight:700;letter-spacing:1px"));
    header->addWidget(h); header->addStretch(); header->addWidget(confidence);
    auto* summary=new QLabel(QStringLiteral("Collecting daily drive-space history…"),forecast);
    summary->setObjectName(QStringLiteral("forecastSummary")); summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("color:#e8edf6;font-size:15px;font-weight:600"));
    auto* chart=new StorageChart(forecast); chart->setObjectName(QStringLiteral("storageForecastChart"));
    forecastLayout->addLayout(header); forecastLayout->addWidget(summary); forecastLayout->addWidget(chart,1);
    contentLayout->addWidget(forecast,2);
    auto* volumes = makePanel(content); volumes->setObjectName(QStringLiteral("volumeForecastContainer"));
    auto* volumeLayout = new QVBoxLayout(volumes);
    volumeLayout->setContentsMargins(20,17,20,17); volumeLayout->setSpacing(10);
    auto* volumeTitle = new QLabel(QStringLiteral("Other fixed drives"),volumes);
    volumeTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* volumeGrid = new QGridLayout;
    volumeGrid->setObjectName(QStringLiteral("volumeForecastGrid"));
    volumeGrid->setHorizontalSpacing(12); volumeGrid->setVerticalSpacing(12);
    volumeLayout->addWidget(volumeTitle); volumeLayout->addLayout(volumeGrid);
    contentLayout->addWidget(volumes);
    auto* cards=new QGridLayout; cards->setHorizontalSpacing(12); cards->setVerticalSpacing(12);
    auto* memoryCard=makePanel(content);
    auto* memoryLayout=new QVBoxLayout(memoryCard);
    memoryLayout->setContentsMargins(18,16,18,14); memoryLayout->setSpacing(8);
    auto* memoryHeader=new QHBoxLayout;
    auto* memoryTitle=new QLabel(QStringLiteral("Memory pressure outlook"),memoryCard);
    memoryTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* memoryBadge=new QLabel(QStringLiteral("COLLECTING HISTORY"),memoryCard);
    memoryBadge->setObjectName(QStringLiteral("memoryForecastBadge"));
    memoryBadge->setStyleSheet(QStringLiteral("color:#a9a8ff;background:#20253a;border:1px solid #363957;border-radius:8px;padding:6px 9px;font-size:10px;font-weight:700;letter-spacing:1px"));
    memoryHeader->addWidget(memoryTitle); memoryHeader->addStretch(); memoryHeader->addWidget(memoryBadge);
    auto* memorySummary=new QLabel(QStringLiteral("Ausyn checks recent five-minute averages for a consistent rise. It does not diagnose a memory leak."),memoryCard);
    memorySummary->setObjectName(QStringLiteral("memoryForecastSummary")); memorySummary->setWordWrap(true);
    auto* memoryChart=new MemoryChart(memoryCard); memoryChart->setObjectName(QStringLiteral("memoryForecastChart"));
    memoryLayout->addLayout(memoryHeader); memoryLayout->addWidget(memorySummary); memoryLayout->addWidget(memoryChart);
    cards->addWidget(memoryCard,0,0);
    auto* batteryCard=makePanel(content);
    auto* batteryLayout=new QVBoxLayout(batteryCard);
    batteryLayout->setContentsMargins(18,16,18,14); batteryLayout->setSpacing(8);
    auto* batteryHeader=new QHBoxLayout;
    auto* batteryTitle=new QLabel(QStringLiteral("Battery discharge outlook"),batteryCard);
    batteryTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* batteryBadge=new QLabel(QStringLiteral("COLLECTING HISTORY"),batteryCard);
    batteryBadge->setObjectName(QStringLiteral("batteryForecastBadge"));
    batteryBadge->setStyleSheet(QStringLiteral("color:#a9a8ff;background:#20253a;border:1px solid #363957;border-radius:8px;padding:6px 9px;font-size:10px;font-weight:700;letter-spacing:1px"));
    batteryHeader->addWidget(batteryTitle); batteryHeader->addStretch(); batteryHeader->addWidget(batteryBadge);
    auto* batterySummary=new QLabel(QStringLiteral("While running on battery, Ausyn uses recent local charge history to estimate when charge could reach 15%. It does not predict battery wear or lifespan."),batteryCard);
    batterySummary->setObjectName(QStringLiteral("batteryForecastSummary")); batterySummary->setWordWrap(true);
    auto* batteryChart=new BatteryChart(batteryCard); batteryChart->setObjectName(QStringLiteral("batteryForecastChart"));
    batteryLayout->addLayout(batteryHeader); batteryLayout->addWidget(batterySummary); batteryLayout->addWidget(batteryChart); batteryLayout->addStretch(1);
    cards->addWidget(batteryCard,0,1);
    cards->addWidget(unavailableCard(QStringLiteral("Thermal risk outlook"),
        QStringLiteral("CPU and GPU temperature history is unavailable through the standard interfaces used here. Ausyn won’t infer temperature from processor load."),content),1,0);
    contentLayout->addLayout(cards);
    auto* note=new QLabel(QStringLiteral("Forecasts are estimates, not promises. Storage estimates project the observed daily free-space trend to the existing 10% attention threshold. Memory outlooks project only a consistent recent five-minute rise and do not diagnose a leak."),content);
    note->setObjectName(QStringLiteral("subtle")); note->setWordWrap(true); contentLayout->addWidget(note);
    contentLayout->addStretch(); scroll->setWidget(content); outer->addWidget(scroll,1);
}
void PredictionPage::setForecast(const StorageForecast& forecast,
                                 const BatteryForecast& batteryForecast,
                                 const QVector<VolumeStorageForecast>& volumeForecasts,
                                 const MemoryPressureForecast& memoryForecast,
                                 int retentionDays) {
    if (auto* chart=findChild<QWidget*>(QStringLiteral("storageForecastChart")))
        static_cast<StorageChart*>(chart)->setPoints(forecast.observations);
    if (auto* badge=findChild<QLabel*>(QStringLiteral("forecastConfidence"))) {
        if (forecast.rapidDropDetected)
            badge->setText(QStringLiteral("RECENT SHARP DROP"));
        else if (forecast.state == ForecastState::InsufficientData || forecast.state == ForecastState::Irregular)
            badge->setText(QStringLiteral("INSUFFICIENT DATA"));
        else if (forecast.state == ForecastState::ThresholdReached)
            badge->setText(QStringLiteral("CURRENT ATTENTION"));
        else
            badge->setText(forecast.confidence.toUpper() + QStringLiteral(" CONFIDENCE"));
    }
    if (auto* label=findChild<QLabel*>(QStringLiteral("forecastSummary"))) {
        QString text=forecast.explanation;
        if (forecast.observedDays>0) text+=QStringLiteral("  Observed across %1 day(s); up to %2 days are retained locally.").arg(forecast.observedDays).arg(retentionDays);
        if (forecast.recentDaysConsidered>0)
            text+=QStringLiteral("  Recent direction: %1 of the latest %2 daily changes lost at least 0.1 percentage point.")
                .arg(forecast.recentDecliningDays).arg(forecast.recentDaysConsidered);
        if (forecast.state==ForecastState::Declining)
            text+=QStringLiteral("  Current free space: %1% · robust trend: %2 percentage points/day · fit: %3%.")
                .arg(forecast.currentFreePercent,0,'f',1).arg(forecast.dailyChangePercentagePoints,0,'f',2).arg(forecast.rSquared*100.0,0,'f',0);
        label->setText(text);
    }
    if (auto* batteryLabel=findChild<QLabel*>(QStringLiteral("batteryForecastSummary"))) {
        QString text=batteryForecast.explanation;
        if (batteryForecast.observationCount>0)
            text+=QStringLiteral("  Evidence: %1 recent charge readings.").arg(batteryForecast.observationCount);
        if (batteryForecast.fitAvailable)
            text+=QStringLiteral("  Trend fit: %1%.").arg(batteryForecast.fitQuality*100.0,0,'f',0);
        if (batteryForecast.observedMinutes>0)
            text+=QStringLiteral("  History spans about %1 minutes.").arg(batteryForecast.observedMinutes);
        if (batteryForecast.hasEstimate)
            text+=QStringLiteral("  Recent discharge: %1 percentage points/hour; estimated time to 15%: about %2 minutes.")
                .arg(batteryForecast.dischargePercentPerHour,0,'f',1)
                .arg(static_cast<int>(std::lround(batteryForecast.minutesUntil15Percent)));
        batteryLabel->setText(text);
    }
    if (auto* badge=findChild<QLabel*>(QStringLiteral("batteryForecastBadge"))) {
        QString state=QStringLiteral("COLLECTING HISTORY");
        QColor foreground(QStringLiteral("#a9a8ff")), background(QStringLiteral("#20253a")), border(QStringLiteral("#363957"));
        switch (batteryForecast.state) {
        case BatteryForecastState::NotApplicable: state=QStringLiteral("ON AC OR NO BATTERY"); foreground=QColor(QStringLiteral("#aeb8c8")); background=QColor(QStringLiteral("#222b37")); border=QColor(QStringLiteral("#394657")); break;
        case BatteryForecastState::InsufficientData: state=QStringLiteral("COLLECTING HISTORY"); break;
        case BatteryForecastState::Stable: state=QStringLiteral("NO SUSTAINED DROP"); foreground=QColor(QStringLiteral("#79c9a5")); background=QColor(QStringLiteral("#1d332e")); border=QColor(QStringLiteral("#31584b")); break;
        case BatteryForecastState::Irregular: state=QStringLiteral("IRREGULAR TREND"); foreground=QColor(QStringLiteral("#e6bd7a")); background=QColor(QStringLiteral("#342d22")); border=QColor(QStringLiteral("#5c4b30")); break;
        case BatteryForecastState::ThresholdReached: state=QStringLiteral("LOW CHARGE NOW"); foreground=QColor(QStringLiteral("#ff9f9f")); background=QColor(QStringLiteral("#3a2529")); border=QColor(QStringLiteral("#704047")); break;
        case BatteryForecastState::EstimateAvailable: state=QStringLiteral("OUTLOOK AVAILABLE"); foreground=QColor(QStringLiteral("#e6bd7a")); background=QColor(QStringLiteral("#342d22")); border=QColor(QStringLiteral("#5c4b30")); break;
        }
        badge->setText(state);
        setStatusBadgeStyle(badge,foreground,background,border);
    }
    if (auto* chartWidget=findChild<QWidget*>(QStringLiteral("batteryForecastChart")))
        static_cast<BatteryChart*>(chartWidget)->setForecast(batteryForecast);
    if (auto* memoryLabel=findChild<QLabel*>(QStringLiteral("memoryForecastSummary"))) {
        QString text=memoryForecast.explanation;
        if (memoryForecast.observedMinutes>0)
            text+=QStringLiteral("  Observed across %1 minutes.").arg(memoryForecast.observedMinutes);
        if (memoryForecast.fitAvailable)
            text+=QStringLiteral("  Trend fit: %1%.").arg(memoryForecast.fitQuality*100.0,0,'f',0);
        if (memoryForecast.recentWindowsConsidered > 0)
            text+=QStringLiteral("  Recent direction: %1 of the latest %2 windows rose by at least 0.1 percentage point.")
                .arg(memoryForecast.recentRisingWindows).arg(memoryForecast.recentWindowsConsidered);
        if (memoryForecast.hasEstimate)
            text+=QStringLiteral("  Robust trend: +%1 percentage points/hour; estimated time to 90%: about %2 minutes.")
                .arg(memoryForecast.risePerHour,0,'f',1)
                .arg(static_cast<int>(std::lround(memoryForecast.minutesUntil90Percent)));
        memoryLabel->setText(text);
    }
    if (auto* chartWidget=findChild<QWidget*>(QStringLiteral("memoryForecastChart")))
        static_cast<MemoryChart*>(chartWidget)->setForecast(memoryForecast);
    if (auto* badge=findChild<QLabel*>(QStringLiteral("memoryForecastBadge"))) {
        QString state=QStringLiteral("COLLECTING HISTORY");
        QColor foreground(QStringLiteral("#a9a8ff")), background(QStringLiteral("#20253a")), border(QStringLiteral("#363957"));
        if (memoryForecast.hasEstimate) state=QStringLiteral("OUTLOOK AVAILABLE");
        if (memoryForecast.hasEstimate) { foreground=QColor(QStringLiteral("#e6bd7a")); background=QColor(QStringLiteral("#342d22")); border=QColor(QStringLiteral("#5c4b30")); }
        else if (memoryForecast.state==MemoryForecastState::ThresholdReached) { state=QStringLiteral("CURRENT PRESSURE"); foreground=QColor(QStringLiteral("#ff9f9f")); background=QColor(QStringLiteral("#3a2529")); border=QColor(QStringLiteral("#704047")); }
        else if (memoryForecast.state==MemoryForecastState::Stable) { state=QStringLiteral("BROADLY STABLE"); foreground=QColor(QStringLiteral("#79c9a5")); background=QColor(QStringLiteral("#1d332e")); border=QColor(QStringLiteral("#31584b")); }
        else if (memoryForecast.state==MemoryForecastState::Declining) { state=QStringLiteral("DECLINING"); foreground=QColor(QStringLiteral("#79c9a5")); background=QColor(QStringLiteral("#1d332e")); border=QColor(QStringLiteral("#31584b")); }
        else if (memoryForecast.state==MemoryForecastState::Irregular) { state=QStringLiteral("IRREGULAR TREND"); foreground=QColor(QStringLiteral("#e6bd7a")); background=QColor(QStringLiteral("#342d22")); border=QColor(QStringLiteral("#5c4b30")); }
        else if (memoryForecast.state==MemoryForecastState::Rising) state=QStringLiteral("RISING · NO ETA");
        badge->setText(state);
        setStatusBadgeStyle(badge,foreground,background,border);
    }
    if (auto* grid=findChild<QGridLayout*>(QStringLiteral("volumeForecastGrid"))) {
        while (QLayoutItem* item=grid->takeAt(0)) {
            if (QWidget* widget=item->widget()) widget->deleteLater();
            delete item;
        }
        if (volumeForecasts.isEmpty()) {
            auto* empty=new QLabel(QStringLiteral("No secondary fixed drives are currently available."),this);
            empty->setObjectName(QStringLiteral("subtle")); empty->setWordWrap(true);
            grid->addWidget(empty,0,0,1,2);
        }
        for (qsizetype i=0;i<volumeForecasts.size();++i) {
            const VolumeStorageForecast& volume=volumeForecasts.at(i);
            auto* card=makePanel(this);
            auto* layout=new QVBoxLayout(card); layout->setContentsMargins(15,13,15,13); layout->setSpacing(7);
            const QString driveTitle=volume.label.isEmpty() ? volume.rootPath
                : QStringLiteral("%1 · %2").arg(volume.label,volume.rootPath);
            auto* heading=new QLabel(driveTitle,card); heading->setObjectName(QStringLiteral("panelTitle"));
            auto* details=new QLabel(card); details->setObjectName(QStringLiteral("subtle")); details->setWordWrap(true);
            QString detail=volume.forecast.explanation;
            if (volume.forecast.observedDays>0)
                detail+=QStringLiteral("  ·  %1 days of history").arg(volume.forecast.observedDays);
            if (volume.forecast.currentFreePercent>0.0)
                detail+=QStringLiteral("  ·  %1% free now").arg(volume.forecast.currentFreePercent,0,'f',1);
            if (volume.forecast.hasEstimate)
                detail+=QStringLiteral("  ·  roughly %1 days to 10% at the observed rate")
                    .arg(volume.forecast.daysUntilTenPercent,0,'f',0);
            else if (volume.forecast.rapidDropDetected)
                detail+=QStringLiteral("  ·  recent sharp drop: %1 percentage points")
                    .arg(volume.forecast.rapidDropPercentagePoints,0,'f',1);
            details->setText(detail);
            auto* chart=new StorageChart(card,true);
            chart->setPoints(volume.forecast.observations);
            layout->addWidget(heading); layout->addWidget(details); layout->addWidget(chart);
            grid->addWidget(card,static_cast<int>(i/2),static_cast<int>(i%2));
        }
    }
}
} // namespace Ausyn
