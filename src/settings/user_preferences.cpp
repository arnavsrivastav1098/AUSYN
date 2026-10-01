#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dpapi.h>

#include "user_preferences.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>

#pragma comment(lib, "Crypt32.lib")

namespace Ausyn {
namespace {

constexpr auto kWindowsRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto kWindowsRunValue = "Ausyn";

QString settingsFilePath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return appData.isEmpty() ? QString() : QDir(appData).filePath(QStringLiteral("settings.ini"));
}

QSettings makeSettings()
{
    const QString path = settingsFilePath();
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
    }
    return QSettings(path, QSettings::IniFormat);
}

void setError(QString* output, const QString& error)
{
    if (output) {
        *output = error;
    }
}

} // namespace

UserPreferences UserPreferencesStore::load()
{
    QSettings settings = makeSettings();
    UserPreferences preferences;
    preferences.casualTone = settings.value(QStringLiteral("assistant/casualTone"), true).toBool();
    preferences.technicalDetail = settings.value(QStringLiteral("assistant/technicalDetail"), false).toBool();
    preferences.showAdvancedTools = settings.value(QStringLiteral("interface/showAdvancedTools"), false).toBool();
    preferences.showQuickStartGuide = settings.value(QStringLiteral("interface/showQuickStartGuide"), true).toBool();
    preferences.lightTheme = settings.value(QStringLiteral("interface/lightTheme"), false).toBool();
    preferences.adaptiveSamplingEnabled = settings.value(QStringLiteral("monitoring/adaptiveSamplingEnabled"), true).toBool();
    preferences.monitoringEnabled = settings.value(QStringLiteral("monitoring/enabled"), true).toBool();
    preferences.workloadAwarenessEnabled = settings.value(QStringLiteral("monitoring/workloadAwareness"), true).toBool();
    preferences.autonomousCompanionEnabled = settings.value(QStringLiteral("companion/enabled"), true).toBool();
    preferences.rememberActivity = settings.value(QStringLiteral("companion/rememberActivity"), false).toBool();
    preferences.automaticReliefEnabled = settings.value(QStringLiteral("companion/automaticRelief"), false).toBool();
    preferences.smartAttentionEnabled = settings.value(QStringLiteral("companion/smartAttention"), true).toBool();
    preferences.attentionBudget = std::clamp(settings.value(QStringLiteral("companion/attentionBudget"), 3).toInt(), 1, 6);
    preferences.workSessionMinutes = std::clamp(settings.value(QStringLiteral("companion/workSessionMinutes"), 60).toInt(), 15, 240);
    preferences.automaticReliefPaths = settings.value(QStringLiteral("companion/approvedPaths")).toStringList().mid(0, 8);
    preferences.cloudConversationContext = settings.value(QStringLiteral("privacy/cloudConversationContext"), false).toBool();
    preferences.cloudSystemExplanations = settings.value(QStringLiteral("privacy/cloudSystemExplanations"), false).toBool();
    preferences.animationsEnabled = settings.value(QStringLiteral("interface/animationsEnabled"), true).toBool();
    preferences.closeToTray = settings.value(QStringLiteral("interface/closeToTray"), true).toBool();
    preferences.startMinimized = settings.value(QStringLiteral("interface/startMinimized"), false).toBool();
    preferences.landingPage = std::clamp(settings.value(QStringLiteral("interface/landingPage"), 0).toInt(), 0, 4);
    preferences.proactiveBriefingEnabled = settings.value(QStringLiteral("monitoring/proactiveBriefing"), true).toBool();
    preferences.monitorGameLaunches = settings.value(QStringLiteral("monitoring/gameLaunches"), true).toBool();
    preferences.proactiveEventScans = settings.value(QStringLiteral("monitoring/eventScans"), true).toBool();
    preferences.proactiveSecurityScans = settings.value(QStringLiteral("monitoring/securityScans"), true).toBool();
    preferences.startupResourceAnalysis = settings.value(QStringLiteral("monitoring/startupAnalysis"), true).toBool();
    preferences.backgroundScanMinutes = std::clamp(settings.value(QStringLiteral("monitoring/backgroundScanMinutes"), 5).toInt(), 5, 30);
    preferences.alertsOnlyInBackground = settings.value(QStringLiteral("notifications/onlyInBackground"), true).toBool();
    preferences.ausynPopupCards = settings.value(QStringLiteral("notifications/ausynCards"), true).toBool();
    preferences.notificationCooldownMinutes = std::clamp(settings.value(QStringLiteral("notifications/cooldownMinutes"), 15).toInt(), 1, 60);
    for (int i = 0; i < 5; ++i)
        preferences.pageDetails.insert(i, settings.value(QStringLiteral("interface/pageDetails/%1").arg(i), i == 2 && preferences.technicalDetail).toBool());
    preferences.cloudAiEnabled = settings.value(QStringLiteral("privacy/cloudAiEnabled"), false).toBool();
    preferences.webSearchEnabled = settings.value(QStringLiteral("privacy/webSearchEnabled"), false).toBool();
    preferences.shareProcessNames = settings.value(QStringLiteral("privacy/shareProcessNames"), false).toBool();
    preferences.saveConversationsLocally = settings.value(QStringLiteral("privacy/saveConversationsLocally"), false).toBool();
    preferences.desktopNotificationsEnabled = settings.value(QStringLiteral("notifications/desktopEnabled"), true).toBool();
    preferences.startWithWindows = startsWithWindows();
    preferences.notifySystemFindings = settings.value(QStringLiteral("notifications/systemFindings"), true).toBool();
    preferences.notifyForecasts = settings.value(QStringLiteral("notifications/forecasts"), true).toBool();
    preferences.notifyEventLogs = settings.value(QStringLiteral("notifications/eventLogs"), true).toBool();
    preferences.notifyStartupApps = settings.value(QStringLiteral("notifications/startupApps"), true).toBool();
    preferences.notifySecurity = settings.value(QStringLiteral("notifications/security"), true).toBool();
    preferences.resourceAlertSensitivity = std::clamp(
        settings.value(QStringLiteral("notifications/resourceAlertSensitivity"), 3).toInt(), 1, 5);
    preferences.quietHoursEnabled = settings.value(QStringLiteral("notifications/quietHoursEnabled"), false).toBool();
    preferences.quietHoursStartMinute = std::clamp(settings.value(QStringLiteral("notifications/quietHoursStartMinute"), 22 * 60).toInt(), 0, 1439);
    preferences.quietHoursEndMinute = std::clamp(settings.value(QStringLiteral("notifications/quietHoursEndMinute"), 7 * 60).toInt(), 0, 1439);
    preferences.historyRetentionDays = std::clamp(
        settings.value(QStringLiteral("privacy/historyRetentionDays"), 30).toInt(), 7, 90);
    bool intervalOk = false;
    const int configuredInterval = settings.value(QStringLiteral("monitoring/samplingIntervalSeconds"), 5)
        .toInt(&intervalOk);
    preferences.samplingIntervalSeconds = intervalOk
        ? std::clamp(configuredInterval, 1, 10) : 5;
    preferences.apiEndpoint = settings.value(QStringLiteral("ai/endpoint"), preferences.apiEndpoint).toString();
    preferences.apiModel = settings.value(QStringLiteral("ai/model")).toString();
    preferences.encryptedApiKey = QByteArray::fromBase64(
        settings.value(QStringLiteral("ai/protectedApiKey")).toByteArray());
    preferences.dismissedBriefingKeys = settings.value(QStringLiteral("briefing/dismissedKeys")).toStringList();
    const QVariantMap savedSnoozes = settings.value(QStringLiteral("briefing/snoozedKeys")).toMap();
    for (auto it = savedSnoozes.cbegin(); it != savedSnoozes.cend(); ++it) {
        const QDateTime until = it.value().toDateTime();
        if (until.isValid() && until > QDateTime::currentDateTime())
            preferences.snoozedBriefingKeys.insert(it.key(), until);
    }
    return preferences;
}

