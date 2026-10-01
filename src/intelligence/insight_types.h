#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

namespace Ausyn {

enum class FindingSeverity : int {
    Information = 0,
    Warning = 1,
    Critical = 2,
};

enum class RecommendationOutcome : int {
    Worse = -1,
    NoChange = 0,
    Improved = 1,
    Unsure = 2,
};

struct Finding {
    QString ruleId;
    FindingSeverity severity = FindingSeverity::Information;
    QString title;
    QString summary;
    QString evidence;
    QString confidence;
    QString confidenceBasis;
    QString recommendation;
    QDateTime firstSeen;
    QDateTime lastSeen;
    QDateTime resolvedAt;
    std::optional<RecommendationOutcome> recommendationOutcome;
    QDateTime recommendationOutcomeAt;
    int priorRatedOutcomeReports = 0;
    int priorImprovementReports = 0;
    QDateTime verificationStartedAt;
    QDateTime verificationCheckedAt;
    std::optional<double> verificationCpuBefore;
    std::optional<double> verificationMemoryBefore;
    std::optional<double> verificationCpuAfter;
    std::optional<double> verificationMemoryAfter;
};

struct RecommendationOutcomeSummary {
    QString ruleId;
    QString title;
    QString recentRecommendation;
    int ratedReports = 0;
    int improvementReports = 0;
};

struct HealthComponent {
    QString name;
    int score = 0;
    int weight = 0;
    QString evidence;
};

struct HealthAssessment {
    std::optional<int> score;
    int coveragePercent = 0;
    QString explanation;
    QVector<HealthComponent> components;
};

struct HistoryPoint {
    QDateTime capturedAt;
    std::optional<double> processorPercent;
    std::optional<double> processorPeakPercent;
    int processorSampleCount = 0;
    std::optional<double> memoryPercent;
    std::optional<double> memoryPeakPercent;
    int memorySampleCount = 0;
    int sampleCount = 1;
    std::optional<double> graphicsPercent;
    std::optional<double> systemDriveUsedPercent;
    std::optional<unsigned int> batteryPercent;
};

struct BatteryHealthTrendPoint {
    QDateTime capturedAt;
    double estimatedHealthPercent = 0.0;
    int sampleCount = 0;
};

struct MetricAverages {
    std::optional<double> processorPercent;
    std::optional<double> memoryPercent;
    int processorSamples = 0;
    int memorySamples = 0;
};

struct MemoryTrendPoint {
    QDateTime capturedAt;
    double averagePercent = 0.0;
    int sampleCount = 0;
};

struct BatteryTrendPoint {
    QDateTime capturedAt;
    double chargePercent = 0.0;
};

enum class MemoryForecastState : int {
    InsufficientData,
    Stable,
    Rising,
    Declining,
    Irregular,
    ThresholdReached,
    EstimateAvailable,
};

struct MemoryPressureForecast {
    MemoryForecastState state = MemoryForecastState::InsufficientData;
    QString explanation;
    QVector<MemoryTrendPoint> observations;
    double currentPercent = 0.0;
    double risePerHour = 0.0;
    double minutesUntil90Percent = 0.0;
    double fitQuality = 0.0;
    int observedMinutes = 0;
    int recentRisingWindows = 0;
    int recentWindowsConsidered = 0;
    bool recentRiseConfirmed = false;
    bool fitAvailable = false;
    bool hasEstimate = false;
};

struct StorageTrendPoint {
    QDateTime capturedAt;
    double freePercent = 0.0;
    int sampleCount = 0;
    quint64 totalBytes = 0;
    QString rootPath;
};

enum class ForecastState : int {
    InsufficientData,
    Stable,
    Growing,
    Declining,
    Irregular,
    ThresholdReached,
};

struct StorageForecast {
    ForecastState state = ForecastState::InsufficientData;
    QString confidence;
    QString explanation;
    QVector<StorageTrendPoint> observations;
    double currentFreePercent = 0.0;
    double dailyChangePercentagePoints = 0.0;
    double daysUntilTenPercent = 0.0;
    double rSquared = 0.0;
    int observedDays = 0;
    int recentDecliningDays = 0;
    int recentDaysConsidered = 0;
    bool recentDeclineConfirmed = false;
    bool hasEstimate = false;
    bool rapidDropDetected = false;
    double rapidDropPercentagePoints = 0.0;
    QDateTime rapidDropAt;
};

struct VolumeStorageForecast {
    QString rootPath;
    QString label;
    StorageForecast forecast;
};

enum class BatteryForecastState : int {
    NotApplicable,
    InsufficientData,
    Stable,
    Irregular,
    ThresholdReached,
    EstimateAvailable,
};

struct BatteryForecast {
    BatteryForecastState state = BatteryForecastState::InsufficientData;
    QString explanation;
    QVector<BatteryTrendPoint> observations;
    double dischargePercentPerHour = 0.0;
    double minutesUntil15Percent = 0.0;
    int observationCount = 0;
    int observedMinutes = 0;
    double fitQuality = 0.0;
    bool fitAvailable = false;
    bool hasEstimate = false;
};

struct GameRequirements {
    QString title;
    double minimumRamGb = 0.0;
    double minimumVramGb = 0.0;
    double requiredFreeStorageGb = 0.0;
    QString driveRoot;
};

enum class RequirementState : int { Met, NotMet, Unknown };

struct RequirementCheck {
    QString name;
    QString actual;
    QString required;
    RequirementState state = RequirementState::Unknown;
};

struct GameReadiness {
    QString title;
    QString summary;
    QString confidence;
    QVector<RequirementCheck> checks;
    int knownChecks = 0;
    int passedChecks = 0;
    int failedChecks = 0;
};

struct AnalysisUpdate {
    HealthAssessment health;
    QVector<Finding> findings;
    bool historyAvailable = false;
    QString historyPath;
    qint64 historySizeBytes = 0;
    QString historyMessage;
    StorageForecast storageForecast;
    QVector<VolumeStorageForecast> volumeStorageForecasts;
    MemoryPressureForecast memoryForecast;
    BatteryForecast batteryForecast;
    QVector<BatteryHealthTrendPoint> batteryHealthTrend;
};

struct HistoryUpdate {
    QVector<HistoryPoint> points;
    bool historyAvailable = false;
    QString historyMessage;
    QVector<Finding> incidentFindings;
    bool incidentFindingsAvailable = false;
    QString incidentFindingsMessage;
    QVector<RecommendationOutcomeSummary> recommendationOutcomeSummaries;
    bool recommendationOutcomeHistoryAvailable = false;
    QString recommendationOutcomeHistoryMessage;
};

} // namespace Ausyn

Q_DECLARE_METATYPE(Ausyn::Finding)
Q_DECLARE_METATYPE(QVector<Ausyn::Finding>)
Q_DECLARE_METATYPE(Ausyn::HealthAssessment)
Q_DECLARE_METATYPE(Ausyn::HistoryPoint)
Q_DECLARE_METATYPE(QVector<Ausyn::HistoryPoint>)
Q_DECLARE_METATYPE(Ausyn::AnalysisUpdate)
Q_DECLARE_METATYPE(Ausyn::HistoryUpdate)
