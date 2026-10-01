#pragma once
#include "../monitoring/system_snapshot.h"
#include <QSet>
#include <optional>

namespace Ausyn {
struct WorkloadNotice {
    QString key, title, body;
    int resource = 1;
    bool critical = false;
};

// Bounded, session-only activity context. No window titles, URLs, keystrokes or
// file contents are collected. Foreground duration is evidence, not consent.
class WorkloadCoach final {
public:
    void observe(const SystemSnapshot& snapshot, bool sustainedPressure);
    void reset();
    bool keepCurrentApp();
    bool keepApp(const QString& name);
    void releaseApp();
    void beginComparison();
    [[nodiscard]] QString summary() const;
    [[nodiscard]] QString plan() const;
    [[nodiscard]] QString comparison() const;
    [[nodiscard]] QString appName() const { return confirmed_ ? keptApp_ : candidateApp_; }
    [[nodiscard]] bool confirmed() const { return confirmed_; }
    [[nodiscard]] bool continuedUse() const { return continuedUse_; }
    [[nodiscard]] bool fresh(const QDateTime& now) const;
    [[nodiscard]] std::optional<ProcessSample> backgroundCandidate() const;
    [[nodiscard]] std::optional<WorkloadNotice> notice() const { return notice_; }
    void markDelivered(const QString& key);
    [[nodiscard]] static QString resolveFollowup(const QString& question, const QVector<QString>& previousUserQuestions);
private:
    struct Point { QDateTime at; std::optional<double> cpu, memory; quint64 available = 0; };
    QVector<Point> points_;
    SystemSnapshot latest_;
    QString candidateApp_, keptApp_;
    QString backgroundApp_, notifiedBackgroundApp_;
    QDateTime backgroundSince_;
    QDateTime candidateSince_, candidateLastSeen_, episodeAt_, recoveryAt_, comparisonAt_;
    bool confirmed_ = false, continuedUse_ = false, pressure_ = false;
    int episode_ = 0, recoveryReadings_ = 0;
    std::optional<double> notifiedCpu_, notifiedMemory_;
    quint64 notifiedAvailable_ = 0;
    QSet<QString> delivered_;
    std::optional<WorkloadNotice> notice_;
    std::optional<WorkloadNotice> pendingRecovery_;
    std::optional<double> comparisonCpu_, comparisonMemory_;
    quint64 comparisonAvailable_ = 0;
    QString comparisonResult_;
};
}
