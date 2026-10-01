#include "telemetry_service.h"

#include "../data/local_history_store.h"
#include "../intelligence/insight_engine.h"
#include "../intelligence/prediction_engine.h"
#include "system_collector.h"
#include "snapshot_validator.h"

#include <QMetaObject>
#include <QDebug>
#include <QTimeZone>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <optional>

#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Ausyn {
namespace {
constexpr int kDefaultSampleIntervalMilliseconds = 5000;
constexpr qint64 kHistoryPersistIntervalMilliseconds = 10'000;
constexpr qint64 kHistoryRefreshIntervalMilliseconds = 30'000;
constexpr qint64 kHistoryRetryIntervalMilliseconds = 30'000;
constexpr qint64 kAnalysisWindowMilliseconds = 5 * 60 * 1000;
constexpr qint64 kPersonalBaselineWindowMilliseconds = 24LL * 60 * 60 * 1000;
constexpr qint64 kPersonalBaselineRefreshMilliseconds = 5 * 60 * 1000;
constexpr qsizetype kMaximumBufferedHistorySamples = 180;

std::optional<quint64> currentProcessCpuTicks()
{
#ifdef Q_OS_WIN
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return std::nullopt;
    ULARGE_INTEGER kernelTicks{};
    ULARGE_INTEGER userTicks{};
    kernelTicks.LowPart = kernel.dwLowDateTime;
    kernelTicks.HighPart = kernel.dwHighDateTime;
    userTicks.LowPart = user.dwLowDateTime;
    userTicks.HighPart = user.dwHighDateTime;
    return kernelTicks.QuadPart + userTicks.QuadPart;
#else
    return std::nullopt;
#endif
}

SystemSnapshot historyOnlySnapshot(const SystemSnapshot& source)
{
    SystemSnapshot compact;
    compact.capturedAt = source.capturedAt;
    compact.processorUsagePercent = source.processorUsagePercent;
    compact.memoryUsagePercent = source.memoryUsagePercent;
    compact.memoryUsedBytes = source.memoryUsedBytes;
    compact.memoryTotalBytes = source.memoryTotalBytes;
    compact.graphicsUsagePercent = source.graphicsUsagePercent;
    compact.systemVolumeFreeBytes = source.systemVolumeFreeBytes;
    compact.systemVolumeTotalBytes = source.systemVolumeTotalBytes;
    compact.volumes = source.volumes;
    compact.batteryPercent = source.batteryPercent;
    compact.batteryHealthPercent = source.batteryHealthPercent;
    compact.networkReceiveBytesPerSecond = source.networkReceiveBytesPerSecond;
    compact.networkSendBytesPerSecond = source.networkSendBytesPerSecond;
    return compact;
}

QVector<VolumeStorageForecast> buildVolumeForecasts(
    const QHash<QString, QVector<StorageTrendPoint>>& trends,
    const SystemSnapshot& snapshot)
{
    QVector<VolumeStorageForecast> forecasts;
    for (const VolumeSample& volume : snapshot.volumes) {
        if (volume.rootPath.isEmpty() || volume.totalBytes == 0 ||
            volume.rootPath.compare(snapshot.systemVolumePath, Qt::CaseInsensitive) == 0) continue;
        auto trend = trends.constFind(volume.rootPath);
        if (trend == trends.cend()) {
            for (auto it = trends.cbegin(); it != trends.cend(); ++it) {
                if (it.key().compare(volume.rootPath, Qt::CaseInsensitive) == 0) {
                    trend = it;
                    break;
                }
            }
        }
        VolumeStorageForecast item;
        item.rootPath = volume.rootPath;
        item.label = volume.label;
        if (trend != trends.cend()) {
            item.forecast = PredictionEngine::analyzeStorage(*trend, snapshot.capturedAt);
            item.forecast.currentFreePercent = 100.0 * static_cast<double>(volume.freeBytes) /
                static_cast<double>(volume.totalBytes);
            if (item.forecast.currentFreePercent <= 10.0 &&
                item.forecast.state != ForecastState::InsufficientData &&
                item.forecast.state != ForecastState::Irregular)
                item.forecast.state = ForecastState::ThresholdReached;
        }
        else item.forecast.explanation = QStringLiteral("Ausyn is collecting local history for this drive.");
        if (item.forecast.explanation.isEmpty())
            item.forecast.explanation = QStringLiteral("Not enough daily history is available yet.");
        forecasts.append(std::move(item));
    }
    std::sort(forecasts.begin(), forecasts.end(), [](const auto& left, const auto& right) {
        return left.rootPath.compare(right.rootPath, Qt::CaseInsensitive) < 0;
    });
    return forecasts;
}
}

