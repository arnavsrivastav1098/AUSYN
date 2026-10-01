#pragma once

#include "../intelligence/insight_types.h"
#include "system_snapshot.h"
#include "hardware_change_tracker.h"

#include <QObject>
#include <QHash>
#include <QQueue>
#include <QThread>

#include <chrono>
#include <memory>

class QTimer;

namespace Ausyn {

class LocalHistoryStore;
class SystemCollector;

class TelemetryWorker final : public QObject {
    Q_OBJECT

public slots:
    void setMonitoringEnabled(bool enabled);
    void start();
    void stop();
    void setRetentionDays(int days);
    void setSamplingIntervalSeconds(int seconds);
    void setAdaptiveSamplingEnabled(bool enabled);
    void exportHistory(QString path);
    void backupHistory(QString path);
    void restoreHistory(QString path);
    void clearHistory();
    void recordRecommendationOutcome(QString ruleId, QDateTime firstSeen, int outcome);
    void recordRecommendationVerification(QString ruleId, QDateTime firstSeen,
                                          Ausyn::SystemSnapshot snapshot, bool captureBaseline);
    void requestHistory(int hours);

signals:
    void snapshotReady(Ausyn::SystemSnapshot snapshot);
    void analysisReady(Ausyn::AnalysisUpdate update);
    void historyReady(Ausyn::HistoryUpdate update);
    void historyOperationFinished(QString operation, bool success, QString message);
    void recommendationOutcomeFinished(QString ruleId, QDateTime firstSeen, int outcome,
                                       bool success, QString message);
    void recommendationVerificationFinished(QString ruleId, QDateTime firstSeen, bool captureBaseline,
                                             QVector<Ausyn::Finding> findings, bool success, QString message);

private slots:
    void sample();

private:
    void bufferHistorySample(const SystemSnapshot& snapshot, qint64 capturedAtMs);
    [[nodiscard]] bool flushBufferedHistory(QString* errorMessage);

    QTimer* timer_ = nullptr;
    SystemCollector* collector_ = nullptr;
    LocalHistoryStore* historyStore_ = nullptr;
    qint64 lastPersistedAtMs_ = 0;
    qint64 lastAnalysisEmittedAtMs_ = 0;
    qint64 lastHistoryRefreshAtMs_ = 0;
    qint64 lastHistoryRetryAtMs_ = 0;
    qint64 lastPredictionAtMs_ = 0;
    qint64 lastPersonalBaselineAtMs_ = 0;
    qint64 lastBufferedAtMs_ = 0;
    QQueue<SystemSnapshot> pendingHistorySnapshots_;
    quint64 droppedBufferedHistorySamples_ = 0;
    bool historyAvailable_ = false;
    QString historyMessage_;
    QHash<QString, Finding> activeFindings_;
    int retentionDays_ = 30;
    int samplingIntervalSeconds_ = 5;
    int effectiveSamplingIntervalSeconds_ = 5;
    int expensiveCollectionStreak_ = 0;
    int stableCollectionStreak_ = 0;
    quint64 previousProcessCpuTicks_ = 0;
    std::chrono::steady_clock::time_point previousProcessCpuSampleAt_{};
    bool hasPreviousProcessCpuSample_ = false;
    bool adaptiveSamplingEnabled_ = true;
    bool monitoringEnabled_ = true;
    int requestedHistoryHours_ = 24;
    StorageForecast storageForecast_;
    QVector<VolumeStorageForecast> volumeStorageForecasts_;
    MemoryPressureForecast memoryForecast_;
    QVector<BatteryHealthTrendPoint> batteryHealthTrend_;
    MetricAverages personalBaselineAverages_;
    MetricAverages precedingWindowAverages_;
    HardwareChangeTracker hardwareChangeTracker_;
};

class TelemetryService final : public QObject {
    Q_OBJECT

public:
    explicit TelemetryService(QObject* parent = nullptr);
    ~TelemetryService() override;

    void start();
    void setRetentionDays(int days);
    void setSamplingIntervalSeconds(int seconds);
    void setAdaptiveSamplingEnabled(bool enabled);
    void setMonitoringEnabled(bool enabled);
    void exportHistory(const QString& path);
    void backupHistory(const QString& path);
    void restoreHistory(const QString& path);
    void clearHistory();
    void recordRecommendationOutcome(const QString& ruleId, const QDateTime& firstSeen,
                                     RecommendationOutcome outcome);
    void recordRecommendationVerification(const QString& ruleId, const QDateTime& firstSeen,
                                          const SystemSnapshot& snapshot, bool captureBaseline);
    void requestHistory(int hours);

signals:
    void snapshotReady(Ausyn::SystemSnapshot snapshot);
    void analysisReady(Ausyn::AnalysisUpdate update);
    void historyReady(Ausyn::HistoryUpdate update);
    void historyOperationFinished(QString operation, bool success, QString message);
    void recommendationOutcomeFinished(QString ruleId, QDateTime firstSeen, int outcome,
                                       bool success, QString message);
    void recommendationVerificationFinished(QString ruleId, QDateTime firstSeen, bool captureBaseline,
                                             QVector<Ausyn::Finding> findings, bool success, QString message);

private:
    QThread workerThread_;
    TelemetryWorker* worker_ = nullptr;
    int retentionDays_ = 30;
    int samplingIntervalSeconds_ = 5;
    bool adaptiveSamplingEnabled_ = true;
};

} // namespace Ausyn