bool UserPreferencesStore::startsWithWindows()
{
    QSettings runEntries(QString::fromLatin1(kWindowsRunKey), QSettings::NativeFormat);
    return runEntries.contains(QString::fromLatin1(kWindowsRunValue));
}

bool UserPreferencesStore::setStartWithWindows(bool enabled, QString* errorMessage)
{
    QSettings runEntries(QString::fromLatin1(kWindowsRunKey), QSettings::NativeFormat);
    if (enabled) {
        const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        if (executable.isEmpty()) {
            setError(errorMessage, QStringLiteral("Ausyn could not determine its installed program path."));
            return false;
        }
        runEntries.setValue(QString::fromLatin1(kWindowsRunValue), QStringLiteral("\"%1\"").arg(executable));
    } else {
        runEntries.remove(QString::fromLatin1(kWindowsRunValue));
    }
    runEntries.sync();
    if (runEntries.status() != QSettings::NoError) {
        setError(errorMessage, enabled
            ? QStringLiteral("Windows could not add Ausyn to this account’s sign-in startup list.")
            : QStringLiteral("Windows could not remove Ausyn from this account’s sign-in startup list."));
        return false;
    }
    if (startsWithWindows() != enabled) {
        setError(errorMessage, enabled
            ? QStringLiteral("Ausyn’s startup entry could not be verified in Windows.")
            : QStringLiteral("Ausyn’s startup entry could not be verified as removed from Windows."));
        return false;
    }
    return true;
}

