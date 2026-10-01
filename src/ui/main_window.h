#pragma once

#include "../monitoring/system_snapshot.h"
#include "../intelligence/insight_types.h"
#include "../settings/user_preferences.h"
#include "../intelligence/event_log_types.h"
#include "../intelligence/app_inventory_types.h"
#include "../intelligence/security_status_types.h"

#include "window_chrome.h"
#include "alert_policy.h"
#include "../intelligence/workload_coach.h"
#include "../intelligence/autonomous_companion.h"
#include <QPointer>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QVector>

#include <optional>

class QLabel;
class QCloseEvent;
class QShowEvent;
class QPushButton;
class QProgressBar;
class QStackedWidget;
class QLabel;
class QTableWidget;
class QToolButton;
class QVBoxLayout;
class QSystemTrayIcon;
class QMenu;
class QAction;
class QJsonArray;
template <typename T> class QFutureWatcher;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QTimer;
class QTimeEdit;
class QSlider;
class QTextBrowser;
class QLabel;

namespace Ausyn {

class TelemetryService;
class HistoryChart;
class InsightListWidget;
class AssistantClient;
class PredictionPage;
class GamingPage;
class BatteryPage;
class EventLogPage;
class AppInventoryPage;
class SecurityPage;
class DataQualityPage;
class AgentHealthPage;
class NetworkPage;
class ProcessTrendChart;
class ProactiveBriefingWidget;
class TroubleshootingPage;
class ThemeController;
class PageHub;
class LiveActivityChart;
class ResourceRing;
class ActivityPage;
class ResourceInspector;
class NotificationPopup;
class BackgroundRelief;

    struct ProcessTrendPoint {
    QDateTime capturedAt;
    std::optional<double> cpuPercent;
    std::optional<double> workingSetGigabytes;
    std::optional<double> workingSetSharePercent;
    std::optional<double> systemCpuPercent;
    std::optional<double> systemMemoryPercent;
    std::optional<double> readMegabytesPerSecond;
    std::optional<double> writeMegabytesPerSecond;
    };

struct BriefingFollowup {
    QString category;
    QString title;
    QString summary;
    QString evidence;
    QString nextStep;
};

class MainWindow final : public FramelessWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool uiCheck = false);
    ~MainWindow() override;
    bool shouldStartHidden() const { return preferences_.startMinimized; }
    bool runUiCheck(const QString& outputDirectory);
    void startCollectorCheck(const QString& outputDirectory);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    using NotificationCategory = AlertCategory;
    void openResource(int resource);
    int notificationResource(const QString& title, const QString& body) const;
    void showAdviceBanner(const QString& title, const QString& body, int resource);
    NotificationPopup* popupWindow();
    void addNavigationItem(const QString& label, const QString& glyph, int pageIndex, bool advanced = false);
    void setAdvancedToolsVisible(bool visible);
    void applyTheme(bool light);
    void showPage(int pageIndex, const QString& title);
    void showHub(int hubIndex);
    void updateDetailMode(int hubIndex);
    void checkSystemOnDashboard();
    void updateDashboardReadout();
    void applyBehaviorPreferences();
    void recordActivity(const QString& title, const QString& detail, int destination = 0);
    QWidget* legacyPage(int id) const;
    QWidget* makeBehaviorSettings();
    QWidget* makeWorkloadPanel();
    void updateWorkloadContext(const SystemSnapshot& snapshot);
    void refreshWorkloadPanel();
    void reviewBackgroundRelief();
    QWidget* makeCompanionPanel();
    QWidget* makeCompanionSettings();
    QWidget* makeCompanionJournal();
    void configureCompanion();
    void updateCompanion(const SystemSnapshot& snapshot);
    void refreshCompanionPanel();
    void installCompanionTray(QMenu* menu);
    void showCompanionBriefing();
    bool runCompanionChecks(const QString& directory, QJsonArray& checks);

