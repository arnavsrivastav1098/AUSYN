#pragma once

#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"
#include "../settings/user_preferences.h"
#include <QObject>
#include <QJsonArray>

class QNetworkAccessManager;
class QNetworkReply;

namespace Ausyn {
class AssistantClient final : public QObject {
    Q_OBJECT
public:
    explicit AssistantClient(QObject* parent = nullptr);
    [[nodiscard]] bool isBusy() const noexcept;
    void cancel();
    [[nodiscard]] static QString dataPreview(const QString& question, const SystemSnapshot& snapshot,
                                             const AnalysisUpdate& analysis,
                                             const UserPreferences& preferences,
                                             const QVector<ChatMessage>& conversation = {},
                                             const QString& workloadContext = {});
    [[nodiscard]] static QJsonArray conversationMessages(const QVector<ChatMessage>& conversation,
                                                         const SystemSnapshot& snapshot,
                                                         const UserPreferences& preferences);
    void ask(QString question, QString localFallback, SystemSnapshot snapshot,
             AnalysisUpdate analysis, UserPreferences preferences, bool useWebSearch = false,
             QVector<ChatMessage> conversation = {}, QString workloadContext = {});
signals:
    void responseReady(QString question, QString response, bool usedCloud, QString note);
private:
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* activeReply_ = nullptr;
};
} // namespace Ausyn