bool UserPreferencesStore::save(const UserPreferences& preferences, QString* errorMessage)
{
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("assistant/casualTone"), preferences.casualTone);
    settings.setValue(QStringLiteral("assistant/technicalDetail"), preferences.technicalDetail);
    settings.setValue(QStringLiteral("interface/showAdvancedTools"), preferences.showAdvancedTools);
    settings.setValue(QStringLiteral("interface/showQuickStartGuide"), preferences.showQuickStartGuide);
    settings.setValue(QStringLiteral("interface/lightTheme"), preferences.lightTheme);
    settings.setValue(QStringLiteral("monitoring/adaptiveSamplingEnabled"), preferences.adaptiveSamplingEnabled);
    settings.setValue(QStringLiteral("monitoring/enabled"), preferences.monitoringEnabled);
    settings.setValue(QStringLiteral("monitoring/workloadAwareness"), preferences.workloadAwarenessEnabled);
    settings.setValue(QStringLiteral("companion/enabled"), preferences.autonomousCompanionEnabled);
    settings.setValue(QStringLiteral("companion/rememberActivity"), preferences.rememberActivity);
    settings.setValue(QStringLiteral("companion/automaticRelief"), preferences.automaticReliefEnabled);
    settings.setValue(QStringLiteral("companion/smartAttention"), preferences.smartAttentionEnabled);
    settings.setValue(QStringLiteral("companion/attentionBudget"), std::clamp(preferences.attentionBudget, 1, 6));
    settings.setValue(QStringLiteral("companion/workSessionMinutes"), std::clamp(preferences.workSessionMinutes, 15, 240));
    settings.setValue(QStringLiteral("companion/approvedPaths"), preferences.automaticReliefPaths.mid(0, 8));
    settings.setValue(QStringLiteral("privacy/cloudConversationContext"), preferences.cloudConversationContext);
    settings.setValue(QStringLiteral("privacy/cloudSystemExplanations"), preferences.cloudSystemExplanations);
    settings.setValue(QStringLiteral("interface/animationsEnabled"), preferences.animationsEnabled);
    settings.setValue(QStringLiteral("interface/closeToTray"), preferences.closeToTray);
    settings.setValue(QStringLiteral("interface/startMinimized"), preferences.startMinimized);
    settings.setValue(QStringLiteral("interface/landingPage"), preferences.landingPage);
    settings.setValue(QStringLiteral("monitoring/proactiveBriefing"), preferences.proactiveBriefingEnabled);
    settings.setValue(QStringLiteral("monitoring/gameLaunches"), preferences.monitorGameLaunches);
    settings.setValue(QStringLiteral("monitoring/eventScans"), preferences.proactiveEventScans);
    settings.setValue(QStringLiteral("monitoring/securityScans"), preferences.proactiveSecurityScans);
    settings.setValue(QStringLiteral("monitoring/startupAnalysis"), preferences.startupResourceAnalysis);
    settings.setValue(QStringLiteral("monitoring/backgroundScanMinutes"), preferences.backgroundScanMinutes);
    settings.setValue(QStringLiteral("notifications/onlyInBackground"), preferences.alertsOnlyInBackground);
    settings.setValue(QStringLiteral("notifications/ausynCards"), preferences.ausynPopupCards);
    settings.setValue(QStringLiteral("notifications/cooldownMinutes"), preferences.notificationCooldownMinutes);
    for (int i = 0; i < 5; ++i)
        settings.setValue(QStringLiteral("interface/pageDetails/%1").arg(i), preferences.pageDetails.value(i, false));
    settings.setValue(QStringLiteral("privacy/cloudAiEnabled"), preferences.cloudAiEnabled);
    settings.setValue(QStringLiteral("privacy/webSearchEnabled"), preferences.webSearchEnabled);
    settings.setValue(QStringLiteral("privacy/shareProcessNames"), preferences.shareProcessNames);
    settings.setValue(QStringLiteral("privacy/saveConversationsLocally"), preferences.saveConversationsLocally);
    settings.setValue(QStringLiteral("notifications/desktopEnabled"), preferences.desktopNotificationsEnabled);
    settings.setValue(QStringLiteral("notifications/systemFindings"), preferences.notifySystemFindings);
    settings.setValue(QStringLiteral("notifications/forecasts"), preferences.notifyForecasts);
    settings.setValue(QStringLiteral("notifications/eventLogs"), preferences.notifyEventLogs);
    settings.setValue(QStringLiteral("notifications/startupApps"), preferences.notifyStartupApps);
    settings.setValue(QStringLiteral("notifications/security"), preferences.notifySecurity);
    settings.setValue(QStringLiteral("notifications/resourceAlertSensitivity"),
                      std::clamp(preferences.resourceAlertSensitivity, 1, 5));
    settings.setValue(QStringLiteral("notifications/quietHoursEnabled"), preferences.quietHoursEnabled);
    settings.setValue(QStringLiteral("notifications/quietHoursStartMinute"), std::clamp(preferences.quietHoursStartMinute, 0, 1439));
    settings.setValue(QStringLiteral("notifications/quietHoursEndMinute"), std::clamp(preferences.quietHoursEndMinute, 0, 1439));
    settings.setValue(QStringLiteral("privacy/historyRetentionDays"),
                      std::clamp(preferences.historyRetentionDays, 7, 90));
    settings.setValue(QStringLiteral("monitoring/samplingIntervalSeconds"),
                      std::clamp(preferences.samplingIntervalSeconds, 1, 10));
    settings.setValue(QStringLiteral("ai/endpoint"), preferences.apiEndpoint.trimmed());
    settings.setValue(QStringLiteral("ai/model"), preferences.apiModel.trimmed());
    settings.setValue(QStringLiteral("ai/protectedApiKey"),
                      QString::fromLatin1(preferences.encryptedApiKey.toBase64()));
    settings.setValue(QStringLiteral("briefing/dismissedKeys"), preferences.dismissedBriefingKeys.mid(0, 500));
    QVariantMap snoozes;
    for (auto it = preferences.snoozedBriefingKeys.cbegin(); it != preferences.snoozedBriefingKeys.cend(); ++it) {
        if (it.value().isValid() && it.value() > QDateTime::currentDateTime())
            snoozes.insert(it.key(), it.value());
    }
    settings.setValue(QStringLiteral("briefing/snoozedKeys"), snoozes);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setError(errorMessage, QStringLiteral("Ausyn could not save the local preferences file."));
        return false;
    }
    return true;
}