void TelemetryWorker::setRetentionDays(int days)
{
    retentionDays_ = std::clamp(days, 7, 90);
    if (historyStore_ && historyAvailable_) {
        QString error;
        if (!historyStore_->setRetentionDays(retentionDays_, &error)) {
            qWarning().noquote() << "Ausyn could not apply history retention:" << error;
        } else {
            lastPredictionAtMs_ = 0;
        }
    }
}

void TelemetryWorker::setSamplingIntervalSeconds(int seconds)
{
    const int requestedInterval = std::clamp(seconds, 1, 10);
    if (requestedInterval == samplingIntervalSeconds_)
        return;
    samplingIntervalSeconds_ = requestedInterval;
    if (!timer_ || samplingIntervalSeconds_ >= effectiveSamplingIntervalSeconds_ || !adaptiveSamplingEnabled_) {
        effectiveSamplingIntervalSeconds_ = samplingIntervalSeconds_;
        expensiveCollectionStreak_ = 0;
        stableCollectionStreak_ = 0;
    }
    if (timer_) timer_->setInterval(effectiveSamplingIntervalSeconds_ * 1000);
}

void TelemetryWorker::setAdaptiveSamplingEnabled(bool enabled)
{
    if (adaptiveSamplingEnabled_ == enabled)
        return;
    adaptiveSamplingEnabled_ = enabled;
    expensiveCollectionStreak_ = 0;
    stableCollectionStreak_ = 0;
    if (!enabled) {
        effectiveSamplingIntervalSeconds_ = samplingIntervalSeconds_;
        if (timer_) timer_->setInterval(effectiveSamplingIntervalSeconds_ * 1000);
    }
}

void TelemetryWorker::setMonitoringEnabled(bool enabled)
{
    if (monitoringEnabled_ == enabled) return;
    monitoringEnabled_ = enabled;
    if (!timer_) return;
    if (enabled) {
        timer_->start();
        sample();
    } else {
        timer_->stop();
    }
}

void TelemetryWorker::bufferHistorySample(const SystemSnapshot& snapshot, qint64 capturedAtMs)
{
    if (!pendingHistorySnapshots_.isEmpty() &&
        capturedAtMs - lastBufferedAtMs_ < kHistoryPersistIntervalMilliseconds) {
        return;
    }

    if (pendingHistorySnapshots_.size() >= kMaximumBufferedHistorySamples) {
        pendingHistorySnapshots_.dequeue();
        ++droppedBufferedHistorySamples_;
        if (droppedBufferedHistorySamples_ == 1) {
            qWarning() << "Ausyn's temporary history buffer reached its limit; oldest samples will be dropped until storage recovers.";
        }
    }
    pendingHistorySnapshots_.enqueue(historyOnlySnapshot(snapshot));
    lastBufferedAtMs_ = capturedAtMs;
}

bool TelemetryWorker::flushBufferedHistory(QString* errorMessage)
{
    while (!pendingHistorySnapshots_.isEmpty()) {
        const SystemSnapshot& snapshot = pendingHistorySnapshots_.head();
        if (!historyStore_ || !historyStore_->saveSnapshot(snapshot, errorMessage)) {
            return false;
        }
        lastPersistedAtMs_ = snapshot.capturedAt.toUTC().toMSecsSinceEpoch();
        pendingHistorySnapshots_.dequeue();
    }
    lastBufferedAtMs_ = 0;
    return true;
}

void TelemetryWorker::exportHistory(QString path)
{
    QString error;
    const bool ok = historyStore_ && historyAvailable_ && historyStore_->exportCsv(path, &error);
    if (!ok && error.isEmpty()) error = QStringLiteral("Local history is unavailable.");
    emit historyOperationFinished(QStringLiteral("export"), ok,
        ok ? QStringLiteral("History exported successfully.") : error);
}

void TelemetryWorker::backupHistory(QString path)
{
    QString error;
    bool ok = historyStore_ && historyAvailable_;
    if (ok && !flushBufferedHistory(&error)) ok = false;
    if (ok) ok = historyStore_->backupTo(path, &error);
    if (!ok && error.isEmpty()) error = QStringLiteral("Local history is unavailable.");
    emit historyOperationFinished(QStringLiteral("backup"), ok,
        ok ? QStringLiteral("Verified Ausyn history backup created successfully.") : error);
}

