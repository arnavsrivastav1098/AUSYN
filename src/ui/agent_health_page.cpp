#include "agent_health_page.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Psapi.h>

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QString qualityState(MetricQualityState state)
{
    switch (state) {
    case MetricQualityState::Valid: return QStringLiteral("Available");
    case MetricQualityState::Estimated: return QStringLiteral("Estimated");
    case MetricQualityState::WarmingUp: return QStringLiteral("Warming up");
    case MetricQualityState::Unavailable: return QStringLiteral("Unavailable");
    case MetricQualityState::Stale: return QStringLiteral("Stale");
    case MetricQualityState::Invalid: return QStringLiteral("Invalid");
    case MetricQualityState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString duration(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
    return QStringLiteral("%1d %2h %3m")
        .arg(seconds / 86400).arg((seconds / 3600) % 24).arg((seconds / 60) % 60);
}

class ResourceUsageChart final : public QWidget
{
public:
    explicit ResourceUsageChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(112);
        setMaximumHeight(126);
        setAccessibleName(QStringLiteral("Ausyn CPU and working-set memory trends"));
    }

    void setSamples(const QVector<double>& cpuSamples, const QVector<double>& memorySamples)
    {
        cpuSamples_ = cpuSamples;
        memorySamples_ = memorySamples;
        const double latestCpu = cpuSamples_.isEmpty() ? -1.0 : cpuSamples_.last();
        const double latestMemory = memorySamples_.isEmpty() ? -1.0 : memorySamples_.last();
        const QString cpuText = latestCpu >= 0.0
            ? QStringLiteral("%1 percent of total system capacity").arg(latestCpu, 0, 'f', 2)
            : QStringLiteral("unavailable");
        const QString memoryText = latestMemory >= 0.0
            ? QStringLiteral("%1 megabytes").arg(latestMemory, 0, 'f', 1)
            : QStringLiteral("unavailable");
        setAccessibleDescription(cpuSamples_.isEmpty() && memorySamples_.isEmpty()
            ? QStringLiteral("Collecting Ausyn process-resource samples. Measurements remain in memory only.")
            : QStringLiteral("Up to 60 recent Ausyn process samples. Latest CPU use is %1 and working set is %2. The two metrics use separate scales. Samples remain in memory only.")
                .arg(cpuText, memoryText));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area(rect().adjusted(0, 3, -8, -17));
        const QColor muted(QStringLiteral("#8390a5"));
        const QColor grid(QStringLiteral("#253142"));
        const QColor cpuLine(QStringLiteral("#79c9bd"));
        const QColor memoryLine(QStringLiteral("#a9a8ff"));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        const double paneGap = 6.0;
        const double paneHeight = (area.height() - paneGap) / 2.0;
        const auto drawSeries = [&painter, &muted, &grid](const QRectF& pane, const QVector<double>& values,
                                                         const QString& label, const QString& unit,
                                                         const QColor& color, double cap) {
            const QRectF plot(pane.adjusted(48, 12, 0, 0));
            painter.setPen(color);
            painter.drawText(QRectF(pane.left(), pane.top(), 45, 12), Qt::AlignLeft | Qt::AlignVCenter, label);
            double maximum = 1.0;
            for (double value : values) if (value >= 0.0) maximum = std::max(maximum, value);
            maximum = std::min(cap, std::max(1.0, std::ceil(maximum * 1.2)));
            for (int tick = 0; tick <= 1; ++tick) {
                const double y = plot.bottom() - static_cast<double>(tick) * plot.height();
                painter.setPen(QPen(grid, 1)); painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
                painter.setPen(muted);
                painter.drawText(QRectF(0, y - 7, 42, 14), Qt::AlignRight | Qt::AlignVCenter,
                    QStringLiteral("%1 %2").arg(maximum * static_cast<double>(tick), 0, 'f', 0).arg(unit));
            }
            if (values.isEmpty()) return;
            QPainterPath path;
            bool connected = false;
            for (qsizetype i = 0; i < values.size(); ++i) {
                const double value = values.at(i);
                if (value < 0.0) { connected = false; continue; }
                const double x = values.size() == 1 ? plot.center().x()
                    : plot.left() + static_cast<double>(i) / static_cast<double>(values.size() - 1) * plot.width();
                const double y = plot.bottom() - std::clamp(value, 0.0, maximum) / maximum * plot.height();
                if (!connected) path.moveTo(x, y); else path.lineTo(x, y);
                connected = true;
            }
            painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); painter.drawPath(path);
            for (qsizetype i = 0; i < values.size(); ++i) {
                if (values.at(i) < 0.0) continue;
                const double x = values.size() == 1 ? plot.center().x()
                    : plot.left() + static_cast<double>(i) / static_cast<double>(values.size() - 1) * plot.width();
                const double y = plot.bottom() - std::clamp(values.at(i), 0.0, maximum) / maximum * plot.height();
                painter.setPen(Qt::NoPen); painter.setBrush(color); painter.drawEllipse(QPointF(x, y), 1.8, 1.8);
            }
        };
        const QRectF cpuPane(area.left(), area.top(), area.width(), paneHeight);
        const QRectF memoryPane(area.left(), area.top() + paneHeight + paneGap, area.width(), paneHeight);
        drawSeries(cpuPane, cpuSamples_, QStringLiteral("CPU"), QStringLiteral("%"), cpuLine, 100.0);
        drawSeries(memoryPane, memorySamples_, QStringLiteral("Working set"), QStringLiteral("MB"), memoryLine, 100'000.0);
        painter.setPen(muted);
        painter.drawText(QRectF(area.left() + 48, area.bottom() + 1, area.width() / 2.0, 15),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Older"));
        painter.drawText(QRectF(area.center().x(), area.bottom() + 1, area.width() / 2.0, 15),
                         Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Latest · %1 samples").arg(std::max(cpuSamples_.size(), memorySamples_.size())));
    }

private:
    QVector<double> cpuSamples_;
    QVector<double> memorySamples_;
};

quint64 fileTimeTicks(const FILETIME& value)
{
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}

} // namespace

AgentHealthPage::AgentHealthPage(QWidget* parent)
    : QWidget(parent)
{
    uptimeClock_.start();
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(15);

    auto* eyebrow = new QLabel(QStringLiteral("ADVANCED · LOCAL MONITORING"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Agent health"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("A live view of Ausyn’s monitoring loop, sample timing, and the Windows data sources it can currently read."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* statusPanel = panel(this);
    auto* statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(16, 14, 16, 14);
    statusLayout->setSpacing(8);
    status_ = new QLabel(QStringLiteral("Starting local monitor…"), statusPanel);
    status_->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:#87d7b0"));
    freshness_ = new QLabel(QStringLiteral("Waiting for first sample"), statusPanel);
    freshness_->setObjectName(QStringLiteral("subtle"));
    uptime_ = new QLabel(QStringLiteral("Session uptime · 0m"), statusPanel);
    uptime_->setObjectName(QStringLiteral("subtle"));
    cadence_ = new QLabel(QStringLiteral("Sampling cadence · target every 5 seconds"), statusPanel);
    cadence_->setObjectName(QStringLiteral("subtle"));
    collectionLatency_ = new QLabel(QStringLiteral("System collection latency · waiting for sample"), statusPanel);
    collectionLatency_->setObjectName(QStringLiteral("subtle"));
    ausynResourceUse_ = new QLabel(QStringLiteral("Ausyn process use · waiting for sample"), statusPanel);
    ausynResourceUse_->setObjectName(QStringLiteral("subtle"));
    statusLayout->addWidget(status_);
    statusLayout->addWidget(freshness_);
    statusLayout->addWidget(uptime_);
    statusLayout->addWidget(cadence_);
    statusLayout->addWidget(collectionLatency_);
    statusLayout->addWidget(ausynResourceUse_);
    auto* cpuTrendTitle=new QLabel(QStringLiteral("Ausyn resource use · recent samples · separate scales"),statusPanel);
    cpuTrendTitle->setObjectName(QStringLiteral("subtle"));
    ausynCpuTrend_=new ResourceUsageChart(statusPanel);
    statusLayout->addWidget(cpuTrendTitle);
    statusLayout->addWidget(ausynCpuTrend_);
    outer->addWidget(statusPanel);

    auto* collectorPanel = panel(this);
    auto* collectorLayout = new QVBoxLayout(collectorPanel);
    collectorLayout->setContentsMargins(15, 15, 15, 15);
    collectorLayout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Collector availability"), collectorPanel);
    title->setObjectName(QStringLiteral("sectionTitle"));
    collectors_ = new QTableWidget(collectorPanel);
    collectors_->setObjectName(QStringLiteral("processTable"));
    collectors_->setColumnCount(4);
    collectors_->setHorizontalHeaderLabels({QStringLiteral("Reading"), QStringLiteral("State"),
        QStringLiteral("Source"), QStringLiteral("Note")});
    collectors_->setAlternatingRowColors(true);
    collectors_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    collectors_->setSelectionMode(QAbstractItemView::NoSelection);
    collectors_->verticalHeader()->hide();
    collectors_->setShowGrid(false);
    collectors_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    collectors_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    collectors_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    collectors_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    collectorLayout->addWidget(title);
    collectorLayout->addWidget(collectors_, 1);
    outer->addWidget(collectorPanel, 1);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(1000);
    connect(refreshTimer_, &QTimer::timeout, this, &AgentHealthPage::refreshFreshness);
    refreshTimer_->start();
}

void AgentHealthPage::setSnapshot(const SystemSnapshot& snapshot)
{
    const qint64 now = uptimeClock_.elapsed();
    FILETIME created{}, exited{}, kernel{}, user{};
    QString cpuText = QStringLiteral("CPU warming up");
    double currentCpuPercent = -1.0;
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        const quint64 totalTicks = fileTimeTicks(kernel) + fileTimeTicks(user);
        if (previousAusynSampleMs_ >= 0 && now > previousAusynSampleMs_ && totalTicks >= previousAusynCpuTicks_) {
            const qint64 elapsedMs = now - previousAusynSampleMs_;
            const quint64 processorCount = snapshot.logicalProcessorCount > 0
                ? static_cast<quint64>(snapshot.logicalProcessorCount) : 1ULL;
            const double systemCapacityPercent =
                100.0 * static_cast<double>(totalTicks - previousAusynCpuTicks_) /
                (static_cast<double>(elapsedMs) * 10'000.0 * static_cast<double>(processorCount));
            currentCpuPercent = systemCapacityPercent;
            cpuText = QStringLiteral("%1% of total system CPU capacity").arg(systemCapacityPercent, 0, 'f', 2);
        }
        previousAusynCpuTicks_ = totalTicks;
        previousAusynSampleMs_ = now;
    } else {
        cpuText = QStringLiteral("CPU unavailable");
    }
    if (currentCpuPercent >= 0.0) {
        recentAusynCpuPercent_.append(currentCpuPercent);
        if (recentAusynCpuPercent_.size() > 60) recentAusynCpuPercent_.removeFirst();
        double cpuTotal = 0.0;
        double cpuPeak = 0.0;
        for (const double value : recentAusynCpuPercent_) {
            cpuTotal += value;
            cpuPeak = std::max(cpuPeak, value);
        }
        cpuText += QStringLiteral(" · 60-sample avg %1% · peak %2%")
            .arg(cpuTotal / static_cast<double>(recentAusynCpuPercent_.size()), 0, 'f', 2)
            .arg(cpuPeak, 0, 'f', 2);
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    QString memoryText = QStringLiteral("memory unavailable");
    double workingSetMb = -1.0;
    double privateMemoryMb = -1.0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        workingSetMb = static_cast<double>(memory.WorkingSetSize) / 1'000'000.0;
        privateMemoryMb = static_cast<double>(memory.PrivateUsage) / 1'000'000.0;
    }
    recentAusynWorkingSetMb_.append(workingSetMb);
    recentAusynPrivateMemoryMb_.append(privateMemoryMb);
    if (recentAusynWorkingSetMb_.size() > 60) recentAusynWorkingSetMb_.removeFirst();
    if (recentAusynPrivateMemoryMb_.size() > 60) recentAusynPrivateMemoryMb_.removeFirst();
    double workingSetTotalMb = 0.0;
    double workingSetPeakMb = 0.0;
    int workingSetCount = 0;
    double privateMemoryTotalMb = 0.0;
    double privateMemoryPeakMb = 0.0;
    int privateMemoryCount = 0;
    for (const double value : recentAusynWorkingSetMb_) {
        if (value < 0.0) continue;
        workingSetTotalMb += value;
        workingSetPeakMb = std::max(workingSetPeakMb, value);
        ++workingSetCount;
    }
    for (const double value : recentAusynPrivateMemoryMb_) {
        if (value < 0.0) continue;
        privateMemoryTotalMb += value;
        privateMemoryPeakMb = std::max(privateMemoryPeakMb, value);
        ++privateMemoryCount;
    }
    if (workingSetMb >= 0.0 && privateMemoryMb >= 0.0) {
        memoryText = QStringLiteral("%1 MB working set · recent avg %2 MB · peak %3 MB · %4 MB private memory (avg %5 MB · peak %6 MB)")
            .arg(workingSetMb, 0, 'f', 1)
            .arg(workingSetCount > 0 ? workingSetTotalMb / workingSetCount : workingSetMb, 0, 'f', 1)
            .arg(workingSetPeakMb, 0, 'f', 1)
            .arg(privateMemoryMb, 0, 'f', 1)
            .arg(privateMemoryCount > 0 ? privateMemoryTotalMb / privateMemoryCount : privateMemoryMb, 0, 'f', 1)
            .arg(privateMemoryPeakMb, 0, 'f', 1);
    }
    ausynResourceUse_->setText(QStringLiteral("Ausyn process use · %1 · %2 · sampled with the monitor; not stored")
        .arg(cpuText, memoryText));
    recentAusynCpuTrendPercent_.append(currentCpuPercent);
    if (recentAusynCpuTrendPercent_.size() > 60) recentAusynCpuTrendPercent_.removeFirst();
    if (ausynCpuTrend_)
        static_cast<ResourceUsageChart*>(ausynCpuTrend_)->setSamples(recentAusynCpuTrendPercent_, recentAusynWorkingSetMb_);
    if (previousMonotonicSampleMs_ >= 0) {
        const qint64 monotonicGap = now - previousMonotonicSampleMs_;
        const qint64 wallGap = previousCapture_.msecsTo(snapshot.capturedAt);
        const qint64 drift = qAbs(wallGap - monotonicGap);
        if (wallGap <= 0 || drift > 30'000) {
            cadence_->setText(QStringLiteral("Sampling cadence · system clock changed; elapsed time uses a monotonic timer"));
        } else {
            cadence_->setText(QStringLiteral("Sampling cadence · latest interval %1 ms · target %2 ms")
                .arg(monotonicGap).arg(samplingIntervalSeconds_ * 1000));
        }
    }
    if (!snapshot.monitoringAdaptationNote.isEmpty())
        cadence_->setText(snapshot.monitoringAdaptationNote);
    previousCapture_ = snapshot.capturedAt;
    previousMonotonicSampleMs_ = now;
    sampleClock_.restart();
    ++sampleCount_;
    recentCollectionMicroseconds_.append(snapshot.collectionDurationMicroseconds);
    if (recentCollectionMicroseconds_.size() > 60) recentCollectionMicroseconds_.removeFirst();
    qint64 recentTotalMicroseconds = 0;
    qint64 maximumCollectionMicroseconds = 0;
    for (const qint64 duration : recentCollectionMicroseconds_) {
        recentTotalMicroseconds += duration;
        maximumCollectionMicroseconds = std::max(maximumCollectionMicroseconds, duration);
    }
    const double latestMilliseconds = static_cast<double>(snapshot.collectionDurationMicroseconds) / 1000.0;
    const double averageMilliseconds = static_cast<double>(recentTotalMicroseconds) /
        static_cast<double>(recentCollectionMicroseconds_.size()) / 1000.0;
    const double maximumMilliseconds = static_cast<double>(maximumCollectionMicroseconds) / 1000.0;
    const double samplingBudgetPercent = averageMilliseconds * 100.0 /
        static_cast<double>(samplingIntervalSeconds_ * 1000);
    collectionLatency_->setText(QStringLiteral("System collection latency · latest %1 ms · rolling average %2 ms · peak %3 ms · %4% of sampling interval")
        .arg(latestMilliseconds, 0, 'f', 2)
        .arg(averageMilliseconds, 0, 'f', 2)
        .arg(maximumMilliseconds, 0, 'f', 2)
        .arg(samplingBudgetPercent, 0, 'f', 1));
    collectors_->setRowCount(snapshot.dataQuality.size());
    int row = 0;
    int problems = 0;
    for (const MetricQualityRecord& record : snapshot.dataQuality) {
        const bool issue = record.state == MetricQualityState::Error ||
            record.state == MetricQualityState::Invalid || record.state == MetricQualityState::Stale;
        if (issue) ++problems;
        const QStringList values{record.metric, qualityState(record.state), record.source, record.detail};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values.at(col));
            item->setToolTip(values.at(col));
            if (col == 1 && issue) item->setForeground(QColor(QStringLiteral("#ff8f88")));
            collectors_->setItem(row, col, item);
        }
        ++row;
    }
    const bool elevatedCollectionCost = recentCollectionMicroseconds_.size() >= 10 && samplingBudgetPercent >= 20.0;
    status_->setText(problems > 0
        ? QStringLiteral("●  Monitoring active · %1 reading(s) need attention").arg(problems)
        : elevatedCollectionCost
            ? QStringLiteral("●  Monitoring active · collection is using %1% of the sampling interval on average")
                .arg(samplingBudgetPercent, 0, 'f', 1)
            : QStringLiteral("●  Monitoring healthy · %1 validated samples this session").arg(sampleCount_));
    status_->setStyleSheet(problems == 0 && !elevatedCollectionCost
        ? QStringLiteral("font-size:18px;font-weight:700;color:#87d7b0")
        : QStringLiteral("font-size:18px;font-weight:700;color:#e4c27d"));
    refreshFreshness();
}

void AgentHealthPage::refreshFreshness()
{
    uptime_->setText(QStringLiteral("Session uptime · %1").arg(duration(uptimeClock_.elapsed())));
    if (!sampleClock_.isValid()) return;
    const qint64 age = sampleClock_.elapsed();
    freshness_->setText(QStringLiteral("Last validated sample · %1 ago · target every %2 seconds")
        .arg(age < 1000 ? QStringLiteral("just now") : QStringLiteral("%1 seconds").arg(age / 1000))
        .arg(samplingIntervalSeconds_));
    if (age > std::max(10'000, samplingIntervalSeconds_ * 3000)) {
        status_->setText(QStringLiteral("●  Monitoring delayed · no validated sample for %1 seconds").arg(age / 1000));
        status_->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:#ff8f88"));
    }
}

void AgentHealthPage::setSamplingIntervalSeconds(int seconds)
{
    samplingIntervalSeconds_ = std::clamp(seconds, 1, 10);
    cadence_->setText(QStringLiteral("Sampling cadence · target every %1 seconds").arg(samplingIntervalSeconds_));
}

} // namespace Ausyn
