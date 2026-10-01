#pragma once

#include "insight_types.h"
#include "../monitoring/system_snapshot.h"

namespace Ausyn {

class PredictionEngine final {
public:
    [[nodiscard]] static StorageForecast analyzeStorage(const QVector<StorageTrendPoint>& observations,
                                                        const QDateTime& now = QDateTime::currentDateTime());
    [[nodiscard]] static MemoryPressureForecast analyzeMemoryPressure(
        const QVector<MemoryTrendPoint>& observations,
        const QDateTime& now = QDateTime::currentDateTime());
    [[nodiscard]] static BatteryForecast analyzeBattery(const SystemSnapshot& current,
                                                        const QVector<HistoryPoint>& observations,
                                                        const QDateTime& now = QDateTime::currentDateTime());
};

} // namespace Ausyn
