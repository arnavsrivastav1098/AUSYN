#include "prediction_engine.h"

#include <algorithm>
#include <cmath>

namespace Ausyn {

StorageForecast PredictionEngine::analyzeStorage(const QVector<StorageTrendPoint>& observations,
                                                 const QDateTime& now)
{
    StorageForecast result;
    result.confidence = QStringLiteral("Insufficient data");
    if (!now.isValid()) {
        result.explanation = QStringLiteral("Ausyn can’t evaluate storage history without a valid current time.");
        return result;
    }
    result.observations.reserve(observations.size());
    const qint64 latestPlausibleTimestamp = now.toMSecsSinceEpoch() + 5 * 60 * 1000;
    for (const StorageTrendPoint& point : observations) {
        if (!point.capturedAt.isValid() || point.totalBytes == 0 || point.sampleCount <= 0 ||
            !std::isfinite(point.freePercent) || point.freePercent < 0.0 || point.freePercent > 100.0 ||
            point.capturedAt.toMSecsSinceEpoch() > latestPlausibleTimestamp) continue;
        result.observations.append(point);
    }
    std::stable_sort(result.observations.begin(), result.observations.end(),
        [](const StorageTrendPoint& left, const StorageTrendPoint& right) {
            return left.capturedAt < right.capturedAt;
        });
    auto uniqueEnd = std::unique(result.observations.begin(), result.observations.end(),
        [](const StorageTrendPoint& left, const StorageTrendPoint& right) {
            return left.capturedAt.toMSecsSinceEpoch() == right.capturedAt.toMSecsSinceEpoch();
        });
    result.observations.erase(uniqueEnd, result.observations.end());
    const QVector<StorageTrendPoint>& validObservations = result.observations;
    if (validObservations.isEmpty()) {
        result.explanation = QStringLiteral("Ausyn needs several days of local drive history before it can estimate a trend.");
        return result;
    }
    result.currentFreePercent = validObservations.last().freePercent;
    const qint64 observedMilliseconds = validObservations.first().capturedAt.msecsTo(validObservations.last().capturedAt);
    result.observedDays = static_cast<int>(observedMilliseconds / 86'400'000);
    const qint64 latestStorageAgeMilliseconds = validObservations.last().capturedAt.msecsTo(now);
    if (latestStorageAgeMilliseconds < 0) {
        result.explanation = QStringLiteral("The latest storage sample is timestamped in the future, so Ausyn won’t estimate a current trend until the clock and sample times agree.");
        return result;
    }
    if (latestStorageAgeMilliseconds > 48LL * 60 * 60 * 1000) {
        result.explanation = QStringLiteral("The latest storage sample is stale. Open Ausyn and let it collect fresh history before forecasting.");
        return result;
    }
    if (validObservations.size() < 5 || observedMilliseconds < 4LL * 86'400'000) {
        result.explanation = QStringLiteral("Not enough history yet. Ausyn needs at least five daily samples spanning four days.");
        return result;
    }
    for (qsizetype i = 1; i < validObservations.size(); ++i) {
        const double baselineSize = static_cast<double>(validObservations.first().totalBytes);
        const double observedSize = static_cast<double>(validObservations.at(i).totalBytes);
        if (baselineSize > 0.0 && std::abs(observedSize - baselineSize) / baselineSize > 0.02) {
            result.explanation = QStringLiteral("The reported drive capacity changed during this history window, so Ausyn won’t combine those samples into one forecast.");
            return result;
        }
        if (validObservations.at(i - 1).capturedAt.msecsTo(validObservations.at(i).capturedAt) > 36LL * 60 * 60 * 1000) {
            result.explanation = QStringLiteral("The history has large gaps, so a continuous drive-space trend cannot be estimated yet.");
            return result;
        }
    }
    if (result.currentFreePercent <= 10.0) {
        result.state = ForecastState::ThresholdReached;
        result.explanation = QStringLiteral("Free space is already at or below Ausyn’s 10% attention threshold. This is a current reading, not a future estimate.");
        result.confidence = QStringLiteral("Measured");
        return result;
    }

    const StorageTrendPoint& latest = validObservations.last();
    const StorageTrendPoint& previous = validObservations.at(validObservations.size() - 2);
    const qint64 latestInterval = previous.capturedAt.msecsTo(latest.capturedAt);
    const double latestDrop = previous.freePercent - latest.freePercent;
    if (latestInterval >= 18LL * 60 * 60 * 1000 && latestInterval <= 36LL * 60 * 60 * 1000 && latestDrop >= 5.0) {
        result.state = ForecastState::Irregular;
        result.rapidDropDetected = true;
        result.rapidDropPercentagePoints = latestDrop;
        result.rapidDropAt = latest.capturedAt;
        result.explanation = QStringLiteral("Daily drive samples show a sharp %1 percentage-point decrease in free space. Ausyn is treating this as a recent change instead of projecting a depletion date; it cannot identify which files or activity used the space.")
            .arg(latestDrop, 0, 'f', 1);
        result.confidence = QStringLiteral("Measured change");
        return result;
    }

    const qint64 originMs = validObservations.first().capturedAt.toMSecsSinceEpoch();
    double meanX = 0.0;
    double meanY = 0.0;
    QVector<double> x;
    x.reserve(validObservations.size());
    for (const auto& point : validObservations) {
        const double day = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - originMs) / 86'400'000.0;
        x.append(day);
        meanX += day;
        meanY += point.freePercent;
    }
    meanX /= static_cast<double>(validObservations.size());
    meanY /= static_cast<double>(validObservations.size());
    double covariance = 0.0;
    double varianceX = 0.0;
    double totalVariance = 0.0;
    for (qsizetype i = 0; i < validObservations.size(); ++i) {
        const double dx = x.at(i) - meanX;
        const double dy = validObservations.at(i).freePercent - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        totalVariance += dy * dy;
    }
    if (varianceX <= 0.0) {
        result.explanation = QStringLiteral("The recorded dates are too close together to estimate a trend.");
        return result;
    }
    const double leastSquaresSlope = covariance / varianceX;
    double residualVariance = 0.0;
    for (qsizetype i = 0; i < validObservations.size(); ++i) {
        const double predicted = meanY + leastSquaresSlope * (x.at(i) - meanX);
        const double residual = validObservations.at(i).freePercent - predicted;
        residualVariance += residual * residual;
    }
    result.rSquared = totalVariance <= 0.0001 ? 1.0 : std::clamp(1.0 - residualVariance / totalVariance, 0.0, 1.0);
    QVector<double> pairwiseSlopes;
    pairwiseSlopes.reserve(validObservations.size() * (validObservations.size() - 1) / 2);
    for (qsizetype left = 0; left < validObservations.size(); ++left) {
        for (qsizetype right = left + 1; right < validObservations.size(); ++right) {
            const double elapsedDays = x.at(right) - x.at(left);
            if (elapsedDays <= 0.0) continue;
            pairwiseSlopes.append((validObservations.at(right).freePercent -
                                   validObservations.at(left).freePercent) / elapsedDays);
        }
    }
    if (pairwiseSlopes.isEmpty()) {
        result.explanation = QStringLiteral("The recorded drive samples are too close together to estimate a trend.");
        return result;
    }
    std::sort(pairwiseSlopes.begin(), pairwiseSlopes.end());
    const qsizetype middle = pairwiseSlopes.size() / 2;
    result.dailyChangePercentagePoints = pairwiseSlopes.size() % 2 == 0
        ? (pairwiseSlopes.at(middle - 1) + pairwiseSlopes.at(middle)) / 2.0
        : pairwiseSlopes.at(middle);
    if (result.dailyChangePercentagePoints > 0.05) {
        result.state = ForecastState::Growing;
        result.explanation = QStringLiteral("Free space increased over the observed period. No depletion date is estimated.");
        result.confidence = result.observedDays >= 7 ? QStringLiteral("Medium") : QStringLiteral("Low");
        return result;
    }
    if (result.dailyChangePercentagePoints >= -0.05) {
        result.state = ForecastState::Stable;
        result.explanation = QStringLiteral("No sustained decrease in free space was detected over the observed period.");
        result.confidence = result.observedDays >= 7 ? QStringLiteral("Medium") : QStringLiteral("Low");
        return result;
    }
    result.recentDaysConsidered = static_cast<int>(std::min<qsizetype>(3, validObservations.size() - 1));
    for (qsizetype i = validObservations.size() - result.recentDaysConsidered;
         i < validObservations.size(); ++i) {
        const double change = validObservations.at(i - 1).freePercent - validObservations.at(i).freePercent;
        if (change >= 0.1) ++result.recentDecliningDays;
    }
    result.recentDeclineConfirmed = result.recentDaysConsidered == 3 && result.recentDecliningDays >= 2;
    if (!result.recentDeclineConfirmed) {
        result.state = ForecastState::Irregular;
        result.confidence = QStringLiteral("Insufficient");
        result.explanation = QStringLiteral("The robust multi-day trend is downward, but fewer than two of the latest three daily changes lost at least 0.1 percentage point. Ausyn is waiting for recent declines to confirm before estimating a date.");
        return result;
    }
    if (result.rSquared < 0.65) {
        result.state = ForecastState::Irregular;
        result.explanation = QStringLiteral("Free space changed irregularly. The trend is too inconsistent for a useful date estimate.");
        result.confidence = QStringLiteral("Insufficient");
        return result;
    }

