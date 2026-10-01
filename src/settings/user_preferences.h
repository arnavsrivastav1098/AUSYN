#pragma once

#include <QDateTime>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Ausyn {

struct UserPreferences {
    bool casualTone = true;
    bool technicalDetail = false;
    bool showAdvancedTools = false;
    bool showQuickStartGuide = true;
    bool lightTheme = false;
    bool adaptiveSamplingEnabled = true;
    bool monitoringEnabled = true;
    bool animationsEnabled = true;
    bool closeToTray = true;
    bool startMinimized = false;
    bool proactiveBriefingEnabled = true;
    bool workloadAwarenessEnabled = true;
    bool autonomousCompanionEnabled = true;
    bool rememberActivity = false;
    bool automaticReliefEnabled = false;
    bool smartAttentionEnabled = true;
    int attentionBudget = 3;
    int workSessionMinutes = 60;
    QStringList automaticReliefPaths;
    bool cloudConversationContext = false;
    bool cloudSystemExplanations = false;
    bool monitorGameLaunches = true;
    bool proactiveEventScans = true;
    bool proactiveSecurityScans = true;
    bool startupResourceAnalysis = true;
    bool alertsOnlyInBackground = true;
    bool ausynPopupCards = true;
    int backgroundScanMinutes = 5;
    int notificationCooldownMinutes = 15;
    int landingPage = 0;
    QHash<int, bool> pageDetails;
    bool cloudAiEnabled = false;
    bool webSearchEnabled = false;
    bool shareProcessNames = false;
    bool saveConversationsLocally = false;
    bool desktopNotificationsEnabled = true;
    bool startWithWindows = false;
    bool notifySystemFindings = true;
    bool notifyForecasts = true;
    bool notifyEventLogs = true;
    bool notifyStartupApps = true;
    bool notifySecurity = true;
    bool quietHoursEnabled = false;
    int quietHoursStartMinute = 22 * 60;
    int quietHoursEndMinute = 7 * 60;
    int historyRetentionDays = 30;
    int samplingIntervalSeconds = 5;
    int resourceAlertSensitivity = 3;
    QString apiEndpoint = QStringLiteral("https://api.openai.com/v1");
    QString apiModel;
    QByteArray encryptedApiKey;
    QStringList dismissedBriefingKeys;
    QHash<QString, QDateTime> snoozedBriefingKeys;
};

struct ChatMessage {
    bool fromUser = false;
    QString text;
    QDateTime timestamp;
};

class UserPreferencesStore final {
public:
    [[nodiscard]] static UserPreferences load();
    [[nodiscard]] static bool save(const UserPreferences& preferences, QString* errorMessage = nullptr);
    [[nodiscard]] static bool setStartWithWindows(bool enabled, QString* errorMessage = nullptr);
    [[nodiscard]] static bool startsWithWindows();

    [[nodiscard]] static QByteArray protectApiKey(const QString& apiKey, QString* errorMessage = nullptr);
    [[nodiscard]] static QString unprotectApiKey(const QByteArray& protectedKey, QString* errorMessage = nullptr);

    [[nodiscard]] static QString conversationFilePath();
    [[nodiscard]] static QVector<ChatMessage> loadConversations(QString* errorMessage = nullptr);
    [[nodiscard]] static bool saveConversations(const QVector<ChatMessage>& messages,
                                                QString* errorMessage = nullptr);
    [[nodiscard]] static bool deleteConversations(QString* errorMessage = nullptr);
};

} // namespace Ausyn
