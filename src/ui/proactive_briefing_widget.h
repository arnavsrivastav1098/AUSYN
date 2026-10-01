#pragma once

#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"
#include "../intelligence/event_log_types.h"

#include <QSet>
#include <QHash>
#include <QDateTime>
#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QTimer;
class QVBoxLayout;
class QWidget;

namespace Ausyn {

class ProactiveBriefingWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ProactiveBriefingWidget(QWidget* parent = nullptr);
    void setBriefing(const AnalysisUpdate& analysis, const SystemSnapshot& snapshot,
                     const EventLogUpdate& eventLogs,
                     const QStringList& securityNotices, const QStringList& eventNotices,
                     const QStringList& startupNotices);
    void setPersistentState(const QStringList& dismissedKeys,
                           const QHash<QString, QDateTime>& snoozedKeys);
    [[nodiscard]] QStringList dismissedKeys() const;
    [[nodiscard]] QHash<QString, QDateTime> snoozedKeys() const;
    void setDetailed(bool detailed);
protected:
    void showEvent(QShowEvent* event) override;

signals:
    void reviewRequested(int pageIndex, const QString& title);
    void askAboutRequested(const QString& category, const QString& title,
                           const QString& summary, const QString& evidence,
                           const QString& nextStep);
    void persistentStateChanged(const QStringList& dismissedKeys,
                                const QHash<QString, QDateTime>& snoozedKeys);

private:
    struct Card {
        QString key;
        QString category;
        QString title;
        QString summary;
        QString evidence;
        QString nextStep;
        QString actionLabel;
        int pageIndex = 6;
        int priority = 0;
    };

    void rebuild();
    QVector<Card> cards_;
    QSet<QString> dismissed_;
    QHash<QString, QDateTime> snoozed_;
    QComboBox* filter_ = nullptr;
    QComboBox* snoozeDuration_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* currentRead_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QWidget* listContent_ = nullptr;
    QString sampleStatus_;
    QDateTime previousSampleAt_;
    std::optional<double> previousCpuPercent_;
    std::optional<double> previousGpuPercent_;
    std::optional<double> previousMemoryPercent_;
    QDateTime lastCpuChangeReportedAt_;
    QDateTime lastGpuChangeReportedAt_;
    QDateTime lastMemoryChangeReportedAt_;
    bool showDismissed_ = false;
    QTimer* expiryTimer_ = nullptr;
    bool detailed_ = false;
    QString renderedLayoutKey_;
    QHash<QString, QWidget*> renderedCards_;
};

} // namespace Ausyn
