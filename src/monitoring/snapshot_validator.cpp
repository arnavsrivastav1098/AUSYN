#include "snapshot_validator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ausyn {
namespace {

void addRecord(SystemSnapshot& snapshot, const QString& metric, const QString& unit,
               const QString& source, MetricQualityState state,
               const QString& observed, const QString& detail)
{
    snapshot.dataQuality.append({metric, unit, source, state, observed, detail});
}

void validatePercent(SystemSnapshot& snapshot, const QString& metric, const QString& source,
                     std::optional<double>& value, const QString& missingDetail)
{
    if (!value) {
        addRecord(snapshot, metric, QStringLiteral("%"), source,
                  metric == QStringLiteral("Processor") ? MetricQualityState::WarmingUp
                                                        : MetricQualityState::Unavailable,
                  QStringLiteral("—"), missingDetail);
        return;
    }
    const double raw = *value;
    if (!std::isfinite(raw) || raw < 0.0 || raw > 100.0) {
        addRecord(snapshot, metric, QStringLiteral("%"), source, MetricQualityState::Invalid,
                  QString::number(raw, 'g', 10), QStringLiteral("Rejected: percentage was outside the valid 0–100 range or was not finite."));
        value.reset();
        return;
    }
    addRecord(snapshot, metric, QStringLiteral("%"), source, MetricQualityState::Valid,
              QStringLiteral("%1%").arg(raw, 0, 'f', 1), QStringLiteral("Within the valid percentage range."));
}

void validateRate(SystemSnapshot& snapshot, const QString& metric, const QString& source,
                  std::optional<double>& value)
{
    if (!value) {
        addRecord(snapshot, metric, QStringLiteral("bytes/s"), source, MetricQualityState::Unavailable,
                  QStringLiteral("—"), snapshot.networkNote.isEmpty()
                    ? QStringLiteral("Windows has not provided a rate sample yet.") : snapshot.networkNote);
        return;
    }
    const double raw = *value;
    constexpr double kMaximumPlausibleBytesPerSecond = 1.0e12;
    if (!std::isfinite(raw) || raw < 0.0 || raw > kMaximumPlausibleBytesPerSecond) {
        addRecord(snapshot, metric, QStringLiteral("bytes/s"), source, MetricQualityState::Invalid,
                  QString::number(raw, 'g', 10), QStringLiteral("Rejected: rate was negative, non-finite, or outside the supported sanity bound."));
        value.reset();
        return;
    }
    addRecord(snapshot, metric, QStringLiteral("bytes/s"), source, MetricQualityState::Valid,
              QString::number(raw, 'f', 1), QStringLiteral("Non-negative Windows byte-rate delta."));
}

QString bytesValue(quint64 value)
{
    return QString::number(value) + QStringLiteral(" bytes");
}

} // namespace

