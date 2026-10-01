#pragma once

#include "insight_types.h"
#include "../monitoring/system_snapshot.h"

namespace Ausyn {

struct AnalysisResult {
    HealthAssessment health;
    QVector<Finding> findings;
};

class InsightEngine final {
public:
    [[nodiscard]] static AnalysisResult analyze(const SystemSnapshot& snapshot,
                                                const MetricAverages& recentAverages,
                                                const MetricAverages& personalBaseline = {},
                                                const QVector<BatteryHealthTrendPoint>& batteryHealthTrend = {},
                                                const MetricAverages& precedingWindowAverages = {});
};

} // namespace Ausyn
