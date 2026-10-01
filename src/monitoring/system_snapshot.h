#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QStringList>

#include <optional>

namespace Ausyn {

enum class MetricQualityState : int {
    Valid,
    Estimated,
    WarmingUp,
    Unavailable,
    Stale,
    Invalid,
    Error,
};

struct MetricQualityRecord {
    QString metric;
    QString unit;
    QString source;
    MetricQualityState state = MetricQualityState::Unavailable;
    QString observedValue;
    QString detail;
};

struct ProcessSample {
    quint32 processId = 0;
    QString name;
    QString executablePath;
    std::optional<double> cpuPercent;
    std::optional<quint64> workingSetBytes;
    std::optional<double> readIoBytesPerSecond;
    std::optional<double> writeIoBytesPerSecond;
    std::optional<quint32> tcpConnectionCount;
    std::optional<quint32> udpEndpointCount;
};

struct VolumeSample {
    QString rootPath;
    QString label;
    quint64 totalBytes = 0;
    quint64 freeBytes = 0;
    QString physicalDiskNumbers;
};

struct PhysicalDiskSample {
    quint32 deviceNumber = 0;
    QString model;
    QString vendor;
    QString firmware;
    QString busType;
    std::optional<unsigned int> windowsHealthStatus;
    std::optional<unsigned int> mediaType;
    std::optional<unsigned int> temperatureCelsius;
    std::optional<unsigned int> maximumTemperatureCelsius;
    std::optional<unsigned int> wearPercent;
    std::optional<quint64> powerOnHours;
    std::optional<quint64> uncorrectedReadErrors;
    std::optional<quint64> uncorrectedWriteErrors;
};

struct DiskActivitySample {
    std::optional<double> readBytesPerSecond;
    std::optional<double> writeBytesPerSecond;
};

struct ThermalSensorSample {
    QString name;
    QString instanceName;
    QString source;
    double temperatureCelsius = 0.0;
    std::optional<double> passiveTripPointCelsius;
    std::optional<double> criticalTripPointCelsius;
    std::optional<quint32> secondsAbovePassiveTripPoint;
};

struct FanSample {
    QString name;
    QString deviceId;
    QString status;
    std::optional<quint64> requestedSpeedRpm;
    std::optional<quint64> previousRequestedSpeedRpm;
    QDateTime requestedSpeedChangedAt;
    std::optional<bool> activeCooling;
    std::optional<bool> variableSpeed;
};

struct NetworkAdapterSample {
    QString name;
    QString description;
    quint32 interfaceIndex = 0;
    quint64 receiveLinkSpeedBitsPerSecond = 0;
    quint64 transmitLinkSpeedBitsPerSecond = 0;
};

struct ProcessorCoreSample {
    QString name;
    std::optional<double> utilizationPercent;
};

struct GraphicsAdapterSample {
    QString name;
    quint32 vendorId = 0;
    quint64 dedicatedMemoryBytes = 0;
    std::optional<quint64> localMemoryUsageBytes;
    std::optional<quint64> localMemoryBudgetBytes;
};

struct SystemSnapshot {
    QDateTime capturedAt;
    int samplingIntervalSeconds = 5;
    qint64 collectionDurationMicroseconds = 0;
    QString monitoringAdaptationNote;

    QString deviceName;
    QString operatingSystem;
    QString operatingSystemVersion;
    QString operatingSystemArchitecture;
    QDateTime systemBootTime;
    quint64 systemUptimeSeconds = 0;
    QString deviceFormFactor;
    QString deviceFormFactorBasis;
    QString processorName;
    QString graphicsName;
    QString systemManufacturer;
    QString systemModel;
    QString biosVendor;
    QString biosVersion;
    QString biosReleaseDate;
    QVector<PhysicalDiskSample> physicalDisks;
    QVector<NetworkAdapterSample> activeNetworkAdapters;
    DiskActivitySample diskActivity;
    QVector<ThermalSensorSample> thermalSensors;
    QString thermalSensorStatus;
    QVector<FanSample> fans;
    QVector<GraphicsAdapterSample> graphicsAdapters;
    QStringList hardwareChanges;
    quint32 logicalProcessorCount = 0;
    quint64 graphicsMemoryBytes = 0;

    std::optional<double> processorUsagePercent;
    QVector<ProcessorCoreSample> processorCores;
    std::optional<double> graphicsUsagePercent;
    std::optional<double> memoryUsagePercent;
    quint64 memoryUsedBytes = 0;
    quint64 memoryAvailableBytes = 0;
    quint64 memoryTotalBytes = 0;
    std::optional<quint64> memorySystemCacheBytes;

    QString systemVolumePath;
    quint64 systemVolumeTotalBytes = 0;
    quint64 systemVolumeFreeBytes = 0;
    QVector<VolumeSample> volumes;

    std::optional<unsigned int> batteryPercent;
    std::optional<bool> batteryOnAcPower;
    bool batteryCharging = false;
    std::optional<quint64> batteryRemainingCapacityMwh;
    std::optional<quint64> batteryMaximumCapacityMwh;
    std::optional<quint64> batteryDesignCapacityMwh;
    std::optional<quint64> batteryFullChargeCapacityMwh;
    std::optional<double> batteryHealthPercent;
    std::optional<qint64> batteryRateMilliwatts;
    std::optional<quint32> batteryEstimatedSeconds;
    std::optional<double> networkReceiveBytesPerSecond;
    std::optional<double> networkSendBytesPerSecond;
    std::optional<quint64> networkTcpEntryCount;
    std::optional<quint64> networkUdpEndpointCount;
    QString networkNote;

    QVector<ProcessSample> topProcesses;
    QDateTime processSamplesCapturedAt;
    QString processCollectionStatus;
    quint32 foregroundProcessId = 0;
    QString foregroundProcessName;
    QVector<MetricQualityRecord> dataQuality;
};

} // namespace Ausyn

Q_DECLARE_METATYPE(Ausyn::SystemSnapshot)
