#include "insight_engine.h"

#include <algorithm>
#include <cmath>
#include <QStringList>

namespace Ausyn {
namespace {

bool hasFreshProcessSample(const SystemSnapshot& snapshot)
{
    if (!snapshot.processSamplesCapturedAt.isValid()) return false;
    const qint64 ageMilliseconds = snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime());
    const qint64 freshnessLimitSeconds = std::max<qint64>(
        10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
    return ageMilliseconds >= 0 && ageMilliseconds <= freshnessLimitSeconds * 1000;
}

int cpuScore(double averagePercent)
{
    const double value = std::clamp(averagePercent, 0.0, 100.0);
    if (value <= 60.0) return 100;
    if (value <= 85.0) return static_cast<int>(std::lround(100.0 - (value - 60.0) * 0.8));
    if (value <= 95.0) return static_cast<int>(std::lround(80.0 - (value - 85.0) * 4.0));
    return static_cast<int>(std::lround(std::max(0.0, 40.0 - (value - 95.0) * 8.0)));
}

int memoryScore(double averagePercent)
{
    const double value = std::clamp(averagePercent, 0.0, 100.0);
    if (value <= 70.0) return 100;
    if (value <= 90.0) return static_cast<int>(std::lround(100.0 - (value - 70.0)));
    if (value <= 97.0) return static_cast<int>(std::lround(80.0 - (value - 90.0) * 5.0));
    return static_cast<int>(std::lround(std::max(0.0, 45.0 - (value - 97.0) * 15.0)));
}

int storageScore(double freePercent)
{
    const double value = std::clamp(freePercent, 0.0, 100.0);
    if (value >= 30.0) return 100;
    if (value >= 20.0) return static_cast<int>(std::lround(80.0 + (value - 20.0) * 2.0));
    if (value >= 10.0) return static_cast<int>(std::lround(50.0 + (value - 10.0) * 3.0));
    if (value >= 5.0) return static_cast<int>(std::lround(20.0 + (value - 5.0) * 6.0));
    return static_cast<int>(std::lround(value * 4.0));
}

Finding makeFinding(const QString& ruleId,
                    FindingSeverity severity,
                    const QString& title,
                    const QString& summary,
                    const QString& evidence,
                    const QString& recommendation,
                    const QDateTime& now)
{
    Finding finding;
    finding.ruleId = ruleId;
    finding.severity = severity;
    finding.title = title;
    finding.summary = summary;
    finding.evidence = evidence;
    if (ruleId == QStringLiteral("processor-sustained-load") ||
        ruleId == QStringLiteral("memory-sustained-pressure") ||
        ruleId == QStringLiteral("concurrent-cpu-memory-pressure")) {
        finding.confidence = QStringLiteral("High");
        finding.confidenceBasis = QStringLiteral(
            "Repeated measurements support a sustained pressure pattern; they do not establish its cause.");
    } else if (ruleId == QStringLiteral("system-drive-low-space") ||
               ruleId.startsWith(QStringLiteral("fixed-volume-low-space-"))) {
        finding.confidence = QStringLiteral("High");
        finding.confidenceBasis = QStringLiteral(
            "Windows directly reports the current volume capacity; practical impact depends on your workload.");
    } else if (ruleId == QStringLiteral("personal-baseline-resource-shift")) {
        finding.confidence = QStringLiteral("Moderate");
        finding.confidenceBasis = QStringLiteral(
            "Recent local averages are notably above a one-hour-or-longer personal baseline; this identifies a change in workload, not its cause or whether it is unwanted.");
    } else if (ruleId == QStringLiteral("memory-rapid-rise")) {
        finding.confidence = QStringLiteral("Moderate");
        finding.confidenceBasis = QStringLiteral(
            "Two adjacent five-minute local averages show a clear increase; this describes a change in memory use, not a leak or its cause.");
    } else if (ruleId == QStringLiteral("battery-low-charge")) {
        finding.confidence = QStringLiteral("High");
        finding.confidenceBasis = QStringLiteral(
            "Windows reports low charge while the device is not connected to AC power.");
    } else if (ruleId == QStringLiteral("thermal-zone-sustained-passive-point")) {
        finding.confidence = QStringLiteral("Moderate");
        finding.confidenceBasis = QStringLiteral(
            "The reading stayed at or above a firmware-reported ACPI passive trip point for at least two minutes. ACPI zones can be mislabeled and do not reliably identify CPU or GPU die temperature.");
    } else if (ruleId == QStringLiteral("battery-capacity-estimate-decline")) {
        finding.confidence = QStringLiteral("Moderate");
        finding.confidenceBasis = QStringLiteral(
            "Daily means of Windows-reported full-charge/design-capacity ratios show a persistent decrease across at least two weeks. Battery firmware can recalibrate these estimates; this is not a health diagnosis.");
    } else if (ruleId.startsWith(QStringLiteral("storage-device-reliability-"))) {
        finding.confidence = QStringLiteral("Moderate");
        finding.confidenceBasis = QStringLiteral(
            "Windows reports one or more device reliability indicators. Counter semantics and coverage vary by firmware and driver; this does not confirm imminent failure.");
    }
    finding.recommendation = recommendation;
    finding.lastSeen = now;
    return finding;
}

} // namespace

AnalysisResult InsightEngine::analyze(const SystemSnapshot& snapshot,
                                      const MetricAverages& recentAverages,
                                      const MetricAverages& personalBaseline,
                                      const QVector<BatteryHealthTrendPoint>& batteryHealthTrend,
                                      const MetricAverages& precedingWindowAverages)
{
    AnalysisResult result;
    int weightedScore = 0;
    int availableWeight = 0;
    const QDateTime now = snapshot.capturedAt;
    const bool sustainedCpuPressure = recentAverages.processorPercent &&
        recentAverages.processorSamples >= 12 && *recentAverages.processorPercent >= 90.0;
    const bool sustainedMemoryPressure = recentAverages.memoryPercent &&
        recentAverages.memorySamples >= 12 && *recentAverages.memoryPercent >= 90.0;
    const bool rapidMemoryRise = precedingWindowAverages.memoryPercent &&
        precedingWindowAverages.memorySamples >= 12 && recentAverages.memoryPercent &&
        recentAverages.memorySamples >= 12 && *recentAverages.memoryPercent >= 75.0 &&
        *recentAverages.memoryPercent < 90.0 &&
        *recentAverages.memoryPercent - *precedingWindowAverages.memoryPercent >= 12.0 &&
        !sustainedMemoryPressure;

    if (rapidMemoryRise) {
        result.findings.append(makeFinding(
            QStringLiteral("memory-rapid-rise"), FindingSeverity::Information,
            QStringLiteral("Memory use rose quickly"),
            QStringLiteral("The latest five-minute average is substantially above the previous five-minute average. A newly opened workload can explain this change; it does not establish a memory leak."),
            QStringLiteral("Memory averaged %1% across %2 recent samples, up from %3% across %4 samples in the preceding five-minute window (+%5 percentage points).")
                .arg(*recentAverages.memoryPercent, 0, 'f', 1)
                .arg(recentAverages.memorySamples)
                .arg(*precedingWindowAverages.memoryPercent, 0, 'f', 1)
                .arg(precedingWindowAverages.memorySamples)
                .arg(*recentAverages.memoryPercent - *precedingWindowAverages.memoryPercent, 0, 'f', 1),
            QStringLiteral("If the increase was unexpected, review Performance and what was recently opened. Compare another few readings before deciding whether an app needs attention; Ausyn will not close it."),
            now));
    }

    if (recentAverages.processorPercent && recentAverages.processorSamples >= 12) {
        const int componentScore = cpuScore(*recentAverages.processorPercent);
        result.health.components.append({
            QStringLiteral("Processor"), componentScore, 30,
            QStringLiteral("%1% average over recent stored samples; workload context is unknown")
                .arg(*recentAverages.processorPercent, 0, 'f', 0),
        });
        weightedScore += componentScore * 30;
        availableWeight += 30;

        if (*recentAverages.processorPercent >= 90.0 && !sustainedMemoryPressure) {
            const auto severity = *recentAverages.processorPercent >= 97.0
                ? FindingSeverity::Critical : FindingSeverity::Warning;
            result.findings.append(makeFinding(
                QStringLiteral("processor-sustained-load"), severity,
                QStringLiteral("Processor has stayed busy"),
                QStringLiteral("High processor activity has persisted across multiple samples. A demanding task can make this normal."),
                QStringLiteral("Recent average: %1% across %2 stored samples.")
                    .arg(*recentAverages.processorPercent, 0, 'f', 0)
                    .arg(recentAverages.processorSamples),
                QStringLiteral("Compare this with what you were doing. If the load is unexpected, review Performance before closing anything."),
                now));
        }
    }

    if (recentAverages.memoryPercent && recentAverages.memorySamples >= 12) {
        const int componentScore = memoryScore(*recentAverages.memoryPercent);
        result.health.components.append({
            QStringLiteral("Memory"), componentScore, 35,
            QStringLiteral("%1% average over recent stored samples")
                .arg(*recentAverages.memoryPercent, 0, 'f', 0),
        });
        weightedScore += componentScore * 35;
        availableWeight += 35;

        if (*recentAverages.memoryPercent >= 90.0 && !sustainedCpuPressure) {
            const auto severity = *recentAverages.memoryPercent >= 97.0
                ? FindingSeverity::Critical : FindingSeverity::Warning;
            QString evidence = QStringLiteral("Recent average: %1% across %2 stored samples.")
                .arg(*recentAverages.memoryPercent, 0, 'f', 0)
                .arg(recentAverages.memorySamples);
            if (hasFreshProcessSample(snapshot) && !snapshot.topProcesses.isEmpty() &&
                snapshot.topProcesses.first().workingSetBytes) {
                evidence += QStringLiteral(" Largest observed working set: %1 (%2).")
                    .arg(snapshot.topProcesses.first().name)
                    .arg(static_cast<double>(*snapshot.topProcesses.first().workingSetBytes) /
                         1'000'000'000.0, 0, 'f', 1)
                    .append(QStringLiteral(" GB"));
            }
            result.findings.append(makeFinding(
                QStringLiteral("memory-sustained-pressure"), severity,
                QStringLiteral("Memory pressure has stayed high"),
                QStringLiteral("Available physical memory has remained low across multiple samples."),
                evidence,
                QStringLiteral("Open Performance to review the largest processes. Save your work before closing any application."),
                now));
        }
    }

    if (sustainedCpuPressure && sustainedMemoryPressure) {
        const auto severity = *recentAverages.processorPercent >= 97.0 ||
                              *recentAverages.memoryPercent >= 97.0
            ? FindingSeverity::Critical : FindingSeverity::Warning;
        QString evidence = QStringLiteral("CPU average: %1% across %2 stored samples; memory average: %3% across %4 stored samples.")
            .arg(*recentAverages.processorPercent, 0, 'f', 1).arg(recentAverages.processorSamples)
            .arg(*recentAverages.memoryPercent, 0, 'f', 1).arg(recentAverages.memorySamples);
        const ProcessSample* highestCpuProcess = nullptr;
        if (hasFreshProcessSample(snapshot)) {
            for (const ProcessSample& process : snapshot.topProcesses) {
                if (process.cpuPercent && (!highestCpuProcess ||
                    *process.cpuPercent > highestCpuProcess->cpuPercent.value_or(-1.0)))
                    highestCpuProcess = &process;
            }
        }
        if (highestCpuProcess) {
            evidence += QStringLiteral(" Highest measured process CPU in the latest process sample: %1 at %2%.")
                .arg(highestCpuProcess->name,
                     QString::number(*highestCpuProcess->cpuPercent, 'f', 1));
        } else if (hasFreshProcessSample(snapshot)) {
            evidence += QStringLiteral(" Per-process CPU data is unavailable in the latest process sample.");
        }
        result.findings.append(makeFinding(
            QStringLiteral("concurrent-cpu-memory-pressure"), severity,
            QStringLiteral("CPU and memory pressure occurred together"),
            QStringLiteral("Recent stored averages show sustained CPU and memory pressure in the same analysis window. This combination can make the PC feel slow, but it does not establish a single cause."),
            evidence,
            QStringLiteral("Review Performance for the current process list and compare it with what you were doing. The listed process CPU reading is a recent association, not proof that it caused the sustained system load."),
            now));
    }

    QStringList unusualResources;
    if (personalBaseline.processorPercent && personalBaseline.processorSamples >= 360 &&
        recentAverages.processorPercent && recentAverages.processorSamples >= 24 &&
        *recentAverages.processorPercent >= 35.0 &&
        *recentAverages.processorPercent - *personalBaseline.processorPercent >= 25.0 &&
        !sustainedCpuPressure) {
        unusualResources << QStringLiteral("CPU averaged %1% in the last five minutes versus %2% across the prior personal baseline")
            .arg(*recentAverages.processorPercent, 0, 'f', 0)
            .arg(*personalBaseline.processorPercent, 0, 'f', 0);
    }
    if (personalBaseline.memoryPercent && personalBaseline.memorySamples >= 360 &&
        recentAverages.memoryPercent && recentAverages.memorySamples >= 24 &&
        *recentAverages.memoryPercent >= 75.0 &&
        *recentAverages.memoryPercent - *personalBaseline.memoryPercent >= 15.0 &&
        !sustainedMemoryPressure && !rapidMemoryRise) {
        unusualResources << QStringLiteral("Memory averaged %1% in the last five minutes versus %2% across the prior personal baseline")
            .arg(*recentAverages.memoryPercent, 0, 'f', 0)
            .arg(*personalBaseline.memoryPercent, 0, 'f', 0);
    }
    if (!unusualResources.isEmpty()) {
        result.findings.append(makeFinding(
            QStringLiteral("personal-baseline-resource-shift"), FindingSeverity::Information,
            QStringLiteral("Resource use is above this PC’s usual range"),
            QStringLiteral("Recent CPU or memory use is materially higher than this device’s saved personal baseline. A game, update, or other expected workload can explain the change."),
            unusualResources.join(QStringLiteral("; ")) + QStringLiteral(". Baseline requires at least 360 saved samples; the recent window requires at least 24."),
            QStringLiteral("If you weren’t expecting this change, review Performance and what was recently opened. Don’t close unfamiliar processes based on this comparison alone."),
            now));
    }

    if (snapshot.systemVolumeTotalBytes > 0) {
        const quint64 freeBytes = std::min(snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes);
        const double freePercent = 100.0 * static_cast<double>(freeBytes) /
                                  static_cast<double>(snapshot.systemVolumeTotalBytes);
        const int componentScore = storageScore(freePercent);
        result.health.components.append({
            QStringLiteral("System drive"), componentScore, 35,
            QStringLiteral("%1% free on %2")
                .arg(freePercent, 0, 'f', 0)
                .arg(snapshot.systemVolumePath),
        });
        weightedScore += componentScore * 35;
        availableWeight += 35;

        if (freePercent <= 10.0) {
            const auto severity = freePercent <= 5.0
                ? FindingSeverity::Critical : FindingSeverity::Warning;
            result.findings.append(makeFinding(
                QStringLiteral("system-drive-low-space"), severity,
                QStringLiteral("System drive is running low on space"),
                QStringLiteral("Low free space can prevent updates, downloads, and applications from saving files."),
                QStringLiteral("%1 free of %2 on %3.")
                    .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(static_cast<double>(snapshot.systemVolumeTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(snapshot.systemVolumePath),
                QStringLiteral("Review large personal files or unused applications and choose what to remove yourself. Ausyn will not delete files."),
                now));
        }
    }

    for (const VolumeSample& volume : snapshot.volumes) {
        if (volume.totalBytes == 0 || volume.rootPath.compare(snapshot.systemVolumePath, Qt::CaseInsensitive) == 0)
            continue;
        const quint64 freeBytes = std::min(volume.freeBytes, volume.totalBytes);
        const double freePercent = 100.0 * static_cast<double>(freeBytes) /
                                   static_cast<double>(volume.totalBytes);
        if (freePercent > 10.0) continue;
        const QString driveId = volume.rootPath.left(2).toUpper().replace(QLatin1Char(':'), QLatin1Char('-'));
        const auto severity = freePercent <= 5.0 ? FindingSeverity::Critical : FindingSeverity::Warning;
        result.findings.append(makeFinding(
            QStringLiteral("fixed-volume-low-space-%1").arg(driveId), severity,
            QStringLiteral("%1 is running low on space").arg(volume.rootPath),
            QStringLiteral("Low free space can prevent apps on this volume from saving files or completing updates."),
            QStringLiteral("%1 free of %2 on fixed volume %3 (%4% free).")
                .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(volume.totalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(volume.rootPath)
                .arg(freePercent, 0, 'f', 1),
            QStringLiteral("Review large personal files or unused applications on %1 and choose what to remove yourself. Ausyn will not scan or delete files.")
                .arg(volume.rootPath),
            now));
    }

    for (const PhysicalDiskSample& disk : snapshot.physicalDisks) {
        QStringList indicators;
        if (disk.windowsHealthStatus == 1)
            indicators << QStringLiteral("Windows Storage reports Warning health status");
        else if (disk.windowsHealthStatus == 2)
            indicators << QStringLiteral("Windows Storage reports Unhealthy status");
        if (disk.wearPercent && *disk.wearPercent >= 100)
            indicators << QStringLiteral("wear indicator is %1%, the device-reported wear limit").arg(*disk.wearPercent);
        if (disk.uncorrectedReadErrors && *disk.uncorrectedReadErrors > 0)
            indicators << QStringLiteral("%1 uncorrected read error(s) are reported").arg(*disk.uncorrectedReadErrors);
        if (disk.uncorrectedWriteErrors && *disk.uncorrectedWriteErrors > 0)
            indicators << QStringLiteral("%1 uncorrected write error(s) are reported").arg(*disk.uncorrectedWriteErrors);
        if (indicators.isEmpty()) continue;

        const QString model = disk.model.isEmpty() ? QStringLiteral("Physical drive %1").arg(disk.deviceNumber)
                                                    : disk.model.trimmed();
        result.findings.append(makeFinding(
            QStringLiteral("storage-device-reliability-%1").arg(disk.deviceNumber), FindingSeverity::Warning,
            QStringLiteral("Review a drive-reported reliability indicator"),
            QStringLiteral("Windows reports a storage-device indicator that is worth reviewing. It does not by itself prove the drive is failing."),
            QStringLiteral("Physical drive %1 (%2): %3.")
                .arg(disk.deviceNumber).arg(model, indicators.join(QStringLiteral("; "))),
            QStringLiteral("Make sure important files have a current backup. Review the drive maker’s diagnostic guidance; Ausyn does not run repair commands or predict a failure date."),
            now));
    }

    if (availableWeight > 0) {
        result.health.score = static_cast<int>(std::lround(
            static_cast<double>(weightedScore) / static_cast<double>(availableWeight)));
        result.health.coveragePercent = availableWeight;
        result.health.explanation = QStringLiteral(
            "A partial performance and storage summary based on %1 of 3 available signal groups. "
            "Battery condition, thermal sensors, security, and update state are not included.")
            .arg(result.health.components.size());
    } else {
        result.health.explanation = QStringLiteral(
            "Collecting enough local history to compare normal activity with sustained pressure.");
    }

    if (snapshot.batteryPercent && snapshot.batteryOnAcPower && !*snapshot.batteryOnAcPower &&
        *snapshot.batteryPercent <= 15) {
        result.findings.append(makeFinding(
            QStringLiteral("battery-low-charge"), FindingSeverity::Warning,
            QStringLiteral("Battery charge is low"),
            QStringLiteral("The device reports a low battery charge and is not connected to AC power."),
            QStringLiteral("Windows reports %1% charge.").arg(*snapshot.batteryPercent),
            QStringLiteral("Connect power soon and save any work that would be difficult to recover."),
            now));
    }

    const ThermalSensorSample* sustainedThermalZone = nullptr;
    for (const ThermalSensorSample& sensor : snapshot.thermalSensors) {
        if (!sensor.passiveTripPointCelsius || !sensor.secondsAbovePassiveTripPoint ||
            *sensor.secondsAbovePassiveTripPoint < 120 ||
            sensor.temperatureCelsius < *sensor.passiveTripPointCelsius) continue;
        if (!sustainedThermalZone || sensor.temperatureCelsius - *sensor.passiveTripPointCelsius >
            sustainedThermalZone->temperatureCelsius - *sustainedThermalZone->passiveTripPointCelsius)
            sustainedThermalZone = &sensor;
    }
    if (sustainedThermalZone) {
        const ThermalSensorSample& sensor = *sustainedThermalZone;
        const bool atCriticalPoint = sensor.criticalTripPointCelsius &&
            sensor.temperatureCelsius >= *sensor.criticalTripPointCelsius;
        QString evidence = QStringLiteral("%1 reported %2 °C, at or above its passive trip point of %3 °C for at least %4 minutes.")
            .arg(sensor.name)
            .arg(sensor.temperatureCelsius, 0, 'f', 1)
            .arg(*sensor.passiveTripPointCelsius, 0, 'f', 1)
            .arg(*sensor.secondsAbovePassiveTripPoint / 60);
        if (sensor.criticalTripPointCelsius)
            evidence += QStringLiteral(" The reported critical point is %1 °C.")
                .arg(*sensor.criticalTripPointCelsius, 0, 'f', 1);
        result.findings.append(makeFinding(
            QStringLiteral("thermal-zone-sustained-passive-point"),
            atCriticalPoint ? FindingSeverity::Critical : FindingSeverity::Warning,
            atCriticalPoint ? QStringLiteral("A reported thermal zone reached its critical point")
                            : QStringLiteral("A reported thermal zone stayed above its passive point"),
            QStringLiteral("Windows ACPI reported a thermal-zone reading at or above firmware’s passive trip point for at least two minutes. Zone labels and thresholds may not map to CPU or GPU temperatures."),
            evidence,
            atCriticalPoint
                ? QStringLiteral("Save your work, reduce demanding activity, and keep the laptop’s vents unobstructed. If this repeats, check the device maker’s diagnostics; this ACPI reading is not a component-specific temperature.")
                : QStringLiteral("If the workload is unexpected, consider pausing demanding work and keep the vents unobstructed. Recheck the reading; Windows ACPI zones may not represent CPU or GPU die temperature."),
            now));
    }

    if (snapshot.batteryHealthPercent && batteryHealthTrend.size() >= 4) {
        const BatteryHealthTrendPoint& first = batteryHealthTrend.first();
        const BatteryHealthTrendPoint& latest = batteryHealthTrend.last();
        const qint64 observedDays = first.capturedAt.daysTo(latest.capturedAt);
        const qint64 latestAgeDays = latest.capturedAt.daysTo(now);
        const double change = latest.estimatedHealthPercent - first.estimatedHealthPercent;
        if (first.capturedAt.isValid() && latest.capturedAt.isValid() && observedDays >= 14 &&
            latestAgeDays >= 0 && latestAgeDays <= 2 && change <= -5.0) {
            result.findings.append(makeFinding(
                QStringLiteral("battery-capacity-estimate-decline"), FindingSeverity::Information,
                QStringLiteral("Battery capacity estimate has declined across local history"),
                QStringLiteral("Daily Windows-derived full-charge/design-capacity estimates are lower across the observed period. Battery firmware may recalibrate these values, so this change alone does not diagnose battery condition."),
                QStringLiteral("Daily mean changed from %1% to %2% across %3 days (%4 percentage points); %5 valid daily readings were available.")
                    .arg(first.estimatedHealthPercent, 0, 'f', 1)
                    .arg(latest.estimatedHealthPercent, 0, 'f', 1)
                    .arg(observedDays)
                    .arg(change, 0, 'f', 1)
                    .arg(batteryHealthTrend.size()),
                QStringLiteral("Keep watching the daily trend. If the decrease continues, compare with the laptop maker’s battery diagnostics; Ausyn does not estimate a replacement date."),
                now));
        }
    }

    return result;
}

} // namespace Ausyn
