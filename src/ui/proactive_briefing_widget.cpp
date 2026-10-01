#include "proactive_briefing_widget.h"

#include <QCryptographicHash>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QStringList>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {

QFrame* makeInsightCard(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("panel"));
    card->setStyleSheet(QStringLiteral("QFrame#panel{background:#151d2a;border:1px solid #29364a;border-radius:10px;}"));
    return card;
}

QString processContextForFinding(const Finding& finding, const SystemSnapshot& snapshot)
{
    const QString rule = finding.ruleId;
    const bool cpuPressure = rule == QLatin1String("processor-sustained-load") ||
        rule == QLatin1String("live-sustained-resource-load") ||
        rule == QLatin1String("concurrent-cpu-memory-pressure");
    const bool memoryPressure = rule == QLatin1String("memory-sustained-pressure") ||
        rule == QLatin1String("live-sustained-resource-load") ||
        rule == QLatin1String("memory-rapid-rise") ||
        rule == QLatin1String("concurrent-cpu-memory-pressure");
    if (!cpuPressure && !memoryPressure) return {};

    const qint64 ageMilliseconds = snapshot.processSamplesCapturedAt.isValid()
        ? snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 freshnessLimit = std::max<qint64>(
        10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
    if (ageMilliseconds < 0 || ageMilliseconds > freshnessLimit * 1000)
        return QStringLiteral("Per-process readings are stale or unavailable, so I can’t responsibly point to an app from this snapshot.");

    const ProcessSample* highestCpu = nullptr;
    const ProcessSample* largestWorkingSet = nullptr;
    for (const ProcessSample& process : snapshot.topProcesses) {
        if (process.name.isEmpty()) continue;
        if (process.cpuPercent && std::isfinite(*process.cpuPercent) &&
            (!highestCpu || *process.cpuPercent > highestCpu->cpuPercent.value_or(-1.0)))
            highestCpu = &process;
        if (process.workingSetBytes &&
            (!largestWorkingSet || *process.workingSetBytes > largestWorkingSet->workingSetBytes.value_or(0)))
            largestWorkingSet = &process;
    }

    QStringList measurements;
    if (cpuPressure) {
        if (highestCpu) {
            measurements << QStringLiteral("highest current process CPU is %1 at %2%")
                .arg(highestCpu->name, QString::number(*highestCpu->cpuPercent, 'f', 1));
        } else {
            measurements << QStringLiteral("per-process CPU is unavailable");
        }
    }
    if (memoryPressure) {
        if (largestWorkingSet) {
            measurements << QStringLiteral("largest current working set is %1 at %2 GB")
                .arg(largestWorkingSet->name)
                .arg(static_cast<double>(*largestWorkingSet->workingSetBytes) / 1'000'000'000.0, 0, 'f', 1);
        } else {
            measurements << QStringLiteral("per-process memory is unavailable");
        }
    }
    return QStringLiteral("Latest per-process sample: %1. These readings occurred alongside the system pattern; they do not prove which app caused it.")
        .arg(measurements.join(QStringLiteral("; ")));
}

QString eventContextForFinding(const Finding& finding, const EventLogUpdate& eventLogs)
{
    const bool resourceFinding = finding.ruleId == QLatin1String("processor-sustained-load") ||
        finding.ruleId == QLatin1String("live-sustained-resource-load") ||
        finding.ruleId == QLatin1String("memory-sustained-pressure") ||
        finding.ruleId == QLatin1String("concurrent-cpu-memory-pressure") ||
        finding.ruleId == QLatin1String("memory-rapid-rise");
    if (!resourceFinding || !eventLogs.available || !eventLogs.checkedAt.isValid()) return {};
    const qint64 scanAgeMilliseconds = eventLogs.checkedAt.msecsTo(QDateTime::currentDateTime());
    if (scanAgeMilliseconds < 0 || scanAgeMilliseconds > 30 * 60 * 1000) return {};

    QStringList nearby;
    for (const EventInsight& event : eventLogs.insights) {
        if (!event.latestAt.isValid()) continue;
        const auto near = [&event](const QDateTime& time) {
            return time.isValid() && qAbs(time.msecsTo(event.latestAt)) <= 120 * 1000;
        };
        if (!near(finding.firstSeen) && !near(finding.lastSeen)) continue;
        const QString severity = event.severity == EventSeverity::Critical ? QStringLiteral("critical")
            : event.severity == EventSeverity::Error ? QStringLiteral("error") : QStringLiteral("warning");
        nearby << QStringLiteral("%1 from %2, event %3 at %4")
            .arg(severity, event.provider).arg(event.eventId)
            .arg(event.latestAt.toLocalTime().toString(QStringLiteral("h:mm ap")));
        if (nearby.size() == 2) break;
    }
    if (nearby.isEmpty()) return {};
    return QStringLiteral("Timing context: Windows also recorded %1 near this resource-pressure finding. Nearby timing can guide a review, but does not show the event caused the slowdown.")
        .arg(nearby.join(QStringLiteral("; ")));
}

QString confidenceContextForFinding(const Finding& finding)
{
    if (finding.confidence.isEmpty() || finding.confidenceBasis.isEmpty()) return {};
    return QStringLiteral("Confidence in the observed condition: %1. %2")
        .arg(finding.confidence, finding.confidenceBasis);
}

QString priorOutcomeContextForFinding(const Finding& finding)
{
    if (finding.priorRatedOutcomeReports < 5) return {};
    const double improvementPercent = 100.0 * finding.priorImprovementReports /
                                     finding.priorRatedOutcomeReports;
    return QStringLiteral("On this PC, %1 of %2 earlier rated reports for this finding said conditions improved (%3%). That is self-reported context, not proof that a suggested step caused the change.")
        .arg(finding.priorImprovementReports)
        .arg(finding.priorRatedOutcomeReports)
        .arg(improvementPercent, 0, 'f', 0);
}

QString sampleChangeForMetric(const QString& name, const std::optional<double>& current,
                              const std::optional<double>& previous, double threshold,
                              const QDateTime& capturedAt, QDateTime& lastReportedAt)
{
    if (!current || !previous || !std::isfinite(*current) || !std::isfinite(*previous)) return {};
    const double delta = *current - *previous;
    if (std::abs(delta) < threshold) return {};
    if (lastReportedAt.isValid()) {
        const qint64 sinceLastReportMilliseconds = lastReportedAt.msecsTo(capturedAt);
        if (sinceLastReportMilliseconds >= 0 && sinceLastReportMilliseconds < 30 * 1000) return {};
    }
    lastReportedAt = capturedAt;
    return QStringLiteral("%1 moved from %2% to %3% (%4 by %5 percentage points)")
        .arg(name)
        .arg(*previous, 0, 'f', 0)
        .arg(*current, 0, 'f', 0)
        .arg(delta > 0.0 ? QStringLiteral("rose") : QStringLiteral("fell"))
        .arg(std::abs(delta), 0, 'f', 0);
}

int informationPriority(const Finding& finding)
{
    if (finding.ruleId == QLatin1String("memory-rapid-rise")) return 30;
    if (finding.ruleId == QLatin1String("personal-baseline-resource-shift")) return 20;
    return 10;
}

QString makeCurrentRead(const AnalysisUpdate& analysis, const SystemSnapshot& snapshot,
                        const EventLogUpdate& eventLogs,
                        const QStringList& securityNotices, const QStringList& eventNotices,
                        const QStringList& startupNotices)
{
    if (!snapshot.capturedAt.isValid())
        return QStringLiteral("I’m waiting for my first fresh Windows readings. Once they arrive, I’ll call out sustained changes and explain the evidence here.");

    const qint64 ageMilliseconds = snapshot.capturedAt.msecsTo(QDateTime::currentDateTime());
    const QString time = snapshot.capturedAt.toLocalTime().toString(QStringLiteral("h:mm ap"));
    if (ageMilliseconds < 0) {
        return QStringLiteral("This sample’s timestamp is ahead of this PC’s current clock, so I’m holding off on a live interpretation until the timestamps line up.");
    }
    const qint64 staleAfterSeconds = std::max<qint64>(
        10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
    if (ageMilliseconds > staleAfterSeconds * 1000) {
        const qint64 ageSeconds = ageMilliseconds / 1000;
        const QString age = ageSeconds < 3600 ? QStringLiteral("%1 minute(s)").arg(std::max<qint64>(1, ageSeconds / 60))
            : QStringLiteral("%1 hour(s)").arg(std::max<qint64>(1, ageSeconds / 3600));
        return QStringLiteral("My latest reading is from %1, about %2 ago, so I’m holding off on a current diagnosis. I’ll update this read when fresh system data arrives.")
            .arg(time, age);
    }

    const Finding* leadingFinding = nullptr;
    const Finding* notableChange = nullptr;
    for (const Finding& finding : analysis.findings) {
        if (finding.severity == FindingSeverity::Information) {
            if (!notableChange || informationPriority(finding) > informationPriority(*notableChange) ||
                (informationPriority(finding) == informationPriority(*notableChange) &&
                 finding.lastSeen > notableChange->lastSeen))
                notableChange = &finding;
        } else if (!leadingFinding || finding.severity > leadingFinding->severity) {
            leadingFinding = &finding;
        }
    }
    if (leadingFinding) {
        const QString evidence = leadingFinding->evidence.isEmpty()
            ? QStringLiteral("Ausyn’s latest analysis detected a sustained pattern.")
            : leadingFinding->evidence;
        const QString nextStep = leadingFinding->recommendation.isEmpty()
            ? QStringLiteral("Review the related page for details.") : leadingFinding->recommendation;
        const QString processContext = processContextForFinding(*leadingFinding, snapshot);
        const QString eventContext = eventContextForFinding(*leadingFinding, eventLogs);
        const QString confidenceContext = confidenceContextForFinding(*leadingFinding);
        QString read = QStringLiteral("Here’s what stands out: %1. %2 Evidence: %3")
            .arg(leadingFinding->title, leadingFinding->summary, evidence);
        if (!confidenceContext.isEmpty()) read += QStringLiteral("\n%1").arg(confidenceContext);
        const QString priorOutcomeContext = priorOutcomeContextForFinding(*leadingFinding);
        if (!priorOutcomeContext.isEmpty()) read += QStringLiteral("\n%1").arg(priorOutcomeContext);
        if (!processContext.isEmpty()) read += QStringLiteral("\n%1").arg(processContext);
        if (!eventContext.isEmpty()) read += QStringLiteral("\n%1").arg(eventContext);
        read += QStringLiteral("\nSuggested next step: %1 This describes the observed pattern; it does not prove its cause. Latest sample: %2.")
            .arg(nextStep, time);
        return read;
    }

    const auto leadNotice = [](const QStringList& notices) -> QString {
        return notices.isEmpty() ? QString() : notices.first();
    };
    QString notice = leadNotice(securityNotices);
    if (notice.isEmpty()) notice = leadNotice(eventNotices);
    if (notice.isEmpty()) notice = leadNotice(startupNotices);
    if (!notice.isEmpty())
        return QStringLiteral("One thing to review: %1 Latest system sample: %2. Open its card below for details and context.")
            .arg(notice, time);

    if (notableChange) {
        const QString evidence = notableChange->evidence.isEmpty()
            ? QStringLiteral("Ausyn’s recent analysis detected a measurable change.") : notableChange->evidence;
        const QString nextStep = notableChange->recommendation.isEmpty()
            ? QStringLiteral("Compare the readings again after a few more samples.") : notableChange->recommendation;
        const QString confidenceContext = confidenceContextForFinding(*notableChange);
        QString read = QStringLiteral("A change stood out: %1. %2 Evidence: %3")
            .arg(notableChange->title, notableChange->summary, evidence);
        if (!confidenceContext.isEmpty()) read += QStringLiteral("\n%1").arg(confidenceContext);
        const QString priorOutcomeContext = priorOutcomeContextForFinding(*notableChange);
        if (!priorOutcomeContext.isEmpty()) read += QStringLiteral("\n%1").arg(priorOutcomeContext);
        read += QStringLiteral("\nSuggested next step: %1 This is an observed shift, not proof of a fault. Latest sample: %2.")
            .arg(nextStep, time);
        return read;
    }

    if (analysis.memoryForecast.hasEstimate && analysis.memoryForecast.minutesUntil90Percent >= 0.0 &&
        analysis.memoryForecast.minutesUntil90Percent <= 60.0) {
        return QStringLiteral("Memory use is trending upward in recent windows. If that pattern continues, average use could reach 90% in about %1 minute(s). That’s a cautious projection, not a memory-leak diagnosis.")
            .arg(std::max(1, static_cast<int>(std::lround(analysis.memoryForecast.minutesUntil90Percent))));
    }
    if (analysis.storageForecast.rapidDropDetected ||
        (analysis.storageForecast.hasEstimate && analysis.storageForecast.daysUntilTenPercent >= 0.0 &&
         analysis.storageForecast.daysUntilTenPercent <= 90.0)) {
        return QStringLiteral("Your system-drive space has a notable trend. %1 Review the forecast card below; Ausyn doesn’t inspect files or infer what used the space.")
            .arg(analysis.storageForecast.explanation);
    }
    if (analysis.batteryForecast.hasEstimate && analysis.batteryForecast.minutesUntil15Percent >= 0.0 &&
        analysis.batteryForecast.minutesUntil15Percent <= 120.0) {
        return QStringLiteral("Your battery’s recent discharge pattern projects a low-charge point in about %1 minute(s), if the rate continues. Workload can change that estimate.")
            .arg(std::max(1, static_cast<int>(std::lround(analysis.batteryForecast.minutesUntil15Percent))));
    }

    QStringList readings;
    QStringList unavailableMetrics;
    if (snapshot.processorUsagePercent)
        readings << QStringLiteral("CPU %1%").arg(*snapshot.processorUsagePercent, 0, 'f', 0);
    else
        unavailableMetrics << QStringLiteral("CPU");
    if (snapshot.memoryUsagePercent)
        readings << QStringLiteral("memory %1%").arg(*snapshot.memoryUsagePercent, 0, 'f', 0);
    else
        unavailableMetrics << QStringLiteral("memory");

    if (readings.isEmpty()) {
        return QStringLiteral("I can’t assess CPU or memory pressure because usable readings are unavailable right now, so I can’t rule out a resource issue. Check Ausyn’s monitoring status for details. Latest sample: %1.")
            .arg(time);
    }

    const QString latest = QStringLiteral("The latest sample reads %1.")
        .arg(readings.join(QStringLiteral(" and ")));
    if (!unavailableMetrics.isEmpty()) {
        return QStringLiteral("I don’t see a sustained warning in the readings available to me. %1 is unavailable, so I can’t assess it yet. %2 I’ll keep comparing fresh samples. Latest sample: %3.")
            .arg(unavailableMetrics.join(QStringLiteral(" and ")), latest, time);
    }
    return QStringLiteral("I don’t see a sustained warning in my latest analysis. %1 I’m continuing to compare readings over time and will surface a notable change here. Sample: %2.")
        .arg(latest, time);
}

} // namespace

ProactiveBriefingWidget::ProactiveBriefingWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(9);

    auto* readCard = makeInsightCard(this);
    auto* readLayout = new QVBoxLayout(readCard);
    readLayout->setContentsMargins(13, 10, 13, 10);
    readLayout->setSpacing(5);
    auto* readTitle = new QLabel(QStringLiteral("Ausyn’s read"), readCard);
    readTitle->setObjectName(QStringLiteral("panelTitle"));
    currentRead_ = new QLabel(QStringLiteral("I’m preparing a fresh system read…"), readCard);
    currentRead_->setObjectName(QStringLiteral("heroBody"));
    currentRead_->setWordWrap(true);
    readLayout->addWidget(readTitle);
    readLayout->addWidget(currentRead_);
    root->addWidget(readCard);

    auto* controls = new QHBoxLayout;
    count_ = new QLabel(QStringLiteral("Preparing your briefing…"), this);
    count_->setObjectName(QStringLiteral("subtle"));
    count_->setWordWrap(true);
    filter_ = new QComboBox(this);
    filter_->addItem(QStringLiteral("All signals"), QString{});
    filter_->addItem(QStringLiteral("System findings"), QStringLiteral("System"));
    filter_->addItem(QStringLiteral("Forecasts"), QStringLiteral("Forecast"));
    filter_->addItem(QStringLiteral("Security"), QStringLiteral("Security"));
    filter_->addItem(QStringLiteral("Windows events"), QStringLiteral("Windows events"));
    filter_->addItem(QStringLiteral("Startup"), QStringLiteral("Startup"));
    snoozeDuration_ = new QComboBox(this);
    snoozeDuration_->addItem(QStringLiteral("Snooze · 1 hour"), 60 * 60 * 1000);
    snoozeDuration_->addItem(QStringLiteral("30 minutes"), 30 * 60 * 1000);
    snoozeDuration_->addItem(QStringLiteral("4 hours"), 4 * 60 * 60 * 1000);
    snoozeDuration_->addItem(QStringLiteral("Until tomorrow"), -1);
    snoozeDuration_->setObjectName(QStringLiteral("snoozeDuration"));
    auto* dismissed = new QToolButton(this);
    dismissed->setText(QStringLiteral("Show hidden"));
    dismissed->setCheckable(true);
    dismissed->setObjectName(QStringLiteral("secondaryButton"));
    dismissed->setToolTip(QStringLiteral("Show cards you've dismissed or snoozed. Choices are saved locally."));
    connect(dismissed, &QToolButton::toggled, this, [this](bool checked) {
        showDismissed_ = checked;
        rebuild();
    });
    connect(filter_, &QComboBox::currentIndexChanged, this, &ProactiveBriefingWidget::rebuild);
    controls->addWidget(count_, 1);
    controls->addWidget(filter_);
    controls->addWidget(snoozeDuration_);
    controls->addWidget(dismissed);
    auto* controlsWidget = new QWidget(this);
    controlsWidget->setLayout(controls);
    controlsWidget->setObjectName(QStringLiteral("briefingFilters"));
    controlsWidget->setProperty("detailOnly", true);
    root->addWidget(controlsWidget);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listContent_ = new QWidget(scroll);
    listLayout_ = new QVBoxLayout(listContent_);
    listLayout_->setContentsMargins(1, 1, 5, 1);
    listLayout_->setSpacing(8);
    scroll->setWidget(listContent_);
    root->addWidget(scroll, 1);

    expiryTimer_ = new QTimer(this);
    expiryTimer_->setInterval(30 * 1000);
    expiryTimer_->setTimerType(Qt::CoarseTimer);
    connect(expiryTimer_, &QTimer::timeout, this, [this] {
        bool expired = false;
        for (auto it = snoozed_.begin(); it != snoozed_.end();) {
            if (!it.value().isValid() || it.value() <= QDateTime::currentDateTime()) {
                it = snoozed_.erase(it);
                expired = true;
            } else {
                ++it;
            }
        }
        if (expired) {
            emit persistentStateChanged(dismissedKeys(), snoozedKeys());
            rebuild();
        }
        if (snoozed_.isEmpty()) expiryTimer_->stop();
    });
}

void ProactiveBriefingWidget::setPersistentState(const QStringList& dismissedKeys,
                                                  const QHash<QString, QDateTime>& snoozedKeys)
{
    dismissed_.clear();
    for (const QString& key : dismissedKeys) dismissed_.insert(key);
    snoozed_.clear();
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = snoozedKeys.cbegin(); it != snoozedKeys.cend(); ++it) {
        if (it.value().isValid() && it.value() > now) snoozed_.insert(it.key(), it.value());
    }
    if (!snoozed_.isEmpty()) expiryTimer_->start();
    rebuild();
}