void TelemetryWorker::restoreHistory(QString path)
{
    QString error;
    bool ok = true;
    if (historyStore_ && historyAvailable_ && !pendingHistorySnapshots_.isEmpty()) {
        QString flushError;
        if (!flushBufferedHistory(&flushError))
            qWarning().noquote() << "Ausyn will restore the selected backup after a buffered history write failed:" << flushError;
    }
    if (!historyStore_) historyStore_ = new LocalHistoryStore;
    ok = historyStore_->restoreFrom(path, &error);
    if (ok) {
        historyAvailable_ = true;
        historyMessage_.clear();
        activeFindings_.clear();
        storageForecast_ = {};
        volumeStorageForecasts_.clear();
        memoryForecast_ = {};
        batteryHealthTrend_.clear();
        personalBaselineAverages_ = {};
        precedingWindowAverages_ = {};
        lastPredictionAtMs_ = 0;
        lastHistoryRefreshAtMs_ = 0;
        lastPersistedAtMs_ = 0;
        requestHistory(requestedHistoryHours_);
    } else {
        historyAvailable_ = false;
        delete historyStore_;
        historyStore_ = nullptr;
    }
    if (!ok && error.isEmpty()) error = QStringLiteral("Local history is unavailable.");
    emit historyOperationFinished(QStringLiteral("restore"), ok,
        ok ? QStringLiteral("History restored and verified. Ausyn continues monitoring this PC.") : error);
}

void TelemetryWorker::clearHistory()
{
    QString error;
    const bool ok = historyStore_ && historyAvailable_ && historyStore_->clear(&error);
    if (!ok && error.isEmpty()) error = QStringLiteral("Local history is unavailable.");
    if (ok) {
        activeFindings_.clear();
        storageForecast_ = {};
        volumeStorageForecasts_.clear();
        memoryForecast_ = {};
        batteryHealthTrend_.clear();
        personalBaselineAverages_ = {};
        precedingWindowAverages_ = {};
        lastPredictionAtMs_ = 0;
        HistoryUpdate update;
        update.historyAvailable = true;
        emit historyReady(std::move(update));
    }
    emit historyOperationFinished(QStringLiteral("clear"), ok,
        ok ? QStringLiteral("Local history has been cleared.") : error);
}

void TelemetryWorker::recordRecommendationOutcome(QString ruleId, QDateTime firstSeen, int outcome)
{
    QString error;
    const auto selected = static_cast<RecommendationOutcome>(outcome);
    const bool ok = historyStore_ && historyAvailable_ &&
        historyStore_->recordRecommendationOutcome(ruleId, firstSeen, selected, &error);
    if (!ok && error.isEmpty()) error = QStringLiteral("Local history is unavailable.");
    emit recommendationOutcomeFinished(std::move(ruleId), std::move(firstSeen), outcome, ok,
        ok ? QStringLiteral("Feedback saved locally. Ausyn will treat it as your report, not proof of cause.") : error);
}

void TelemetryWorker::recordRecommendationVerification(QString ruleId, QDateTime firstSeen,
                                                         SystemSnapshot snapshot, bool captureBaseline)
{
    QString error;
    bool ok = historyStore_ && historyAvailable_ &&
        historyStore_->recordRecommendationVerification(ruleId, firstSeen, snapshot, captureBaseline, &error);
    if (!historyStore_ || !historyAvailable_)
        error = QStringLiteral("Local history is unavailable.");
    QVector<Finding> findings;
    if (ok) findings = historyStore_->activeFindings(&error);
    if (error.isEmpty()) {
        emit recommendationVerificationFinished(std::move(ruleId), std::move(firstSeen), captureBaseline,
            std::move(findings), ok, ok
                ? (captureBaseline
                    ? QStringLiteral("Before-reading saved locally. Try the recommendation, then record a later reading.")
                    : QStringLiteral("Later reading saved. Compare the values as observations, not proof of cause."))
                : QStringLiteral("The comparison could not be saved."));
    } else {
        emit recommendationVerificationFinished(std::move(ruleId), std::move(firstSeen), captureBaseline,
            std::move(findings), false, error);
    }
}

void TelemetryWorker::requestHistory(int hours)
{
    HistoryUpdate update;
    requestedHistoryHours_ = std::clamp(hours, 1, retentionDays_ * 24);
    if (!historyStore_ || !historyAvailable_) {
        update.historyMessage = QStringLiteral("Local history is unavailable.");
        emit historyReady(std::move(update));
        return;
    }
    QString error;
    update.points = historyStore_->recentHistory(requestedHistoryHours_, &error);
    update.historyAvailable = error.isEmpty();
    update.historyMessage = error;
    QString incidentError;
    update.incidentFindings = historyStore_->incidentHistorySince(
        QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), 250, &incidentError);
    update.incidentFindingsAvailable = incidentError.isEmpty();
    update.incidentFindingsMessage = incidentError;
    QString outcomeError;
    update.recommendationOutcomeSummaries = historyStore_->recommendationOutcomeSummariesSince(
        QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), &outcomeError);
    update.recommendationOutcomeHistoryAvailable = outcomeError.isEmpty();
    update.recommendationOutcomeHistoryMessage = outcomeError;
    emit historyReady(std::move(update));
}

void TelemetryWorker::start()
{
    if (timer_) {
        return;
    }

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::CoarseTimer);
    timer_->setInterval(samplingIntervalSeconds_ > 0
        ? samplingIntervalSeconds_ * 1000 : kDefaultSampleIntervalMilliseconds);
    connect(timer_, &QTimer::timeout, this, &TelemetryWorker::sample);
    if (monitoringEnabled_) {
        timer_->start();
        sample();
    }
}

