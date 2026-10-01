#include "workload_coach.h"
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {
bool valid(const std::optional<double>& v) { return v && std::isfinite(*v) && *v >= 0 && *v <= 100; }
QString percent(const std::optional<double>& v) { return valid(v) ? QStringLiteral("%1%").arg(*v, 0, 'f', 0) : QStringLiteral("unavailable"); }
QString ram(quint64 bytes) { return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / 1'000'000'000.0, 0, 'f', 2); }
QString available(const SystemSnapshot& s) { return s.memoryTotalBytes > 0 && s.memoryAvailableBytes <= s.memoryTotalBytes && valid(s.memoryUsagePercent) ? ram(s.memoryAvailableBytes) : QStringLiteral("RAM headroom unavailable"); }
bool sameApp(const QString& a, const QString& b) { return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0; }
bool excluded(const QString& name) {
    static const QSet<QString> names{QStringLiteral("ausyn.exe"), QStringLiteral("system"), QStringLiteral("registry"),
        QStringLiteral("idle"), QStringLiteral("system idle process"), QStringLiteral("svchost.exe"),
        QStringLiteral("csrss.exe"), QStringLiteral("wininit.exe"), QStringLiteral("winlogon.exe"),
        QStringLiteral("lsass.exe"), QStringLiteral("services.exe"), QStringLiteral("smss.exe"),
        QStringLiteral("dwm.exe"), QStringLiteral("explorer.exe"), QStringLiteral("msmpeng.exe")};
    return name.isEmpty() || names.contains(name.toLower());
}
}

void WorkloadCoach::reset() { *this = WorkloadCoach{}; }
bool WorkloadCoach::fresh(const QDateTime& now) const {
    const qint64 age = latest_.capturedAt.isValid() ? latest_.capturedAt.msecsTo(now) : -1;
    return age >= 0 && age <= std::max(15, latest_.samplingIntervalSeconds * 3) * 1000LL;
}
void WorkloadCoach::observe(const SystemSnapshot& s, bool sustainedPressure) {
    if (!s.capturedAt.isValid() || (latest_.capturedAt.isValid() && s.capturedAt <= latest_.capturedAt)) return;
    if (latest_.capturedAt.isValid() && latest_.capturedAt.msecsTo(s.capturedAt) > std::max(15, s.samplingIntervalSeconds * 3) * 1000LL) {
        const QString keep = keptApp_; const bool confirmed = confirmed_;
        reset(); keptApp_ = keep; confirmed_ = confirmed;
    }
    latest_ = s;
    if (!valid(latest_.processorUsagePercent)) latest_.processorUsagePercent.reset();
    if (!valid(latest_.memoryUsagePercent) || s.memoryTotalBytes == 0 || s.memoryAvailableBytes > s.memoryTotalBytes)
        latest_.memoryUsagePercent.reset();
    notice_ = pendingRecovery_;
    const auto background = backgroundCandidate();
    const QString backgroundName = background ? background->name : QString{};
    if (!sameApp(backgroundApp_, backgroundName)) { backgroundApp_ = backgroundName; backgroundSince_ = s.capturedAt; }
    const qint64 processAge = s.processSamplesCapturedAt.isValid() ? s.processSamplesCapturedAt.msecsTo(s.capturedAt) : -1;
    if (processAge >= 0 && processAge <= 30'000 && !excluded(s.foregroundProcessName)) {
        if (!sameApp(candidateApp_, s.foregroundProcessName) || (candidateLastSeen_.isValid() && candidateLastSeen_.secsTo(s.capturedAt) > 45)) {
            candidateApp_ = s.foregroundProcessName; candidateSince_ = s.capturedAt; continuedUse_ = false;
        }
        candidateLastSeen_ = s.capturedAt;
        if (episodeAt_.isValid() && candidateSince_.isValid() &&
            std::max(candidateSince_, episodeAt_).secsTo(s.capturedAt) >= 35) continuedUse_ = true;
    } else if (!s.foregroundProcessName.isEmpty() && !sameApp(s.foregroundProcessName, QStringLiteral("ausyn.exe"))) {
        // A different/system foreground interrupts continuity. An Ausyn visit does
        // not discard the last app, so "Keep this app" can be clicked.
        candidateSince_ = s.capturedAt; continuedUse_ = false;
    }
    points_.append({s.capturedAt, latest_.processorUsagePercent, latest_.memoryUsagePercent, s.memoryAvailableBytes});
    while (!points_.isEmpty() && (points_.size() > 64 || points_.first().at.secsTo(s.capturedAt) > 90)) points_.removeFirst();

    if (comparisonAt_.isValid() && comparisonAt_.secsTo(s.capturedAt) >= 30) {
        double cpu = 0, memory = 0, available = 0; int cpuCount = 0, memoryCount = 0, count = 0;
        QDateTime first;
        for (const auto& p : points_) if (p.at >= comparisonAt_) {
            if (!first.isValid()) first = p.at;
            if (valid(p.cpu)) { cpu += *p.cpu; ++cpuCount; }
            if (valid(p.memory)) { memory += *p.memory; ++memoryCount; available += static_cast<double>(p.available); }
            ++count;
        }
        if (count >= 4 && first.isValid() && first.secsTo(s.capturedAt) >= 20) {
            QStringList changes;
            if (comparisonCpu_ && cpuCount >= 4) changes << QStringLiteral("CPU %1 → %2% average")
                .arg(percent(comparisonCpu_)).arg(cpu / cpuCount, 0, 'f', 0);
            if (comparisonMemory_ && memoryCount >= 4) changes << QStringLiteral("RAM %1 → %2% average; available %3 → %4")
                .arg(percent(comparisonMemory_)).arg(memory / memoryCount, 0, 'f', 0)
                .arg(ram(comparisonAvailable_), ram(static_cast<quint64>(available / memoryCount)));
            comparisonResult_ = changes.isEmpty() ? QStringLiteral("Not enough valid readings to compare this change.")
                : changes.join(QStringLiteral(". ")) + QStringLiteral(". Observed after your change; this does not prove what caused it.");
            comparisonAt_ = {};
        }
    }
    const bool hasEvidence = latest_.processorUsagePercent || latest_.memoryUsagePercent;
    sustainedPressure = sustainedPressure && hasEvidence;
    if (sustainedPressure && !pressure_) {
        pressure_ = true; episodeAt_ = s.capturedAt; ++episode_; delivered_.clear();
        pendingRecovery_.reset(); notice_.reset();
        notifiedCpu_.reset(); notifiedMemory_.reset(); notifiedAvailable_ = 0; continuedUse_ = false;
    }
    if (!pressure_) return;
    QString phase;
    WorkloadNotice n;
    n.resource = latest_.memoryUsagePercent.value_or(0) >= 85 ? 1 : 0;
    n.critical = sustainedPressure && latest_.memoryUsagePercent.value_or(0) >= 97 && s.memoryAvailableBytes <= 256ULL * 1024 * 1024;
    if (!sustainedPressure) {
        const bool recovered = latest_.processorUsagePercent.value_or(100) < 70 && latest_.memoryUsagePercent.value_or(100) < 85;
        if (!recovered) { recoveryAt_ = {}; recoveryReadings_ = 0; return; }
        if (!recoveryAt_.isValid()) recoveryAt_ = s.capturedAt;
        if (++recoveryReadings_ < 3 || recoveryAt_.secsTo(s.capturedAt) < 15) return;
        phase = QStringLiteral("recovery"); n.title = QStringLiteral("Your PC has breathing room again");
        n.body = QStringLiteral("CPU is %1 and RAM is %2, with %3 available across repeated calmer readings. %4I’ll keep watching without repeating the earlier warning.")
            .arg(percent(latest_.processorUsagePercent), percent(latest_.memoryUsagePercent), available(s),
                 appName().isEmpty() ? QString{} : QStringLiteral("Check whether %1 feels smoother. ").arg(appName()));
    } else {
        recoveryAt_ = {}; recoveryReadings_ = 0;
        const bool worsening = (notifiedMemory_ && latest_.memoryUsagePercent && *latest_.memoryUsagePercent - *notifiedMemory_ >= 5) ||
            (notifiedCpu_ && latest_.processorUsagePercent && *latest_.processorUsagePercent - *notifiedCpu_ >= 20) ||
            (notifiedAvailable_ > 0 && latest_.memoryUsagePercent && s.memoryAvailableBytes < notifiedAvailable_ / 2 && notifiedAvailable_ - s.memoryAvailableBytes >= 128ULL * 1024 * 1024);
        const bool changedBackground = !delivered_.isEmpty() && !backgroundApp_.isEmpty() && !sameApp(backgroundApp_, notifiedBackgroundApp_) &&
            backgroundSince_.isValid() && backgroundSince_.secsTo(s.capturedAt) >= 20 && latest_.processorUsagePercent.value_or(0) >= 75;
        phase = n.critical ? QStringLiteral("critical") : worsening ? QStringLiteral("tightening")
            : changedBackground ? QStringLiteral("background/") + backgroundApp_.toLower()
            : (continuedUse_ || confirmed_) ? QStringLiteral("working") : QStringLiteral("first");
        n.title = n.critical ? QStringLiteral("Save your work — RAM is nearly exhausted")
            : worsening ? QStringLiteral("Pressure is increasing — the plan needs adjusting")
            : changedBackground ? QStringLiteral("A different app is competing for CPU time")
            : (continuedUse_ || confirmed_) ? QStringLiteral("Let’s keep %1 running comfortably").arg(appName())
            : n.resource == 1 ? QStringLiteral("RAM is tight — here’s the current picture") : QStringLiteral("Your processor is busy — here’s what I see");
        const QString intent = confirmed_ ? QStringLiteral("You marked %1 as important for this session. ").arg(keptApp_)
            : continuedUse_ ? QStringLiteral("You’ve kept %1 in front while load has stayed high; it may be the app you need. ").arg(candidateApp_) : QString{};
        n.body = intent + QStringLiteral("CPU %1 · RAM %2 · %3 available. ").arg(percent(latest_.processorUsagePercent), percent(latest_.memoryUsagePercent), available(s)) + plan();
    }
    n.key = QStringLiteral("workload/%1/%2").arg(episode_).arg(phase);
    if (phase == QLatin1String("first") && !delivered_.isEmpty()) return;
    if (phase == QLatin1String("tightening") && delivered_.contains(QStringLiteral("workload/%1/critical").arg(episode_))) return;
    if (!delivered_.contains(n.key) && (delivered_.size() < 12 || n.critical || phase == QLatin1String("recovery"))) notice_ = n;
    if (phase == QLatin1String("recovery")) { pressure_ = false; episodeAt_ = {}; continuedUse_ = false; pendingRecovery_ = notice_; }
}
void WorkloadCoach::markDelivered(const QString& key) {
    if (!notice_ || notice_->key != key) return;
    delivered_.insert(key); notifiedCpu_ = latest_.processorUsagePercent;
    notifiedMemory_ = latest_.memoryUsagePercent; notifiedAvailable_ = latest_.memoryAvailableBytes;
    notifiedBackgroundApp_ = backgroundApp_;
    if (key.endsWith(QStringLiteral("/recovery"))) pendingRecovery_.reset();
    notice_.reset();
}
bool WorkloadCoach::keepCurrentApp() {
    if (candidateApp_.isEmpty() || !candidateLastSeen_.isValid() || candidateLastSeen_.secsTo(latest_.capturedAt) > 45) return false;
    keptApp_ = candidateApp_; confirmed_ = true; return true;
}
void WorkloadCoach::releaseApp() { keptApp_.clear(); confirmed_ = false; }
bool WorkloadCoach::keepApp(const QString& name) {
    if (excluded(name)) return false;
    const qint64 age = latest_.processSamplesCapturedAt.isValid() ? latest_.processSamplesCapturedAt.msecsTo(latest_.capturedAt) : -1;
    if (age < 0 || age > 30'000) return false;
    const bool running = std::any_of(latest_.topProcesses.cbegin(), latest_.topProcesses.cend(), [&name](const ProcessSample& p) { return sameApp(p.name, name); });
    if (!running) return false;
    keptApp_ = name; confirmed_ = true; return true;
}
void WorkloadCoach::beginComparison() {
    comparisonAt_ = latest_.capturedAt; comparisonCpu_ = latest_.processorUsagePercent;
    comparisonMemory_ = latest_.memoryUsagePercent; comparisonAvailable_ = latest_.memoryAvailableBytes; comparisonResult_.clear();
}
QString WorkloadCoach::comparison() const {
    if (comparisonAt_.isValid()) return QStringLiteral("Comparing the next 30 seconds with the reading before your change…");
    return comparisonResult_;
}
std::optional<ProcessSample> WorkloadCoach::backgroundCandidate() const {
    const qint64 age = latest_.processSamplesCapturedAt.isValid() ? latest_.processSamplesCapturedAt.msecsTo(latest_.capturedAt) : -1;
    if (age < 0 || age > 30'000) return std::nullopt;
    const ProcessSample* best = nullptr;
    for (const auto& p : latest_.topProcesses) {
        if (excluded(p.name) || sameApp(p.name, appName()) || sameApp(p.name, candidateApp_) || p.processId == latest_.foregroundProcessId || !valid(p.cpuPercent) || *p.cpuPercent < 5 || p.executablePath.isEmpty()) continue;
        if (!best || *p.cpuPercent > *best->cpuPercent) best = &p;
    }
    return best ? std::optional<ProcessSample>(*best) : std::nullopt;
}
QString WorkloadCoach::summary() const {
    if (!latest_.capturedAt.isValid()) return QStringLiteral("I’m waiting for fresh readings to understand your workload.");
    if (confirmed_) return QStringLiteral("Your priority this session: %1. I’ll suggest relief around it.").arg(keptApp_);
    if (continuedUse_) return QStringLiteral("You’ve continued using %1. Keep it as your priority, or choose another app.").arg(candidateApp_);
    return candidateApp_.isEmpty() ? QStringLiteral("Use an app for a moment; I’ll bring its workload into context.")
        : QStringLiteral("Current activity: %1. Foreground activity is a clue; you decide what matters.").arg(candidateApp_);
}
QString WorkloadCoach::plan() const {
    QStringList steps;
    const QString keep = appName();
    const bool browser = keep.contains(QStringLiteral("edge"), Qt::CaseInsensitive) || keep.contains(QStringLiteral("chrome"), Qt::CaseInsensitive) || keep.contains(QStringLiteral("firefox"), Qt::CaseInsensitive) || keep.contains(QStringLiteral("brave"), Qt::CaseInsensitive);
    if (latest_.memoryUsagePercent.value_or(0) >= 85) {
        steps << (browser ? QStringLiteral("Keep your active tab; pause unused tabs or enable the browser’s sleeping-tab feature.")
            : QStringLiteral("Keep your current task; save and review other apps or documents you no longer need."));
        steps << QStringLiteral("CPU priority changes won’t free RAM.");
    }
    if (latest_.processorUsagePercent.value_or(0) >= 75) {
        const auto background = backgroundCandidate();
        if (background) steps << QStringLiteral("%1 is also using %2% CPU in the latest sample. Review a temporary background-priority change before applying it.").arg(background->name).arg(*background->cpuPercent, 0, 'f', 0);
        else steps << (browser ? QStringLiteral("If you’re watching video, try a lower resolution or playback speed while keeping the video open.")
            : QStringLiteral("Reduce the app’s optional quality or background work, then compare the next readings."));
    }
    if (latest_.graphicsUsagePercent.value_or(0) >= 85) steps << QStringLiteral("GPU activity is high; reducing rendering quality can leave more graphics headroom.");
    if (latest_.batteryOnAcPower && !*latest_.batteryOnAcPower && latest_.batteryPercent.value_or(100) <= 20)
        steps << QStringLiteral("Plug in if possible; battery headroom is low too.");
    if (steps.isEmpty()) steps << QStringLiteral("Current readings leave room for your task. Keep going; I’ll flag a meaningful change.");
    return steps.join(QLatin1Char(' '));
}
QString WorkloadCoach::resolveFollowup(const QString& question, const QVector<QString>& previous) {
    const QString q = question.toLower().trimmed();
    static const QSet<QString> followups{QStringLiteral("why"), QStringLiteral("why?"), QStringLiteral("how"), QStringLiteral("how?"),
        QStringLiteral("explain"), QStringLiteral("tell me more"), QStringLiteral("what should i do"), QStringLiteral("what now"),
        QStringLiteral("how do i fix it"), QStringLiteral("what about that"), QStringLiteral("is that bad"), QStringLiteral("how do you know")};
    if (!followups.contains(q)) return question;
    for (auto it = previous.crbegin(); it != previous.crend(); ++it) {
        const QString earlier = it->toLower();
        if (earlier.contains(QStringLiteral("memory")) || earlier.contains(QStringLiteral("ram"))) return QStringLiteral("Explain memory pressure and safe steps: %1").arg(question);
        if (earlier.contains(QStringLiteral("cpu")) || earlier.contains(QStringLiteral("processor"))) return QStringLiteral("Explain CPU load and safe steps: %1").arg(question);
        if (earlier.contains(QStringLiteral("slow")) || earlier.contains(QStringLiteral("system")) || earlier.contains(QStringLiteral("going on"))) return QStringLiteral("Why is my PC slow? %1").arg(question);
        if (earlier.contains(QStringLiteral("battery"))) return QStringLiteral("Explain battery readings: %1").arg(question);
        if (earlier.contains(QStringLiteral("storage")) || earlier.contains(QStringLiteral("disk"))) return QStringLiteral("Explain storage readings: %1").arg(question);
        if (!followups.contains(earlier.trimmed())) break;
    }
    return question;
}
}