QStringList ProactiveBriefingWidget::dismissedKeys() const
{
    QStringList keys = dismissed_.values();
    std::sort(keys.begin(), keys.end());
    return keys;
}

QHash<QString, QDateTime> ProactiveBriefingWidget::snoozedKeys() const
{
    return snoozed_;
}

void ProactiveBriefingWidget::setBriefing(const AnalysisUpdate& analysis,
                                          const SystemSnapshot& snapshot,
                                          const EventLogUpdate& eventLogs,
                                          const QStringList& securityNotices,
                                          const QStringList& eventNotices,
                                          const QStringList& startupNotices)
{
    QString sampleChange;
    if (snapshot.capturedAt.isValid()) {
        const qint64 ageMilliseconds = snapshot.capturedAt.msecsTo(QDateTime::currentDateTime());
        if (ageMilliseconds >= 0 && ageMilliseconds <= 30 * 1000) {
            const qint64 gapMilliseconds = previousSampleAt_.isValid()
                ? previousSampleAt_.msecsTo(snapshot.capturedAt) : -1;
            const qint64 maxGapSeconds = std::max<qint64>(
                15, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
            const qint64 allowedGapMilliseconds = maxGapSeconds * 1000;
            if (gapMilliseconds > 0 && gapMilliseconds <= allowedGapMilliseconds) {
                QStringList changes;
                const QString cpuChange = sampleChangeForMetric(
                    QStringLiteral("CPU"), snapshot.processorUsagePercent, previousCpuPercent_,
                    15.0, snapshot.capturedAt, lastCpuChangeReportedAt_);
                const QString gpuChange = sampleChangeForMetric(
                    QStringLiteral("GPU"), snapshot.graphicsUsagePercent, previousGpuPercent_,
                    20.0, snapshot.capturedAt, lastGpuChangeReportedAt_);
                const QString memoryChange = sampleChangeForMetric(
                    QStringLiteral("Memory"), snapshot.memoryUsagePercent, previousMemoryPercent_,
                    5.0, snapshot.capturedAt, lastMemoryChangeReportedAt_);
                if (!cpuChange.isEmpty()) changes << cpuChange;
                if (!gpuChange.isEmpty()) changes << gpuChange;
                if (!memoryChange.isEmpty()) changes << memoryChange;
                if (!changes.isEmpty()) {
                    sampleChange = QStringLiteral("Since the previous sample (%1 second(s) ago), %2. This is a direct reading change, not a diagnosis.")
                        .arg(std::max<qint64>(1, (gapMilliseconds + 500) / 1000))
                        .arg(changes.join(QStringLiteral(" and ")));
                }
            }
            if (previousSampleAt_.isValid() && snapshot.capturedAt > previousSampleAt_) {
                previousSampleAt_ = snapshot.capturedAt;
                previousCpuPercent_ = snapshot.processorUsagePercent;
                previousGpuPercent_ = snapshot.graphicsUsagePercent;
                previousMemoryPercent_ = snapshot.memoryUsagePercent;
            } else if (!previousSampleAt_.isValid()) {
                previousSampleAt_ = snapshot.capturedAt;
                previousCpuPercent_ = snapshot.processorUsagePercent;
                previousGpuPercent_ = snapshot.graphicsUsagePercent;
                previousMemoryPercent_ = snapshot.memoryUsagePercent;
            }
        } else {
            previousSampleAt_ = {};
            previousCpuPercent_.reset();
            previousGpuPercent_.reset();
            previousMemoryPercent_.reset();
        }
    }

    QString read = makeCurrentRead(analysis, snapshot, eventLogs, securityNotices,
                                   eventNotices, startupNotices);
    if (!sampleChange.isEmpty()) read += QStringLiteral("\n%1").arg(sampleChange);
    if (currentRead_->text() != read) currentRead_->setText(read);

    QVector<Card> next;
    next.reserve(analysis.findings.size() + securityNotices.size() + eventNotices.size() +
                 startupNotices.size() + analysis.volumeStorageForecasts.size() + 4);
    for (const Finding& finding : analysis.findings) {
        Card card;
        card.key = QStringLiteral("finding:%1:%2").arg(finding.ruleId)
            .arg(finding.firstSeen.isValid() ? finding.firstSeen.toUTC().toMSecsSinceEpoch() : 0);
        card.category = QStringLiteral("System");
        card.title = finding.title;
        card.summary = finding.summary;
        card.evidence = finding.evidence;
        card.nextStep = finding.recommendation;
        card.actionLabel = QStringLiteral("Review finding");
        card.pageIndex = finding.severity == FindingSeverity::Information ? 6 : 7;
        card.priority = finding.severity == FindingSeverity::Critical ? 100
            : (finding.severity == FindingSeverity::Warning ? 75 : 45);
        next.append(std::move(card));
    }

    const StorageForecast& storage = analysis.storageForecast;
    if (storage.rapidDropDetected || (storage.hasEstimate && storage.daysUntilTenPercent >= 0.0 &&
                                      storage.daysUntilTenPercent <= 90.0)) {
        Card card;
        card.key = QStringLiteral("forecast:system-storage");
        card.category = QStringLiteral("Forecast");
        card.title = storage.rapidDropDetected ? QStringLiteral("System-drive space changed sharply")
                                               : QStringLiteral("System-drive space may need attention");
        card.summary = storage.explanation;
        card.evidence = storage.rapidDropDetected
            ? QStringLiteral("Daily capacity samples show a %1 percentage-point decrease.").arg(storage.rapidDropPercentagePoints, 0, 'f', 1)
            : QStringLiteral("Recent daily measurements project 10% free space in about %1 day(s), if the pattern continues; %2 of the latest 3 changes confirm the decline.")
                .arg(static_cast<int>(std::lround(storage.daysUntilTenPercent)))
                .arg(storage.recentDecliningDays);
        card.nextStep = QStringLiteral("Review the storage trend and recent files you recognize. Ausyn does not inspect files or identify what used the space.");
        card.actionLabel = QStringLiteral("Review forecast");
        card.pageIndex = 10;
        card.priority = storage.rapidDropDetected ? 82
            : (storage.daysUntilTenPercent <= 14.0 ? 78 : 52);
        next.append(std::move(card));
    }
    for (const VolumeStorageForecast& volume : analysis.volumeStorageForecasts) {
        const StorageForecast& forecast = volume.forecast;
        if (forecast.currentFreePercent > 10.0 && !forecast.rapidDropDetected &&
            (!forecast.hasEstimate || forecast.daysUntilTenPercent < 0.0 || forecast.daysUntilTenPercent > 90.0)) continue;
        Card card;
        card.key = QStringLiteral("forecast:volume:%1").arg(volume.rootPath);
        card.category = QStringLiteral("Forecast");
        card.title = QStringLiteral("%1 storage outlook").arg(volume.label.isEmpty() ? volume.rootPath : volume.label);
        card.summary = forecast.explanation;
        card.evidence = forecast.rapidDropDetected
            ? QStringLiteral("Daily samples show free space fell by %1 percentage points.").arg(forecast.rapidDropPercentagePoints, 0, 'f', 1)
            : QStringLiteral("Current free space is %1%; recent trend estimates %2 day(s) to 10%; %3 of the latest 3 daily changes confirm decline.")
                .arg(forecast.currentFreePercent, 0, 'f', 1)
                .arg(static_cast<int>(std::lround(forecast.daysUntilTenPercent)))
                .arg(forecast.recentDecliningDays);
        card.nextStep = QStringLiteral("Review the drive trend. This projection assumes the recent pattern continues.");
        card.actionLabel = QStringLiteral("Review forecast");
        card.pageIndex = 10;
        card.priority = forecast.rapidDropDetected ? 76 : (forecast.daysUntilTenPercent <= 14.0 ? 72 : 48);
        next.append(std::move(card));
    }

    const MemoryPressureForecast& memory = analysis.memoryForecast;
    if (memory.hasEstimate && memory.minutesUntil90Percent >= 0.0 && memory.minutesUntil90Percent <= 60.0) {
        Card card;
        card.key = QStringLiteral("forecast:memory");
        card.category = QStringLiteral("Forecast");
        card.title = QStringLiteral("Memory pressure is trending upward");
        card.summary = QStringLiteral("If the recent pattern continues, average use could reach 90% in about %1 minute(s).")
            .arg(std::max(1, static_cast<int>(std::lround(memory.minutesUntil90Percent))));
        card.evidence = QStringLiteral("Based on %1 minutes; robust rise %2 percentage points/hour; %3 of the latest 4 windows confirmed a rise; linear-fit quality %4%.")
            .arg(memory.observedMinutes).arg(memory.risePerHour, 0, 'f', 1)
            .arg(memory.recentRisingWindows).arg(memory.fitQuality * 100.0, 0, 'f', 0);
        card.nextStep = QStringLiteral("Save work if needed and review Performance for current resource use. This is not a memory-leak diagnosis.");
        card.actionLabel = QStringLiteral("Review performance");
        card.pageIndex = 2;
        card.priority = memory.minutesUntil90Percent <= 15.0 ? 82 : 65;
        next.append(std::move(card));
    }

    const BatteryForecast& battery = analysis.batteryForecast;
    if (battery.hasEstimate && battery.minutesUntil15Percent >= 0.0 && battery.minutesUntil15Percent <= 120.0) {
        Card card;
        card.key = QStringLiteral("forecast:battery");
        card.category = QStringLiteral("Forecast");
        card.title = QStringLiteral("Battery may reach a low-charge point soon");
        card.summary = battery.explanation;
        card.evidence = QStringLiteral("Recent discharge trend estimates about %1 minute(s) to 15%.")
            .arg(std::max(1, static_cast<int>(std::lround(battery.minutesUntil15Percent))));
        card.nextStep = QStringLiteral("Connect power if the estimated timing matters. Actual runtime depends on workload and conditions.");
        card.actionLabel = QStringLiteral("Review battery");
        card.pageIndex = 4;
        card.priority = battery.minutesUntil15Percent <= 30.0 ? 84 : 62;
        next.append(std::move(card));
    }

    auto appendNotices = [&next](const QStringList& notices, const QString& category,
                                 const QString& keyPrefix, int page, int priority,
                                 const QString& title, const QString& limits) {
        for (const QString& notice : notices) {
            Card card;
            const QByteArray stableDigest = QCryptographicHash::hash(
                notice.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
            card.key = keyPrefix + QLatin1Char(':') + QString::fromLatin1(stableDigest);
            card.category = category;
            card.title = title;
            card.summary = notice;
            card.evidence = limits;
            card.nextStep = QStringLiteral("Open the related page for details and review options.");
            card.actionLabel = QStringLiteral("Review details");
            card.pageIndex = page;
            card.priority = priority;
            next.append(std::move(card));
        }
    };
    appendNotices(securityNotices, QStringLiteral("Security"), QStringLiteral("security"), 13, 92,
                  QStringLiteral("Security status deserves a review"),
                  QStringLiteral("Reported by Windows Security Center or local update-cache metadata; Ausyn does not install updates."));
    appendNotices(eventNotices, QStringLiteral("Windows events"), QStringLiteral("event"), 11, 68,
                  QStringLiteral("Recent Windows event pattern"),
                  QStringLiteral("Event records provide timing and Windows messages; they do not establish a cause."));
    appendNotices(startupNotices, QStringLiteral("Startup"), QStringLiteral("startup"), 12, 56,
                  QStringLiteral("Startup app activity may be worth checking"),
                  QStringLiteral("A process match is suggestive context and does not prove that a startup item launched it."));

    std::stable_sort(next.begin(), next.end(), [](const Card& a, const Card& b) {
        return a.priority > b.priority;
    });
    const bool unchanged = next.size() == cards_.size() && std::equal(next.cbegin(), next.cend(), cards_.cbegin(),
        [](const Card& a, const Card& b) {
            return a.key == b.key && a.category == b.category && a.title == b.title &&
                a.summary == b.summary && a.evidence == b.evidence && a.nextStep == b.nextStep &&
                a.actionLabel == b.actionLabel && a.pageIndex == b.pageIndex && a.priority == b.priority;
        });
    if (snapshot.capturedAt.isValid()) {
        const qint64 ageMilliseconds = snapshot.capturedAt.msecsTo(QDateTime::currentDateTime());
        const bool clockMismatch = ageMilliseconds < 0;
        const qint64 ageSeconds = ageMilliseconds / 1000;
        const qint64 displayedAgeSeconds = std::max<qint64>(0, ageSeconds);
        const QString age = clockMismatch ? QStringLiteral("clock mismatch")
            : displayedAgeSeconds < 60 ? QStringLiteral("%1s ago").arg(displayedAgeSeconds)
            : displayedAgeSeconds < 3600 ? QStringLiteral("%1m ago").arg(displayedAgeSeconds / 60)
                                         : QStringLiteral("%1h ago").arg(displayedAgeSeconds / 3600);
        const bool possiblyStale = ageMilliseconds > 30 * 1000;
        sampleStatus_ = QStringLiteral("Latest sample %1 (%2%3)")
            .arg(snapshot.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")), age,
                 possiblyStale ? QStringLiteral(" · may be out of date") : QString{});
        count_->setToolTip(QStringLiteral("Briefing evidence uses the system sample captured at %1. %2")
            .arg(snapshot.capturedAt.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm:ss ap")),
                 clockMismatch ? QStringLiteral("Its timestamp is ahead of this PC’s clock, so Ausyn will not treat it as current.")
                 : possiblyStale ? QStringLiteral("It is over 30 seconds old; check Ausyn’s monitoring status before treating it as current.")
                               : QStringLiteral("It was captured recently.")));
    } else {
        sampleStatus_ = QStringLiteral("Latest sample time unavailable");
        count_->setToolTip(QStringLiteral("Ausyn has not provided a timestamp for the briefing evidence."));
    }
    if (unchanged) {
        int hidden = 0;
        for (const Card& card : cards_)
            if (dismissed_.contains(card.key) || snoozed_.contains(card.key)) ++hidden;
        count_->setText(QStringLiteral("%1 active · %2 hidden · ranked by urgency · %3")
            .arg(cards_.size() - hidden).arg(hidden).arg(sampleStatus_));
        return;
    }

    QSet<QString> activeKeys;
    for (const Card& card : next) activeKeys.insert(card.key);
    bool statePruned = false;
    for (auto it = dismissed_.begin(); it != dismissed_.end();) {
        if (!activeKeys.contains(*it)) { it = dismissed_.erase(it); statePruned = true; }
        else ++it;
    }
    for (auto it = snoozed_.begin(); it != snoozed_.end();) {
        if (!activeKeys.contains(it.key())) { it = snoozed_.erase(it); statePruned = true; }
        else ++it;
    }
    cards_ = std::move(next);
    if (snoozed_.isEmpty()) expiryTimer_->stop();
    if (statePruned) emit persistentStateChanged(dismissedKeys(), snoozedKeys());
    rebuild();
}

void ProactiveBriefingWidget::rebuild()
{
    if (!isVisible()) return;
    const QString selectedCategory = filter_->currentData().toString();
    QString layoutKey = QString::number(detailed_) + QString::number(showDismissed_) + selectedCategory;
    int visibleCards = 0;
    for (const Card& card : cards_) {
        const bool hidden = dismissed_.contains(card.key) ||
            (snoozed_.contains(card.key) && snoozed_.value(card.key) > QDateTime::currentDateTime());
        if ((!showDismissed_ && hidden) || (!selectedCategory.isEmpty() && card.category != selectedCategory)) continue;
        if (++visibleCards > (detailed_ ? 12 : 3)) break;
        layoutKey += card.key + QString::number(hidden);
    }
    const auto updateCount = [this] {
        int hidden = 0;
        for (const Card& card : cards_)
            if (dismissed_.contains(card.key) || snoozed_.contains(card.key)) ++hidden;
        count_->setText(QStringLiteral("%1 active · %2 hidden · %3")
            .arg(cards_.size() - hidden).arg(hidden).arg(sampleStatus_));
    };
    if (layoutKey == renderedLayoutKey_) {
        for (const Card& card : cards_) {
            QWidget* frame = renderedCards_.value(card.key, nullptr);
            if (!frame) continue;
            const QHash<QString, QString> values{{QStringLiteral("cardTitle"), card.title},
                {QStringLiteral("cardSummary"), card.summary},
                {QStringLiteral("cardEvidence"), QStringLiteral("Evidence · %1").arg(card.evidence)},
                {QStringLiteral("cardNextStep"), QStringLiteral("Next step · %1").arg(card.nextStep)}};
            for (auto it = values.cbegin(); it != values.cend(); ++it) {
                if (auto* label = frame->findChild<QLabel*>(it.key()); label && label->text() != it.value())
                    label->setText(it.value());
            }
        }
        updateCount();
        return;
    }
    renderedLayoutKey_ = layoutKey;
    renderedCards_.clear();
    while (QLayoutItem* item = listLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    int shown = 0;
    for (const Card& card : cards_) {
        const bool isDismissed = dismissed_.contains(card.key);
        const bool isSnoozed = snoozed_.contains(card.key) && snoozed_.value(card.key) > QDateTime::currentDateTime();
        const bool isHidden = isDismissed || isSnoozed;
        if ((!showDismissed_ && isHidden) || (!selectedCategory.isEmpty() && card.category != selectedCategory)) continue;
        ++shown;
        if (shown > (detailed_ ? 12 : 3)) { --shown; break; }
        auto* frame = makeInsightCard(listContent_);
        renderedCards_.insert(card.key, frame);
        auto* cardLayout = new QVBoxLayout(frame);
        cardLayout->setContentsMargins(13, 11, 13, 11);
        cardLayout->setSpacing(6);
        auto* meta = new QLabel(QStringLiteral("%1  ·  %2%3")
            .arg(card.category, card.priority >= 80 ? QStringLiteral("High attention") :
                 (card.priority >= 60 ? QStringLiteral("Worth a look") : QStringLiteral("Monitor")),
                 isDismissed ? QStringLiteral("  ·  Dismissed")
                    : (isSnoozed ? QStringLiteral("  ·  Snoozed until %1")
                        .arg(snoozed_.value(card.key).toLocalTime().toString(QStringLiteral("h:mm ap"))) : QString{})), frame);
        meta->setObjectName(QStringLiteral("eyebrow"));
        auto* title = new QLabel(card.title, frame);
        title->setObjectName(QStringLiteral("cardTitle"));
        title->setStyleSheet(QStringLiteral("font-weight:650;font-size:14px;"));
        title->setWordWrap(true);
        auto* summary = new QLabel(card.summary, frame);
        summary->setObjectName(QStringLiteral("cardSummary"));
        summary->setWordWrap(true);
        auto* evidence = new QLabel(QStringLiteral("Evidence · %1").arg(card.evidence), frame);
        evidence->setObjectName(QStringLiteral("cardEvidence"));
        evidence->setVisible(detailed_);
        evidence->setProperty("detailOnly", true);
        evidence->setWordWrap(true);
        auto* next = new QLabel(QStringLiteral("Next step · %1").arg(card.nextStep), frame);
        next->setObjectName(QStringLiteral("cardNextStep"));
        next->setWordWrap(true);
        auto* actions = new QHBoxLayout;
        auto* review = new QPushButton(card.actionLabel, frame);
        review->setObjectName(QStringLiteral("secondaryButton"));
        connect(review, &QPushButton::clicked, this, [this, page = card.pageIndex, title = card.title] {
            emit reviewRequested(page, title);
        });
        auto* explain = new QPushButton(QStringLiteral("Ask Ausyn to explain"), frame);
        explain->setObjectName(QStringLiteral("secondaryButton"));
        connect(explain, &QPushButton::clicked, this, [this, key = card.key] {
            const auto item = std::find_if(cards_.cbegin(), cards_.cend(), [&key](const Card& c) { return c.key == key; });
            if (item != cards_.cend()) emit askAboutRequested(item->category, item->title, item->summary, item->evidence, item->nextStep);
        });
        explain->setVisible(detailed_);
        explain->setProperty("detailOnly", true);
        auto* snooze = new QPushButton(QStringLiteral("Snooze"), frame);
        snooze->setObjectName(QStringLiteral("secondaryButton"));
        auto* dismiss = new QToolButton(frame);
        dismiss->setObjectName(QStringLiteral("secondaryButton"));
        dismiss->setText(isHidden ? QStringLiteral("Restore") : QStringLiteral("Dismiss"));
        const QString key = card.key;
        connect(snooze, &QPushButton::clicked, this, [this, key] {
            const int duration = snoozeDuration_->currentData().toInt();
            QDateTime until = QDateTime::currentDateTime();
            if (duration == -1) {
                until = until.addDays(1);
                until.setTime(QTime(0, 0));
            } else {
                until = until.addMSecs(duration);
            }
            dismissed_.remove(key);
            snoozed_.insert(key, until);
            expiryTimer_->start();
            emit persistentStateChanged(dismissedKeys(), snoozedKeys());
            rebuild();
        });
        snooze->setVisible(!isHidden);
        connect(dismiss, &QToolButton::clicked, this, [this, key, isHidden] {
            if (isHidden) {
                dismissed_.remove(key);
                snoozed_.remove(key);
            } else {
                dismissed_.insert(key);
                snoozed_.remove(key);
            }
            if (snoozed_.isEmpty()) expiryTimer_->stop();
            emit persistentStateChanged(dismissedKeys(), snoozedKeys());
            rebuild();
        });
        actions->addWidget(review);
        actions->addWidget(explain);
        actions->addStretch();
        actions->addWidget(snooze);
        actions->addWidget(dismiss);
        cardLayout->addWidget(meta);
        cardLayout->addWidget(title);
        cardLayout->addWidget(summary);
        cardLayout->addWidget(evidence);
        cardLayout->addWidget(next);
        cardLayout->addLayout(actions);
        listLayout_->addWidget(frame);
    }
    if (shown == 0) {
        auto* empty = new QLabel(cards_.isEmpty()
            ? QStringLiteral("No new heads-up right now. Ausyn will surface a note when measured patterns, forecasts, or system events merit your attention.")
            : QStringLiteral("Nothing in this view needs attention. Dismissed and snoozed cards remain available with “Show hidden.”"), listContent_);
        empty->setObjectName(QStringLiteral("subtle"));
        empty->setWordWrap(true);
        empty->setContentsMargins(6, 10, 6, 10);
        listLayout_->addWidget(empty);
    }
    listLayout_->addStretch(1);
    updateCount();
}

void ProactiveBriefingWidget::setDetailed(bool detailed)
{
    detailed_ = detailed;
    if (auto* filters = findChild<QWidget*>(QStringLiteral("briefingFilters"))) filters->setVisible(detailed);
    rebuild();
}

void ProactiveBriefingWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    rebuild();
}

} // namespace Ausyn