QByteArray UserPreferencesStore::protectApiKey(const QString& apiKey, QString* errorMessage)
{
    if (apiKey.isEmpty()) {
        return {};
    }

    const QByteArray utf8 = apiKey.toUtf8();
    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(utf8.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(utf8.constData()));

    static constexpr char kEntropyText[] = "Ausyn local API credential v1";
    DATA_BLOB entropy{};
    entropy.cbData = static_cast<DWORD>(sizeof(kEntropyText) - 1);
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropyText));

    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Ausyn AI credential", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        setError(errorMessage, QStringLiteral("Windows could not protect the API key for this user account."));
        return {};
    }

    const QByteArray protectedBytes(reinterpret_cast<const char*>(output.pbData),
                                    static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    return protectedBytes;
}

QString UserPreferencesStore::unprotectApiKey(const QByteArray& protectedKey, QString* errorMessage)
{
    if (protectedKey.isEmpty()) {
        return {};
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(protectedKey.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(protectedKey.constData()));

    static constexpr char kEntropyText[] = "Ausyn local API credential v1";
    DATA_BLOB entropy{};
    entropy.cbData = static_cast<DWORD>(sizeof(kEntropyText) - 1);
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropyText));

    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        setError(errorMessage, QStringLiteral("Windows could not unlock the saved API key for this user account."));
        return {};
    }

    const QString apiKey = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData),
                                             static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return apiKey;
}

