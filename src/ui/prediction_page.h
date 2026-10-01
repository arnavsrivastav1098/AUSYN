#pragma once
#include "../intelligence/insight_types.h"
#include <QWidget>
namespace Ausyn {
class PredictionPage final : public QWidget {
public:
    explicit PredictionPage(QWidget* parent = nullptr);
    void setForecast(const StorageForecast& forecast, const BatteryForecast& batteryForecast,
                     const QVector<VolumeStorageForecast>& volumeForecasts,
                     const MemoryPressureForecast& memoryForecast, int retentionDays);
};
}
