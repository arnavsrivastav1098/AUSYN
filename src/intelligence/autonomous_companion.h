#pragma once
#include "../monitoring/system_snapshot.h"
#include "../settings/user_preferences.h"
#include <QHash>
#include <QJsonObject>
#include <QStringList>

namespace Ausyn {
struct CompanionEvent {
    QDateTime at;
    QString key, title, body;
    int resource = 1;
    bool notify = false;
};
// Local, bounded inference from observed activity. A goal is guidance context;
// only an explicit executable allowlist authorizes an operating-system action.
class AutonomousCompanion final {
public:
    void configure(const UserPreferences& preferences);
    void observe(const SystemSnapshot& snapshot, bool pressure);
    void resetSession();
    void forget();
    QString goal() const { return goal_; }
    QString mode() const { return mode_; }
    QString status() const;
    QString detail() const;
    QString digest() const;
    QString journalText() const;
    QString profileSummary() const;
    QString runway() const { return runway_; }
    QVector<CompanionEvent> takeEvents();
    bool economical() const { return economical_; }
    std::optional<ProcessSample> automaticCandidate(const SystemSnapshot& snapshot, const QString& protectedApp) const;
    bool ruleAllows(const QString& path) const;
    static QString normalizedPath(const QString& path);
    static QString activityMode(const QString& name);
    bool canInterrupt(const QDateTime& now, bool urgent) const;
    void interrupted(const QDateTime& now, bool urgent);
    void defer(const QString& key, const QString& title);
    void beginAutomaticAction(const ProcessSample& target, const SystemSnapshot& before, const QString& protectedApp);
    // Returns a reason requiring restoration, once. Missing evidence is an undo,
    // never recorded as a successful intervention.
    QString takeUndoReason();
    QString actionGoal() const { return actionGoal_; }
    void endAutomaticAction(const QString& reason);
    bool actionPending() const { return actionAt_.isValid(); }
    QJsonObject state() const;
    bool restoreState(const QJsonObject& state, const QDateTime& now);
    bool load(const QString& path, const QDateTime& now);
    bool save(const QString& path, const QDateTime& now, bool force = false);
    int profileCount() const { return profiles_.size(); }
    int journalCount() const { return journal_.size(); }
private:
    struct Point { QDateTime at; double cpu = -1, memory = -1; quint64 available = 0, total = 0; };
    struct Profile {
        QString app; QDateTime lastSeen;
        int samples = 0, incidents = 0;
        double cpu = 0, memory = 0, cpuVariance = 0, memoryVariance = 0;
        qint64 observedSeconds = 0;
    };
    struct Outcome { int improved = 0, ineffective = 0; QDateTime blockedUntil; };
    void event(const QString& key, const QString& title, const QString& body, int resource = 1, bool notify = false, int cooldownSeconds = 600);
    void prune(const QDateTime& now);
    void updateRunway();
    bool pathApproved(const QString& path) const;
    UserPreferences preferences_;
    SystemSnapshot latest_;
    QVector<Point> points_;
    QHash<QString, Profile> profiles_;
    QHash<QString, Outcome> outcomes_;
    QVector<CompanionEvent> journal_, events_;
    QHash<QString, QDateTime> eventAt_;
    QHash<QString, QDateTime> busyBackgroundSince_;
    QVector<QDateTime> interruptions_;
    QStringList deferred_;
    QString foreground_, goal_, mode_, runway_, baseline_, undoReason_, actionPath_, actionGoal_;
    QDateTime foregroundAt_, foregroundLastSeen_, anomalyAt_, actionAt_, lastSaveAt_, lastProcessAt_;
    double actionCpu_ = 0, actionMemory_ = 0;
    int anomalySamples_ = 0, expensiveSamples_ = 0, cheapSamples_ = 0;
    int taskReadings_ = 0;
    double taskCpuSum_ = 0, taskMemoryPeak_ = -1;
    bool pressure_ = false, economical_ = false, dirty_ = false;
};
}