    result.state = ForecastState::Declining;
    result.daysUntilTenPercent = (result.currentFreePercent - 10.0) / -result.dailyChangePercentagePoints;
    result.hasEstimate = std::isfinite(result.daysUntilTenPercent) && result.daysUntilTenPercent > 0.0;
    result.confidence = (result.observedDays >= 14 && result.rSquared >= 0.8) ? QStringLiteral("High")
        : (result.observedDays >= 7 && result.rSquared >= 0.65) ? QStringLiteral("Medium")
        : QStringLiteral("Low");
    result.explanation = result.hasEstimate
        ? QStringLiteral("If the observed rate continues, free space could reach 10% in about %1 days. This is a trend-based estimate, not a guarantee.")
            .arg(result.daysUntilTenPercent, 0, 'f', 0)
        : QStringLiteral("Free space is decreasing, but the available samples don’t support a reliable threshold date.");
    if (result.daysUntilTenPercent > 3650.0) {
        result.hasEstimate = false;
        result.explanation = QStringLiteral("A decrease is visible, but the projected 10% threshold is more than ten years away at this rate; Ausyn does not show a date estimate that far out.");
    }
    return result;
}

MemoryPressureForecast PredictionEngine::analyzeMemoryPressure(
    const QVector<MemoryTrendPoint>& observations, const QDateTime& now)
{
    MemoryPressureForecast result;
    if (!now.isValid()) {
        result.explanation = QStringLiteral("Ausyn can’t evaluate a memory trend without a valid current time.");
        return result;
    }
    for (const MemoryTrendPoint& point : observations) {
        if (!point.capturedAt.isValid() || point.sampleCount < 12 ||
            !std::isfinite(point.averagePercent) || point.averagePercent < 0.0 ||
            point.averagePercent > 100.0 || point.capturedAt > now.addSecs(5 * 60)) continue;
        result.observations.append(point);
    }
    std::stable_sort(result.observations.begin(), result.observations.end(),
        [](const MemoryTrendPoint& left, const MemoryTrendPoint& right) {
            return left.capturedAt < right.capturedAt;
        });
    const auto uniqueEnd = std::unique(result.observations.begin(), result.observations.end(),
        [](const MemoryTrendPoint& left, const MemoryTrendPoint& right) {
            return left.capturedAt.toMSecsSinceEpoch() == right.capturedAt.toMSecsSinceEpoch();
        });
    result.observations.erase(uniqueEnd, result.observations.end());
    if (result.observations.isEmpty()) {
        result.explanation = QStringLiteral("Ausyn is collecting enough local memory history for an outlook.");
        return result;
    }
    result.currentPercent = result.observations.last().averagePercent;
    const qint64 latestMemoryAgeMilliseconds = result.observations.last().capturedAt.msecsTo(now);
    if (latestMemoryAgeMilliseconds < 0) {
        result.state = MemoryForecastState::InsufficientData;
        result.explanation = QStringLiteral("The latest memory history is timestamped in the future, so Ausyn won’t project a pressure point until the clock and sample times agree.");
        return result;
    }
    if (latestMemoryAgeMilliseconds > 10 * 60 * 1000) {
        result.state = MemoryForecastState::InsufficientData;
        result.explanation = QStringLiteral("The latest memory history is stale, so Ausyn won’t project a future pressure point.");
        return result;
    }
    if (result.currentPercent >= 90.0) {
        result.state = MemoryForecastState::ThresholdReached;
        result.explanation = QStringLiteral("The latest five-minute memory average is already at Ausyn’s existing 90% pressure threshold. This is a current reading, not a forecast.");
        return result;
    }
    if (result.observations.size() < 5) {
        result.explanation = QStringLiteral("Ausyn needs at least five well-sampled five-minute windows before it can assess a memory trend.");
        return result;
    }

    const qint64 originMs = result.observations.first().capturedAt.toMSecsSinceEpoch();
    const qint64 latestMs = result.observations.last().capturedAt.toMSecsSinceEpoch();
    result.observedMinutes = static_cast<int>((latestMs - originMs) / 60'000);
    if (result.observedMinutes < 20) {
        result.explanation = QStringLiteral("The recent memory windows do not yet span 20 minutes of usable history.");
        return result;
    }
    for (qsizetype i = 1; i < result.observations.size(); ++i) {
        if (result.observations.at(i - 1).capturedAt.msecsTo(result.observations.at(i).capturedAt) > 10LL * 60 * 1000) {
            result.state = MemoryForecastState::Irregular;
            result.explanation = QStringLiteral("Memory history has a large gap, so Ausyn won’t treat the change as a continuous trend.");
            return result;
        }
    }

    double meanX = 0.0;
    double meanY = 0.0;
    QVector<double> x;
    x.reserve(result.observations.size());
    for (const MemoryTrendPoint& point : result.observations) {
        const double minutes = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - originMs) / 60'000.0;
        x.append(minutes);
        meanX += minutes;
        meanY += point.averagePercent;
    }
    meanX /= static_cast<double>(result.observations.size());
    meanY /= static_cast<double>(result.observations.size());
    double covariance = 0.0;
    double varianceX = 0.0;
    double totalVariance = 0.0;
    for (qsizetype i = 0; i < result.observations.size(); ++i) {
        const double dx = x.at(i) - meanX;
        const double dy = result.observations.at(i).averagePercent - meanY;
        covariance += dx * dy;
        varianceX += dx * dx;
        totalVariance += dy * dy;
    }
    if (varianceX <= 0.0) {
        result.explanation = QStringLiteral("The recorded memory windows are too close together to estimate a trend.");
        return result;
    }
    const double slopePerMinute = covariance / varianceX;
    double residualVariance = 0.0;
    for (qsizetype i = 0; i < result.observations.size(); ++i) {
        const double predicted = meanY + slopePerMinute * (x.at(i) - meanX);
        const double residual = result.observations.at(i).averagePercent - predicted;
        residualVariance += residual * residual;
    }
    result.fitQuality = totalVariance <= 0.0001 ? 1.0
        : std::clamp(1.0 - residualVariance / totalVariance, 0.0, 1.0);
    result.fitAvailable = true;
    QVector<double> pairwiseSlopes;
    pairwiseSlopes.reserve(result.observations.size() * (result.observations.size() - 1) / 2);
    for (qsizetype left = 0; left < result.observations.size(); ++left) {
        for (qsizetype right = left + 1; right < result.observations.size(); ++right) {
            const double elapsedMinutes = x.at(right) - x.at(left);
            if (elapsedMinutes <= 0.0) continue;
            pairwiseSlopes.append((result.observations.at(right).averagePercent -
                                   result.observations.at(left).averagePercent) / elapsedMinutes);
        }
    }
    if (pairwiseSlopes.isEmpty()) {
        result.explanation = QStringLiteral("The recorded memory windows are too close together to estimate a trend.");
        return result;
    }
    std::sort(pairwiseSlopes.begin(), pairwiseSlopes.end());
    const qsizetype middle = pairwiseSlopes.size() / 2;
    const double robustSlopePerMinute = pairwiseSlopes.size() % 2 == 0
        ? (pairwiseSlopes.at(middle - 1) + pairwiseSlopes.at(middle)) / 2.0
        : pairwiseSlopes.at(middle);
    result.risePerHour = robustSlopePerMinute * 60.0;
    result.recentWindowsConsidered = static_cast<int>(std::min<qsizetype>(4, result.observations.size() - 1));
    for (qsizetype i = result.observations.size() - result.recentWindowsConsidered;
         i < result.observations.size(); ++i) {
        const double change = result.observations.at(i).averagePercent -
            result.observations.at(i - 1).averagePercent;
        if (change >= 0.1) ++result.recentRisingWindows;
    }
    result.recentRiseConfirmed = result.recentWindowsConsidered == 4 && result.recentRisingWindows >= 3;
    if (robustSlopePerMinute < -0.05) {
        result.state = MemoryForecastState::Declining;
        result.explanation = QStringLiteral("Average memory use has been declining across the recent windows. Ausyn has no supported reason to project rising pressure right now.");
        return result;
    }
    if (robustSlopePerMinute <= 0.05) {
        result.state = MemoryForecastState::Stable;
        result.explanation = QStringLiteral("The recent five-minute memory averages are broadly stable; no near-term pressure estimate is available.");
        return result;
    }
    result.state = MemoryForecastState::Rising;
    if (!result.recentRiseConfirmed) {
        result.state = MemoryForecastState::Irregular;
        result.explanation = QStringLiteral("The overall memory trend is rising, but fewer than three of the latest four windows rose by at least 0.1 percentage point. Ausyn is waiting for the recent direction to confirm before estimating a pressure time.");
        return result;
    }
    if (result.fitQuality < 0.65) {
        result.state = MemoryForecastState::Irregular;
        result.explanation = QStringLiteral("Memory use is rising unevenly. The recent pattern is too irregular for a useful time estimate.");
        return result;
    }
    result.minutesUntil90Percent = (90.0 - result.currentPercent) / robustSlopePerMinute;
    if (!std::isfinite(result.minutesUntil90Percent) || result.minutesUntil90Percent <= 0.0 ||
        result.minutesUntil90Percent > 120.0) {
        result.explanation = QStringLiteral("Memory use is rising across the recent windows, but Ausyn won’t project the 90% pressure threshold beyond the next two hours.");
        return result;
    }
    result.state = MemoryForecastState::EstimateAvailable;
    result.hasEstimate = true;
    result.explanation = QStringLiteral("If the recent five-minute memory trend continues, average use could reach 90% in about %1 minute(s). This is a short-term estimate, not a memory-leak diagnosis.")
        .arg(std::max(1, static_cast<int>(std::lround(result.minutesUntil90Percent))));
    return result;
}