void TelemetryWorker::stop()
{
    if (timer_) {
        timer_->stop();
    }
    delete historyStore_;
    historyStore_ = nullptr;
    delete collector_;
    collector_ = nullptr;
}

void TelemetryWorker::sample()
{
    if (!monitoringEnabled_) return;
    try {
        if (!collector_) {
            collector_ = new SystemCollector;
        }
        const auto collectionStarted = std::chrono::steady_clock::now();
        SystemSnapshot snapshot = collector_->collect();
        const auto processCpuSampleAt = std::chrono::steady_clock::now();
        const std::optional<quint64> processCpuTicks = currentProcessCpuTicks();
        std::optional<double> ausynCpuPercent;
        if (processCpuTicks && hasPreviousProcessCpuSample_ &&
            *processCpuTicks >= previousProcessCpuTicks_) {
            const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                processCpuSampleAt - previousProcessCpuSampleAt_).count();
            if (elapsedMilliseconds > 0) {
                const quint64 logicalProcessorCount = std::max<quint32>(
                    1, snapshot.logicalProcessorCount);
                const double totalCapacityTicks = static_cast<double>(elapsedMilliseconds) *
                    10'000.0 * static_cast<double>(logicalProcessorCount);
                ausynCpuPercent = 100.0 * static_cast<double>(*processCpuTicks - previousProcessCpuTicks_) /
                    totalCapacityTicks;
            }
        }
        if (processCpuTicks) {
            previousProcessCpuTicks_ = *processCpuTicks;
            previousProcessCpuSampleAt_ = processCpuSampleAt;
            hasPreviousProcessCpuSample_ = true;
        } else {
            hasPreviousProcessCpuSample_ = false;
        }
        snapshot.collectionDurationMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - collectionStarted).count();
        if (adaptiveSamplingEnabled_) {
            const double budgetPercent = static_cast<double>(snapshot.collectionDurationMicroseconds) /
                (static_cast<double>(effectiveSamplingIntervalSeconds_) * 10'000.0);
            const bool collectorAboveBudget = budgetPercent >= 20.0;
            const bool ausynCpuElevated = ausynCpuPercent && *ausynCpuPercent >= 2.0;
            const bool collectorWithinBudget = budgetPercent <= 8.0;
            const bool ausynCpuLow = !ausynCpuPercent || *ausynCpuPercent < 1.0;
            if (collectorAboveBudget || ausynCpuElevated) {
                ++expensiveCollectionStreak_;
                stableCollectionStreak_ = 0;
            } else if (collectorWithinBudget && ausynCpuLow) {
                ++stableCollectionStreak_;
                expensiveCollectionStreak_ = 0;
            } else {
                expensiveCollectionStreak_ = 0;
                stableCollectionStreak_ = 0;
            }
            if (expensiveCollectionStreak_ >= 6 && effectiveSamplingIntervalSeconds_ < 10) {
                effectiveSamplingIntervalSeconds_ = 10;
                stableCollectionStreak_ = 0;
                if (timer_) timer_->setInterval(effectiveSamplingIntervalSeconds_ * 1000);
            } else if (stableCollectionStreak_ >= 12 &&
                       effectiveSamplingIntervalSeconds_ > samplingIntervalSeconds_) {
                effectiveSamplingIntervalSeconds_ = samplingIntervalSeconds_;
                expensiveCollectionStreak_ = 0;
                if (timer_) timer_->setInterval(effectiveSamplingIntervalSeconds_ * 1000);
            }
        }
        snapshot.samplingIntervalSeconds = effectiveSamplingIntervalSeconds_;
        if (effectiveSamplingIntervalSeconds_ > samplingIntervalSeconds_) {
            snapshot.monitoringAdaptationNote = QStringLiteral(
                "Adaptive low-overhead mode · sampling temporarily slowed to %1 seconds after sustained collector or Ausyn CPU load; will restore your %2-second setting when stable")
                .arg(effectiveSamplingIntervalSeconds_).arg(samplingIntervalSeconds_);
        } else if (adaptiveSamplingEnabled_) {
            snapshot.monitoringAdaptationNote = QStringLiteral(
                "Adaptive low-overhead mode · on · target every %1 seconds; sustained collector or Ausyn CPU load can temporarily slow sampling")
                .arg(samplingIntervalSeconds_);
        } else {
            snapshot.monitoringAdaptationNote = QStringLiteral(
                "Adaptive low-overhead mode · off · sampling target every %1 seconds")
                .arg(samplingIntervalSeconds_);
        }
        snapshot.hardwareChanges = hardwareChangeTracker_.observe(snapshot);
        if (!SnapshotValidator::validate(snapshot)) {
            qWarning() << "Ausyn rejected a telemetry sample with an invalid capture timestamp.";
            return;
        }
        emit snapshotReady(snapshot);

        const qint64 nowMs = snapshot.capturedAt.toUTC().toMSecsSinceEpoch();
        const bool persistenceDue = lastPersistedAtMs_ == 0 ||
            nowMs - lastPersistedAtMs_ >= kHistoryPersistIntervalMilliseconds;

        if ((!historyStore_ || !historyAvailable_) &&
            (lastHistoryRetryAtMs_ == 0 || nowMs - lastHistoryRetryAtMs_ >= kHistoryRetryIntervalMilliseconds)) {
            delete historyStore_;
            historyStore_ = new LocalHistoryStore;
            historyAvailable_ = historyStore_->initialize(retentionDays_, &historyMessage_);
            lastHistoryRetryAtMs_ = nowMs;
            if (!historyAvailable_) {
                qWarning().noquote() << "Ausyn local history is unavailable:" << historyMessage_;
                delete historyStore_;
                historyStore_ = nullptr;
            } else {
                historyMessage_.clear();
                const auto storedFindings = historyStore_->activeFindings(&historyMessage_);
                for (const Finding& finding : storedFindings) {
                    activeFindings_.insert(finding.ruleId, finding);
                }
                AnalysisUpdate initialAnalysis;
                initialAnalysis.findings = storedFindings;
                QString predictionError;
                storageForecast_ = PredictionEngine::analyzeStorage(
                    historyStore_->dailyStorageTrend(retentionDays_, &predictionError));
                volumeStorageForecasts_ = buildVolumeForecasts(
                    historyStore_->dailyVolumeStorageTrends(retentionDays_, &predictionError), snapshot);
                QString memoryTrendError;
                memoryForecast_ = PredictionEngine::analyzeMemoryPressure(
                    historyStore_->recentMemoryTrend(10, 5, &memoryTrendError), snapshot.capturedAt);
                if (!memoryTrendError.isEmpty()) {
                    memoryForecast_.state = MemoryForecastState::InsufficientData;
                    memoryForecast_.explanation = QStringLiteral("Memory history could not be read: %1").arg(memoryTrendError);
                }
                if (!predictionError.isEmpty()) {
                    storageForecast_.state = ForecastState::InsufficientData;
                    storageForecast_.explanation = QStringLiteral("Storage history could not be read: %1").arg(predictionError);
                }
                QString batteryTrendError;
                batteryHealthTrend_ = historyStore_->dailyBatteryHealthTrend(retentionDays_, &batteryTrendError);
                initialAnalysis.storageForecast = storageForecast_;
                initialAnalysis.volumeStorageForecasts = volumeStorageForecasts_;
                initialAnalysis.memoryForecast = memoryForecast_;
                initialAnalysis.batteryHealthTrend = batteryHealthTrend_;
                initialAnalysis.historyAvailable = true;
                initialAnalysis.historyPath = historyStore_->databasePath();
                initialAnalysis.historySizeBytes = historyStore_->databaseSizeBytes();
                emit analysisReady(std::move(initialAnalysis));

                HistoryUpdate initialHistory;
                QString initialHistoryError;
                initialHistory.points = historyStore_->recentHistory(requestedHistoryHours_, &initialHistoryError);
                initialHistory.historyAvailable = initialHistoryError.isEmpty();
                initialHistory.historyMessage = initialHistoryError;
                QString incidentError;
                initialHistory.incidentFindings = historyStore_->incidentHistorySince(
                    QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), 250, &incidentError);
                initialHistory.incidentFindingsAvailable = incidentError.isEmpty();
                initialHistory.incidentFindingsMessage = incidentError;
                QString outcomeError;
                initialHistory.recommendationOutcomeSummaries = historyStore_->recommendationOutcomeSummariesSince(
                    QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), &outcomeError);
                initialHistory.recommendationOutcomeHistoryAvailable = outcomeError.isEmpty();
                initialHistory.recommendationOutcomeHistoryMessage = outcomeError;
                if (!initialHistoryError.isEmpty()) {
                    historyMessage_ = initialHistoryError;
                }
                emit historyReady(std::move(initialHistory));
                lastHistoryRefreshAtMs_ = nowMs;
            }
        }

        QString persistenceError;
        bool persisted = false;
        bool historyRecoveryFailed = false;
        if (historyAvailable_ && historyStore_ && !pendingHistorySnapshots_.isEmpty()) {
            persisted = true;
            if (!flushBufferedHistory(&persistenceError)) {
                historyAvailable_ = false;
                historyMessage_ = persistenceError;
                historyRecoveryFailed = true;
                qWarning().noquote() << "Ausyn could not restore buffered local history:" << persistenceError;
                delete historyStore_;
                historyStore_ = nullptr;
                HistoryUpdate unavailableHistory;
                unavailableHistory.historyAvailable = false;
                unavailableHistory.historyMessage = historyMessage_;
                emit historyReady(std::move(unavailableHistory));
            }
        }

        if (historyAvailable_ && historyStore_ && persistenceDue) {
            persisted = historyStore_->saveSnapshot(snapshot, &persistenceError);
            if (persisted) {
                lastPersistedAtMs_ = nowMs;
                historyMessage_.clear();
            } else {
                historyAvailable_ = false;
                historyMessage_ = persistenceError;
                qWarning().noquote() << "Ausyn could not save local history:" << persistenceError;
                bufferHistorySample(snapshot, nowMs);
            }
        } else if (!historyAvailable_ && persistenceDue) {
            bufferHistorySample(snapshot, nowMs);
        }

        MetricAverages averages;
        QString historyReadError;
        if (historyAvailable_ && historyStore_) {
            averages = historyStore_->averagesSince(
                QDateTime::fromMSecsSinceEpoch(
                    nowMs - kAnalysisWindowMilliseconds, QTimeZone(QByteArrayLiteral("UTC"))),
                &historyReadError);
            if (!historyReadError.isEmpty()) {
                historyMessage_ = historyReadError;
            }
            if (persistenceDue) {
                QString trendError;
                precedingWindowAverages_ = historyStore_->averagesBetween(
                    QDateTime::fromMSecsSinceEpoch(nowMs - 2 * kAnalysisWindowMilliseconds,
                                                   QTimeZone(QByteArrayLiteral("UTC"))),
                    QDateTime::fromMSecsSinceEpoch(nowMs - kAnalysisWindowMilliseconds,
                                                   QTimeZone(QByteArrayLiteral("UTC"))),
                    &trendError);
                if (!trendError.isEmpty()) precedingWindowAverages_ = {};
            }
        }

        if (historyAvailable_ && historyStore_ &&
            (lastPersonalBaselineAtMs_ == 0 ||
             nowMs - lastPersonalBaselineAtMs_ >= kPersonalBaselineRefreshMilliseconds)) {
            QString baselineError;
            const qint64 baselineEndMs = nowMs - kAnalysisWindowMilliseconds;
            personalBaselineAverages_ = historyStore_->averagesBetween(
                QDateTime::fromMSecsSinceEpoch(baselineEndMs - kPersonalBaselineWindowMilliseconds,
                                               QTimeZone(QByteArrayLiteral("UTC"))),
                QDateTime::fromMSecsSinceEpoch(baselineEndMs, QTimeZone(QByteArrayLiteral("UTC"))),
                &baselineError);
            lastPersonalBaselineAtMs_ = nowMs;
        } else if (!historyAvailable_ || !historyStore_) {
            personalBaselineAverages_ = {};
            precedingWindowAverages_ = {};
        }

        AnalysisResult analysis = InsightEngine::analyze(snapshot, averages, personalBaselineAverages_,
                                                          batteryHealthTrend_, precedingWindowAverages_);
        QHash<QString, Finding> currentFindings;
        for (Finding& finding : analysis.findings) {
            const auto previous = activeFindings_.constFind(finding.ruleId);
            finding.firstSeen = previous == activeFindings_.cend() ? snapshot.capturedAt : previous->firstSeen;
            finding.lastSeen = snapshot.capturedAt;
            currentFindings.insert(finding.ruleId, finding);
        }
        activeFindings_ = std::move(currentFindings);

        if (historyAvailable_ && historyStore_ && persistenceDue) {
            if (!historyStore_->syncActiveFindings(analysis.findings, snapshot.capturedAt, &historyReadError)) {
                historyMessage_ = historyReadError;
            } else {
                auto persistedFindings = historyStore_->activeFindings(&historyReadError);
                if (historyReadError.isEmpty()) {
                    analysis.findings = std::move(persistedFindings);
                    activeFindings_.clear();
                    for (const Finding& finding : analysis.findings) {
                        activeFindings_.insert(finding.ruleId, finding);
                    }
                }
            }
        }

        AnalysisUpdate analysisUpdate;
        analysisUpdate.health = std::move(analysis.health);
        analysisUpdate.findings = std::move(analysis.findings);
        analysisUpdate.historyAvailable = historyAvailable_;
        analysisUpdate.historyMessage = historyMessage_;
        if (historyAvailable_ && historyStore_ &&
            (lastPredictionAtMs_ == 0 || nowMs - lastPredictionAtMs_ >= 5 * 60 * 1000)) {
            QString predictionError;
            const auto trend = historyStore_->dailyStorageTrend(retentionDays_, &predictionError);
            storageForecast_ = PredictionEngine::analyzeStorage(trend, snapshot.capturedAt);
            volumeStorageForecasts_ = buildVolumeForecasts(
                historyStore_->dailyVolumeStorageTrends(retentionDays_, &predictionError), snapshot);
            QString memoryTrendError;
            memoryForecast_ = PredictionEngine::analyzeMemoryPressure(
                historyStore_->recentMemoryTrend(10, 5, &memoryTrendError), snapshot.capturedAt);
            if (!memoryTrendError.isEmpty()) {
                memoryForecast_.state = MemoryForecastState::InsufficientData;
                memoryForecast_.explanation = QStringLiteral("Memory history could not be read: %1").arg(memoryTrendError);
            }
            if (!predictionError.isEmpty()) {
                storageForecast_.state = ForecastState::InsufficientData;
                storageForecast_.explanation = QStringLiteral("Storage history could not be read: %1").arg(predictionError);
            }
            QString batteryTrendError;
            batteryHealthTrend_ = historyStore_->dailyBatteryHealthTrend(retentionDays_, &batteryTrendError);
            lastPredictionAtMs_ = nowMs;
        }
        analysisUpdate.storageForecast = storageForecast_;
        for (VolumeStorageForecast& item : volumeStorageForecasts_) {
            const auto current = std::find_if(snapshot.volumes.cbegin(), snapshot.volumes.cend(),
                [&item](const VolumeSample& volume) {
                    return volume.rootPath.compare(item.rootPath, Qt::CaseInsensitive) == 0;
                });
            if (current != snapshot.volumes.cend() && current->totalBytes > 0) {
                item.forecast.currentFreePercent = 100.0 * static_cast<double>(current->freeBytes) /
                    static_cast<double>(current->totalBytes);
                if (item.forecast.currentFreePercent <= 10.0 &&
                    item.forecast.state != ForecastState::InsufficientData &&
                    item.forecast.state != ForecastState::Irregular)
                    item.forecast.state = ForecastState::ThresholdReached;
            }
        }
        analysisUpdate.volumeStorageForecasts = volumeStorageForecasts_;
        analysisUpdate.memoryForecast = memoryForecast_;
        analysisUpdate.batteryHealthTrend = batteryHealthTrend_;
        if (historyStore_) {
            analysisUpdate.historyPath = historyStore_->databasePath();
            analysisUpdate.historySizeBytes = historyStore_->databaseSizeBytes();
        }
        if (historyRecoveryFailed || lastAnalysisEmittedAtMs_ == 0 ||
            nowMs - lastAnalysisEmittedAtMs_ >= kHistoryPersistIntervalMilliseconds) {
            QVector<HistoryPoint> batteryHistory;
            if (historyAvailable_ && historyStore_)
                batteryHistory = historyStore_->recentHistory(3, &historyReadError);
            analysisUpdate.batteryForecast = PredictionEngine::analyzeBattery(
                snapshot, batteryHistory, snapshot.capturedAt);
            emit analysisReady(std::move(analysisUpdate));
            lastAnalysisEmittedAtMs_ = nowMs;
        }

        if (historyAvailable_ && historyStore_ &&
            (lastHistoryRefreshAtMs_ == 0 ||
             nowMs - lastHistoryRefreshAtMs_ >= kHistoryRefreshIntervalMilliseconds)) {
            HistoryUpdate historyUpdate;
            historyUpdate.points = historyStore_->recentHistory(requestedHistoryHours_, &historyReadError);
            historyUpdate.historyAvailable = historyReadError.isEmpty();
            historyUpdate.historyMessage = historyReadError;
            QString incidentError;
            historyUpdate.incidentFindings = historyStore_->incidentHistorySince(
                QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), 250, &incidentError);
            historyUpdate.incidentFindingsAvailable = incidentError.isEmpty();
            historyUpdate.incidentFindingsMessage = incidentError;
            QString outcomeError;
            historyUpdate.recommendationOutcomeSummaries = historyStore_->recommendationOutcomeSummariesSince(
                QDateTime::currentDateTime().addSecs(-requestedHistoryHours_ * 3600), &outcomeError);
            historyUpdate.recommendationOutcomeHistoryAvailable = outcomeError.isEmpty();
            historyUpdate.recommendationOutcomeHistoryMessage = outcomeError;
            emit historyReady(std::move(historyUpdate));
            lastHistoryRefreshAtMs_ = nowMs;
        }
    } catch (...) {
        qWarning() << "Ausyn's optional monitoring or history service encountered an error.";
    }
}

