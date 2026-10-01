#pragma once

#include "../intelligence/insight_types.h"
#include "../intelligence/game_catalog.h"
#include "../monitoring/system_snapshot.h"

#include <QWidget>
#include <QSet>
#include <QHash>
#include <optional>

class QComboBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QPushButton;

namespace Ausyn {

class GamingPage final : public QWidget {
    Q_OBJECT
public:
    explicit GamingPage(QWidget* parent = nullptr);
    void setSnapshot(const SystemSnapshot& snapshot);
    void setLaunchMonitoringEnabled(bool enabled) { launchMonitoringEnabled_ = enabled; }
    [[nodiscard]] GameReadiness currentAssessment() const;
    [[nodiscard]] bool selectCatalogProfileForQuestion(const QString& question);

signals:
    void gameLaunchDetected(const QString& title, const QString& assessment);

private:
    void refreshAssessment();
    void runPreGameScan();
    void launchGameWithReadinessCheck();
    void updateSessionSummary();
    void appendSessionSample(const SystemSnapshot& snapshot);
    [[nodiscard]] GameRequirements requirements() const;
    void reloadProfiles();
    void loadSelectedProfile();
    void saveCurrentProfile();
    void deleteSelectedProfile();
    void applyCatalogProfile(const QString& id);
    void updateAdaptiveGuidance();
    void detectLaunchedGames();

    SystemSnapshot snapshot_;
    const GameCatalogProfile* activeGameProfile_ = nullptr;
    QLineEdit* gameTitle_ = nullptr;
    QLineEdit* requirementsSource_ = nullptr;
    QLineEdit* customGameExecutable_ = nullptr;
    QComboBox* savedProfiles_ = nullptr;
    QComboBox* catalogProfile_ = nullptr;
    QComboBox* adaptiveMode_ = nullptr;
    QLabel* profileStatus_ = nullptr;
    QLabel* launchStatus_ = nullptr;
    QLabel* autoRecommendation_ = nullptr;
    QLabel* catalogDetails_ = nullptr;
    QDoubleSpinBox* minimumRam_ = nullptr;
    QDoubleSpinBox* minimumVram_ = nullptr;
    QDoubleSpinBox* storageRequired_ = nullptr;
    QComboBox* installDrive_ = nullptr;
    QLabel* hardwareSummary_ = nullptr;
    QPushButton* sessionButton_ = nullptr;
    QLabel* sessionSummary_ = nullptr;
    QPushButton* preGameScanButton_ = nullptr;
    QPushButton* preGameLaunchButton_ = nullptr;
    QLabel* preGameScanSummary_ = nullptr;
    QLabel* resultTitle_ = nullptr;
    QLabel* resultSummary_ = nullptr;
    QLabel* resultConfidence_ = nullptr;
    QWidget* checksContainer_ = nullptr;
    QString lastAssessmentKey_;
    bool sessionActive_ = false;
    bool launchMonitoringEnabled_ = true;
    QDateTime sessionStartedAt_;
    QDateTime lastSessionSampleAt_;
    double cpuSum_ = 0.0;
    double cpuPeak_ = 0.0;
    int cpuSampleCount_ = 0;
    double memorySum_ = 0.0;
    double memoryPeak_ = 0.0;
    int memorySampleCount_ = 0;
    double gpuSum_ = 0.0;
    double gpuPeak_ = 0.0;
    int gpuSampleCount_ = 0;
    double thermalPeakCelsius_ = 0.0;
    QString thermalPeakSensor_;
    int thermalSampleCount_ = 0;
    int thermalNearPassiveCount_ = 0;
    QSet<quint32> observedGameProcessIds_;
    QSet<QString> activeCatalogGameIds_;
    QHash<QString, QString> customGameExeToProfile_;
    QDateTime lastAdaptiveSampleAt_;
    QVector<double> recentCpuLoad_;
    QVector<double> recentMemoryLoad_;
    QVector<double> recentGpuLoad_;
    QVector<double> recentThermalTripRatio_;
};

} // namespace Ausyn