private slots:
    void updateSystemSnapshot(Ausyn::SystemSnapshot snapshot);
    void updateMonitoringStatus();
    void updateAnalysis(Ausyn::AnalysisUpdate update);
    void updateHistory(Ausyn::HistoryUpdate update);
    void refreshHistorySummary();
    void exportSystemReport();
    void sendAssistantMessage();
    void savePreferences();
    bool notificationsAllowed(NotificationCategory category) const;
    bool showDesktopNotification(NotificationCategory category, const QString& title, const QString& body, int icon, bool test = false, int resourceOverride = -100, const QString& occurrenceKey = {});
    void completeAssistantReply(QString question, QString response, bool usedCloud, QString note);
    void refreshEventLogs();
    void refreshAppInventory();
    void refreshSecurityStatus();
    void refreshProactiveEventLogs();
    void scanLocalUpdates();
    void updateSelectedProcessDetails();
    void updateProcessTrend(const Ausyn::SystemSnapshot& snapshot);
    void updateSelectedProcessImpact();
    void updateEarlyPressureHeadsUp(const Ausyn::SystemSnapshot& snapshot);
    void handleProactiveSecurityStatus(const Ausyn::SecurityStatusUpdate& update);
    void handleProactiveEventLogs(const Ausyn::EventLogUpdate& update);
    void updateProactiveStartupSignals();
    void updateProactiveBriefing();