TelemetryService::TelemetryService(QObject* parent)
    : QObject(parent)
    , worker_(new TelemetryWorker)
{
    qRegisterMetaType<Ausyn::SystemSnapshot>("Ausyn::SystemSnapshot");
    qRegisterMetaType<Ausyn::AnalysisUpdate>("Ausyn::AnalysisUpdate");
    qRegisterMetaType<Ausyn::HistoryUpdate>("Ausyn::HistoryUpdate");
    qRegisterMetaType<QVector<Ausyn::Finding>>("QVector<Ausyn::Finding>");
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::started, worker_, &TelemetryWorker::start);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &TelemetryWorker::snapshotReady, this, &TelemetryService::snapshotReady,
            Qt::QueuedConnection);
    connect(worker_, &TelemetryWorker::analysisReady, this, &TelemetryService::analysisReady,
            Qt::QueuedConnection);
    connect(worker_, &TelemetryWorker::historyReady, this, &TelemetryService::historyReady,
            Qt::QueuedConnection);
    connect(worker_, &TelemetryWorker::historyOperationFinished,
            this, &TelemetryService::historyOperationFinished, Qt::QueuedConnection);
    connect(worker_, &TelemetryWorker::recommendationOutcomeFinished,
            this, &TelemetryService::recommendationOutcomeFinished, Qt::QueuedConnection);
    connect(worker_, &TelemetryWorker::recommendationVerificationFinished,
            this, &TelemetryService::recommendationVerificationFinished, Qt::QueuedConnection);
}

