#pragma once

#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"

#include <QString>
#include <QVector>
#include <QHash>

#include <memory>

namespace Ausyn {

class LocalHistoryStore final {
public:
    LocalHistoryStore();
    ~LocalHistoryStore();

    LocalHistoryStore(const LocalHistoryStore&) = delete;
    LocalHistoryStore& operator=(const LocalHistoryStore&) = delete;

    [[nodiscard]] bool initialize(int retentionDays = 30, QString* errorMessage = nullptr);
    [[nodiscard]] bool saveSnapshot(const SystemSnapshot& snapshot, QString* errorMessage = nullptr);
    [[nodiscard]] MetricAverages averagesSince(const QDateTime& since, QString* errorMessage = nullptr);
    [[nodiscard]] MetricAverages averagesBetween(const QDateTime& since, const QDateTime& until,
                                                  QString* errorMessage = nullptr);
    [[nodiscard]] QVector<MemoryTrendPoint> recentMemoryTrend(int windowCount, int windowMinutes,
                                                               QString* errorMessage = nullptr);
    [[nodiscard]] QVector<HistoryPoint> recentHistory(int hours, QString* errorMessage = nullptr);
    [[nodiscard]] QVector<StorageTrendPoint> dailyStorageTrend(int days, QString* errorMessage = nullptr);
    [[nodiscard]] QHash<QString, QVector<StorageTrendPoint>> dailyVolumeStorageTrends(
        int days, QString* errorMessage = nullptr);
    [[nodiscard]] QVector<BatteryHealthTrendPoint> dailyBatteryHealthTrend(int days,
                                                                           QString* errorMessage = nullptr);
    [[nodiscard]] bool syncActiveFindings(const QVector<Finding>& findings,
                                          const QDateTime& now,
                                          QString* errorMessage = nullptr);
    [[nodiscard]] bool recordRecommendationOutcome(const QString& ruleId,
                                                    const QDateTime& firstSeen,
                                                    RecommendationOutcome outcome,
                                                    QString* errorMessage = nullptr);
    [[nodiscard]] bool recordRecommendationVerification(const QString& ruleId,
                                                         const QDateTime& firstSeen,
                                                         const SystemSnapshot& snapshot,
                                                         bool captureBaseline,
                                                         QString* errorMessage = nullptr);
    [[nodiscard]] QVector<Finding> activeFindings(QString* errorMessage = nullptr);
    [[nodiscard]] QVector<Finding> incidentHistorySince(const QDateTime& since, int limit = 250,
                                                        QString* errorMessage = nullptr);
    [[nodiscard]] QVector<RecommendationOutcomeSummary> recommendationOutcomeSummariesSince(
        const QDateTime& since, QString* errorMessage = nullptr);
    [[nodiscard]] QString databasePath() const;
    [[nodiscard]] qint64 databaseSizeBytes() const;
    [[nodiscard]] bool exportCsv(const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] bool backupTo(const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] bool restoreFrom(const QString& filePath, QString* errorMessage = nullptr);
    [[nodiscard]] bool clear(QString* errorMessage = nullptr);
    [[nodiscard]] bool setRetentionDays(int days, QString* errorMessage = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Ausyn