private:
    TelemetryService* telemetry_ = nullptr;
    QVBoxLayout* sidebarLayout_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QVector<PageHub*> hubs_;
    QHash<int, QWidget*> legacyPages_;
    QHash<int, int> legacyHub_;
    int activeLegacyPage_ = 0;
    LiveActivityChart* dashboardChart_ = nullptr;
    LiveActivityChart* performanceChart_ = nullptr;
    ResourceRing* performanceRing_ = nullptr;
    ActivityPage* activityPage_ = nullptr;
    QLabel* dashboardSummary_ = nullptr;
    WorkloadCoach workloadCoach_;
    AutonomousCompanion companion_;
    QWidget* companionPanel_ = nullptr;
    QLabel* companionStatus_ = nullptr;
    QLabel* companionDetails_ = nullptr;
    QLabel* companionJournal_ = nullptr;
    QLabel* companionRules_ = nullptr;
    QLabel* companionLearning_ = nullptr;
    QTextBrowser* companionSavedJournal_ = nullptr;
    QString companionRenderedJournal_;
    QCheckBox* companionEnabled_ = nullptr;
    QCheckBox* rememberActivity_ = nullptr;
    QCheckBox* automaticReliefEnabled_ = nullptr;
    QCheckBox* smartAttention_ = nullptr;
    QComboBox* attentionBudget_ = nullptr;
    QComboBox* workSessionMinutes_ = nullptr;
    QAction* trayCompanionStatus_ = nullptr;
    bool automaticLease_ = false;
    bool companionEconomical_ = false;
    QString companionStatePath_;
    QDateTime lastAutomaticAttempt_;
    BackgroundRelief* backgroundRelief_ = nullptr;
    QCheckBox* workloadAwareness_ = nullptr;
    QCheckBox* cloudConversationContext_ = nullptr;
    QCheckBox* cloudSystemExplanations_ = nullptr;
    QWidget* workloadPanel_ = nullptr;
    QLabel* workloadSummary_ = nullptr;
    QLabel* workloadPlan_ = nullptr;
    QLabel* workloadComparison_ = nullptr;
    QLabel* workloadActionStatus_ = nullptr;
    QComboBox* workloadAppChoice_ = nullptr;
    QPushButton* keepWorkloadApp_ = nullptr;
    QPushButton* compareWorkload_ = nullptr;
    QPushButton* relieveWorkload_ = nullptr;
    QPushButton* restoreWorkload_ = nullptr;
    QString workloadEpisodeKey_;
    QString workloadBannerTitle_;
    QString lastDesktopAlertOccurrenceKey_;
    QLabel* assistantModeStatus_ = nullptr;
    QLabel* dashboardNextStep_ = nullptr;
    QLabel* dashboardActivity_ = nullptr;
    QLabel* dashboardCheckResult_ = nullptr;
    QPushButton* pauseMonitoring_ = nullptr;
    QCheckBox* monitoringEnabled_ = nullptr;
    QCheckBox* animationsEnabled_ = nullptr;
    QCheckBox* closeToTray_ = nullptr;
    QCheckBox* startMinimized_ = nullptr;
    QCheckBox* proactiveBriefingEnabled_ = nullptr;
    QCheckBox* monitorGameLaunches_ = nullptr;
    QCheckBox* proactiveEventScans_ = nullptr;
    QCheckBox* proactiveSecurityScans_ = nullptr;
    QCheckBox* startupResourceAnalysis_ = nullptr;
    QCheckBox* alertsOnlyInBackground_ = nullptr;
    QComboBox* backgroundScanMinutes_ = nullptr;
    QComboBox* notificationCooldown_ = nullptr;
    QComboBox* landingPage_ = nullptr;
    QTimer* proactiveEventTimer_ = nullptr;
    QTimer* securityMonitorTimer_ = nullptr;
    QHash<QString, QDateTime> notificationCooldowns_;
    QDateTime lastDesktopAlertAt_;
    int lastDesktopAlertIcon_ = 0;
    int lastDesktopAlertResource_ = -100;
    QHash<int, QPointer<ResourceInspector>> resourceWindows_;
    QPointer<NotificationPopup> notificationPopup_;
    QWidget* adviceBanner_ = nullptr;
    QLabel* adviceBannerTitle_ = nullptr;
    QLabel* adviceBannerBody_ = nullptr;
    int adviceResource_ = 1;
    QString dismissedAdviceTitle_;
    QComboBox* alertChannel_ = nullptr;
    QLabel* alertDeliveryStatus_ = nullptr;
    QDateTime desktopSnoozedUntil_;
    QHash<QString, int> deliveredFindingSeverity_;
    QHash<QString, QString> suppressedAlertReasons_;
    QDateTime lastUiRenderedAt_;
    QDateTime processRenderedAt_;
    quint64 receivedSamples_ = 0;
    bool preferencesReady_ = false;
    bool uiCheck_ = false;
    QTableWidget* processTable_ = nullptr;
    QLineEdit* processSearch_ = nullptr;
    QLabel* processSnapshotAge_ = nullptr;
    QLabel* foregroundWorkload_ = nullptr;
    QPushButton* inspectForegroundProcess_ = nullptr;
    QLabel* processDetails_ = nullptr;
    QLabel* processImpactDetails_ = nullptr;
    ProcessTrendChart* processTrendChart_ = nullptr;
    QVector<ProcessTrendPoint> processTrend_;
    QDateTime processTrendCapturedAt_;
    quint32 processTrendProcessId_ = 0;
    QWidget* processorCoreGrid_ = nullptr;
    QVector<QLabel*> processorCoreValues_;
    QVector<QLabel*> processorCoreNames_;
    QVector<QProgressBar*> processorCoreBars_;
    QLabel* hardwareDetails_ = nullptr;
    QLabel* hardwareVolumes_ = nullptr;
    QLabel* hardwareChangeStatus_ = nullptr;
    QLabel* hardwareTimeline_ = nullptr;
    QLabel* historyStatus_ = nullptr;
    QLabel* historySummary_ = nullptr;
    QComboBox* historyPeriod_ = nullptr;
    HistoryChart* historyChart_ = nullptr;
    QVector<HistoryPoint> latestHistoryPoints_;
    QVector<RecommendationOutcomeSummary> latestRecommendationOutcomeSummaries_;
    int historyPeriodHours_ = 24;
    bool historyQueryAvailable_ = false;
    bool recommendationOutcomeHistoryAvailable_ = false;
    InsightListWidget* diagnosticsView_ = nullptr;
    InsightListWidget* recommendationsView_ = nullptr;
    QList<QToolButton*> navigationButtons_;
    QLabel* pageTitle_ = nullptr;
    QWidget* quickStartPanel_ = nullptr;
    QLabel* advancedToolsSection_ = nullptr;
    QLabel* monitoringStatus_ = nullptr;
    QLabel* monitoringStatusDot_ = nullptr;
    QTimer* monitoringFreshnessTimer_ = nullptr;
    PredictionPage* predictionPage_ = nullptr;
    GamingPage* gamingPage_ = nullptr;
    BatteryPage* batteryPage_ = nullptr;
    EventLogPage* eventLogPage_ = nullptr;
    EventLogUpdate latestEventLogUpdate_;
    QFutureWatcher<Ausyn::EventLogUpdate>* eventLogWatcher_ = nullptr;
    QFutureWatcher<Ausyn::EventLogUpdate>* proactiveEventWatcher_ = nullptr;
    QSet<QString> activeProactiveEvents_;
    QSet<QString> pendingProactiveEvents_;
    QHash<QString, QDateTime> eventAlertCooldowns_;
    QDateTime lastEventAlertAt_;
    QStringList proactiveEventNotices_;
    bool hasSeenProactiveEvents_ = false;
    AppInventoryPage* appInventoryPage_ = nullptr;
    QSet<QString> activeStartupNotices_;
    QHash<QString, QDateTime> startupNoticeCooldowns_;
    QStringList proactiveStartupNotices_;
    QFutureWatcher<Ausyn::AppInventoryUpdate>* appInventoryWatcher_ = nullptr;
    SecurityPage* securityPage_ = nullptr;
    QFutureWatcher<Ausyn::SecurityStatusUpdate>* securityStatusWatcher_ = nullptr;
    QSet<QString> activeSecurityNotices_;
    QStringList proactiveSecurityNotices_;
    bool hasSeenSecurityStatus_ = false;
    QFutureWatcher<Ausyn::UpdateCacheResult>* updateCacheWatcher_ = nullptr;
    DataQualityPage* dataQualityPage_ = nullptr;
    AgentHealthPage* agentHealthPage_ = nullptr;
    NetworkPage* networkPage_ = nullptr;
    ProactiveBriefingWidget* proactiveBriefing_ = nullptr;
    TroubleshootingPage* troubleshootingPage_ = nullptr;
    QCheckBox* desktopNotifications_ = nullptr;
    QCheckBox* systemFindingAlerts_ = nullptr;
    QCheckBox* forecastAlerts_ = nullptr;
    QCheckBox* eventLogAlerts_ = nullptr;
    QCheckBox* startupAlerts_ = nullptr;
    QCheckBox* securityAlerts_ = nullptr;
    QSlider* resourceAlertSensitivity_ = nullptr;
    QLabel* resourceAlertSensitivityValue_ = nullptr;
    QCheckBox* quietHoursEnabled_ = nullptr;
    QCheckBox* startWithWindows_ = nullptr;
    QTimeEdit* quietHoursStart_ = nullptr;
    QTimeEdit* quietHoursEnd_ = nullptr;
    QSystemTrayIcon* trayIcon_ = nullptr;
    bool quitRequested_ = false;
    NotificationCategory lastNotificationCategory_ = NotificationCategory::SystemFinding;
    QHash<QString, int> lastNotifiedSeverity_;
    QHash<QString, QDateTime> lastInformationalNoticeAt_;
    QDateTime lastThermalNotificationAt_;
    QVector<QPair<qint64, QPair<double, double>>> recentLoadSamples_;
    Finding earlyPressureFinding_;
    QDateTime lastEarlyPressureNotificationAt_;
    QDateTime lastEarlyPressureBriefingAt_;
    bool earlyPressureActive_ = false;
    bool storageForecastNotified_ = false;
    bool rapidStorageDropNotified_ = false;
    QSet<QString> activeVolumeStorageNotices_;
    bool memoryForecastNotified_ = false;
    bool batteryForecastNotified_ = false;
    bool hasSeenAnalysis_ = false;
    bool hasSeenAdaptiveSamplingState_ = false;
    QTextBrowser* chatTranscript_ = nullptr;
    QLineEdit* chatInput_ = nullptr;
    QCheckBox* casualTone_ = nullptr;
    QCheckBox* technicalDetail_ = nullptr;
    QCheckBox* showAdvancedTools_ = nullptr;
    QCheckBox* showQuickStartGuide_ = nullptr;
    QComboBox* appearance_ = nullptr;
    QCheckBox* adaptiveSampling_ = nullptr;
    QCheckBox* cloudAi_ = nullptr;
    QCheckBox* webSearch_ = nullptr;
    QCheckBox* shareProcessNames_ = nullptr;
    QCheckBox* saveConversations_ = nullptr;
    QLineEdit* apiEndpoint_ = nullptr;
    QLineEdit* apiModel_ = nullptr;
    QLineEdit* apiKey_ = nullptr;
    QLabel* apiKeyStatus_ = nullptr;
    QComboBox* retentionDays_ = nullptr;
    QComboBox* samplingInterval_ = nullptr;
    UserPreferences preferences_;
    SystemSnapshot latestSnapshot_;
    bool proactiveSnapshotStale_ = true;
    AnalysisUpdate latestAnalysis_;
    AppInventoryUpdate latestAppInventory_;
    QVector<ChatMessage> chatMessages_;
    std::optional<BriefingFollowup> pendingBriefingFollowup_;
    int brandClickCount_ = 0;
    AssistantClient* assistantClient_ = nullptr;
    ThemeController* themeController_ = nullptr;
};

} // namespace Ausyn