QString UserPreferencesStore::conversationFilePath()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return appData.isEmpty() ? QString() : QDir(appData).filePath(QStringLiteral("conversation-history.json"));
}

QVector<ChatMessage> UserPreferencesStore::loadConversations(QString* errorMessage)
{
    QVector<ChatMessage> messages;
    const QString path = conversationFilePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return messages;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Saved conversations could not be opened."));
        return messages;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("Saved conversation history is unreadable."));
        return messages;
    }

    const QJsonArray array = document.object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        const QString role = object.value(QStringLiteral("role")).toString();
        const QString text = object.value(QStringLiteral("text")).toString();
        if ((role != QStringLiteral("user") && role != QStringLiteral("assistant")) || text.isEmpty()) continue;
        ChatMessage message;
        message.fromUser = role == QStringLiteral("user");
        message.text = text.left(8000);
        message.timestamp = QDateTime::fromMSecsSinceEpoch(
            object.value(QStringLiteral("timestampMs")).toVariant().toLongLong(),
            QTimeZone(QByteArrayLiteral("UTC"))).toLocalTime();
        messages.append(std::move(message));
    }
    if (messages.size() > 100) {
        messages.erase(messages.begin(), messages.end() - 100);
    }
    return messages;
}

bool UserPreferencesStore::saveConversations(const QVector<ChatMessage>& messages, QString* errorMessage)
{
    const QString path = conversationFilePath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        setError(errorMessage, QStringLiteral("Ausyn could not create the local conversation folder."));
        return false;
    }

    QJsonArray array;
    const qsizetype first = std::max<qsizetype>(0, messages.size() - 100);
    for (qsizetype i = first; i < messages.size(); ++i) {
        const ChatMessage& message = messages.at(i);
        QJsonObject object;
        object.insert(QStringLiteral("role"), message.fromUser ? QStringLiteral("user") : QStringLiteral("assistant"));
        object.insert(QStringLiteral("text"), message.text.left(8000));
        object.insert(QStringLiteral("timestampMs"), message.timestamp.toUTC().toMSecsSinceEpoch());
        array.append(object);
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QStringLiteral("Ausyn could not save local conversation history."));
        return false;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                        {QStringLiteral("messages"), array}})
                   .toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setError(errorMessage, QStringLiteral("Ausyn could not finish saving local conversation history."));
        return false;
    }
    return true;
}

bool UserPreferencesStore::deleteConversations(QString* errorMessage)
{
    const QString path = conversationFilePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return true;
    }
    if (!QFile::remove(path)) {
        setError(errorMessage, QStringLiteral("Saved conversations could not be deleted."));
        return false;
    }
    return true;
}

} // namespace Ausyn
