#include "autonomous_companion.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {
bool valid(const std::optional<double>& value) { return value && std::isfinite(*value) && *value >= 0 && *value <= 100; }
bool appUsable(const QString& app) {
    static const QStringList excluded{QStringLiteral("ausyn.exe"), QStringLiteral("explorer.exe"), QStringLiteral("dwm.exe"), QStringLiteral("system"), QStringLiteral("svchost.exe"), QStringLiteral("searchhost.exe"), QStringLiteral("lockapp.exe")};
    return app.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) && app.size() <= 128 && !excluded.contains(app, Qt::CaseInsensitive) && !app.contains(QLatin1Char('/')) && !app.contains(QLatin1Char('\\'));
}
QString pct(double value) { return QStringLiteral("%1%").arg(value, 0, 'f', 0); }
bool same(const QString& a, const QString& b) { return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0; }
}
QString AutonomousCompanion::normalizedPath(const QString& path) {
    if (!QFileInfo(path).isAbsolute() || path.size() > 1024 || !path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}
QString AutonomousCompanion::activityMode(const QString& name) {
    const QString n = name.toLower();
    if (QStringList{QStringLiteral("msedge.exe"), QStringLiteral("chrome.exe"), QStringLiteral("firefox.exe"), QStringLiteral("brave.exe"), QStringLiteral("opera.exe")}.contains(n)) return QStringLiteral("Browser session");
    if (QStringList{QStringLiteral("gta5.exe"), QStringLiteral("gta5_enhanced.exe"), QStringLiteral("valorant-win64-shipping.exe"), QStringLiteral("cs2.exe"), QStringLiteral("fortniteclient-win64-shipping.exe"), QStringLiteral("minecraft.exe")}.contains(n)) return QStringLiteral("Game session");
    if (QStringList{QStringLiteral("teams.exe"), QStringLiteral("ms-teams.exe"), QStringLiteral("zoom.exe"), QStringLiteral("discord.exe")}.contains(n)) return QStringLiteral("Communication app");
    if (QStringList{QStringLiteral("code.exe"), QStringLiteral("devenv.exe"), QStringLiteral("clion64.exe"), QStringLiteral("qtcreator.exe")}.contains(n)) return QStringLiteral("Development session");
    if (QStringList{QStringLiteral("winword.exe"), QStringLiteral("excel.exe"), QStringLiteral("powerpnt.exe"), QStringLiteral("notepad.exe"), QStringLiteral("acrobat.exe")}.contains(n)) return QStringLiteral("Document work");
    if (QStringList{QStringLiteral("vlc.exe"), QStringLiteral("mpv.exe"), QStringLiteral("wmplayer.exe"), QStringLiteral("photoshop.exe"), QStringLiteral("blender.exe"), QStringLiteral("resolve.exe")}.contains(n)) return QStringLiteral("Media / creative app");
    return QStringLiteral("App session");
}
void AutonomousCompanion::configure(const UserPreferences& p) {
    if (preferences_.rememberActivity && !p.rememberActivity) { profiles_.clear(); outcomes_.clear(); dirty_ = true; }
    preferences_ = p;
    for (auto it = outcomes_.begin(); it != outcomes_.end();) {
        bool approved = false;
        for (const auto& path : p.automaticReliefPaths) if (normalizedPath(path) == it.key()) { approved = true; break; }
        if (!approved) it = outcomes_.erase(it); else ++it;
    }
    if (!p.autonomousCompanionEnabled || !p.monitoringEnabled || !p.workloadAwarenessEnabled) resetSession();
}
void AutonomousCompanion::resetSession() {
    latest_ = {}; points_.clear(); foreground_.clear(); goal_.clear(); mode_.clear(); runway_.clear(); baseline_.clear();
    foregroundAt_ = {}; foregroundLastSeen_ = {}; anomalyAt_ = {}; lastProcessAt_ = {}; anomalySamples_ = 0;
    busyBackgroundSince_.clear(); taskReadings_ = 0; taskCpuSum_ = 0; taskMemoryPeak_ = -1;
    pressure_ = false; economical_ = false; expensiveSamples_ = cheapSamples_ = 0;
    interruptions_.clear(); deferred_.clear(); events_.clear();
    if (actionAt_.isValid()) undoReason_ = QStringLiteral("Activity monitoring stopped; restore the temporary action.");
    actionAt_ = {}; actionGoal_.clear();
}
void AutonomousCompanion::forget() {
    resetSession(); profiles_.clear(); outcomes_.clear(); journal_.clear(); eventAt_.clear(); dirty_ = true;
}
void AutonomousCompanion::prune(const QDateTime& now) {
    for (auto it = profiles_.begin(); it != profiles_.end();) {
        if (!it->lastSeen.isValid() || it->lastSeen > now || it->lastSeen.daysTo(now) >= 30) it = profiles_.erase(it); else ++it;
    }
    while (profiles_.size() > 64) {
        auto oldest = profiles_.begin();
        for (auto it = profiles_.begin(); it != profiles_.end(); ++it) if (it->lastSeen < oldest->lastSeen) oldest = it;
        profiles_.erase(oldest);
    }
    while (!journal_.isEmpty() && (journal_.size() > 72 || journal_.first().at.daysTo(now) >= 7)) journal_.removeFirst();
    while (!interruptions_.isEmpty() && (interruptions_.first() > now || interruptions_.first().secsTo(now) >= 600)) interruptions_.removeFirst();
    for (auto it = eventAt_.begin(); it != eventAt_.end();) {
        if (it.value() > now || it.value().secsTo(now) > 3600) it = eventAt_.erase(it); else ++it;
    }
    if (eventAt_.size() > 192) eventAt_.clear();
}
void AutonomousCompanion::event(const QString& key, const QString& title, const QString& body, int resource, bool notify, int cooldown) {
    const auto at = latest_.capturedAt;
    if (!at.isValid()) return;
    const auto previous = eventAt_.value(key);
    if (previous.isValid() && previous.secsTo(at) >= 0 && previous.secsTo(at) < cooldown) return;
    eventAt_.insert(key, at);
    const CompanionEvent e{at, key.left(180), title.left(160), body.left(900), resource, notify};
    journal_.append(e); events_.append(e); dirty_ = true;
    while (journal_.size() > 72) journal_.removeFirst();
    while (events_.size() > 24) events_.removeFirst();
}
void AutonomousCompanion::observe(const SystemSnapshot& s, bool pressure) {
    if (!preferences_.autonomousCompanionEnabled || !preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled ||
        !s.capturedAt.isValid() || (latest_.capturedAt.isValid() && s.capturedAt <= latest_.capturedAt)) return;
    const qint64 gap = latest_.capturedAt.isValid() ? latest_.capturedAt.secsTo(s.capturedAt) : 0;
    const bool interrupted = gap > std::max(15, std::clamp(s.samplingIntervalSeconds, 1, 10) * 3);
    if (interrupted) {
        points_.clear(); foregroundAt_ = {}; anomalyAt_ = {}; anomalySamples_ = 0;
        busyBackgroundSince_.clear(); taskReadings_ = 0; taskCpuSum_ = 0; taskMemoryPeak_ = -1;
        foregroundLastSeen_ = {}; pressure_ = false;
        goal_.clear(); mode_.clear(); baseline_.clear(); lastProcessAt_ = {};
        if (actionAt_.isValid()) { undoReason_ = QStringLiteral("Readings were interrupted; the action cannot be verified."); actionAt_ = {}; }
    }
    latest_ = s; prune(s.capturedAt);
    if (interrupted) event(QStringLiteral("resume"), QStringLiteral("Watching fresh readings again"), QStringLiteral("A %1-second sampling gap interrupted the earlier context. Forecasts and action comparisons restarted with fresh evidence.").arg(gap), 0, false);
    const bool memoryValid = valid(s.memoryUsagePercent) && s.memoryTotalBytes > 0 && s.memoryAvailableBytes <= s.memoryTotalBytes;
    points_.append({s.capturedAt, valid(s.processorUsagePercent) ? *s.processorUsagePercent : -1,
        memoryValid ? *s.memoryUsagePercent : -1, memoryValid ? s.memoryAvailableBytes : 0, memoryValid ? s.memoryTotalBytes : 0});
    while (points_.size() > 120 || points_.first().at.secsTo(s.capturedAt) > 180) points_.removeFirst();
    const qint64 processAge = s.processSamplesCapturedAt.isValid() ? s.processSamplesCapturedAt.secsTo(s.capturedAt) : -1;
    const bool foregroundFresh = processAge >= 0 && processAge <= 30 && s.foregroundProcessId != 0 && appUsable(s.foregroundProcessName);
    if (foregroundFresh) {
        if (!same(foreground_, s.foregroundProcessName) || !foregroundAt_.isValid() || (foregroundLastSeen_.isValid() && foregroundLastSeen_.secsTo(s.capturedAt) > 30)) {
            foreground_ = s.foregroundProcessName; foregroundAt_ = s.capturedAt; anomalyAt_ = {}; anomalySamples_ = 0;
        }
        foregroundLastSeen_ = s.capturedAt;
        if (foregroundAt_.secsTo(s.capturedAt) >= 20 && !same(goal_, foreground_)) {
            const QString previous = goal_;
            if (!previous.isEmpty() && taskReadings_ >= 4) event(QStringLiteral("recap/") + previous.toLower(), QStringLiteral("Previous task recap"),
                QStringLiteral("During %1’s stable foreground observations, system CPU averaged %2 and RAM peaked at %3 across %4 readings. You have switched tasks; earlier load is not assumed to apply to the new one.")
                    .arg(previous, pct(taskCpuSum_ / taskReadings_), taskMemoryPeak_ < 0 ? QStringLiteral("unavailable") : pct(taskMemoryPeak_)).arg(taskReadings_), 0, false, 0);
            taskReadings_ = 0; taskCpuSum_ = 0; taskMemoryPeak_ = -1;
            goal_ = foreground_; mode_ = activityMode(goal_);
            event(QStringLiteral("focus/") + goal_.toLower(), QStringLiteral("Following your current task"),
                QStringLiteral("%1 has stayed in front across observations. Guidance now follows this %2; no app setting was changed. %3")
                .arg(goal_, mode_.toLower(), previous.isEmpty() ? QString{} : QStringLiteral("Previous focus: %1.").arg(previous)), 0, false, 120);
        }
    } else if (!same(s.foregroundProcessName, QStringLiteral("ausyn.exe"))) {
        foregroundAt_ = {}; anomalyAt_ = {}; anomalySamples_ = 0;
    }
    if (foregroundLastSeen_.isValid() && foregroundLastSeen_.secsTo(s.capturedAt) > 45) { goal_.clear(); mode_.clear(); baseline_.clear(); }
    if (foregroundFresh && same(goal_, foreground_) && valid(s.processorUsagePercent) && taskReadings_ < 100000) {
        ++taskReadings_; taskCpuSum_ += *s.processorUsagePercent;
        if (memoryValid) taskMemoryPeak_ = std::max(taskMemoryPeak_, *s.memoryUsagePercent);
    }
    QHash<QString, QDateTime> currentlyBusy;
    const bool newProcessReading = processAge >= 0 && processAge <= 30 && (!lastProcessAt_.isValid() || s.processSamplesCapturedAt > lastProcessAt_);
    if (newProcessReading && lastProcessAt_.isValid() && lastProcessAt_.secsTo(s.processSamplesCapturedAt) > std::max(15, s.samplingIntervalSeconds * 3)) busyBackgroundSince_.clear();
    if (newProcessReading) for (const auto& p : s.topProcesses) {
        if (same(p.name, goal_) || !ruleAllows(p.executablePath) || !valid(p.cpuPercent) || *p.cpuPercent < 5 || p.processId == s.foregroundProcessId) continue;
        const QString identity = normalizedPath(p.executablePath) + QStringLiteral("/%1").arg(p.processId);
        currentlyBusy.insert(identity, busyBackgroundSince_.value(identity, s.capturedAt));
        if (currentlyBusy.size() >= 32) break;
    }
    if (newProcessReading) { busyBackgroundSince_ = currentlyBusy; lastProcessAt_ = s.processSamplesCapturedAt; }
    else if (processAge < 0 || processAge > 30) busyBackgroundSince_.clear();
    if (pressure && !pressure_ && !goal_.isEmpty()) {
        if (preferences_.rememberActivity) {
            auto& p = profiles_[goal_.toLower()]; p.app = goal_; p.lastSeen = s.capturedAt; p.incidents = std::min(9999, p.incidents + 1);
            if (p.incidents >= 3) event(QStringLiteral("repeat/") + goal_.toLower(), QStringLiteral("This task has run into pressure before"),
                QStringLiteral("%1 local pressure episodes were observed while %2 was your task. This is an association, not proof that the app caused them. Review its workload or consider more RAM if this pattern keeps recurring.").arg(p.incidents).arg(goal_), 1, true, 1800);
        }
        event(QStringLiteral("pressure/") + goal_.toLower(), QStringLiteral("Protecting your task context"), QStringLiteral("Continuing %1 during resource pressure. Approved background rules will be considered; otherwise Ausyn prepares advice without closing apps.").arg(goal_), 1, false, 60);
    }
    if (!pressure && pressure_) event(QStringLiteral("recovered"), QStringLiteral("Resource pressure settled"), QStringLiteral("The sustained pressure detector is clear. Check whether your current task feels smoother; calmer readings do not prove a specific repair."), 1, false, 60);
    pressure_ = pressure;
    baseline_.clear();
    if (preferences_.rememberActivity && foregroundFresh && same(goal_, foreground_) && foregroundAt_.secsTo(s.capturedAt) >= 20 && valid(s.processorUsagePercent) && memoryValid) {
        auto& p = profiles_[goal_.toLower()]; p.app = goal_; p.lastSeen = s.capturedAt;
        if (p.samples >= 30) {
            baseline_ = QStringLiteral("During %1’s foreground observations: usual system CPU %2, RAM %3 (%4 readings).")
                .arg(goal_, pct(p.cpu), pct(p.memory)).arg(p.samples);
            const bool unusual = *s.processorUsagePercent > p.cpu + std::max(20.0, 2.5 * std::sqrt(std::max(0.0, p.cpuVariance))) ||
                *s.memoryUsagePercent > p.memory + std::max(8.0, 2.5 * std::sqrt(std::max(0.0, p.memoryVariance)));
            if (unusual) {
                if (!anomalyAt_.isValid()) anomalyAt_ = s.capturedAt;
                if (++anomalySamples_ >= 4 && anomalyAt_.secsTo(s.capturedAt) >= 20)
                    event(QStringLiteral("unusual/") + goal_.toLower(), QStringLiteral("This session is heavier than usual"),
                        baseline_ + QStringLiteral(" Now CPU %1, RAM %2. Extra tabs, another app or an update may explain the difference; I have not inspected your content.").arg(pct(*s.processorUsagePercent), pct(*s.memoryUsagePercent)), 1, true, 1800);
            } else { anomalyAt_ = {}; anomalySamples_ = 0; }
        }
        // Time-weighted bounded EMA. Do not teach an established baseline from
        // an active incident; irregular gaps never add unobserved dwell time.
        if (p.samples < 30 || !pressure) {
            const double alpha = p.samples < 30 ? 1.0 / (p.samples + 1) : std::clamp(static_cast<double>(gap) / 600.0, 0.002, 0.05);
            const double cpuDelta = *s.processorUsagePercent - p.cpu, memoryDelta = *s.memoryUsagePercent - p.memory;
            p.cpuVariance = (1 - alpha) * (p.cpuVariance + alpha * cpuDelta * cpuDelta);
            p.memoryVariance = (1 - alpha) * (p.memoryVariance + alpha * memoryDelta * memoryDelta);
            p.cpu += alpha * cpuDelta; p.memory += alpha * memoryDelta; p.samples = std::min(100000, p.samples + 1);
        }
        if (!interrupted) p.observedSeconds = std::min<qint64>(30 * 86400, p.observedSeconds + std::clamp<qint64>(gap, 0, 30));
        dirty_ = true; prune(s.capturedAt);
    }
    updateRunway();
    if (runway_.startsWith(QStringLiteral("RAM headroom may"))) event(QStringLiteral("runway"), QStringLiteral("RAM headroom is shrinking"), runway_, 1, true, 600);
    if (s.batteryOnAcPower && !*s.batteryOnAcPower && s.batteryEstimatedSeconds && *s.batteryEstimatedSeconds > 0 && *s.batteryEstimatedSeconds != 0xffffffffu &&
        *s.batteryEstimatedSeconds < static_cast<unsigned int>(preferences_.workSessionMinutes * 60))
        event(QStringLiteral("battery-goal"), QStringLiteral("Your planned session may outlast the battery"), QStringLiteral("Windows currently estimates %1 minutes remaining; your session target is %2 minutes. Plug in or reduce the workload before the battery becomes urgent. Runtime changes with load; no power plan was changed.")
            .arg(*s.batteryEstimatedSeconds / 60).arg(preferences_.workSessionMinutes), 4, true, 1800);
    bool expensive = s.collectionDurationMicroseconds > static_cast<qint64>(std::clamp(s.samplingIntervalSeconds, 1, 10)) * 200000;
    for (const auto& p : s.topProcesses) if (processAge >= 0 && processAge <= 30 && same(p.name, QStringLiteral("ausyn.exe"))) {
        expensive = expensive || (valid(p.cpuPercent) && *p.cpuPercent > 3) || (p.workingSetBytes && *p.workingSetBytes > 300ULL * 1024 * 1024);
    }
    if (expensive) { ++expensiveSamples_; cheapSamples_ = 0; } else { expensiveSamples_ = 0; ++cheapSamples_; }
    if (!economical_ && expensiveSamples_ >= 6) {
        economical_ = true; event(QStringLiteral("self-budget"), QStringLiteral("Ausyn reduced its own activity"), QStringLiteral("Repeated collector cost, Ausyn CPU above 3%, or memory above 300 MB triggered economical sampling and disabled chart animations. Monitoring remains active."), 0, false);
    } else if (economical_ && cheapSamples_ >= 12) {
        economical_ = false; event(QStringLiteral("self-budget-restored"), QStringLiteral("Ausyn’s resource budget recovered"), QStringLiteral("Repeated lower-cost samples allow your chosen cadence and animations again."), 0, false);
    }
    if (actionAt_.isValid()) {
        if (foregroundFresh && !same(s.foregroundProcessName, actionGoal_)) {
            undoReason_ = QStringLiteral("Your foreground task changed; restore the action made for the earlier task."); actionAt_ = {};
        } else if (actionAt_.secsTo(s.capturedAt) >= 30) {
            double cpu = 0, memory = 0; int count = 0; QDateTime first;
            for (const auto& point : points_) if (point.at > actionAt_ && point.cpu >= 0 && point.memory >= 0) {
                if (!first.isValid()) first = point.at;
                cpu += point.cpu; memory += point.memory; ++count;
            }
            if (count >= 4 && first.secsTo(s.capturedAt) >= 20) {
                cpu /= count; memory /= count;
                const bool improved = cpu <= actionCpu_ - 8 && memory <= actionMemory_ + 3;
                auto& outcome = outcomes_[actionPath_];
                if (improved) outcome.improved = std::min(9999, outcome.improved + 1);
                else { outcome.ineffective = std::min(9999, outcome.ineffective + 1); outcome.blockedUntil = s.capturedAt.addSecs(1800); undoReason_ = QStringLiteral("No measured CPU relief, or RAM worsened; restoring the original priority."); }
                event(QStringLiteral("verification/") + actionPath_, improved ? QStringLiteral("Measured relief after the approved action") : QStringLiteral("The approved action did not show relief"),
                    QStringLiteral("System CPU baseline %1 → %2 average; RAM %3 → %4 over %5 readings. %6 This measures system load, not app responsiveness or proof of cause.")
                        .arg(pct(actionCpu_), pct(cpu), pct(actionMemory_), pct(memory)).arg(count)
                        .arg(improved ? QStringLiteral("The lease may continue until focus changes or expiry.") : QStringLiteral("Undo requested; this rule waits 30 minutes. Two ineffective outcomes suspend it until you approve it again.")), 0, true, 0);
                dirty_ = true; actionAt_ = {};
            } else if (actionAt_.secsTo(s.capturedAt) > 60) {
                undoReason_ = QStringLiteral("Too few valid readings to verify the action; restoring it."); actionAt_ = {};
            }
        }
    }
    prune(s.capturedAt);
}
void AutonomousCompanion::updateRunway() {
    runway_ = QStringLiteral("RAM forecast needs a continuous, steady decline in available memory.");
    if (points_.size() < 6) return;
    const auto& first = points_.first(); const auto& last = points_.last();
    const double seconds = static_cast<double>(first.at.msecsTo(last.at)) / 1000;
    if (seconds < 30 || last.total == 0 || last.memory < 75 || last.available < 256ULL * 1024 * 1024) return;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0; int declines = 0;
    for (qsizetype i = 0; i < points_.size(); ++i) {
        const auto& point = points_[i];
        if (point.memory < 0 || point.total != last.total || point.available > point.total) return;
        if (i > 0 && points_[i - 1].at.secsTo(point.at) > std::max(15, latest_.samplingIntervalSeconds * 3)) return;
        if (i > 0 && point.available < points_[i - 1].available) ++declines;
        const double x = static_cast<double>(first.at.msecsTo(point.at)) / 1000;
        const double y = static_cast<double>(point.available) / (1024 * 1024);
        sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
    }
    const double n = static_cast<double>(points_.size()), dx = n * sxx - sx * sx, dy = n * syy - sy * sy;
    if (dx <= 0 || dy <= 0 || declines * 4 < (points_.size() - 1) * 3) return;
    const double covariance = n * sxy - sx * sy, slope = covariance / dx;
    const double rSquared = covariance * covariance / (dx * dy);
    if (slope >= -1 || rSquared < 0.85) return;
    const double remaining = (static_cast<double>(last.available) / (1024 * 1024) - 256) / -slope;
    if (remaining < 15 || remaining > 300) return;
    runway_ = QStringLiteral("RAM headroom may reach 256 MB in roughly %1–%2 minutes if the recent decline continues (%3 readings, %4 seconds). Reduce unused workload or save progress. This short trend can change; it is not a guaranteed time.")
        .arg(std::max(1, static_cast<int>(remaining * 0.7 / 60))).arg(std::max(1, static_cast<int>(std::ceil(remaining * 1.3 / 60))))
        .arg(points_.size()).arg(static_cast<int>(seconds));
}
QString AutonomousCompanion::status() const {
    if (!preferences_.monitoringEnabled) return QStringLiteral("Paused · no background decisions");
    if (!preferences_.autonomousCompanionEnabled || !preferences_.workloadAwarenessEnabled) return QStringLiteral("Companion disabled · telemetry follows your settings");
    if (goal_.isEmpty()) return QStringLiteral("Observing · waiting for a stable foreground task");
    return QStringLiteral("%1 · following %2 · %3").arg(mode_, goal_, pressure_ ? QStringLiteral("managing resource pressure") : QStringLiteral("watching for changes"));
}
QString AutonomousCompanion::detail() const {
    return status() + QStringLiteral("\n\n") + runway_ + (baseline_.isEmpty() ? QString{} : QStringLiteral("\n\n") + baseline_) +
        QStringLiteral("\n\nApproved background executables: %1. Learning %2. Session target: %3 minutes. Activity modes are inferred from executable names; Ausyn cannot see your thoughts, page contents or whether a call is actually in progress.")
        .arg(preferences_.automaticReliefPaths.size()).arg(preferences_.rememberActivity ? QStringLiteral("local, 30-day app aggregates") : QStringLiteral("off; current session only")).arg(preferences_.workSessionMinutes);
}
QString AutonomousCompanion::digest() const {
    QStringList lines{status()};
    if (!deferred_.isEmpty()) lines << QStringLiteral("Grouped to avoid interruptions: %1").arg(deferred_.join(QStringLiteral(" · ")));
    int count = 0;
    for (auto it = journal_.crbegin(); it != journal_.crend() && count < 6; ++it, ++count)
        lines << QStringLiteral("%1 · %2\n%3").arg(it->at.toLocalTime().toString(QStringLiteral("h:mm ap")), it->title, it->body);
    if (journal_.isEmpty()) lines << QStringLiteral("No meaningful activity changes recorded yet. Ausyn continues monitoring without inventing findings.");
    return lines.join(QStringLiteral("\n\n"));
}
QString AutonomousCompanion::profileSummary() const {
    if (!preferences_.rememberActivity) return QStringLiteral("App learning is off. Enable it to remember typical system load and recurring pressure while each app is in front.");
    QVector<Profile> sorted; for (const auto& p : profiles_) sorted.append(p);
    std::sort(sorted.begin(), sorted.end(), [](const Profile& a, const Profile& b) { return a.observedSeconds > b.observedSeconds; });
    QStringList lines;
    for (const auto& p : sorted.mid(0, 8)) lines << QStringLiteral("%1 · %2 observed minutes · %3 readings · CPU %4 / RAM %5 · %6 pressure episodes")
        .arg(p.app).arg(p.observedSeconds / 60).arg(p.samples).arg(pct(p.cpu), pct(p.memory)).arg(p.incidents);
    return lines.isEmpty() ? QStringLiteral("Learning starts after an app remains in front for 20 seconds. No saved profile yet.") : lines.join(QLatin1Char('\n'));
}
QString AutonomousCompanion::journalText() const {
    QStringList lines{preferences_.rememberActivity ? QStringLiteral("Local remembered decisions · up to 72 entries / 7 days") : QStringLiteral("Session decisions · not saved after exit")};
    for (auto it = journal_.crbegin(); it != journal_.crend(); ++it)
        lines << QStringLiteral("%1 · %2\n%3").arg(it->at.toLocalTime().toString(QStringLiteral("dd MMM, h:mm:ss ap")), it->title, it->body);
    if (journal_.isEmpty()) lines << QStringLiteral("No meaningful background decisions recorded yet.");
    return lines.join(QStringLiteral("\n\n"));
}
QVector<CompanionEvent> AutonomousCompanion::takeEvents() { auto result = events_; events_.clear(); return result; }
bool AutonomousCompanion::ruleAllows(const QString& path) const {
    return preferences_.automaticReliefEnabled && pathApproved(path);
}
bool AutonomousCompanion::pathApproved(const QString& path) const {
    const auto normalized = normalizedPath(path);
    if (normalized.isEmpty()) return false;
    for (const auto& allowed : preferences_.automaticReliefPaths) if (normalizedPath(allowed) == normalized) return true;
    return false;
}
std::optional<ProcessSample> AutonomousCompanion::automaticCandidate(const SystemSnapshot& s, const QString& protectedApp) const {
    if (!preferences_.autonomousCompanionEnabled || !preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled || !pressure_ || goal_.isEmpty() || !same(goal_, protectedApp) || !same(goal_, s.foregroundProcessName) ||
        !foregroundAt_.isValid() || foregroundAt_.secsTo(s.capturedAt) < 20 || !valid(s.processorUsagePercent) || *s.processorUsagePercent < 75 || actionAt_.isValid()) return {};
    const qint64 age = s.processSamplesCapturedAt.isValid() ? s.processSamplesCapturedAt.msecsTo(s.capturedAt) : -1;
    if (age < 0 || age > 30000 || points_.size() < 4) return {};
    for (int i = 0; i < 4; ++i) if (points_[points_.size() - 1 - i].cpu < 75) return {};
    if (points_[points_.size() - 4].at.secsTo(s.capturedAt) < 15) return {};
    std::optional<ProcessSample> best;
    for (const auto& p : s.topProcesses) {
        if (same(p.name, protectedApp) || p.processId == s.foregroundProcessId || !appUsable(p.name) || !valid(p.cpuPercent) || *p.cpuPercent < 5 || !ruleAllows(p.executablePath)) continue;
        const auto path = normalizedPath(p.executablePath); const auto outcome = outcomes_.value(path);
        const auto busyAt = busyBackgroundSince_.value(path + QStringLiteral("/%1").arg(p.processId));
        if (!busyAt.isValid() || busyAt.secsTo(s.capturedAt) < 20) continue;
        if (outcome.ineffective >= 2 || (outcome.blockedUntil.isValid() && s.capturedAt < outcome.blockedUntil)) continue;
        if (!best || *p.cpuPercent > best->cpuPercent.value_or(0)) best = p;
    }
    return best;
}
bool AutonomousCompanion::canInterrupt(const QDateTime& now, bool urgent) const {
    if (!preferences_.smartAttentionEnabled || urgent) return true;
    int count = 0; for (const auto& at : interruptions_) if (at <= now && at.secsTo(now) < 600) ++count;
    return count < preferences_.attentionBudget;
}
void AutonomousCompanion::interrupted(const QDateTime& now, bool urgent) {
    if (!urgent && now.isValid()) { interruptions_.append(now); while (interruptions_.size() > 12) interruptions_.removeFirst(); }
}
void AutonomousCompanion::defer(const QString& key, const QString& title) {
    Q_UNUSED(key);
    if (!deferred_.contains(title)) deferred_.append(title.left(160));
    while (deferred_.size() > 8) deferred_.removeFirst();
}
void AutonomousCompanion::beginAutomaticAction(const ProcessSample& p, const SystemSnapshot& s, const QString& goal) {
    actionAt_ = s.capturedAt; actionPath_ = normalizedPath(p.executablePath); actionGoal_ = goal;
    actionCpu_ = s.processorUsagePercent.value_or(0); actionMemory_ = s.memoryUsagePercent.value_or(0); undoReason_.clear();
    event(QStringLiteral("action/") + actionPath_, QStringLiteral("Applied your background rule"), QStringLiteral("%1 temporarily uses Below Normal CPU priority while %2 stays foreground. Checking the next 30 seconds; no RAM was freed and no app was closed.").arg(p.name, goal), 0, true, 0);
}
QString AutonomousCompanion::takeUndoReason() { auto reason = undoReason_; undoReason_.clear(); return reason; }
void AutonomousCompanion::endAutomaticAction(const QString& reason) {
    actionAt_ = {}; actionGoal_.clear();
    if (!reason.isEmpty()) event(QStringLiteral("undo"), QStringLiteral("Background rule ended"), reason, 0, false, 0);
}
QJsonObject AutonomousCompanion::state() const {
    QJsonArray profiles, journal, outcomes;
    if (preferences_.rememberActivity) {
        for (const auto& p : profiles_) profiles.append(QJsonObject{{QStringLiteral("app"), p.app}, {QStringLiteral("last"), p.lastSeen.toString(Qt::ISODate)},
            {QStringLiteral("samples"), p.samples}, {QStringLiteral("incidents"), p.incidents}, {QStringLiteral("seconds"), static_cast<double>(p.observedSeconds)},
            {QStringLiteral("cpu"), p.cpu}, {QStringLiteral("memory"), p.memory}, {QStringLiteral("cpuVariance"), p.cpuVariance}, {QStringLiteral("memoryVariance"), p.memoryVariance}});
        for (const auto& e : journal_) journal.append(QJsonObject{{QStringLiteral("at"), e.at.toString(Qt::ISODate)}, {QStringLiteral("key"), e.key}, {QStringLiteral("title"), e.title}, {QStringLiteral("body"), e.body}, {QStringLiteral("resource"), e.resource}});
        for (auto it = outcomes_.cbegin(); it != outcomes_.cend(); ++it) if (pathApproved(it.key())) outcomes.append(QJsonObject{{QStringLiteral("path"), it.key()}, {QStringLiteral("improved"), it->improved}, {QStringLiteral("ineffective"), it->ineffective}, {QStringLiteral("until"), it->blockedUntil.toString(Qt::ISODate)}});
    }
    return {{QStringLiteral("version"), 1}, {QStringLiteral("profiles"), profiles}, {QStringLiteral("journal"), journal}, {QStringLiteral("outcomes"), outcomes}};
}
bool AutonomousCompanion::restoreState(const QJsonObject& object, const QDateTime& now) {
    if (!preferences_.rememberActivity || object.value(QStringLiteral("version")).toInt() != 1 || !now.isValid()) return false;
    profiles_.clear(); journal_.clear(); outcomes_.clear();
    const auto profileArray = object.value(QStringLiteral("profiles")).toArray();
    for (const auto& value : profileArray) {
        if (profiles_.size() >= 64) break;
        const auto p = value.toObject(); const QString app = p.value(QStringLiteral("app")).toString();
        const auto at = QDateTime::fromString(p.value(QStringLiteral("last")).toString(), Qt::ISODate);
        const double cpu = p.value(QStringLiteral("cpu")).toDouble(-1), memory = p.value(QStringLiteral("memory")).toDouble(-1);
        const int samples = p.value(QStringLiteral("samples")).toInt(-1);
        if (!appUsable(app) || !at.isValid() || at > now || at.daysTo(now) >= 30 || !std::isfinite(cpu) || !std::isfinite(memory) || cpu < 0 || cpu > 100 || memory < 0 || memory > 100 || samples < 0 || samples > 100000) continue;
        Profile profile; profile.app = app; profile.lastSeen = at; profile.cpu = cpu; profile.memory = memory; profile.samples = samples;
        profile.incidents = std::clamp(p.value(QStringLiteral("incidents")).toInt(), 0, 9999);
        profile.observedSeconds = static_cast<qint64>(std::clamp(p.value(QStringLiteral("seconds")).toDouble(), 0.0, 30.0 * 86400));
        profile.cpuVariance = std::clamp(p.value(QStringLiteral("cpuVariance")).toDouble(), 0.0, 10000.0);
        profile.memoryVariance = std::clamp(p.value(QStringLiteral("memoryVariance")).toDouble(), 0.0, 10000.0);
        profiles_.insert(app.toLower(), profile);
    }
    for (const auto& value : object.value(QStringLiteral("journal")).toArray()) {
        if (journal_.size() >= 72) break;
        const auto e = value.toObject(); const auto at = QDateTime::fromString(e.value(QStringLiteral("at")).toString(), Qt::ISODate);
        if (!at.isValid() || at > now || at.daysTo(now) >= 7) continue;
        journal_.append({at, e.value(QStringLiteral("key")).toString().left(180), e.value(QStringLiteral("title")).toString().left(160), e.value(QStringLiteral("body")).toString().left(900), std::clamp(e.value(QStringLiteral("resource")).toInt(), 0, 5), false});
    }
    std::sort(journal_.begin(), journal_.end(), [](const CompanionEvent& a, const CompanionEvent& b) { return a.at < b.at; });
    for (const auto& value : object.value(QStringLiteral("outcomes")).toArray()) {
        if (outcomes_.size() >= 8) break;
        const auto o = value.toObject(); const auto path = normalizedPath(o.value(QStringLiteral("path")).toString());
        if (!pathApproved(path)) continue;
        Outcome outcome; outcome.improved = std::clamp(o.value(QStringLiteral("improved")).toInt(), 0, 9999);
        outcome.ineffective = std::clamp(o.value(QStringLiteral("ineffective")).toInt(), 0, 9999);
        outcome.blockedUntil = QDateTime::fromString(o.value(QStringLiteral("until")).toString(), Qt::ISODate);
        if (outcome.blockedUntil > now.addSecs(1800)) outcome.blockedUntil = now.addSecs(1800);
        outcomes_.insert(path, outcome);
    }
    dirty_ = false; return true;
}
bool AutonomousCompanion::load(const QString& path, const QDateTime& now) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 512 * 1024) return false;
    QJsonParseError error; const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject() && restoreState(document.object(), now);
}
bool AutonomousCompanion::save(const QString& path, const QDateTime& now, bool force) {
    if (!dirty_ || !preferences_.rememberActivity) return true;
    if (!force && lastSaveAt_.isValid() && lastSaveAt_.secsTo(now) >= 0 && lastSaveAt_.secsTo(now) < 60) return true;
    lastSaveAt_ = now;
    const auto data = QJsonDocument(state()).toJson(QJsonDocument::Compact);
    if (data.size() > 512 * 1024 || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return false;
    dirty_ = false; return true;
}
}