BatteryForecast PredictionEngine::analyzeBattery(const SystemSnapshot& current,
                                                 const QVector<HistoryPoint>& observations,
                                                 const QDateTime& now)
{
    BatteryForecast result;
    if (!current.batteryPercent) {
        result.state = BatteryForecastState::NotApplicable;
        result.explanation = QStringLiteral("No battery charge reading is available on this device.");
        return result;
    }
    if (current.batteryOnAcPower.value_or(true) || current.batteryCharging) {
        result.state = BatteryForecastState::NotApplicable;
        result.explanation = QStringLiteral("Discharge outlook appears while the device is running on battery.");
        return result;
    }
    if (*current.batteryPercent <= 15) {
        result.state = BatteryForecastState::ThresholdReached;
        result.explanation = QStringLiteral("Charge is already at or below 15%. This is a current reading, not a prediction.");
        return result;
    }

    QVector<QPair<qint64, double>> series;
    const qint64 nowMs = now.toMSecsSinceEpoch();
    const qint64 windowStart = nowMs - 3LL * 60 * 60 * 1000;
    for (const HistoryPoint& point : observations) {
        if (!point.batteryPercent) continue;
        const qint64 time = point.capturedAt.toMSecsSinceEpoch();
        if (time >= windowStart && time <= nowMs && *point.batteryPercent <= 100)
            series.append({time, static_cast<double>(*point.batteryPercent)});
    }
    series.append({nowMs, static_cast<double>(*current.batteryPercent)});
    std::sort(series.begin(), series.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    series.erase(std::unique(series.begin(), series.end(), [](const auto& left, const auto& right) {
        return left.first == right.first;
    }), series.end());
    result.observationCount = static_cast<int>(series.size());
    if (series.size() > 1)
        result.observedMinutes = static_cast<int>((series.last().first - series.first().first) / 60'000);

    constexpr qsizetype maxChartPoints = 60;
    result.observations.reserve(std::min(series.size(), maxChartPoints));
    if (series.size() <= maxChartPoints) {
        for (const auto& point : series)
            result.observations.append({QDateTime::fromMSecsSinceEpoch(point.first), point.second});
    } else {
        for (qsizetype i = 0; i < maxChartPoints; ++i) {
            const qsizetype index = i * (series.size() - 1) / (maxChartPoints - 1);
            const auto& point = series.at(index);
            result.observations.append({QDateTime::fromMSecsSinceEpoch(point.first), point.second});
        }
    }

    if (series.size() < 4 || nowMs - series.last().first > 60'000) {
        result.explanation = QStringLiteral("Ausyn needs at least 10 minutes of recent battery history before estimating discharge.");
        return result;
    }
    qsizetype first = series.size() - 1;
    while (first > 0) {
        const auto& older = series.at(first - 1);
        const auto& newer = series.at(first);
        if (newer.first - older.first > 10 * 60 * 1000 || older.second > newer.second + 1.0) break;
        --first;
    }
    QVector<QPair<qint64, double>> discharge;
    for (qsizetype i = first; i < series.size(); ++i) discharge.append(series.at(i));
    result.observationCount = static_cast<int>(discharge.size());
    const qint64 spanMs = discharge.last().first - discharge.first().first;
    result.observedMinutes = static_cast<int>(spanMs / 60'000);
    const double totalDrop = discharge.first().second - discharge.last().second;
    if (discharge.size() < 4 || spanMs < 10 * 60 * 1000 || totalDrop < 2.0) {
        result.state = BatteryForecastState::InsufficientData;
        result.explanation = QStringLiteral("Not enough recent discharge history yet. Ausyn needs a sustained change across at least 10 minutes.");
        return result;
    }

    const qint64 originMs = discharge.first().first;
    double meanHours = 0.0;
    double meanPercent = 0.0;
    QVector<double> x;
    x.reserve(discharge.size());
    for (const auto& point : discharge) {
        const double hours = static_cast<double>(point.first - originMs) / 3'600'000.0;
        x.append(hours);
        meanHours += hours;
        meanPercent += point.second;
    }
    meanHours /= static_cast<double>(discharge.size());
    meanPercent /= static_cast<double>(discharge.size());
    double covariance = 0.0;
    double varianceTime = 0.0;
    double totalVariance = 0.0;
    for (qsizetype i = 0; i < discharge.size(); ++i) {
        const double dx = x.at(i) - meanHours;
        const double dy = discharge.at(i).second - meanPercent;
        covariance += dx * dy;
        varianceTime += dx * dx;
        totalVariance += dy * dy;
    }
    if (varianceTime <= 0.0) {
        result.explanation = QStringLiteral("The recorded battery timestamps are too close together to estimate a trend.");
        return result;
    }
    const double slopePerHour = covariance / varianceTime;
    double residualVariance = 0.0;
    for (qsizetype i = 0; i < discharge.size(); ++i) {
        const double residual = discharge.at(i).second - (meanPercent + slopePerHour * (x.at(i) - meanHours));
        residualVariance += residual * residual;
    }
    result.fitQuality = totalVariance <= 0.0001 ? 1.0
        : std::clamp(1.0 - residualVariance / totalVariance, 0.0, 1.0);
    result.fitAvailable = true;
    if (slopePerHour >= -0.1) {
        result.state = BatteryForecastState::Stable;
        result.explanation = QStringLiteral("No sustained battery discharge rate is visible in the recent history.");
        return result;
    }
    result.dischargePercentPerHour = -slopePerHour;
    if (result.fitQuality < 0.5) {
        result.state = BatteryForecastState::Irregular;
        result.explanation = QStringLiteral("Battery use changed irregularly. Ausyn is showing the observed trend without projecting a time.");
        return result;
    }
    result.minutesUntil15Percent = (static_cast<double>(*current.batteryPercent) - 15.0) /
        result.dischargePercentPerHour * 60.0;
    result.hasEstimate = std::isfinite(result.minutesUntil15Percent) &&
        result.minutesUntil15Percent > 0.0 && result.minutesUntil15Percent <= 24.0 * 60.0;
    result.state = result.hasEstimate ? BatteryForecastState::EstimateAvailable : BatteryForecastState::Irregular;
    result.explanation = result.hasEstimate
        ? QStringLiteral("Recent charge fell by about %1 percentage points per hour. At that rate, 15% could be reached in about %2. Workload and battery conditions can change this estimate.")
            .arg(result.dischargePercentPerHour, 0, 'f', 1)
            .arg(static_cast<int>(std::round(result.minutesUntil15Percent)))
        : QStringLiteral("A discharge trend is visible, but it does not support a useful short-term estimate.");
    return result;
}

} // namespace Ausyn
