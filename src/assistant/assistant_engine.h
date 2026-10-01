#pragma once

#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"
#include "../intelligence/app_inventory_types.h"
#include "../settings/user_preferences.h"
#include "../intelligence/event_log_types.h"

namespace Ausyn {

class AssistantEngine final {
public:
    [[nodiscard]] static QString reply(const QString& question,
                                       const SystemSnapshot& snapshot,
                                       const AnalysisUpdate& analysis,
                                       const AppInventoryUpdate& appInventory,
                                       const UserPreferences& preferences,
                                       const QVector<HistoryPoint>& history = {},
                                       bool historyAvailable = false,
                                       int historyPeriodHours = 24,
                                       const GameReadiness& gameReadiness = {},
                                       const EventLogUpdate& eventLogs = {},
                                       const QVector<RecommendationOutcomeSummary>& outcomeSummaries = {},
                                       bool outcomeHistoryAvailable = false);
    [[nodiscard]] static bool requiresLocalHistory(const QString& question);
    [[nodiscard]] static bool requiresLocalBatteryHealthHistory(const QString& question);
    [[nodiscard]] static bool requiresLocalForecast(const QString& question);
    [[nodiscard]] static bool requiresLocalProcessBreakdown(const QString& question);
    [[nodiscard]] static bool requiresLocalProcessQuestion(const QString& question,
                                                           const SystemSnapshot& snapshot);
    [[nodiscard]] static bool requiresLocalSystemCheck(const QString& question);
    [[nodiscard]] static bool requiresLocalSlowdownAssessment(const QString& question);
    [[nodiscard]] static bool requiresLocalThermalAssessment(const QString& question);
    [[nodiscard]] static bool requiresLocalMonitoringStatus(const QString& question);
    [[nodiscard]] static bool requiresLocalGamingReadiness(const QString& question);
    [[nodiscard]] static bool requiresLocalStorageReliability(const QString& question);
    [[nodiscard]] static bool requiresLocalPastRecommendationOutcomes(const QString& question);
    [[nodiscard]] static bool requiresLocalBaselineComparison(const QString& question);
    [[nodiscard]] static bool requiresLocalEventLogs(const QString& question);
    [[nodiscard]] static bool requiresLocalEventCorrelation(const QString& question,
                                                            const AnalysisUpdate& analysis,
                                                            const EventLogUpdate& eventLogs);
    [[nodiscard]] static QString explainBriefingItem(const QString& category, const QString& title,
                                                    const QString& summary, const QString& evidence,
                                                    const QString& nextStep,
                                                    const SystemSnapshot& snapshot,
                                                    const UserPreferences& preferences);
};

} // namespace Ausyn