bool SnapshotValidator::validate(SystemSnapshot& snapshot)
{
    snapshot.dataQuality.clear();
    if (!snapshot.capturedAt.isValid()) {
        addRecord(snapshot, QStringLiteral("Snapshot time"), QStringLiteral("timestamp"),
                  QStringLiteral("QDateTime"), MetricQualityState::Invalid,
                  QStringLiteral("Invalid"), QStringLiteral("Snapshot rejected: the capture timestamp is invalid."));
        return false;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime captured = snapshot.capturedAt.toUTC();
    if (captured > now.addSecs(30)) {
        addRecord(snapshot, QStringLiteral("Snapshot time"), QStringLiteral("timestamp"),
                  QStringLiteral("QDateTime"), MetricQualityState::Invalid,
                  snapshot.capturedAt.toString(Qt::ISODate), QStringLiteral("Snapshot rejected: capture time is more than 30 seconds in the future."));
        return false;
    }
    const qint64 ageMilliseconds = captured.msecsTo(now);
    addRecord(snapshot, QStringLiteral("Snapshot time"), QStringLiteral("timestamp"),
              QStringLiteral("QDateTime"), ageMilliseconds > 30'000 ? MetricQualityState::Stale : MetricQualityState::Valid,
              snapshot.capturedAt.toString(Qt::ISODate), ageMilliseconds > 30'000
                ? QStringLiteral("Capture time is more than 30 seconds old.")
                : QStringLiteral("Capture time is current."));
    if (snapshot.collectionDurationMicroseconds >= 0 && snapshot.collectionDurationMicroseconds <= 60'000'000) {
        addRecord(snapshot, QStringLiteral("System collection latency"), QStringLiteral("µs"),
                  QStringLiteral("steady_clock"), MetricQualityState::Valid,
                  QString::number(snapshot.collectionDurationMicroseconds),
                  QStringLiteral("Elapsed time for a complete Windows system-collection pass; excludes UI rendering and database work."));
    } else {
        addRecord(snapshot, QStringLiteral("System collection latency"), QStringLiteral("µs"),
                  QStringLiteral("steady_clock"), MetricQualityState::Invalid,
                  QString::number(snapshot.collectionDurationMicroseconds),
                  QStringLiteral("Collection duration was outside the supported range."));
        snapshot.collectionDurationMicroseconds = 0;
    }

    validatePercent(snapshot, QStringLiteral("Processor"), QStringLiteral("GetSystemTimes"),
                    snapshot.processorUsagePercent, QStringLiteral("Waiting for the first interval to establish a baseline."));
    if (snapshot.processorCores.isEmpty()) {
        addRecord(snapshot, QStringLiteral("Per-core processor load"), QStringLiteral("%"),
                  QStringLiteral("PDH Processor Information counters"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not provide logical-processor performance counters."));
    } else {
        for (ProcessorCoreSample& core : snapshot.processorCores) {
            const QString name = core.name.isEmpty() ? QStringLiteral("Unknown logical processor") : core.name;
            if (!core.utilizationPercent) {
                addRecord(snapshot, name, QStringLiteral("%"),
                    QStringLiteral("PDH Processor Information counters"), MetricQualityState::WarmingUp,
                    QStringLiteral("—"), QStringLiteral("The performance counter has not produced a valid interval yet."));
            } else if (!std::isfinite(*core.utilizationPercent) ||
                       *core.utilizationPercent < 0.0 || *core.utilizationPercent > 100.0) {
                addRecord(snapshot, name, QStringLiteral("%"),
                    QStringLiteral("PDH Processor Information counters"), MetricQualityState::Invalid,
                    QString::number(*core.utilizationPercent, 'g', 10),
                    QStringLiteral("Rejected: logical-processor utilization was outside 0–100%."));
                core.utilizationPercent.reset();
            } else {
                addRecord(snapshot, name, QStringLiteral("%"),
                    QStringLiteral("PDH Processor Information counters"), MetricQualityState::Valid,
                    QStringLiteral("%1%").arg(*core.utilizationPercent, 0, 'f', 1),
                    QStringLiteral("Windows logical-processor utilization for this counter interval."));
            }
        }
    }
    validatePercent(snapshot, QStringLiteral("Graphics engine"), QStringLiteral("PDH GPU Engine counters"),
                    snapshot.graphicsUsagePercent, snapshot.graphicsName.isEmpty()
                        ? QStringLiteral("No physical graphics adapter is available.")
                        : QStringLiteral("Windows did not provide a usable GPU engine counter."));
    if (snapshot.graphicsAdapters.isEmpty()) {
        addRecord(snapshot, QStringLiteral("GPU local memory budget"), QStringLiteral("bytes"),
                  QStringLiteral("IDXGIAdapter3::QueryVideoMemoryInfo"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("No physical graphics adapter was exposed by DXGI."));
    } else {
        for (const GraphicsAdapterSample& adapter : snapshot.graphicsAdapters) {
            const QString metric = QStringLiteral("%1 local GPU memory").arg(adapter.name);
            if (adapter.localMemoryUsageBytes && adapter.localMemoryBudgetBytes && *adapter.localMemoryBudgetBytes > 0) {
                addRecord(snapshot, metric, QStringLiteral("bytes"),
                    QStringLiteral("IDXGIAdapter3::QueryVideoMemoryInfo"), MetricQualityState::Valid,
                    QStringLiteral("%1 used / %2 budget")
                        .arg(*adapter.localMemoryUsageBytes).arg(*adapter.localMemoryBudgetBytes),
                    QStringLiteral("The budget is a dynamic Windows allocation limit, not the adapter’s physical VRAM capacity. Usage can exceed it."));
            } else {
                addRecord(snapshot, metric, QStringLiteral("bytes"),
                    QStringLiteral("IDXGIAdapter3::QueryVideoMemoryInfo"), MetricQualityState::Unavailable,
                    QStringLiteral("—"), QStringLiteral("Windows did not expose current local-memory usage and budget for this adapter."));
            }
        }
    }

    if (snapshot.memoryTotalBytes == 0 || snapshot.memoryUsedBytes > snapshot.memoryTotalBytes ||
        snapshot.memoryAvailableBytes > snapshot.memoryTotalBytes) {
        const QString raw = QStringLiteral("%1 / %2")
            .arg(bytesValue(snapshot.memoryUsedBytes), bytesValue(snapshot.memoryTotalBytes));
        addRecord(snapshot, QStringLiteral("Physical memory"), QStringLiteral("bytes"),
                  QStringLiteral("GlobalMemoryStatusEx"), snapshot.memoryTotalBytes == 0
                    ? MetricQualityState::Unavailable : MetricQualityState::Invalid,
                  raw, snapshot.memoryTotalBytes == 0
                    ? QStringLiteral("Windows did not provide a non-zero physical memory total.")
                    : QStringLiteral("Rejected: used physical memory exceeded the reported total."));
        snapshot.memoryUsedBytes = 0;
        snapshot.memoryAvailableBytes = 0;
        snapshot.memoryTotalBytes = 0;
        snapshot.memorySystemCacheBytes.reset();
        snapshot.memoryUsagePercent.reset();
    } else {
        validatePercent(snapshot, QStringLiteral("Memory pressure"), QStringLiteral("GlobalMemoryStatusEx"),
                        snapshot.memoryUsagePercent, QStringLiteral("Windows did not provide memory pressure."));
        addRecord(snapshot, QStringLiteral("Physical memory"), QStringLiteral("bytes"),
                  QStringLiteral("GlobalMemoryStatusEx"), MetricQualityState::Valid,
                  QStringLiteral("%1 used / %2 total")
                      .arg(bytesValue(snapshot.memoryUsedBytes), bytesValue(snapshot.memoryTotalBytes)),
                  QStringLiteral("Used amount does not exceed total physical memory."));
        addRecord(snapshot, QStringLiteral("Available physical memory"), QStringLiteral("bytes"),
                  QStringLiteral("GlobalMemoryStatusEx"), MetricQualityState::Valid,
                  bytesValue(snapshot.memoryAvailableBytes),
                  QStringLiteral("Immediately available physical memory reported by Windows."));
    }
    const bool cacheWithinBound = snapshot.memorySystemCacheBytes && snapshot.memoryTotalBytes > 0 &&
        (snapshot.memoryTotalBytes > std::numeric_limits<quint64>::max() / 4 ||
         *snapshot.memorySystemCacheBytes <= snapshot.memoryTotalBytes * 4);
    if (cacheWithinBound) {
        addRecord(snapshot, QStringLiteral("System cache"), QStringLiteral("bytes"),
                  QStringLiteral("GetPerformanceInfo"), MetricQualityState::Valid,
                  bytesValue(*snapshot.memorySystemCacheBytes),
                  QStringLiteral("Windows-reported system cache. This is not free memory and may be reclaimed by Windows."));
    } else {
        if (snapshot.memorySystemCacheBytes) {
            addRecord(snapshot, QStringLiteral("System cache"), QStringLiteral("bytes"),
                      QStringLiteral("GetPerformanceInfo"), MetricQualityState::Invalid,
                      bytesValue(*snapshot.memorySystemCacheBytes),
                      QStringLiteral("Rejected: reported system cache exceeded the supported sanity bound."));
            snapshot.memorySystemCacheBytes.reset();
        } else {
            addRecord(snapshot, QStringLiteral("System cache"), QStringLiteral("bytes"),
                      QStringLiteral("GetPerformanceInfo"), MetricQualityState::Unavailable,
                      QStringLiteral("—"), QStringLiteral("Windows did not provide a system-cache reading."));
        }
    }

    const auto invalidVolume = std::find_if(snapshot.volumes.cbegin(), snapshot.volumes.cend(),
        [](const VolumeSample& volume) { return volume.totalBytes == 0 || volume.freeBytes > volume.totalBytes; });
    if (invalidVolume != snapshot.volumes.cend()) {
        addRecord(snapshot, QStringLiteral("Fixed-volume capacity"), QStringLiteral("bytes"),
                  QStringLiteral("GetDiskFreeSpaceExW"), MetricQualityState::Invalid,
                  QStringLiteral("%1 free / %2 total on %3")
                      .arg(bytesValue(invalidVolume->freeBytes), bytesValue(invalidVolume->totalBytes), invalidVolume->rootPath),
                  QStringLiteral("Invalid volume entries were excluded from the current inventory."));
        auto invalidEnd = std::remove_if(snapshot.volumes.begin(), snapshot.volumes.end(), [](const VolumeSample& volume) {
            return volume.totalBytes == 0 || volume.freeBytes > volume.totalBytes;
        });
        snapshot.volumes.erase(invalidEnd, snapshot.volumes.end());
    }
    if (snapshot.systemVolumeTotalBytes == 0 || snapshot.systemVolumeFreeBytes > snapshot.systemVolumeTotalBytes) {
        if (snapshot.systemVolumeTotalBytes > 0 || snapshot.systemVolumeFreeBytes > 0) {
            addRecord(snapshot, QStringLiteral("System-volume capacity"), QStringLiteral("bytes"),
                      QStringLiteral("GetDiskFreeSpaceExW"), MetricQualityState::Invalid,
                      QStringLiteral("%1 free / %2 total")
                          .arg(bytesValue(snapshot.systemVolumeFreeBytes), bytesValue(snapshot.systemVolumeTotalBytes)),
                      QStringLiteral("Rejected: free space exceeded the reported volume total."));
        } else {
            addRecord(snapshot, QStringLiteral("System-volume capacity"), QStringLiteral("bytes"),
                      QStringLiteral("GetDiskFreeSpaceExW"), MetricQualityState::Unavailable,
                      QStringLiteral("—"), QStringLiteral("No fixed system volume was reported."));
        }
        snapshot.systemVolumePath.clear();
        snapshot.systemVolumeTotalBytes = 0;
        snapshot.systemVolumeFreeBytes = 0;
    } else {
        addRecord(snapshot, QStringLiteral("System-volume capacity"), QStringLiteral("bytes"),
                  QStringLiteral("GetDiskFreeSpaceExW"), MetricQualityState::Valid,
                  QStringLiteral("%1 free / %2 total")
                      .arg(bytesValue(snapshot.systemVolumeFreeBytes), bytesValue(snapshot.systemVolumeTotalBytes)),
                  QStringLiteral("Free space does not exceed the volume capacity."));
    }

    const auto validateDiskRate = [&snapshot](const QString& metric, std::optional<double>& rate) {
        constexpr double kMaximumPlausibleBytesPerSecond = 1.0e12;
        if (!rate) {
            addRecord(snapshot, metric, QStringLiteral("bytes/s"), QStringLiteral("PDH PhysicalDisk(_Total)"),
                      MetricQualityState::Unavailable, QStringLiteral("—"),
                      QStringLiteral("Windows did not provide a valid disk-rate sample yet."));
            return;
        }
        if (!std::isfinite(*rate) || *rate < 0.0 || *rate > kMaximumPlausibleBytesPerSecond) {
            addRecord(snapshot, metric, QStringLiteral("bytes/s"), QStringLiteral("PDH PhysicalDisk(_Total)"),
                      MetricQualityState::Invalid, QString::number(*rate, 'g', 10),
                      QStringLiteral("Rejected: disk rate was non-finite, negative, or above the supported sanity bound."));
            rate.reset();
            return;
        }
        addRecord(snapshot, metric, QStringLiteral("bytes/s"), QStringLiteral("PDH PhysicalDisk(_Total)"),
                  MetricQualityState::Valid, QString::number(*rate, 'f', 1),
                  QStringLiteral("Windows-reported aggregate physical-disk throughput across all disks."));
    };
    validateDiskRate(QStringLiteral("Disk read throughput"), snapshot.diskActivity.readBytesPerSecond);
    validateDiskRate(QStringLiteral("Disk write throughput"), snapshot.diskActivity.writeBytesPerSecond);

    if (!snapshot.batteryPercent) {
        addRecord(snapshot, QStringLiteral("Battery charge"), QStringLiteral("%"),
                  QStringLiteral("GetSystemPowerStatus"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not report a battery charge value."));
    } else if (*snapshot.batteryPercent > 100) {
        addRecord(snapshot, QStringLiteral("Battery charge"), QStringLiteral("%"),
                  QStringLiteral("GetSystemPowerStatus"), MetricQualityState::Invalid,
                  QString::number(*snapshot.batteryPercent), QStringLiteral("Rejected: battery charge exceeded 100%."));
        snapshot.batteryPercent.reset();
    } else {
        addRecord(snapshot, QStringLiteral("Battery charge"), QStringLiteral("%"),
                  QStringLiteral("GetSystemPowerStatus"), MetricQualityState::Valid,
                  QStringLiteral("%1%").arg(*snapshot.batteryPercent), QStringLiteral("Windows-reported charge."));
    }
    if (snapshot.batteryRemainingCapacityMwh && snapshot.batteryMaximumCapacityMwh &&
        *snapshot.batteryRemainingCapacityMwh > *snapshot.batteryMaximumCapacityMwh) {
        addRecord(snapshot, QStringLiteral("Battery capacity"), QStringLiteral("mWh"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Invalid,
                  QStringLiteral("%1 / %2")
                      .arg(*snapshot.batteryRemainingCapacityMwh).arg(*snapshot.batteryMaximumCapacityMwh),
                  QStringLiteral("Rejected: remaining capacity exceeded Windows-reported maximum capacity."));
        snapshot.batteryRemainingCapacityMwh.reset();
        snapshot.batteryMaximumCapacityMwh.reset();
    } else if (snapshot.batteryRemainingCapacityMwh && snapshot.batteryMaximumCapacityMwh) {
        addRecord(snapshot, QStringLiteral("Battery capacity"), QStringLiteral("mWh"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Valid,
                  QStringLiteral("%1 / %2")
                      .arg(*snapshot.batteryRemainingCapacityMwh).arg(*snapshot.batteryMaximumCapacityMwh),
                  QStringLiteral("Windows-reported remaining and maximum capacity; not a battery-health score."));
    } else {
        addRecord(snapshot, QStringLiteral("Battery capacity"), QStringLiteral("mWh"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not expose capacity values."));
    }
    if (snapshot.batteryDesignCapacityMwh && snapshot.batteryFullChargeCapacityMwh &&
        *snapshot.batteryDesignCapacityMwh > 0) {
        const double health = 100.0 * static_cast<double>(*snapshot.batteryFullChargeCapacityMwh) /
                              static_cast<double>(*snapshot.batteryDesignCapacityMwh);
        if (std::isfinite(health) && health > 0.0 && health <= 200.0) {
            snapshot.batteryHealthPercent = health;
            addRecord(snapshot, QStringLiteral("Estimated battery capacity health"), QStringLiteral("%"),
                      QStringLiteral("ROOT\\WMI BatteryStaticData + BatteryFullChargedCapacity"),
                      MetricQualityState::Estimated,
                      QStringLiteral("%1% (%2 / %3 mWh)").arg(health, 0, 'f', 1)
                          .arg(*snapshot.batteryFullChargeCapacityMwh).arg(*snapshot.batteryDesignCapacityMwh),
                      QStringLiteral("Estimate: reported full-charge capacity divided by reported design capacity. It can be unavailable, vendor-dependent, or exceed 100%; it is not a battery diagnostic."));
        } else {
            snapshot.batteryDesignCapacityMwh.reset();
            snapshot.batteryFullChargeCapacityMwh.reset();
            snapshot.batteryHealthPercent.reset();
            addRecord(snapshot, QStringLiteral("Estimated battery capacity health"), QStringLiteral("%"),
                      QStringLiteral("ROOT\\WMI battery capacity classes"), MetricQualityState::Invalid,
                      QString::number(health, 'g', 10),
                      QStringLiteral("Rejected: the capacity ratio was outside the supported 0–200% sanity range."));
        }
    } else {
        snapshot.batteryHealthPercent.reset();
        addRecord(snapshot, QStringLiteral("Estimated battery capacity health"), QStringLiteral("%"),
                  QStringLiteral("ROOT\\WMI battery capacity classes"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows or the battery driver did not expose matched design and full-charge capacities."));
    }
    if (snapshot.batteryEstimatedSeconds) {
        addRecord(snapshot, QStringLiteral("Battery runtime"), QStringLiteral("seconds"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Estimated,
                  QString::number(*snapshot.batteryEstimatedSeconds),
                  QStringLiteral("Runtime is an operating-system estimate, not a direct measurement."));
    } else {
        addRecord(snapshot, QStringLiteral("Battery runtime"), QStringLiteral("seconds"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not provide an estimated runtime."));
    }

    if (snapshot.batteryRateMilliwatts) {
        constexpr qint64 kMaximumPlausibleRateMilliwatts = 10'000'000;
        const qint64 raw = *snapshot.batteryRateMilliwatts;
        if (raw < -kMaximumPlausibleRateMilliwatts || raw > kMaximumPlausibleRateMilliwatts) {
            addRecord(snapshot, QStringLiteral("Battery power rate"), QStringLiteral("mW"),
                      QStringLiteral("CallNtPowerInformation"), MetricQualityState::Invalid,
                      QString::number(raw), QStringLiteral("Rejected: rate exceeded the supported plausibility bound."));
            snapshot.batteryRateMilliwatts.reset();
        } else {
            addRecord(snapshot, QStringLiteral("Battery power rate"), QStringLiteral("mW"),
                      QStringLiteral("CallNtPowerInformation"), MetricQualityState::Valid,
                      QString::number(raw), QStringLiteral("Signed Windows-reported rate; positive/negative direction is preserved."));
        }
    } else {
        addRecord(snapshot, QStringLiteral("Battery power rate"), QStringLiteral("mW"),
                  QStringLiteral("CallNtPowerInformation"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not expose a power-rate value."));
    }

    validateRate(snapshot, QStringLiteral("Network receive rate"), QStringLiteral("GetIfTable2"),
                 snapshot.networkReceiveBytesPerSecond);
    validateRate(snapshot, QStringLiteral("Network send rate"), QStringLiteral("GetIfTable2"),
                 snapshot.networkSendBytesPerSecond);
    addRecord(snapshot, QStringLiteral("Active physical network adapters"), QStringLiteral("adapters"),
              QStringLiteral("GetIfTable2"), snapshot.activeNetworkAdapters.isEmpty()
                ? MetricQualityState::Unavailable : MetricQualityState::Valid,
              QString::number(snapshot.activeNetworkAdapters.size()),
              snapshot.activeNetworkAdapters.isEmpty()
                ? QStringLiteral("No active physical adapter was reported.")
                : QStringLiteral("Active hardware interfaces; loopback, tunnel, and virtual interfaces are excluded."));
    const auto validateConnectionCount = [&snapshot](const QString& metric, const QString& source,
            std::optional<quint64>& count, const QString& meaning) {
        if (count) {
            addRecord(snapshot, metric, QStringLiteral("entries"), source, MetricQualityState::Valid,
                      QString::number(*count), meaning);
        } else {
            addRecord(snapshot, metric, QStringLiteral("entries"), source, MetricQualityState::Unavailable,
                      QStringLiteral("—"), QStringLiteral("Windows did not provide the connection table."));
        }
    };
    validateConnectionCount(QStringLiteral("TCP table entries"), QStringLiteral("GetExtendedTcpTable"),
        snapshot.networkTcpEntryCount, QStringLiteral("Includes listening and established IPv4/IPv6 TCP rows."));
    validateConnectionCount(QStringLiteral("UDP endpoints"), QStringLiteral("GetExtendedUdpTable"),
        snapshot.networkUdpEndpointCount, QStringLiteral("IPv4/IPv6 UDP endpoint count; not a measure of bytes transferred."));
    if (snapshot.thermalSensors.isEmpty()) {
        addRecord(snapshot, QStringLiteral("ACPI thermal sensors"), QStringLiteral("°C"),
                  QStringLiteral("MSAcpi_ThermalZoneTemperature"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), snapshot.thermalSensorStatus.isEmpty()
                    ? QStringLiteral("Windows did not expose an ACPI thermal-zone reading; CPU/GPU die temperatures may require vendor-specific sensors.")
                    : snapshot.thermalSensorStatus);
    } else {
        auto invalidEnd = std::remove_if(snapshot.thermalSensors.begin(), snapshot.thermalSensors.end(),
            [&snapshot](ThermalSensorSample& sensor) {
                if (std::isfinite(sensor.temperatureCelsius) &&
                    sensor.temperatureCelsius >= -100.0 && sensor.temperatureCelsius <= 250.0) {
                    addRecord(snapshot, sensor.name, QStringLiteral("°C"), sensor.source,
                        MetricQualityState::Valid,
                        QStringLiteral("%1 °C").arg(sensor.temperatureCelsius, 0, 'f', 1),
                        QStringLiteral("Windows ACPI thermal-zone value normalized from tenths of kelvin. Unusual but physically plausible readings are preserved."));
                    const auto validateTripPoint = [&snapshot, &sensor](const QString& label,
                            std::optional<double>& value) {
                        if (!value) {
                            addRecord(snapshot, QStringLiteral("%1 trip point").arg(label), QStringLiteral("°C"),
                                QStringLiteral("MSAcpi_ThermalZoneTemperature"), MetricQualityState::Unavailable,
                                QStringLiteral("—"), QStringLiteral("Firmware did not expose this thermal-zone trip point."));
                            return;
                        }
                        if (!std::isfinite(*value) || *value < -100.0 || *value > 250.0) {
                            addRecord(snapshot, QStringLiteral("%1 trip point").arg(label), QStringLiteral("°C"),
                                QStringLiteral("MSAcpi_ThermalZoneTemperature"), MetricQualityState::Invalid,
                                QString::number(*value, 'g', 10),
                                QStringLiteral("Rejected: firmware-reported trip point was outside the broad physical sanity range."));
                            value.reset();
                            return;
                        }
                        addRecord(snapshot, QStringLiteral("%1 · %2 trip point").arg(sensor.name, label),
                            QStringLiteral("°C"), QStringLiteral("MSAcpi_ThermalZoneTemperature"),
                            MetricQualityState::Estimated, QStringLiteral("%1 °C").arg(*value, 0, 'f', 1),
                            QStringLiteral("Firmware-defined zone policy; it may not describe a CPU/GPU die limit and is not treated as a universal warning threshold."));
                    };
                    validateTripPoint(QStringLiteral("Passive cooling"), sensor.passiveTripPointCelsius);
                    validateTripPoint(QStringLiteral("Critical"), sensor.criticalTripPointCelsius);
                    if (sensor.secondsAbovePassiveTripPoint) {
                        addRecord(snapshot, QStringLiteral("%1 time at/above passive point").arg(sensor.name),
                            QStringLiteral("seconds"), QStringLiteral("steady_clock + ACPI passive trip point"),
                            MetricQualityState::Estimated, QString::number(*sensor.secondsAbovePassiveTripPoint),
                            QStringLiteral("Duration is tracked in memory between Windows ACPI refreshes and resets after the reading falls below the reported point."));
                    }
                    return false;
                }
                addRecord(snapshot, sensor.name, QStringLiteral("°C"), sensor.source,
                    MetricQualityState::Invalid, QString::number(sensor.temperatureCelsius, 'f', 1),
                    QStringLiteral("Rejected: temperature was non-finite or outside the broad physical sanity range."));
                return true;
            });
        snapshot.thermalSensors.erase(invalidEnd, snapshot.thermalSensors.end());
    }
    if (snapshot.fans.isEmpty()) {
        addRecord(snapshot, QStringLiteral("Cooling fan telemetry"), QStringLiteral("RPM / state"),
                  QStringLiteral("Win32_Fan (ROOT\\CIMV2)"), MetricQualityState::Unavailable,
                  QStringLiteral("—"), QStringLiteral("Windows did not expose fan instances; this does not mean the device has no fans."));
    } else {
        for (const FanSample& fan : snapshot.fans) {
            const QString name = fan.name.isEmpty() ? QStringLiteral("Cooling fan") : fan.name;
            if (fan.requestedSpeedRpm) {
                addRecord(snapshot, QStringLiteral("%1 requested fan speed").arg(name), QStringLiteral("RPM"),
                    QStringLiteral("Win32_Fan.DesiredSpeed"), MetricQualityState::Estimated,
                    QString::number(*fan.requestedSpeedRpm),
                    QStringLiteral("This is the currently requested target, not a confirmed tachometer measurement."));
            } else {
                addRecord(snapshot, QStringLiteral("%1 requested fan speed").arg(name), QStringLiteral("RPM"),
                    QStringLiteral("Win32_Fan.DesiredSpeed"), MetricQualityState::Unavailable,
                    QStringLiteral("—"), QStringLiteral("The provider did not expose a requested fan speed."));
            }
            addRecord(snapshot, QStringLiteral("%1 active cooling").arg(name), QStringLiteral("state"),
                QStringLiteral("Win32_Fan.ActiveCooling"), fan.activeCooling
                    ? MetricQualityState::Valid : MetricQualityState::Unavailable,
                fan.activeCooling ? (*fan.activeCooling ? QStringLiteral("active") : QStringLiteral("inactive"))
                                  : QStringLiteral("—"),
                fan.activeCooling ? QStringLiteral("Windows-reported active-cooling state.")
                                  : QStringLiteral("The provider did not expose active-cooling state."));
            if (fan.previousRequestedSpeedRpm && fan.requestedSpeedRpm) {
                addRecord(snapshot, QStringLiteral("%1 fan target change").arg(name), QStringLiteral("RPM"),
                    QStringLiteral("Win32_Fan.DesiredSpeed"), MetricQualityState::Valid,
                    QStringLiteral("%1 → %2").arg(*fan.previousRequestedSpeedRpm).arg(*fan.requestedSpeedRpm),
                    QStringLiteral("Change between consecutive Windows fan-provider samples; it is a requested target change."));
            }
        }
    }
    addRecord(snapshot, QStringLiteral("Process sample"), QStringLiteral("processes"),
              QStringLiteral("Toolhelp / PSAPI"), snapshot.topProcesses.isEmpty()
                ? MetricQualityState::Unavailable : MetricQualityState::Valid,
              QString::number(snapshot.topProcesses.size()), snapshot.topProcesses.isEmpty()
                ? QStringLiteral("No process list was returned in this sample.")
                : QStringLiteral("Process access is limited to data available to the current account."));
    return true;
}

} // namespace Ausyn