TelemetryService::~TelemetryService()
{
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, &TelemetryWorker::stop, Qt::BlockingQueuedConnection);
        workerThread_.quit();
        workerThread_.wait();
    } else {
        delete worker_;
    }
}

void TelemetryService::start()
{
    if (!workerThread_.isRunning()) {
        workerThread_.start();
    }
}

void TelemetryService::setRetentionDays(int days)
{
    retentionDays_ = std::clamp(days, 7, 90);
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, [worker = worker_, value = retentionDays_] {
            worker->setRetentionDays(value);
        }, Qt::QueuedConnection);
    } else {
        worker_->setRetentionDays(retentionDays_);
    }
}

void TelemetryService::setSamplingIntervalSeconds(int seconds)
{
    samplingIntervalSeconds_ = std::clamp(seconds, 1, 10);
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, [worker = worker_, value = samplingIntervalSeconds_] {
            worker->setSamplingIntervalSeconds(value);
        }, Qt::QueuedConnection);
    } else {
        worker_->setSamplingIntervalSeconds(samplingIntervalSeconds_);
    }
}

void TelemetryService::setAdaptiveSamplingEnabled(bool enabled)
{
    adaptiveSamplingEnabled_ = enabled;
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, [worker = worker_, value = adaptiveSamplingEnabled_] {
            worker->setAdaptiveSamplingEnabled(value);
        }, Qt::QueuedConnection);
    } else {
        worker_->setAdaptiveSamplingEnabled(adaptiveSamplingEnabled_);
    }
}

void TelemetryService::setMonitoringEnabled(bool enabled)
{
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(worker_, [worker = worker_, enabled] {
            worker->setMonitoringEnabled(enabled);
        }, Qt::QueuedConnection);
    } else {
        worker_->setMonitoringEnabled(enabled);
    }
}

void TelemetryService::exportHistory(const QString& path)
{
    QMetaObject::invokeMethod(worker_, [worker = worker_, path] { worker->exportHistory(path); }, Qt::QueuedConnection);
}

void TelemetryService::backupHistory(const QString& path)
{
    QMetaObject::invokeMethod(worker_, [worker = worker_, path] { worker->backupHistory(path); }, Qt::QueuedConnection);
}

void TelemetryService::restoreHistory(const QString& path)
{
    QMetaObject::invokeMethod(worker_, [worker = worker_, path] { worker->restoreHistory(path); }, Qt::QueuedConnection);
}

void TelemetryService::clearHistory()
{
    QMetaObject::invokeMethod(worker_, &TelemetryWorker::clearHistory, Qt::QueuedConnection);
}

void TelemetryService::recordRecommendationOutcome(const QString& ruleId,
                                                    const QDateTime& firstSeen,
                                                    RecommendationOutcome outcome)
{
    const int value = static_cast<int>(outcome);
    QMetaObject::invokeMethod(worker_, [worker = worker_, ruleId, firstSeen, value] {
        worker->recordRecommendationOutcome(ruleId, firstSeen, value);
    }, Qt::QueuedConnection);
}

void TelemetryService::recordRecommendationVerification(const QString& ruleId,
                                                         const QDateTime& firstSeen,
                                                         const SystemSnapshot& snapshot,
                                                         bool captureBaseline)
{
    QMetaObject::invokeMethod(worker_, [worker = worker_, ruleId, firstSeen, snapshot, captureBaseline] {
        worker->recordRecommendationVerification(ruleId, firstSeen, snapshot, captureBaseline);
    }, Qt::QueuedConnection);
}

void TelemetryService::requestHistory(int hours)
{
    QMetaObject::invokeMethod(worker_, [worker = worker_, hours] { worker->requestHistory(hours); },
                              Qt::QueuedConnection);
}

} // namespace Ausyn
