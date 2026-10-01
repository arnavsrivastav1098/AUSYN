#include "assistant_engine.h"

#include <QChar>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

namespace Ausyn {
namespace {

QString percent(const std::optional<double>& value)
{
    return value ? QStringLiteral("%1%").arg(*value, 0, 'f', 1) : QStringLiteral("not available yet");
}

QString rate(const std::optional<double>& bytesPerSecond)
{
    if (!bytesPerSecond) return QStringLiteral("unavailable");
    if (*bytesPerSecond >= 1'000'000.0)
        return QStringLiteral("%1 MB/s").arg(*bytesPerSecond / 1'000'000.0, 0, 'f', 1);
    if (*bytesPerSecond >= 1'000.0)
        return QStringLiteral("%1 KB/s").arg(*bytesPerSecond / 1'000.0, 0, 'f', 0);
    return QStringLiteral("%1 bytes/s").arg(*bytesPerSecond, 0, 'f', 0);
}

bool hasAny(const QString& text, std::initializer_list<QStringView> words);

enum class ProcessMetric { Cpu, Memory, Io, Network, Overview };

qint64 processFreshnessLimitSeconds(const SystemSnapshot& snapshot)
{
    return std::max<qint64>(10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
}

bool isProcessQuestion(const QString& question, const SystemSnapshot& snapshot)
{
    const bool namesApplication = hasAny(question, {u"app", u"application", u"process", u"program",
        u"browser", u"game", u"service"});
    const bool asksImpact = hasAny(question, {u"impact", u"affect", u"affects", u"affecting",
        u"pressure", u"contribute", u"contributing", u"causing", u"cause", u"slow down", u"slowing"});
    const bool referencesSystem = hasAny(question, {u"system", u"pc", u"laptop", u"computer",
        u"performance", u"cpu", u"memory", u"ram", u"slow"});
    if (namesApplication && asksImpact && referencesSystem) return true;

    const bool explicitProcessIntent = hasAny(question, {u"process", u"processes", u"running app", u"running apps",
        u"running programs", u"currently running", u"what is running", u"what's running", u"whats running",
        u"show running", u"open apps", u"open applications", u"which app is", u"what app is",
        u"task manager", u"per-app", u"per app"});
    if (explicitProcessIntent) return true;

    const bool resourceMetric = hasAny(question, {u"cpu", u"processor", u"memory", u"ram", u"disk", u"i/o",
        u"read", u"write", u"network", u"internet", u"bandwidth", u"connection", u"traffic"});
    const bool resourceUse = hasAny(question, {u"use", u"uses", u"using", u"consume", u"consumes", u"consuming",
        u"take", u"takes", u"taking", u"using up", u"taking up", u"largest", u"biggest", u"highest", u"most", u"top"});
    const bool appOrProcess = hasAny(question, {u"app", u"application", u"process", u"program", u"browser",
        u"game", u"task", u"service"});
    const bool asksWhatUsesIt = hasAny(question, {u"what is using", u"what's using", u"whats using",
        u"what is consuming", u"what's consuming", u"which is using", u"who is using"});
    if (resourceMetric && resourceUse && (appOrProcess || asksWhatUsesIt)) return true;

    const bool asksAboutProcessActivity = hasAny(question, {u"cpu", u"processor", u"memory", u"ram", u"disk",
        u"read", u"write", u"network", u"internet", u"bandwidth", u"using", u"uses", u"consuming", u"consume", u"resource",
        u"slow", u"slower", u"lag", u"high", u"busy", u"running", u"causing"});
    if (!asksAboutProcessActivity) return false;
    for (const ProcessSample& process : snapshot.topProcesses) {
        QString processName = process.name.trimmed();
        if (processName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) processName.chop(4);
        if (processName.size() < 3 ||
            QStringList{QStringLiteral("system"), QStringLiteral("registry"), QStringLiteral("service host"),
                        QStringLiteral("task manager"), QStringLiteral("desktop window manager")}
                .contains(processName, Qt::CaseInsensitive)) continue;
        const QRegularExpression exactName(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(processName)),
            QRegularExpression::CaseInsensitiveOption);
        if (exactName.match(question).hasMatch()) return true;
    }
    return false;
}

QString processAnswer(const QString& question, const SystemSnapshot& snapshot)
{
    const qint64 ageMilliseconds = snapshot.processSamplesCapturedAt.isValid()
        ? snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (ageMilliseconds < 0 || ageMilliseconds > processFreshnessLimitSeconds(snapshot) * 1000) {
        if (snapshot.processCollectionStatus.startsWith(QStringLiteral("Windows could not")))
            return QStringLiteral("I couldn’t refresh the process list, and the last successful sample is too old to use safely. %1")
                .arg(snapshot.processCollectionStatus);
        return QStringLiteral("I don’t have a fresh process sample yet. Process readings refresh periodically; wait a few seconds or open Performance and try again.");
    }

    ProcessMetric metric = ProcessMetric::Overview;
    const bool impactQuestion = hasAny(question, {u"impact", u"affect", u"affects", u"affecting",
        u"pressure", u"contribute", u"contributing", u"causing", u"cause", u"slow down", u"slowing"});
    if (hasAny(question, {u"cpu", u"processor", u"busy"})) metric = ProcessMetric::Cpu;
    else if (hasAny(question, {u"memory", u"ram", u"working set"})) metric = ProcessMetric::Memory;
    else if (hasAny(question, {u"disk", u"read", u"write", u"io", u"i/o"})) metric = ProcessMetric::Io;
    else if (hasAny(question, {u"network", u"internet", u"connection", u"socket", u"traffic"})) metric = ProcessMetric::Network;

    QVector<ProcessSample> ranked = snapshot.topProcesses;
    QVector<ProcessSample> namedProcesses;
    for (const ProcessSample& process : std::as_const(ranked)) {
        QString searchableName = process.name.trimmed();
        if (searchableName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            searchableName.chop(4);
        if (searchableName.size() < 3 ||
            QStringList{QStringLiteral("system"), QStringLiteral("registry"),
                        QStringLiteral("service host"), QStringLiteral("task manager"),
                        QStringLiteral("desktop window manager")}.contains(searchableName, Qt::CaseInsensitive))
            continue;
        if (question.contains(searchableName, Qt::CaseInsensitive))
            namedProcesses.append(process);
    }
    const bool focusedOnNamedProcess = !namedProcesses.isEmpty();
    if (focusedOnNamedProcess) ranked = std::move(namedProcesses);
    const auto valueFor = [metric](const ProcessSample& process) -> std::optional<double> {
        switch (metric) {
        case ProcessMetric::Cpu: return process.cpuPercent;
        case ProcessMetric::Memory:
            return process.workingSetBytes ? std::optional<double>(static_cast<double>(*process.workingSetBytes)) : std::nullopt;
        case ProcessMetric::Io:
            if (process.readIoBytesPerSecond || process.writeIoBytesPerSecond)
                return process.readIoBytesPerSecond.value_or(0.0) + process.writeIoBytesPerSecond.value_or(0.0);
            return std::nullopt;
        case ProcessMetric::Network:
            if (process.tcpConnectionCount || process.udpEndpointCount)
                return static_cast<double>(process.tcpConnectionCount.value_or(0) + process.udpEndpointCount.value_or(0));
            return std::nullopt;
        case ProcessMetric::Overview: return process.cpuPercent;
        }
        return std::nullopt;
    };
    std::stable_sort(ranked.begin(), ranked.end(), [&](const ProcessSample& left, const ProcessSample& right) {
        return valueFor(left).value_or(-1.0) > valueFor(right).value_or(-1.0);
    });

    QStringList rows;
    for (const ProcessSample& process : ranked) {
        const auto value = valueFor(process);
        if (!value || *value < 0.0) continue;
        QString row = process.name;
        if (focusedOnNamedProcess)
            row += QStringLiteral(" (PID %1)").arg(process.processId);
        if (metric == ProcessMetric::Cpu)
            row += QStringLiteral(" — %1% CPU").arg(*value, 0, 'f', 1);
        else if (metric == ProcessMetric::Memory)
            row += QStringLiteral(" — %1 GB working set%2")
                .arg(*value / 1'000'000'000.0, 0, 'f', 2)
                .arg(snapshot.memoryTotalBytes > 0
                    ? QStringLiteral(" · %1% of installed RAM")
                        .arg(*value / static_cast<double>(snapshot.memoryTotalBytes) * 100.0, 0, 'f', 1)
                    : QString());
        else if (metric == ProcessMetric::Io)
            row += QStringLiteral(" — %1 read, %2 write").arg(rate(process.readIoBytesPerSecond), rate(process.writeIoBytesPerSecond));
        else if (metric == ProcessMetric::Network)
            row += QStringLiteral(" — %1 TCP connections, %2 UDP endpoints")
                .arg(process.tcpConnectionCount ? QString::number(*process.tcpConnectionCount) : QStringLiteral("unavailable"))
                .arg(process.udpEndpointCount ? QString::number(*process.udpEndpointCount) : QStringLiteral("unavailable"));
        else
            row += QStringLiteral(" — %1% CPU, %2 GB working set")
                .arg(process.cpuPercent ? QString::number(*process.cpuPercent, 'f', 1) : QStringLiteral("unavailable"))
                .arg(process.workingSetBytes ? QString::number(static_cast<double>(*process.workingSetBytes) / 1'000'000'000.0, 'f', 2) : QStringLiteral("unavailable"));
        rows << row;
        if (rows.size() == 5) break;
    }
    if (rows.isEmpty())
        return QStringLiteral("The latest process sample doesn’t include accessible readings for that metric. You can inspect the current process list on Performance.");

    QString answer;
    if (focusedOnNamedProcess) answer = QStringLiteral("Latest readings for the matching process instance(s):");
    else if (metric == ProcessMetric::Cpu) answer = QStringLiteral("Highest process CPU readings in the latest sample:");
    else if (metric == ProcessMetric::Memory) answer = QStringLiteral("Largest process working sets in the latest sample:");
    else if (metric == ProcessMetric::Io) answer = QStringLiteral("Highest process I/O rates in the latest sample:");
    else if (metric == ProcessMetric::Network) answer = QStringLiteral("Process network endpoints in the latest sample:");
    else answer = QStringLiteral("A quick view of the latest process sample (ranked by CPU):");
    answer += QStringLiteral("\n• %1").arg(rows.join(QStringLiteral("\n• ")));
    answer += QStringLiteral("\n\nProcess readings captured at %1 (%2 seconds ago).")
        .arg(snapshot.processSamplesCapturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
        .arg(ageMilliseconds / 1000);
    answer += QStringLiteral("\n\nThese are readings from one recent process sample, not a long-term trend or proof that a process caused a system-wide problem. Compare another sample after a short interval before treating a high value as sustained.");
    if (!focusedOnNamedProcess)
        answer += QStringLiteral(" If you meant a particular app, it wasn’t among the process names in this sample’s displayed results.");
    if (snapshot.processCollectionStatus.startsWith(QStringLiteral("Windows could not")))
        answer += QStringLiteral("\n\nUpdate note: %1").arg(snapshot.processCollectionStatus);
    if (metric == ProcessMetric::Network)
        answer += QStringLiteral(" Ausyn can count TCP connections and UDP endpoints, but it does not collect per-process network bandwidth.");
    if (metric == ProcessMetric::Io)
        answer += QStringLiteral(" Process I/O counters are not guaranteed physical-disk throughput.");
    if (impactQuestion) {
        answer += QStringLiteral("\n\nSystem context at that time: CPU %1; memory %2. These are current system readings compared with a single process snapshot, so they can show an association but cannot establish that a process caused the pressure. Working sets may include shared memory, and Windows may omit inaccessible processes. Select a process on Performance to see its short recent profile.")
            .arg(percent(snapshot.processorUsagePercent), percent(snapshot.memoryUsagePercent));
    }
    if (metric == ProcessMetric::Memory)
        answer += QStringLiteral(" Per-process working sets can include shared pages, so their sum is not the same as total physical memory in use.");
    answer += QStringLiteral(" Open Performance for the full current list.");
    return answer;
}

QString backgroundProcessAnswer(const QString& question, const SystemSnapshot& snapshot)
{
    const qint64 sampleAgeMilliseconds = snapshot.processSamplesCapturedAt.isValid()
        ? snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (sampleAgeMilliseconds < 0 || sampleAgeMilliseconds > processFreshnessLimitSeconds(snapshot) * 1000) {
        if (snapshot.processCollectionStatus.startsWith(QStringLiteral("Windows could not")))
            return QStringLiteral("I couldn’t refresh process readings, so I can’t safely compare the active app with background processes. %1")
                .arg(snapshot.processCollectionStatus);
        return QStringLiteral("I don’t have a fresh process sample to compare against the active app yet. Wait a few seconds and ask again.");
    }
    if (snapshot.foregroundProcessId == 0 || snapshot.foregroundProcessName.isEmpty())
        return QStringLiteral("Windows hasn’t identified the active foreground app for this sample, so I can’t separate it from background processes reliably.");

    const bool rankByMemory = hasAny(question, {u"memory", u"ram"});
    QVector<const ProcessSample*> background;
    background.reserve(snapshot.topProcesses.size());
    double knownCpuTotal = 0.0;
    int knownCpuProcesses = 0;
    for (const ProcessSample& process : snapshot.topProcesses) {
        if (process.processId == snapshot.foregroundProcessId) continue;
        background.push_back(&process);
        if (process.cpuPercent) {
            knownCpuTotal += *process.cpuPercent;
            ++knownCpuProcesses;
        }
    }
    std::stable_sort(background.begin(), background.end(), [rankByMemory](const ProcessSample* left, const ProcessSample* right) {
        if (rankByMemory)
            return left->workingSetBytes.value_or(0) > right->workingSetBytes.value_or(0);
        return left->cpuPercent.value_or(-1.0) > right->cpuPercent.value_or(-1.0);
    });

    QStringList rows;
    for (const ProcessSample* process : std::as_const(background)) {
        if (!process->cpuPercent && !process->workingSetBytes) continue;
        rows << QStringLiteral("%1 — %2% CPU, %3 GB working set")
            .arg(process->name,
                 process->cpuPercent ? QString::number(*process->cpuPercent, 'f', 1) : QStringLiteral("unavailable"),
                 process->workingSetBytes ? QString::number(static_cast<double>(*process->workingSetBytes) / 1'000'000'000.0, 'f', 2)
                                          : QStringLiteral("unavailable"));
        if (rows.size() == 5) break;
    }

    QString answer = QStringLiteral("Windows currently identifies “%1” as the foreground app. In the latest process sample, other processes reported:")
        .arg(snapshot.foregroundProcessName);
    if (rows.isEmpty()) answer += QStringLiteral("\n• No accessible background process CPU or memory readings yet.");
    else answer += QStringLiteral("\n• %1").arg(rows.join(QStringLiteral("\n• ")));
    if (knownCpuProcesses > 0)
        answer += QStringLiteral("\n\nThe sampled non-foreground processes with readable CPU values sum to %1% of total CPU capacity (%2 processes). Some process readings may be unavailable.")
            .arg(knownCpuTotal, 0, 'f', 1).arg(knownCpuProcesses);
    answer += QStringLiteral("\n\nThis is a recent comparison, not a full workload attribution or proof that background activity caused a slowdown. Open Performance for the current process list.");
    if (snapshot.processCollectionStatus.startsWith(QStringLiteral("Windows could not")))
        answer += QStringLiteral("\n\nUpdate note: %1").arg(snapshot.processCollectionStatus);
    return answer;
}

QString historyAnswer(const QVector<HistoryPoint>& history, bool available, int periodHours)
{
    if (!available)
        return QStringLiteral("Local performance history isn’t available right now. Live readings still work; check the History & reports page for the storage status.");

    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-std::max(1, periodHours) * 3600);
    QVector<HistoryPoint> points;
    points.reserve(history.size());
    for (const HistoryPoint& point : history) {
        if (point.capturedAt.isValid() && point.capturedAt >= cutoff)
            points.push_back(point);
    }
    std::sort(points.begin(), points.end(), [](const HistoryPoint& left, const HistoryPoint& right) {
        return left.capturedAt < right.capturedAt;
    });
    if (points.isEmpty())
        return QStringLiteral("There aren’t any saved readings in the selected history window yet. Ausyn needs time to collect local samples before it can summarize a trend.");

    struct MetricSummary { double weightedSum = 0.0; int samples = 0; double peak = 0.0; };
    const auto summarize = [](const QVector<HistoryPoint>& source, bool cpu) {
        MetricSummary summary;
        for (const HistoryPoint& point : source) {
            const auto& value = cpu ? point.processorPercent : point.memoryPercent;
            const auto& peak = cpu ? point.processorPeakPercent : point.memoryPeakPercent;
            const int count = cpu ? point.processorSampleCount : point.memorySampleCount;
            if (!value || count <= 0) continue;
            summary.weightedSum += *value * count;
            summary.samples += count;
            summary.peak = std::max(summary.peak, peak.value_or(*value));
        }
        return summary;
    };
    const MetricSummary cpu = summarize(points, true);
    const MetricSummary memory = summarize(points, false);
    const auto metricText = [](const MetricSummary& metric) {
        return metric.samples > 0
            ? QStringLiteral("%1% average, %2% highest recorded (%3 samples)")
                .arg(metric.weightedSum / metric.samples, 0, 'f', 1).arg(metric.peak, 0, 'f', 1).arg(metric.samples)
            : QStringLiteral("unavailable");
    };

    QStringList facts;
    facts << QStringLiteral("Processor: %1").arg(metricText(cpu))
          << QStringLiteral("Memory: %1").arg(metricText(memory));
    const auto trendText = [&points](bool forCpu) {
        if (points.size() < 6) return QStringLiteral("not enough separate points to estimate direction");
        const qsizetype groupSize = std::max<qsizetype>(2, points.size() / 3);
        const auto average = [&points, forCpu](qsizetype start, qsizetype end) -> std::optional<double> {
            double sum = 0.0;
            int samples = 0;
            for (qsizetype i = start; i < end; ++i) {
                const HistoryPoint& point = points.at(i);
                const auto& value = forCpu ? point.processorPercent : point.memoryPercent;
                const int count = forCpu ? point.processorSampleCount : point.memorySampleCount;
                if (value && count > 0) { sum += *value * count; samples += count; }
            }
            return samples > 0 ? std::optional<double>(sum / samples) : std::nullopt;
        };
        const auto early = average(0, groupSize);
        const auto recent = average(points.size() - groupSize, points.size());
        if (!early || !recent) return QStringLiteral("unavailable from these samples");
        const double delta = *recent - *early;
        if (std::abs(delta) < 2.0) return QStringLiteral("broadly steady across the sampled window");
        return QStringLiteral("%1 by %2 percentage points between early and recent samples")
            .arg(delta > 0.0 ? QStringLiteral("rose") : QStringLiteral("fell"))
            .arg(std::abs(delta), 0, 'f', 1);
    };
    facts << QStringLiteral("Processor direction: %1").arg(trendText(true))
          << QStringLiteral("Memory direction: %1").arg(trendText(false));
    facts << QStringLiteral("Saved window: %1 to %2")
        .arg(points.first().capturedAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")),
             points.last().capturedAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")));
    const int storedSamples = std::accumulate(points.cbegin(), points.cend(), 0,
        [](int total, const HistoryPoint& point) { return total + std::max(0, point.sampleCount); });
    facts << QStringLiteral("%1 saved sample(s) represented in %2 stored point(s)").arg(storedSamples).arg(points.size());
    return QStringLiteral("Here’s what the selected local history supports:\n• %1\n\nThis is a summary of periodic samples, not continuous recording. The early-versus-recent comparison describes direction only; it doesn’t predict what will happen next or identify a cause.")
        .arg(facts.join(QStringLiteral("\n• ")));
}

bool asksForBaselineComparison(const QString& question)
{
    const bool comparisonIntent = hasAny(question, {u"normal", u"usual", u"typical", u"baseline",
        u"compare", u"compared", u"versus", u"vs"});
    const bool systemContext = hasAny(question, {u"cpu", u"processor", u"memory", u"ram", u"usage",
        u"my pc", u"my laptop", u"my computer", u"current system", u"my system"});
    return comparisonIntent && systemContext;
}

QString baselineComparisonAnswer(const QVector<HistoryPoint>& history,
                                 bool available,
                                 int periodHours,
                                 const SystemSnapshot& snapshot)
{
    if (!available)
        return QStringLiteral("I can compare live CPU and memory with Ausyn’s saved baseline once local history is available. Check History & reports for its status.");

    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-std::max(1, periodHours) * 3600);
    double cpuSum = 0.0;
    int cpuSamples = 0;
    double memorySum = 0.0;
    int memorySamples = 0;
    int pointsUsed = 0;
    QDateTime first;
    QDateTime last;
    for (const HistoryPoint& point : history) {
        if (!point.capturedAt.isValid() || point.capturedAt < cutoff) continue;
        bool used = false;
        if (point.processorPercent && point.processorSampleCount > 0) {
            cpuSum += *point.processorPercent * point.processorSampleCount;
            cpuSamples += point.processorSampleCount;
            used = true;
        }
        if (point.memoryPercent && point.memorySampleCount > 0) {
            memorySum += *point.memoryPercent * point.memorySampleCount;
            memorySamples += point.memorySampleCount;
            used = true;
        }
        if (used) {
            ++pointsUsed;
            if (!first.isValid() || point.capturedAt < first) first = point.capturedAt;
            if (!last.isValid() || point.capturedAt > last) last = point.capturedAt;
        }
    }

    if (pointsUsed < 5 || (cpuSamples < 5 && memorySamples < 5))
        return QStringLiteral("There isn’t enough saved history in the selected window for a reliable personal baseline yet. Ausyn needs at least five saved points and five samples for a metric. Keep local monitoring on, then try again later.");

    QStringList comparisons;
    const auto compare = [&comparisons](const QString& name,
                                        const std::optional<double>& current,
                                        double sum,
                                        int samples) {
        if (samples < 5 || !current) {
            comparisons << QStringLiteral("%1: current or baseline reading unavailable").arg(name);
            return;
        }
        const double baseline = sum / samples;
        const double delta = *current - baseline;
        const QString direction = std::abs(delta) < 5.0
            ? QStringLiteral("close to")
            : delta > 0.0 ? QStringLiteral("%1 percentage points above").arg(delta, 0, 'f', 1)
                          : QStringLiteral("%1 percentage points below").arg(-delta, 0, 'f', 1);
        comparisons << QStringLiteral("%1: now %2%; selected-window average %3% (%4)")
            .arg(name).arg(*current, 0, 'f', 1).arg(baseline, 0, 'f', 1).arg(direction);
    };
    compare(QStringLiteral("CPU"), snapshot.processorUsagePercent, cpuSum, cpuSamples);
    compare(QStringLiteral("Memory"), snapshot.memoryUsagePercent, memorySum, memorySamples);

    return QStringLiteral("Compared with this PC’s saved baseline for the selected %1-hour window:\n• %2\n\nBaseline span: %3 to %4, using %5 saved point(s), %6 CPU sample(s), and %7 memory sample(s). This is a personal comparison, not a healthy-device standard or diagnosis; workload and time of day can change usage. A short spike can be normal. If the PC feels slow, compare again after a few readings and review active findings.")
        .arg(periodHours)
        .arg(comparisons.join(QStringLiteral("\n• ")))
        .arg(first.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")),
             last.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")))
        .arg(pointsUsed).arg(cpuSamples).arg(memorySamples);
}

bool hasAny(const QString& text, std::initializer_list<QStringView> words)
{
    for (const QStringView word : words) {
        qsizetype from = 0;
        while (from < text.size()) {
            const qsizetype position = text.indexOf(word, from, Qt::CaseInsensitive);
            if (position < 0) break;
            const qsizetype end = position + word.size();
            const bool leftBoundary = position == 0 ||
                (!text.at(position - 1).isLetterOrNumber() && text.at(position - 1) != QLatin1Char('_'));
            const bool rightBoundary = end == text.size() ||
                (!text.at(end).isLetterOrNumber() && text.at(end) != QLatin1Char('_'));
            if (leftBoundary && rightBoundary) return true;
            from = position + 1;
        }
    }
    return false;
}

QString storageForecastAnswer(const StorageForecast& forecast, const SystemSnapshot& snapshot)
{
    QString answer = QStringLiteral("Storage outlook: %1").arg(forecast.explanation.isEmpty()
        ? QStringLiteral("Ausyn doesn’t have a supported storage estimate yet.") : forecast.explanation);
    if (snapshot.systemVolumeTotalBytes > 0) {
        const double freePercent = 100.0 * static_cast<double>(std::min(
            snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes)) /
            static_cast<double>(snapshot.systemVolumeTotalBytes);
        answer += QStringLiteral("\n\nThe system drive currently has %1 GB free (%2%).")
            .arg(static_cast<double>(snapshot.systemVolumeFreeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(freePercent, 0, 'f', 1);
    }
    if (forecast.observedDays > 0)
        answer += QStringLiteral(" Ausyn based this on %1 observed day(s)").arg(forecast.observedDays);
    if (!forecast.confidence.isEmpty()) answer += QStringLiteral(" with %1 confidence").arg(forecast.confidence.toLower());
    if (forecast.observedDays > 0 || !forecast.confidence.isEmpty()) answer += QLatin1Char('.');
    if (forecast.recentDaysConsidered > 0)
        answer += QStringLiteral(" Recent direction check: %1 of the latest %2 daily changes confirmed a decline.")
            .arg(forecast.recentDecliningDays).arg(forecast.recentDaysConsidered);
    if (forecast.state == ForecastState::Declining)
        answer += QStringLiteral(" The robust observed rate is %1 percentage points/day; the linear-fit quality is %2%.")
            .arg(forecast.dailyChangePercentagePoints, 0, 'f', 2)
            .arg(forecast.rSquared * 100.0, 0, 'f', 0);
    answer += QStringLiteral("\n\nThis estimates when the drive could reach Ausyn’s 10% attention threshold, not when it will be completely full. The estimate assumes the observed daily trend continues; it is not a guarantee.");
    return answer;
}

QString volumeStorageForecastAnswer(const QString& question, const AnalysisUpdate& analysis,
                                    const SystemSnapshot& snapshot, bool technicalDetail)
{
    if (hasAny(question, {u"system drive", u"windows drive", u"boot drive"}))
        return storageForecastAnswer(analysis.storageForecast, snapshot);
    const VolumeSample* requested = nullptr;
    for (const VolumeSample& volume : snapshot.volumes) {
        const QString root = volume.rootPath.endsWith(QLatin1Char('\\'))
            ? volume.rootPath.left(volume.rootPath.size() - 1) : volume.rootPath;
        const QString driveLetter = root.left(1);
        const bool namesRoot = question.contains(root, Qt::CaseInsensitive) ||
            (!driveLetter.isEmpty() && (question.contains(QStringLiteral("%1 drive").arg(driveLetter), Qt::CaseInsensitive) ||
                                        question.contains(QStringLiteral("drive %1").arg(driveLetter), Qt::CaseInsensitive)));
        const bool namesLabel = !volume.label.trimmed().isEmpty() && volume.label.size() >= 3 &&
            question.contains(volume.label.trimmed(), Qt::CaseInsensitive);
        if (namesRoot || namesLabel) {
            requested = &volume;
            break;
        }
    }
    if (requested && requested->rootPath.compare(snapshot.systemVolumePath, Qt::CaseInsensitive) == 0)
        return storageForecastAnswer(analysis.storageForecast, snapshot);

    const auto forecastFor = [&analysis](const QString& root) -> const VolumeStorageForecast* {
        const auto found = std::find_if(analysis.volumeStorageForecasts.cbegin(),
            analysis.volumeStorageForecasts.cend(), [&root](const VolumeStorageForecast& item) {
                return item.rootPath.compare(root, Qt::CaseInsensitive) == 0;
            });
        return found == analysis.volumeStorageForecasts.cend() ? nullptr : &*found;
    };
    if (requested) {
        const VolumeStorageForecast* item = forecastFor(requested->rootPath);
        if (!item) return QStringLiteral("I can see %1 now, but it isn’t available as a secondary fixed-drive forecast. The Windows system drive has its own outlook on the Predictions page.")
            .arg(requested->rootPath);
        const quint64 freeBytes = std::min(requested->freeBytes, requested->totalBytes);
        const StorageForecast& forecast = item->forecast;
        QString answer = QStringLiteral("%1 outlook: %2\n\nIt currently has %3 GB free (%4%).")
            .arg(requested->label.isEmpty() ? requested->rootPath
                                            : QStringLiteral("%1 (%2)").arg(requested->label, requested->rootPath),
                 forecast.explanation)
            .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(requested->totalBytes == 0 ? 0.0 : 100.0 * static_cast<double>(freeBytes) /
                 static_cast<double>(requested->totalBytes), 0, 'f', 1);
        if (forecast.hasEstimate)
            answer += QStringLiteral(" At the observed rate, it could reach Ausyn’s 10% attention threshold in about %1 day(s).")
                .arg(static_cast<int>(std::lround(forecast.daysUntilTenPercent)));
        if (forecast.rapidDropDetected)
            answer += QStringLiteral(" Daily averages also show a recent sharp decrease of %1 percentage points.")
                .arg(forecast.rapidDropPercentagePoints, 0, 'f', 1);
        if (forecast.observedDays > 0)
            answer += QStringLiteral(" The trend covers %1 observed day(s)%2.")
                .arg(forecast.observedDays)
                .arg(forecast.confidence.isEmpty() ? QString() :
                     QStringLiteral(" with %1 confidence").arg(forecast.confidence.toLower()));
        if (forecast.recentDaysConsidered > 0)
            answer += QStringLiteral(" Recent direction check: %1 of the latest %2 daily changes confirmed a decline.")
                .arg(forecast.recentDecliningDays).arg(forecast.recentDaysConsidered);
        if (forecast.state == ForecastState::Declining)
            answer += QStringLiteral(" The robust rate is %1 percentage points/day with a linear-fit quality of %2%.")
                .arg(forecast.dailyChangePercentagePoints, 0, 'f', 2)
                .arg(forecast.rSquared * 100.0, 0, 'f', 0);
        answer += QStringLiteral("\n\nThis is a conditional trend, not a guarantee. Ausyn measures aggregate free space; it doesn’t inspect files or attribute changes to an app.");
        if (technicalDetail && !forecast.observations.isEmpty())
            answer += QStringLiteral(" There are %1 daily observation(s) available for the chart.").arg(forecast.observations.size());
        return answer;
    }

    QStringList outlooks{storageForecastAnswer(analysis.storageForecast, snapshot)};
    for (const VolumeStorageForecast& item : analysis.volumeStorageForecasts) {
        const auto volume = std::find_if(snapshot.volumes.cbegin(), snapshot.volumes.cend(),
            [&item](const VolumeSample& candidate) {
                return candidate.rootPath.compare(item.rootPath, Qt::CaseInsensitive) == 0;
            });
        if (volume == snapshot.volumes.cend()) continue;
        QString row = QStringLiteral("%1: %2")
            .arg(volume->label.isEmpty() ? item.rootPath
                                         : QStringLiteral("%1 (%2)").arg(volume->label, item.rootPath),
                 item.forecast.explanation);
        if (item.forecast.hasEstimate)
            row += QStringLiteral(" At the observed rate, 10% free space is about %1 day(s) away.")
                .arg(static_cast<int>(std::lround(item.forecast.daysUntilTenPercent)));
        else if (item.forecast.rapidDropDetected)
            row += QStringLiteral(" Recent daily free space fell by %1 percentage points.")
                .arg(item.forecast.rapidDropPercentagePoints, 0, 'f', 1);
        outlooks << row;
    }
    return QStringLiteral("Here are the available local storage outlooks:\n\n• %1\n\nEach estimate requires enough recent daily history and assumes its observed trend continues. Ausyn reports aggregate space, not which files or apps caused a change.")
        .arg(outlooks.join(QStringLiteral("\n\n• ")));
}

QString batteryForecastAnswer(const BatteryForecast& forecast)
{
    QString answer = QStringLiteral("Battery outlook: %1").arg(forecast.explanation.isEmpty()
        ? QStringLiteral("Ausyn doesn’t have a supported discharge estimate yet.") : forecast.explanation);
    if (forecast.observationCount > 0)
        answer += QStringLiteral(" The estimate uses %1 recent observation(s)").arg(forecast.observationCount);
    if (forecast.fitQuality > 0.0)
        answer += QStringLiteral(" and a trend fit of %1%").arg(forecast.fitQuality * 100.0, 0, 'f', 0);
    if (forecast.observationCount > 0 || forecast.fitQuality > 0.0) answer += QLatin1Char('.');
    answer += QStringLiteral("\n\nThe outlook estimates reaching 15% charge, not the time until shutdown. It is short-term and can change with workload, power state, and battery reporting; Ausyn does not forecast battery wear or lifespan.");
    return answer;
}

QString storageReliabilityAnswer(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis,
                                 const QString& question)
{
    if (snapshot.physicalDisks.isEmpty())
        return QStringLiteral("Windows has not exposed any physical-drive details yet, so Ausyn cannot report drive reliability counters.");

    QVector<const PhysicalDiskSample*> selected;
    for (const PhysicalDiskSample& disk : snapshot.physicalDisks) {
        const QString diskName = QStringLiteral("disk %1").arg(disk.deviceNumber);
        const QString physicalName = QStringLiteral("physical drive %1").arg(disk.deviceNumber);
        if (question.contains(diskName, Qt::CaseInsensitive) ||
            question.contains(physicalName, Qt::CaseInsensitive)) selected.append(&disk);
    }
    if (selected.isEmpty()) {
        for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.physicalDisks.size(), 8); ++i)
            selected.append(&snapshot.physicalDisks.at(i));
    }

    QStringList rows;
    for (const PhysicalDiskSample* disk : selected) {
        const QString model = disk->model.trimmed().isEmpty()
            ? QStringLiteral("Physical drive %1").arg(disk->deviceNumber) : disk->model.trimmed();
        const auto value = [](const std::optional<unsigned int>& number, const QString& suffix = QString()) {
            return number ? QStringLiteral("%1%2").arg(*number).arg(suffix) : QStringLiteral("not reported");
        };
        const auto count = [](const std::optional<quint64>& number) {
            return number ? QString::number(*number) : QStringLiteral("not reported");
        };
        const QString windowsHealth = !disk->windowsHealthStatus ? QStringLiteral("not reported")
            : *disk->windowsHealthStatus == 0 ? QStringLiteral("Healthy")
            : *disk->windowsHealthStatus == 1 ? QStringLiteral("Warning")
            : *disk->windowsHealthStatus == 2 ? QStringLiteral("Unhealthy")
            : QStringLiteral("Unknown");
        const QString mediaType = !disk->mediaType ? QStringLiteral("not reported")
            : *disk->mediaType == 3 ? QStringLiteral("HDD")
            : *disk->mediaType == 4 ? QStringLiteral("SSD")
            : *disk->mediaType == 5 ? QStringLiteral("storage-class memory")
            : QStringLiteral("unspecified");
        QString row = QStringLiteral("%1 (Disk %2): Windows Storage health %3, media %4, temperature %5, reported maximum %6, wear indicator %7, power-on hours %8, uncorrected read/write errors %9 / %10")
            .arg(model).arg(disk->deviceNumber)
            .arg(windowsHealth, mediaType,
                 value(disk->temperatureCelsius, QStringLiteral(" °C")),
                 value(disk->maximumTemperatureCelsius, QStringLiteral(" °C")),
                 value(disk->wearPercent, QStringLiteral("%")))
            .arg(disk->powerOnHours ? QStringLiteral("%1 h").arg(*disk->powerOnHours) : QStringLiteral("not reported"))
            .arg(count(disk->uncorrectedReadErrors), count(disk->uncorrectedWriteErrors));
        if (!disk->temperatureCelsius && !disk->wearPercent && !disk->powerOnHours &&
            !disk->uncorrectedReadErrors && !disk->uncorrectedWriteErrors) {
            row += QStringLiteral(". The Windows storage provider or drive driver did not expose these counters.");
        }
        rows << row;
    }

    QString answer = QStringLiteral("Windows-reported storage reliability:\n• %1")
        .arg(rows.join(QStringLiteral("\n• ")));
    QStringList findings;
    for (const Finding& finding : analysis.findings) {
        if (!finding.ruleId.startsWith(QStringLiteral("storage-device-reliability-"))) continue;
        findings << QStringLiteral("%1 Evidence: %2 Next step: %3")
            .arg(finding.title, finding.evidence, finding.recommendation);
    }
    if (!findings.isEmpty())
        answer += QStringLiteral("\n\nAusyn’s current review notes:\n• %1")
            .arg(findings.join(QStringLiteral("\n• ")));
    answer += QStringLiteral("\n\nThese values are reported by the drive/Windows storage provider and may be incomplete. A wear indicator at 100% means the device-reported wear limit was reached; error counters do not by themselves prove imminent failure. Keep important files backed up and consult the drive maker for diagnostics. Ausyn does not repair drives or predict a failure date.");
    return answer;
}

QString memoryForecastAnswer(const MemoryPressureForecast& forecast, bool technicalDetail)
{
    QString answer = QStringLiteral("Memory outlook: %1").arg(forecast.explanation.isEmpty()
        ? QStringLiteral("Ausyn doesn’t have a supported short-term memory estimate yet.")
        : forecast.explanation);
    if (!forecast.observations.isEmpty())
        answer += QStringLiteral(" The latest five-minute average is %1%.")
            .arg(forecast.currentPercent, 0, 'f', 1);
    if (forecast.hasEstimate)
        answer += QStringLiteral(" The robust observed rise is about %1 percentage points per hour, with a linear-fit quality of %2%.")
            .arg(forecast.risePerHour, 0, 'f', 1).arg(forecast.fitQuality * 100.0, 0, 'f', 0);
    if (forecast.recentWindowsConsidered > 0)
        answer += QStringLiteral(" Direction check: %1 of the latest %2 windows rose by at least 0.1 percentage point.")
            .arg(forecast.recentRisingWindows).arg(forecast.recentWindowsConsidered);
    if (technicalDetail && forecast.observedMinutes > 0)
        answer += QStringLiteral(" It uses %1 five-minute windows spanning %2 minutes.")
            .arg(forecast.observations.size()).arg(forecast.observedMinutes);
    answer += QStringLiteral(" This short-term estimate assumes the recent pattern continues; it does not identify a process or diagnose a memory leak.");
    return answer;
}

QString healthScoreAnswer(const HealthAssessment& health)
{
    if (!health.score)
        return QStringLiteral("Ausyn doesn’t have enough saved measurements for a useful score yet. %1")
            .arg(health.explanation);

    QString answer = QStringLiteral("Ausyn’s current measured-performance score is %1/100, with %2% coverage. %3")
        .arg(*health.score).arg(health.coveragePercent).arg(health.explanation);
    if (!health.components.isEmpty()) {
        QStringList components;
        for (const HealthComponent& component : health.components) {
            components << QStringLiteral("%1: %2/100 (weight %3) — %4")
                .arg(component.name).arg(component.score).arg(component.weight).arg(component.evidence);
        }
        answer += QStringLiteral("\n\nMeasured components:\n• %1")
            .arg(components.join(QStringLiteral("\n• ")));
    }
    answer += QStringLiteral("\n\nThis is Ausyn’s partial performance-and-storage index, not a complete measure of hardware condition, security, or overall device health. Missing components aren’t treated as healthy or assigned a score.");
    return answer;
}

QString systemCheckAnswer(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis, bool technicalDetail)
{
    QStringList readings;
    readings << QStringLiteral("CPU: %1").arg(percent(snapshot.processorUsagePercent))
             << QStringLiteral("Memory: %1 (%2 GB available)")
                    .arg(percent(snapshot.memoryUsagePercent))
                    .arg(static_cast<double>(snapshot.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 1)
             << QStringLiteral("GPU: %1").arg(percent(snapshot.graphicsUsagePercent));

    if (snapshot.systemVolumeTotalBytes > 0) {
        const quint64 freeBytes = std::min(snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes);
        const double freePercent = 100.0 * static_cast<double>(freeBytes) /
            static_cast<double>(snapshot.systemVolumeTotalBytes);
        readings << QStringLiteral("System drive %1: %2 GB free (%3%)")
            .arg(snapshot.systemVolumePath)
            .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(freePercent, 0, 'f', 1);
    } else {
        readings << QStringLiteral("System drive: capacity unavailable");
    }
    for (const VolumeSample& volume : snapshot.volumes) {
        if (volume.totalBytes == 0 ||
            volume.rootPath.compare(snapshot.systemVolumePath, Qt::CaseInsensitive) == 0) continue;
        const quint64 freeBytes = std::min(volume.freeBytes, volume.totalBytes);
        const double freePercent = 100.0 * static_cast<double>(freeBytes) /
            static_cast<double>(volume.totalBytes);
        const QString name = volume.label.trimmed().isEmpty()
            ? volume.rootPath : QStringLiteral("%1 (%2)").arg(volume.label.trimmed(), volume.rootPath);
        readings << QStringLiteral("Fixed drive %1: %2 GB free of %3 GB (%4% free)")
            .arg(name)
            .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(static_cast<double>(volume.totalBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(freePercent, 0, 'f', 1);
    }
    if (snapshot.batteryPercent) {
        QString battery = QStringLiteral("Battery: %1% charge").arg(*snapshot.batteryPercent);
        if (snapshot.batteryHealthPercent)
            battery += QStringLiteral("; estimated capacity ratio %1% of design")
                .arg(*snapshot.batteryHealthPercent, 0, 'f', 1);
        readings << battery;
    } else {
        readings << QStringLiteral("Battery: no charge reading reported");
    }
    if (technicalDetail) readings << QStringLiteral("Network: %1 receive, %2 send")
        .arg(rate(snapshot.networkReceiveBytesPerSecond), rate(snapshot.networkSendBytesPerSecond));

    if (snapshot.thermalSensors.isEmpty()) {
        readings << QStringLiteral("Temperature: %1").arg(!technicalDetail
            ? QStringLiteral("no supported reading available") : snapshot.thermalSensorStatus.isEmpty()
            ? QStringLiteral("Windows did not expose an ACPI thermal-zone reading")
            : snapshot.thermalSensorStatus);
    } else {
        const qsizetype limit = technicalDetail ? std::min<qsizetype>(snapshot.thermalSensors.size(), 8)
                                                : std::min<qsizetype>(snapshot.thermalSensors.size(), 3);
        for (qsizetype i = 0; i < limit; ++i) {
            const ThermalSensorSample& sensor = snapshot.thermalSensors.at(i);
            QString item = QStringLiteral("Thermal zone %1: %2 °C")
                .arg(sensor.name).arg(sensor.temperatureCelsius, 0, 'f', 1);
            if (technicalDetail) item += QStringLiteral(" (%1)").arg(sensor.source);
            readings << item;
        }
    }

    QString answer = QStringLiteral("Quick system check from the latest Windows readings:\n• %1")
        .arg(readings.join(QStringLiteral("\n• ")));
    if (technicalDetail && analysis.health.score)
        answer += QStringLiteral("\n\nMeasured performance/storage index: %1/100 (%2% coverage). %3")
            .arg(*analysis.health.score).arg(analysis.health.coveragePercent).arg(analysis.health.explanation);

    if (analysis.findings.isEmpty()) {
        answer += QStringLiteral("\n\nAusyn doesn’t currently see a sustained issue in the conditions it monitors.");
    } else {
        QStringList findings;
        const qsizetype limit = technicalDetail ? 6 : 3;
        for (qsizetype i = 0; i < std::min<qsizetype>(analysis.findings.size(), limit); ++i) {
            const Finding& finding = analysis.findings.at(i);
            QString item = QStringLiteral("%1 — %2").arg(finding.title, technicalDetail ? finding.evidence : finding.summary);
            if (technicalDetail && !finding.confidenceBasis.isEmpty())
                item += QStringLiteral(" Confidence in the observed condition: %1. %2")
                    .arg(finding.confidence, finding.confidenceBasis);
            if (!finding.recommendation.isEmpty())
                item += QStringLiteral(" Next step: %1").arg(finding.recommendation);
            findings << item;
        }
        answer += QStringLiteral("\n\nActive findings:\n• %1").arg(findings.join(QStringLiteral("\n• ")));
        if (analysis.findings.size() > findings.size())
            answer += QStringLiteral("\n• More findings are available under Main dashboard → Details.");
    }
    if (snapshot.capturedAt.isValid())
        answer += QStringLiteral("\n\nSampled %1.")
            .arg(snapshot.capturedAt.toLocalTime().toString(QStringLiteral("ddd, d MMM, h:mm:ss ap")));
    answer += technicalDetail
        ? QStringLiteral(" This is a telemetry check, not a malware scan, exhaustive hardware test, or proof that every part of the PC is healthy. Ausyn doesn’t inspect file contents.")
        : QStringLiteral(" This checks supported Windows readings. Use Details to see coverage and evidence.");
    return answer;
}

bool asksForGameReadiness(const QString& question)
{
    return hasAny(question, {u"can i play", u"can my laptop play", u"can my pc play", u"can my computer play",
        u"can i run", u"will this game run", u"will my laptop run", u"will my pc run", u"game readiness",
        u"gaming readiness", u"game requirements", u"game requirement"});
}

bool asksToPrepareForGaming(const QString& question)
{
    return hasAny(question, {u"prepare my laptop for gaming", u"prepare my pc for gaming",
        u"prepare my computer for gaming", u"get my laptop ready for gaming", u"get my pc ready for gaming",
        u"prepare for gaming", u"ready for gaming", u"gaming session"});
}

QString gameReadinessAnswer(const GameReadiness& readiness, bool technicalDetail)
{
    if (readiness.checks.isEmpty())
        return QStringLiteral("I don’t have game minimums to compare yet. Open Gaming readiness, enter the publisher’s minimum RAM, dedicated VRAM, and install size, then choose the drive. I’ll compare those with Windows-reported resources.");

    QString answer = QStringLiteral("For %1: %2")
        .arg(readiness.title, readiness.summary);
    if (readiness.title == QLatin1String("Grand Theft Auto V Legacy"))
        answer += QStringLiteral(" I matched GTA 5 to the Legacy edition; Enhanced has a separate, higher minimum profile.");
    QStringList checks;
    for (const RequirementCheck& check : readiness.checks) {
        QString result;
        switch (check.state) {
        case RequirementState::Met: result = QStringLiteral("meets the entered minimum"); break;
        case RequirementState::NotMet: result = QStringLiteral("below the entered minimum"); break;
        case RequirementState::Unknown: result = QStringLiteral("could not be measured"); break;
        }
        QString item = QStringLiteral("%1: %2; required %3 (%4)")
            .arg(check.name, check.actual, check.required, result);
        checks << item;
    }
    answer += QStringLiteral("\n\nChecks:\n• %1").arg(checks.join(QStringLiteral("\n• ")));
    if (technicalDetail)
        answer += QStringLiteral("\n\nKnown checks passed: %1 of %2 measurable requirement(s); %3 comparison(s) are unknown.")
            .arg(readiness.passedChecks).arg(readiness.knownChecks).arg(readiness.checks.size() - readiness.knownChecks);
    answer += QStringLiteral("\n\nThis compares only the minimum RAM, dedicated VRAM, and selected drive space you entered. It does not check CPU compatibility, anti-cheat, or estimate FPS, and it cannot guarantee the game will launch or run smoothly.");
    return answer;
}

QString gamingPreparationAnswer(const SystemSnapshot& snapshot,
                                const AnalysisUpdate& analysis,
                                const GameReadiness& readiness,
                                bool technicalDetail)
{
    QString answer = QStringLiteral("I checked the current readings and any saved game requirements. Ausyn won’t change Windows settings or close apps for you.");
    if (!readiness.checks.isEmpty()) {
        answer += QStringLiteral("\n\n%1: %2")
            .arg(readiness.title, readiness.summary);
        QStringList checks;
        for (const RequirementCheck& check : readiness.checks) {
            const QString state = check.state == RequirementState::Met
                ? QStringLiteral("meets entered minimum")
                : check.state == RequirementState::NotMet
                    ? QStringLiteral("below entered minimum") : QStringLiteral("unknown");
            checks << QStringLiteral("%1: %2; needs %3 (%4)")
                .arg(check.name, check.actual, check.required, state);
        }
        answer += QStringLiteral("\n• %1").arg(checks.join(QStringLiteral("\n• ")));
    } else {
        answer += QStringLiteral("\n\nNo game profile is set yet. Enter the publisher’s minimum RAM, dedicated VRAM, and install size on Gaming readiness for a resource comparison.");
    }

    QStringList pressureFindings;
    for (const Finding& finding : analysis.findings) {
        if (finding.ruleId == QStringLiteral("processor-sustained-load") ||
            finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
            finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure") ||
            finding.ruleId == QStringLiteral("system-drive-low-space")) {
            pressureFindings << QStringLiteral("%1 — %2 Next step: %3")
                .arg(finding.title, finding.evidence, finding.recommendation);
        }
    }
    if (pressureFindings.isEmpty()) {
        answer += QStringLiteral("\n\nAusyn doesn’t currently have a sustained CPU, memory, or system-drive finding that calls for a specific preparation step. Open Gaming readiness and start its session monitor if you want to capture a baseline while playing.");
    } else {
        answer += QStringLiteral("\n\nBefore launching, review these sustained resource findings:");
        const qsizetype limit = technicalDetail ? 5 : 3;
        for (qsizetype i = 0; i < std::min<qsizetype>(pressureFindings.size(), limit); ++i)
            answer += QStringLiteral("\n• %1").arg(pressureFindings.at(i));
        answer += QStringLiteral("\nSave work before closing anything, and close only apps you recognize and no longer need.");
    }
    if (snapshot.processorUsagePercent || snapshot.memoryUsagePercent) {
        QStringList live;
        if (snapshot.processorUsagePercent)
            live << QStringLiteral("CPU %1%").arg(*snapshot.processorUsagePercent, 0, 'f', 0);
        if (snapshot.memoryUsagePercent)
            live << QStringLiteral("memory %1%").arg(*snapshot.memoryUsagePercent, 0, 'f', 0);
        answer += QStringLiteral("\n\nCurrent snapshot: %1. These are momentary readings, not a performance prediction.")
            .arg(live.join(QStringLiteral(", ")));
    }
    answer += QStringLiteral("\n\nThis check cannot verify game compatibility, anti-cheat, FPS, or smooth performance. It uses current resource readings and the requirements you entered.");
    return answer;
}

bool asksForHardwareSummary(const QString& question)
{
    return hasAny(question, {u"spec", u"specification", u"hardware details", u"hardware info", u"system configuration",
        u"system information", u"computer details", u"device information", u"device model", u"what laptop", u"what pc",
        u"what computer", u"which computer", u"what cpu", u"what processor", u"cpu model", u"processor model", u"processor name"});
}

QString hardwareSummary(const SystemSnapshot& snapshot, bool technicalDetail)
{
    QStringList facts;
    QString device = snapshot.systemManufacturer.trimmed();
    if (!snapshot.systemModel.trimmed().isEmpty()) {
        if (!device.isEmpty()) device += QLatin1Char(' ');
        device += snapshot.systemModel.trimmed();
    }
    if (!device.isEmpty()) facts << QStringLiteral("Device: %1").arg(device);
    if (!snapshot.deviceFormFactor.isEmpty()) {
        QString form = snapshot.deviceFormFactor;
        if (technicalDetail && !snapshot.deviceFormFactorBasis.isEmpty())
            form += QStringLiteral(" (%1)").arg(snapshot.deviceFormFactorBasis);
        facts << QStringLiteral("Form factor: %1").arg(form);
    }
    if (!snapshot.processorName.isEmpty()) {
        QString cpu = snapshot.processorName;
        if (snapshot.logicalProcessorCount > 0)
            cpu += QStringLiteral(" · %1 logical processors").arg(snapshot.logicalProcessorCount);
        facts << QStringLiteral("Processor: %1").arg(cpu);
    }
    if (!snapshot.graphicsAdapters.isEmpty()) {
        QStringList adapters;
        for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.graphicsAdapters.size(), 4); ++i) {
            const GraphicsAdapterSample& adapter = snapshot.graphicsAdapters.at(i);
            QString text = adapter.name;
            if (adapter.dedicatedMemoryBytes > 0)
                text += QStringLiteral(" · %1 GB dedicated memory")
                    .arg(static_cast<double>(adapter.dedicatedMemoryBytes) / 1'000'000'000.0, 0, 'f', 1);
            adapters << text;
        }
        facts << QStringLiteral("Graphics: %1").arg(adapters.join(QStringLiteral("; ")));
    } else if (!snapshot.graphicsName.isEmpty()) {
        facts << QStringLiteral("Graphics: %1").arg(snapshot.graphicsName);
    }
    if (snapshot.memoryTotalBytes > 0) {
        QString memory = QStringLiteral("%1 GB installed")
            .arg(static_cast<double>(snapshot.memoryTotalBytes) / 1'000'000'000.0, 0, 'f', 1);
        if (snapshot.memoryAvailableBytes > 0)
            memory += QStringLiteral(" · %1 GB currently available")
                .arg(static_cast<double>(snapshot.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 1);
        facts << QStringLiteral("Memory: %1").arg(memory);
    }
    if (!snapshot.volumes.isEmpty()) {
        QStringList volumes;
        for (const VolumeSample& volume : snapshot.volumes) {
            if (volume.totalBytes == 0 || volumes.size() >= 6) continue;
            QString text = volume.rootPath;
            if (!volume.label.trimmed().isEmpty()) text += QStringLiteral(" (%1)").arg(volume.label.trimmed());
            text += QStringLiteral(" · %1 GB total, %2 GB free")
                .arg(static_cast<double>(volume.totalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(std::min(volume.freeBytes, volume.totalBytes)) / 1'000'000'000.0, 0, 'f', 1);
            volumes << text;
        }
        if (!volumes.isEmpty()) facts << QStringLiteral("Drives: %1").arg(volumes.join(QStringLiteral("; ")));
    }
    if (!snapshot.operatingSystem.isEmpty()) {
        QString os = snapshot.operatingSystem;
        if (!snapshot.operatingSystemVersion.isEmpty()) os += QStringLiteral(" · build %1").arg(snapshot.operatingSystemVersion);
        if (technicalDetail && !snapshot.operatingSystemArchitecture.isEmpty())
            os += QStringLiteral(" · %1").arg(snapshot.operatingSystemArchitecture);
        facts << QStringLiteral("Operating system: %1").arg(os);
    }
    if (snapshot.batteryHealthPercent)
        facts << QStringLiteral("Battery capacity ratio: %1% of design capacity (Windows-reported)")
            .arg(*snapshot.batteryHealthPercent, 0, 'f', 1);
    if (technicalDetail) {
        for (const PhysicalDiskSample& disk : snapshot.physicalDisks) {
            if (disk.model.isEmpty()) continue;
            QString item = disk.model;
            if (!disk.vendor.isEmpty()) item += QStringLiteral(" · %1").arg(disk.vendor);
            if (!disk.busType.isEmpty()) item += QStringLiteral(" · %1").arg(disk.busType);
            facts << QStringLiteral("Physical drive: %1").arg(item);
        }
        if (!snapshot.biosVendor.isEmpty() || !snapshot.biosVersion.isEmpty())
            facts << QStringLiteral("Firmware: %1 %2%3")
                .arg(snapshot.biosVendor, snapshot.biosVersion,
                     snapshot.biosReleaseDate.isEmpty() ? QString() : QStringLiteral(" · %1").arg(snapshot.biosReleaseDate));
    }
    if (facts.isEmpty())
        return QStringLiteral("Windows hasn’t reported enough hardware details for a system summary yet. Check the Hardware page for the collection status.");
    return QStringLiteral("Here’s the system information Windows currently exposes:\n• %1\n\nSome model, sensor, and capacity details vary by device and driver. Ausyn shows unavailable details as missing rather than guessing.")
        .arg(facts.join(QStringLiteral("\n• ")));
}

bool isOptimizationQuestion(const QString& question)
{
    return hasAny(question, {u"optimize", u"optimization", u"make my laptop faster", u"make my pc faster",
        u"make my computer faster", u"speed up my laptop", u"speed up my pc", u"speed up my computer",
        u"improve performance", u"boost performance", u"perform better", u"performance better"});
}

QString priorOutcomeContext(const Finding& finding)
{
    if (finding.priorRatedOutcomeReports <= 0) return {};
    if (finding.priorRatedOutcomeReports < 5) {
        return QStringLiteral("Ausyn has %1 earlier self-reported outcome(s) for this finding on this PC; that is too little to summarize a pattern.")
            .arg(finding.priorRatedOutcomeReports);
    }
    const double improvementPercent = 100.0 * finding.priorImprovementReports /
                                     finding.priorRatedOutcomeReports;
    return QStringLiteral("On this PC, %1 of %2 earlier rated reports for this finding said conditions improved (%3%). These are self-reports, not proof that the suggested step caused the change.")
        .arg(finding.priorImprovementReports)
        .arg(finding.priorRatedOutcomeReports)
        .arg(improvementPercent, 0, 'f', 0);
}

QString recommendationAnswer(const AnalysisUpdate& analysis, bool optimizationQuestion, bool technicalDetail)
{
    if (analysis.findings.isEmpty()) {
        if (optimizationQuestion)
            return QStringLiteral("I don’t see a sustained issue in the measurements Ausyn currently checks, so I won’t suggest random system tweaks. If you tell me what feels slow and when it happens, I can compare that with the available readings.");
        return QStringLiteral("I don’t see a sustained issue in the rules Ausyn currently monitors. That doesn’t certify every part of the PC—Ausyn only reports conditions supported by its available measurements.");
    }

    QStringList items;
    const qsizetype resultLimit = technicalDetail ? 5 : 3;
    for (qsizetype index = 0; index < std::min<qsizetype>(analysis.findings.size(), resultLimit); ++index) {
        const Finding& finding = analysis.findings.at(index);
        QString item = QStringLiteral("%1\nEvidence: %2\nTry: %3")
            .arg(finding.title, finding.evidence, finding.recommendation);
        const QString priorOutcomes = priorOutcomeContext(finding);
        if (!priorOutcomes.isEmpty())
            item += QStringLiteral("\nPrior experience: %1").arg(priorOutcomes);
        if (technicalDetail && !finding.summary.isEmpty())
            item += QStringLiteral("\nWhy it matters: %1").arg(finding.summary);
        if (!finding.confidence.isEmpty()) {
            item += QStringLiteral("\nConfidence in the observed condition: %1").arg(finding.confidence);
            if (technicalDetail && !finding.confidenceBasis.isEmpty())
                item += QStringLiteral(" — %1").arg(finding.confidenceBasis);
        }

        if (finding.ruleId == QStringLiteral("processor-sustained-load") ||
            finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
            finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure") ||
            finding.ruleId == QStringLiteral("memory-rapid-rise")) {
            item += QStringLiteral("\nPossible benefit: may reduce pressure if the workload matches. Risk: Ausyn won’t close apps; save work before closing one.");
        } else if (finding.ruleId == QStringLiteral("system-drive-low-space") ||
                   finding.ruleId.startsWith(QStringLiteral("fixed-volume-low-space-"))) {
            item += QStringLiteral("\nPossible benefit: preserve room for updates and temporary files. Risk: Ausyn won’t delete files; remove only items you recognize and no longer need.");
        } else if (finding.ruleId == QStringLiteral("battery-low-charge")) {
            item += QStringLiteral("\nPossible benefit: reduce interruption risk. Ausyn does not change power settings.");
        } else {
            item += QStringLiteral("\nBenefit depends on the situation; Ausyn doesn’t apply system changes automatically.");
        }
        items << item;
    }
    QString answer = optimizationQuestion
        ? QStringLiteral("I checked the supported findings before suggesting changes. Here are the evidence-based steps:")
        : QStringLiteral("These sustained findings are active:");
    answer += QStringLiteral("\n\n• %1").arg(items.join(QStringLiteral("\n\n• ")));
    if (analysis.findings.size() > items.size())
        answer += QStringLiteral("\n\nMore findings are available on Diagnostics and Recommendations.");
    answer += QStringLiteral("\n\nThese findings guide user-controlled steps; they do not prove a single root cause.");
    return answer;
}

QString pastRecommendationOutcomesAnswer(const AnalysisUpdate& analysis,
                                         const QVector<RecommendationOutcomeSummary>& outcomeSummaries,
                                         bool outcomeHistoryAvailable)
{
    QStringList reports;
    for (const RecommendationOutcomeSummary& summary : outcomeSummaries) {
        QString item = summary.title + QStringLiteral(": ");
        if (summary.ratedReports < 5) {
            item += QStringLiteral("%1 earlier local report(s), too few to summarize a pattern.")
                .arg(summary.ratedReports);
        } else {
            const double improvementPercent = 100.0 * summary.improvementReports / summary.ratedReports;
            item += QStringLiteral("%1 of %2 earlier reports said conditions improved (%3%).")
                .arg(summary.improvementReports)
                .arg(summary.ratedReports)
                .arg(improvementPercent, 0, 'f', 0);
        }
        if (!summary.recentRecommendation.isEmpty())
            item += QStringLiteral(" A recent suggested step was: %1").arg(summary.recentRecommendation);
        reports << item;
        if (reports.size() == 4) break;
    }
    if (reports.isEmpty()) {
        if (!outcomeHistoryAvailable)
            return QStringLiteral("Local incident history isn’t available right now, so I can’t check saved recommendation feedback. I won’t guess what helped.");
        if (analysis.findings.isEmpty())
            return QStringLiteral("I don’t have rated recommendation-outcome reports in the selected local history window yet. There isn’t a current finding to compare; I’ll have useful history once you record outcomes for future findings.");
        return QStringLiteral("I don’t have rated past-outcome reports in the selected local history window yet. The current findings are still based on live measurements; open Diagnostics to review their evidence and suggested steps.");
    }
    return QStringLiteral("Across the selected local history window on this PC, Ausyn found:\n• %1\n\nThese are self-reports saved locally. They can add context, but they don’t prove a suggested step caused the change.")
        .arg(reports.join(QStringLiteral("\n• ")));
}

QString normalizedSocialText(const QString& text)
{
    QString normalized;
    bool pendingSpace = false;
    for (const QChar character : text) {
        if (character.isLetterOrNumber()) {
            if (pendingSpace && !normalized.isEmpty()) normalized.append(QLatin1Char(' '));
            normalized.append(character.toCaseFolded());
            pendingSpace = false;
        } else {
            pendingSpace = true;
        }
    }
    return normalized;
}

bool isOneOf(const QString& normalized, std::initializer_list<QStringView> phrases)
{
    for (const QStringView phrase : phrases) {
        if (normalized == normalizedSocialText(phrase.toString())) return true;
    }
    return false;
}

bool asksForEvidence(const QString& question)
{
    return hasAny(question, {u"how do you know", u"what is the evidence", u"what's the evidence",
        u"show the evidence", u"show your evidence", u"what are you basing", u"why do you say",
        u"why do you think", u"how can you tell", u"confidence in"});
}

QString evidenceAnswer(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis)
{
    QStringList observations;
    if (snapshot.processorUsagePercent)
        observations << QStringLiteral("Current system CPU: %1%").arg(*snapshot.processorUsagePercent, 0, 'f', 1);
    if (snapshot.memoryUsagePercent)
        observations << QStringLiteral("Current system memory: %1%").arg(*snapshot.memoryUsagePercent, 0, 'f', 1);
    if (snapshot.memoryAvailableBytes > 0)
        observations << QStringLiteral("Windows reports %1 GB available physical memory")
            .arg(static_cast<double>(snapshot.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 1);
    if (snapshot.systemVolumeTotalBytes > 0) {
        const double freePercent = 100.0 * static_cast<double>(std::min(
            snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes)) /
            static_cast<double>(snapshot.systemVolumeTotalBytes);
        observations << QStringLiteral("System drive %1: %2 GB free (%3% of %4 GB)")
            .arg(snapshot.systemVolumePath)
            .arg(static_cast<double>(snapshot.systemVolumeFreeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(freePercent, 0, 'f', 1)
            .arg(static_cast<double>(snapshot.systemVolumeTotalBytes) / 1'000'000'000.0, 0, 'f', 1);
    }
    if (snapshot.batteryPercent)
        observations << QStringLiteral("Windows-reported battery charge: %1%").arg(*snapshot.batteryPercent);

    QStringList findings;
    for (const Finding& finding : analysis.findings) {
        QString item = QStringLiteral("%1 — %2").arg(finding.title, finding.evidence);
        if (!finding.confidence.isEmpty())
            item += QStringLiteral(" Confidence in this observed condition: %1. %2")
                .arg(finding.confidence, finding.confidenceBasis);
        findings << item;
        if (findings.size() >= 6) break;
    }

    QString answer = QStringLiteral("Here’s the evidence behind what Ausyn can currently say.");
    if (!observations.isEmpty())
        answer += QStringLiteral("\n\nCurrent Windows readings:\n• %1")
            .arg(observations.join(QStringLiteral("\n• ")));
    if (!findings.isEmpty())
        answer += QStringLiteral("\n\nActive findings from repeated or threshold-based checks:\n• %1")
            .arg(findings.join(QStringLiteral("\n• ")));
    if (snapshot.capturedAt.isValid())
        answer += QStringLiteral("\n\nLatest system sample: %1.")
            .arg(snapshot.capturedAt.toLocalTime().toString(QStringLiteral("ddd, d MMM yyyy, h:mm:ss ap")));
    if (findings.isEmpty())
        answer += QStringLiteral("\n\nAusyn has no active sustained finding to support a diagnosis right now. Current readings alone may not explain a brief or application-specific problem.");
    answer += QStringLiteral("\n\nThe values above are observations. Any explanation of what caused a problem is an inference; these readings do not by themselves prove a cause.");
    return answer;
}

QString slowdownAssessment(const SystemSnapshot& snapshot,
                           const AnalysisUpdate& analysis,
                           bool technicalDetail)
{
    QStringList signalSummaries;
    QStringList nextSteps;
    for (const Finding& finding : analysis.findings) {
        const bool relevant = finding.ruleId == QStringLiteral("processor-sustained-load") ||
            finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
            finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure") ||
            finding.ruleId == QStringLiteral("memory-rapid-rise") ||
            finding.ruleId == QStringLiteral("personal-baseline-resource-shift") ||
            finding.ruleId == QStringLiteral("thermal-zone-sustained-passive-point") ||
            finding.ruleId == QStringLiteral("system-drive-low-space") ||
            finding.ruleId.startsWith(QStringLiteral("fixed-volume-low-space-"));
        if (!relevant) continue;

        QString item = QStringLiteral("%1 — %2").arg(finding.title, finding.summary);
        if (technicalDetail && !finding.evidence.isEmpty())
            item += QStringLiteral(" Evidence: %1").arg(finding.evidence);
        if (technicalDetail && !finding.confidenceBasis.isEmpty())
            item += QStringLiteral(" Confidence: %1. %2").arg(finding.confidence, finding.confidenceBasis);
        signalSummaries << item;
        if (!finding.recommendation.isEmpty() && !nextSteps.contains(finding.recommendation))
            nextSteps << finding.recommendation;
        if (signalSummaries.size() >= 4) break;
    }

    if (!signalSummaries.isEmpty()) {
        QString result = QStringLiteral("Recent evidence points to these possible contributors:\n• %1")
            .arg(signalSummaries.join(QStringLiteral("\n• ")));
        if (!nextSteps.isEmpty())
            result += QStringLiteral("\n\nA sensible next step: %1").arg(nextSteps.first());
        return result;
    }

    QStringList singleSampleSignals;
    if (snapshot.processorUsagePercent && *snapshot.processorUsagePercent >= 85.0)
        singleSampleSignals << QStringLiteral("CPU is elevated in this sample, but Ausyn has not confirmed sustained pressure.");
    if (snapshot.memoryUsagePercent && *snapshot.memoryUsagePercent >= 85.0)
        singleSampleSignals << QStringLiteral("Memory is elevated in this sample, but Ausyn has not confirmed sustained pressure.");
    if (!singleSampleSignals.isEmpty())
        return singleSampleSignals.join(QLatin1Char(' ')) +
            QStringLiteral(" Compare a few more samples before treating this as the cause.");

    return QStringLiteral("I don’t see a sustained CPU, memory, low-storage, or firmware-reported thermal finding in the available evidence. That does not rule out a brief app-specific freeze, slow disk response, or network delay. Ausyn measures disk transfer rates but not disk latency, and it does not run internet ping tests.");
}

QString thermalAnswer(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis, bool detailed)
{
    QString answer;
    if (snapshot.thermalSensors.isEmpty()) {
        answer = detailed
            ? QStringLiteral("%1 CPU/GPU die sensors often require a device-specific utility or driver; Ausyn won’t guess a temperature.")
                .arg(snapshot.thermalSensorStatus.isEmpty()
                    ? QStringLiteral("Windows did not expose an ACPI thermal-zone reading on this device.")
                    : snapshot.thermalSensorStatus)
            : QStringLiteral("I don’t have a supported temperature reading from Windows, so I can’t confirm how hot the components are.");
    } else {
        QStringList readings;
        for (const ThermalSensorSample& sensor : snapshot.thermalSensors) {
            QString reading = QStringLiteral("%1: %2 °C")
                .arg(sensor.name).arg(sensor.temperatureCelsius, 0, 'f', 1);
            if (detailed) {
                reading += QStringLiteral(" (%1)").arg(sensor.source);
                if (sensor.passiveTripPointCelsius)
                    reading += QStringLiteral("; firmware passive point %1 °C")
                        .arg(*sensor.passiveTripPointCelsius, 0, 'f', 1);
                if (sensor.secondsAbovePassiveTripPoint)
                    reading += QStringLiteral("; at/above that point for %1 s").arg(*sensor.secondsAbovePassiveTripPoint);
                if (sensor.criticalTripPointCelsius)
                    reading += QStringLiteral("; firmware critical point %1 °C")
                        .arg(*sensor.criticalTripPointCelsius, 0, 'f', 1);
            }
            readings << reading;
        }
        answer = QStringLiteral("Windows reports these thermal zones: %1. They may not represent CPU/GPU core temperature.")
            .arg(readings.join(QStringLiteral("; ")));
    }
    answer += QStringLiteral("\n\nCurrent CPU load: %1; graphics activity: %2. Workload can generate heat, but these readings alone don’t identify the cause. Keep the laptop on a hard surface with its vents clear; if heating coincides with heavy work, reduce that workload and compare readings.")
        .arg(percent(snapshot.processorUsagePercent), percent(snapshot.graphicsUsagePercent));
    const auto finding = std::find_if(analysis.findings.cbegin(), analysis.findings.cend(),
        [](const Finding& item) { return item.ruleId == QStringLiteral("thermal-zone-sustained-passive-point"); });
    if (finding != analysis.findings.cend()) {
        answer += QStringLiteral("\n\n%1 %2").arg(finding->title, finding->recommendation);
        if (detailed) answer += QStringLiteral(" Evidence: %1").arg(finding->evidence);
    } else {
        answer += QStringLiteral("\n\nNo sustained thermal-zone finding is available. Missing sensors can hide component heating; this does not establish that temperatures are safe.");
    }
    if (detailed)
        answer += QStringLiteral(" Firmware trip points are specific to each zone, rather than universal component limits. Compare sustained readings with the device maker’s guidance.");
    return answer;
}

} // namespace

bool AssistantEngine::requiresLocalHistory(const QString& question)
{
    return hasAny(question, {u"history", u"trend", u"average", u"averages", u"peak", u"highest recorded",
        u"over time", u"over the last", u"past hour", u"past day", u"past week", u"past month",
        u"yesterday", u"since yesterday"});
}

bool AssistantEngine::requiresLocalBatteryHealthHistory(const QString& question)
{
    return hasAny(question, {u"battery", u"batteries"}) &&
        hasAny(question, {u"health", u"wear", u"capacity", u"full charge", u"design capacity"});
}

bool AssistantEngine::requiresLocalForecast(const QString& question)
{
    const bool forecastIntent = hasAny(question, {u"forecast", u"predict", u"prediction", u"project", u"projection",
        u"outlook", u"how long", u"how soon", u"when will", u"how many days", u"reach", u"run out", u"fill up", u"battery wear", u"battery lifespan"});
    const bool relevantMetric = hasAny(question, {u"storage", u"disk", u"drive", u"space", u"battery", u"charge", u"power", u"memory", u"ram"});
    return forecastIntent && relevantMetric;
}

bool AssistantEngine::requiresLocalProcessBreakdown(const QString& question)
{
    return hasAny(question, {u"background process", u"background processes", u"background app", u"background apps",
        u"background application", u"background applications", u"apps in background", u"processes in background",
        u"background activity", u"background usage"}) ||
        (hasAny(question, {u"background"}) && hasAny(question, {u"cpu", u"memory", u"ram", u"resource", u"process", u"app", u"consume", u"using"}));
}

bool AssistantEngine::requiresLocalProcessQuestion(const QString& question,
                                                   const SystemSnapshot& snapshot)
{
    return isProcessQuestion(question, snapshot);
}

bool AssistantEngine::requiresLocalSystemCheck(const QString& question)
{
    return hasAny(question, {u"check my system", u"check my pc", u"check my laptop", u"check my computer",
        u"scan my system", u"scan my pc", u"scan my laptop", u"scan my computer", u"system check",
        u"quick system check", u"full system check", u"check system performance", u"what is going on",
        u"what's going on", u"what is happening", u"what's happening", u"how is my pc", u"how is my laptop",
        u"how is my computer doing", u"anything wrong with my pc", u"anything wrong with my laptop"});
}

bool AssistantEngine::requiresLocalSlowdownAssessment(const QString& question)
{
    if (requiresLocalThermalAssessment(question) &&
        !hasAny(question, {u"slow", u"lag", u"freeze", u"freezing", u"stutter"})) return false;
    return hasAny(question, {u"slow", u"sluggish", u"lag", u"lagging", u"stutter", u"freezing",
        u"freeze", u"performance issue", u"performance problem"});
}

bool AssistantEngine::requiresLocalThermalAssessment(const QString& question)
{
    return !requiresLocalStorageReliability(question) &&
        hasAny(question, {u"temperature", u"temperatures", u"temp", u"temps", u"thermal", u"hot",
        u"heat", u"heated", u"heating", u"warm", u"overheat", u"overheating"});
}

bool AssistantEngine::requiresLocalMonitoringStatus(const QString& question)
{
    return hasAny(question, {u"ausyn", u"monitor", u"monitoring", u"sampling", u"collector", u"collection"}) &&
        hasAny(question, {u"resource", u"slow", u"slower", u"interval", u"cadence", u"overhead",
                          u"busy", u"adaptive", u"throttle", u"delayed", u"status", u"using", u"load"});
}

bool AssistantEngine::requiresLocalGamingReadiness(const QString& question)
{
    return asksForGameReadiness(question) || asksToPrepareForGaming(question);
}

bool AssistantEngine::requiresLocalStorageReliability(const QString& question)
{
    const bool driveReference = hasAny(question, {u"drive", u"disk", u"storage", u"ssd", u"hdd"});
    const bool healthIntent = hasAny(question, {u"health", u"healthy", u"condition", u"smart", u"wear",
        u"reliability", u"uncorrected", u"failure risk", u"failing", u"fail", u"drive temperature"});
    return driveReference && healthIntent;
}

bool AssistantEngine::requiresLocalPastRecommendationOutcomes(const QString& question)
{
    const bool exactHistoryRequest = hasAny(question, {u"what helped before", u"what worked before", u"has this helped before",
        u"did this help before", u"previous recommendation results", u"past recommendation results",
        u"earlier reports for this", u"past reports for this", u"prior outcome reports"});
    const bool asksAboutEarlier = hasAny(question, {u"before", u"previous", u"previously", u"past",
        u"prior", u"earlier", u"last time", u"historical", u"similar case"});
    const bool asksAboutOutcome = hasAny(question, {u"help", u"work", u"effective", u"improve",
        u"result", u"outcome", u"recommend", u"suggestion", u"step"});
    return exactHistoryRequest || (asksAboutEarlier && asksAboutOutcome);
}

bool AssistantEngine::requiresLocalBaselineComparison(const QString& question)
{
    return asksForBaselineComparison(question);
}

bool AssistantEngine::requiresLocalEventLogs(const QString& question)
{
    return hasAny(question, {u"event log", u"event viewer", u"event intelligence", u"crash", u"crashes",
        u"application crash", u"app crash", u"crashed", u"crashing", u"windows errors",
        u"system errors", u"recent errors", u"logged errors", u"logged warnings",
        u"disk error", u"disk errors", u"drive error", u"drive errors", u"storage driver",
        u"storage event", u"storage events", u"storport", u"ntfs error", u"disk event",
        u"unexpected shutdown", u"unexpected restart", u"kernel power", u"event id 41", u"event 6008",
        u"blue screen", u"blue screens", u"bsod", u"bugcheck", u"bug check", u"stop code",
        u"application hang", u"app hang", u"not responding"});
}

bool AssistantEngine::requiresLocalEventCorrelation(const QString& question,
                                                     const AnalysisUpdate& analysis,
                                                     const EventLogUpdate& eventLogs)
{
    const qint64 eventScanAgeMilliseconds = eventLogs.checkedAt.isValid()
        ? eventLogs.checkedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (!hasAny(question, {u"slow", u"slower", u"sluggish", u"lag", u"lagging", u"freeze", u"freezing"}) ||
        !eventLogs.available || eventScanAgeMilliseconds < 0 ||
        eventScanAgeMilliseconds > 30LL * 60 * 1000) return false;

    for (const Finding& finding : analysis.findings) {
        if (finding.ruleId != QStringLiteral("processor-sustained-load") &&
            finding.ruleId != QStringLiteral("memory-sustained-pressure") &&
            finding.ruleId != QStringLiteral("concurrent-cpu-memory-pressure") &&
            finding.ruleId != QStringLiteral("memory-rapid-rise")) continue;
        if (!finding.firstSeen.isValid()) continue;
        for (const EventInsight& event : eventLogs.insights) {
            const auto near = [&finding](const QDateTime& timestamp) {
                if (!timestamp.isValid()) return false;
                const qint64 deltaMilliseconds = finding.firstSeen.msecsTo(timestamp);
                return deltaMilliseconds >= -120'000 && deltaMilliseconds <= 120'000;
            };
            if (near(event.firstAt) || near(event.latestAt)) return true;
        }
    }
    return false;
}

QString AssistantEngine::reply(const QString& question,
                               const SystemSnapshot& snapshot,
                               const AnalysisUpdate& analysis,
                               const AppInventoryUpdate& appInventory,
                               const UserPreferences& preferences,
                               const QVector<HistoryPoint>& history,
                               bool historyAvailable,
                               int historyPeriodHours,
                               const GameReadiness& gameReadiness,
                               const EventLogUpdate& eventLogs,
                               const QVector<RecommendationOutcomeSummary>& outcomeSummaries,
                               bool outcomeHistoryAvailable)
{
    const QString q = question.trimmed();
    if (q.isEmpty()) return QStringLiteral("Ask me what you’d like to know about this PC.");
    const QString socialText = normalizedSocialText(q);
    if (hasAny(q, {u"who made you", u"who created you", u"your creator", u"who built ausyn",
                   u"founder of ausyn", u"who is ketty to you", u"your first pet", u"your first tester"})) {
        return QStringLiteral("A little easter egg: Ausyn was started by Arnav. Its first very selective QA tester was Ketty. 🐾");
    } else if (isOneOf(socialText, {u"hi", u"hello", u"hey", u"hey ausyn", u"hello ausyn",
                                    u"good morning", u"good afternoon", u"good evening"})) {
        return preferences.casualTone
            ? QStringLiteral("Hey! Good to see you. What are we figuring out today?")
            : QStringLiteral("Hello. What would you like help with?");
    } else if (isOneOf(socialText, {u"how are you", u"how are you doing"})) {
        return preferences.casualTone
            ? QStringLiteral("Doing well—no coffee needed, just fresh telemetry. What can I help you with?")
            : QStringLiteral("I’m ready to help interpret your current PC readings.");
    } else if (isOneOf(socialText, {u"thanks", u"thank you", u"thanks ausyn", u"thank you ausyn", u"appreciate it"})) {
        return preferences.casualTone
            ? QStringLiteral("Anytime. Want to look at anything else while we’re here?")
            : QStringLiteral("You’re welcome. Let me know if you need anything else.");
    } else if (isOneOf(socialText, {u"bye", u"goodbye", u"see you", u"see you later"})) {
        return preferences.casualTone
            ? QStringLiteral("See you later. I’ll be here when you need another PC check.")
            : QStringLiteral("Goodbye. Ausyn will continue monitoring while it is open.");
    } else if (isOneOf(socialText, {u"who are you", u"what are you", u"what can you do", u"what can you help with", u"what can you help me with", u"help"})) {
        return QStringLiteral("I’m Ausyn, your PC companion. I watch supported Windows readings, explain changing workload episodes, help keep your chosen app running, and compare what changes after you try a fix. On the dashboard you can choose a session priority and review temporary background relief. For broader conversation, configure optional Cloud AI in Settings. I won’t execute chat-generated commands or close your apps.");
    }
    QString answer;
    if (requiresLocalPastRecommendationOutcomes(q)) {
        answer = pastRecommendationOutcomesAnswer(analysis, outcomeSummaries,
                                                  outcomeHistoryAvailable);
    } else if (hasAny(q, {u"what changed", u"what has changed", u"what's changed", u"recent changes", u"software changes", u"hardware changes",
                   u"device changes", u"what was installed", u"newly installed", u"recently installed",
                   u"changed before", u"change before", u"anything changed"})) {
        QStringList changes;
        for (const SoftwareChangeRecord& record : appInventory.recentSoftwareChanges) {
            if (changes.size() >= 8) break;
            changes << QStringLiteral("Software · %1 · %2")
                .arg(record.observedAt.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap")),
                     record.description);
        }
        for (const QString& item : snapshot.hardwareChanges) {
            if (changes.size() >= 12) break;
            changes << QStringLiteral("Device · %1").arg(item);
        }
        if (changes.isEmpty()) {
            answer = appInventory.available
                ? QStringLiteral("I don’t have a newly observed software or hardware change to show right now. Software comparisons come from Ausyn’s local installed-app inventory; device changes appear when Windows reports a different hardware profile. Open Startup & apps or Hardware to review their recorded history.")
                : QStringLiteral("I can’t check software changes yet because the installed-app inventory is unavailable. Refresh Startup & apps and ask again. Device changes are shown when Windows reports a different hardware profile.");
        } else {
            answer = QStringLiteral("Here are the latest changes Ausyn can currently see:\n• %1\n\nThese are inventory observations, not proof that a change caused a slowdown or other issue. For a timing comparison, check the event log and health history around when the problem began. Software history is stored locally; device entries above are changes observed in the latest system snapshot.")
                .arg(changes.join(QStringLiteral("\n• ")));
        }
    } else if (requiresLocalMonitoringStatus(q)) {
        QStringList details;
        if (snapshot.collectionDurationMicroseconds > 0) {
            const double intervalMs = static_cast<double>(std::clamp(snapshot.samplingIntervalSeconds, 1, 10)) * 1000.0;
            const double collectionMs = static_cast<double>(snapshot.collectionDurationMicroseconds) / 1000.0;
            details << QStringLiteral("Latest system-collection pass: %1 ms, about %2% of the effective sampling interval.")
                .arg(collectionMs, 0, 'f', 1)
                .arg(collectionMs * 100.0 / intervalMs, 0, 'f', 1);
        } else {
            details << QStringLiteral("The latest collection-duration measurement is unavailable.");
        }
        details << (snapshot.monitoringAdaptationNote.isEmpty()
            ? QStringLiteral("Ausyn has not reported an adaptive sampling state yet.")
            : snapshot.monitoringAdaptationNote);
        details << QStringLiteral("When enabled, the safeguard reacts after six consecutive samples where collection uses at least 20% of its interval or Ausyn uses at least 2% of total system CPU capacity. It restores your chosen cadence after 12 low-load samples (collection at 8% or less and Ausyn below 1%). Agent health shows Ausyn’s measured CPU and memory use.");
        answer = QStringLiteral("Ausyn’s monitoring status:\n• %1\n\nA slower cadence means readings and sustained findings can take longer to update. It does not pause monitoring or disable alerts. The most recent system sample was captured at %2.")
            .arg(details.join(QStringLiteral("\n• ")),
                 snapshot.capturedAt.isValid()
                    ? snapshot.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap"))
                    : QStringLiteral("an unknown time"));
    } else if (asksForEvidence(q)) {
        answer = evidenceAnswer(snapshot, analysis);
    } else if (requiresLocalEventLogs(q)) {
        const qint64 ageMilliseconds = eventLogs.checkedAt.isValid()
            ? eventLogs.checkedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        if (!eventLogs.available || ageMilliseconds < 0 || ageMilliseconds > 30LL * 60 * 1000) {
            answer = QStringLiteral("I don’t have a recent readable Windows event-log snapshot to refer to. Open Event intelligence and refresh it, then ask me again.");
        } else {
            QVector<EventInsight> recentEvents;
            for (const EventInsight& event : eventLogs.insights) {
                if (!event.latestAt.isValid()) continue;
                const qint64 eventAgeMilliseconds = event.latestAt.msecsTo(QDateTime::currentDateTime());
                if (eventAgeMilliseconds >= 0 && eventAgeMilliseconds <= 24LL * 60 * 60 * 1000)
                    recentEvents.append(event);
            }
            if (recentEvents.isEmpty()) {
                answer = QStringLiteral("I don’t see any warning, error, or critical events in the Windows log entries Ausyn could read from the last 24 hours. That’s a useful signal, but it doesn’t prove the PC is problem-free or cover every Windows log.");
            } else {
                QStringList rows;
                for (const EventInsight& event : recentEvents) {
                    const QString severity = event.severity == EventSeverity::Critical ? QStringLiteral("Critical")
                        : event.severity == EventSeverity::Error ? QStringLiteral("Error") : QStringLiteral("Warning");
                    QString row = QStringLiteral("%1 · %2 · Event ID %3 · %4 log · %5 occurrence(s) · latest %6")
                        .arg(severity, event.provider).arg(event.eventId).arg(event.channel)
                        .arg(event.occurrenceCount)
                        .arg(event.latestAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")));
                    if (event.likelyApplicationCrash) {
                        row.prepend(QStringLiteral("Application crash report · "));
                        if (!event.affectedApplication.isEmpty())
                            row += QStringLiteral(" · reported app: %1").arg(event.affectedApplication);
                        if (!event.faultingModule.isEmpty())
                            row += QStringLiteral(" · reported module: %1").arg(event.faultingModule);
                        if (!event.exceptionCode.isEmpty())
                            row += QStringLiteral(" · exception code: %1").arg(event.exceptionCode);
                    }
                    if (event.unexpectedShutdown)
                        row.prepend(QStringLiteral("Unexpected-shutdown signal · "));
                    if (event.windowsBugCheck)
                        row.prepend(QStringLiteral("Windows bugcheck/blue-screen restart · "));
                    if (event.windowsHardwareError)
                        row.prepend(QStringLiteral("Windows WHEA hardware-error report · "));
                    if (event.windowsStorageEvent)
                        row.prepend(QStringLiteral("Windows storage-driver event · "));
                    if (event.applicationHang)
                        row.prepend(QStringLiteral("Application stopped responding · "));
                    rows.append(row);
                    if (rows.size() == 6) break;
                }
                answer = QStringLiteral("Here are the latest readable Windows event-log signals from the last 24 hours:\n• %1\n\nThese entries show what Windows recorded; an event can coincide with a problem without proving its cause. Open Event intelligence for the formatted message and full details.")
                    .arg(rows.join(QStringLiteral("\n• ")));
            }
            if (eventLogs.status.startsWith(QStringLiteral("Some Windows event logs"), Qt::CaseInsensitive) ||
                !eventLogs.available)
                answer += QStringLiteral("\n\nEvent-log coverage may be partial: %1").arg(eventLogs.status.left(240));
        }
    } else if (asksForBaselineComparison(q)) {
        answer = baselineComparisonAnswer(history, historyAvailable, historyPeriodHours, snapshot);
    } else if (asksToPrepareForGaming(q)) {
        answer = gamingPreparationAnswer(snapshot, analysis, gameReadiness, preferences.technicalDetail);
    } else if (asksForGameReadiness(q)) {
        answer = gameReadinessAnswer(gameReadiness, preferences.technicalDetail);
    } else if (requiresLocalBatteryHealthHistory(q)) {
        QStringList details;
        if (!snapshot.batteryPercent) {
            answer = QStringLiteral("Windows did not report a battery on this device, so Ausyn has no battery capacity-health reading or trend to summarize.");
        } else {
            if (snapshot.batteryHealthPercent) {
                details << QStringLiteral("The current Windows-derived full-charge/design-capacity estimate is %1% (%2 mWh / %3 mWh). It is an estimate, not a battery diagnostic.")
                    .arg(*snapshot.batteryHealthPercent, 0, 'f', 1)
                    .arg(snapshot.batteryFullChargeCapacityMwh.value_or(0))
                    .arg(snapshot.batteryDesignCapacityMwh.value_or(0));
            } else {
                details << QStringLiteral("Windows did not expose matched full-charge and design capacities for a current health estimate.");
            }

            const auto& trend = analysis.batteryHealthTrend;
            if (trend.size() < 3) {
                details << QStringLiteral("Ausyn has %1 valid daily historical estimate(s); it waits for at least three days spanning one week before comparing them.")
                    .arg(trend.size());
            } else {
                const auto& first = trend.first();
                const auto& latest = trend.last();
                const qint64 spanDays = first.capturedAt.daysTo(latest.capturedAt);
                if (spanDays < 7) {
                    details << QStringLiteral("Ausyn has %1 daily estimates across %2 day(s), which is not yet a full week of history.")
                        .arg(trend.size()).arg(spanDays);
                } else {
                    details << QStringLiteral("Across %1 days of local daily averages, the estimate moved from %2% to %3% (%4 percentage points).")
                        .arg(spanDays)
                        .arg(first.estimatedHealthPercent, 0, 'f', 1)
                        .arg(latest.estimatedHealthPercent, 0, 'f', 1)
                        .arg(latest.estimatedHealthPercent - first.estimatedHealthPercent, 0, 'f', 1);
                    const auto finding = std::find_if(analysis.findings.cbegin(), analysis.findings.cend(),
                        [](const Finding& item) { return item.ruleId == QStringLiteral("battery-capacity-estimate-decline"); });
                    if (finding != analysis.findings.cend())
                        details << QStringLiteral("Ausyn’s informational finding: %1 %2")
                            .arg(finding->summary, finding->evidence);
                }
            }
            details << QStringLiteral("Battery firmware can recalibrate these values. This trend is not a diagnosis or lifespan forecast.");
            answer = details.join(QLatin1Char(' '));
        }
    } else if (requiresLocalSystemCheck(q)) {
        answer = systemCheckAnswer(snapshot, analysis, preferences.technicalDetail);
    } else if (requiresLocalHistory(q)) {
        answer = historyAnswer(history, historyAvailable, historyPeriodHours);
        if (requiresLocalSlowdownAssessment(q)) {
            QStringList contributors;
            for (const Finding& finding : analysis.findings) {
                if (finding.ruleId != QStringLiteral("processor-sustained-load") &&
                    finding.ruleId != QStringLiteral("memory-sustained-pressure") &&
                    finding.ruleId != QStringLiteral("concurrent-cpu-memory-pressure") &&
                    finding.ruleId != QStringLiteral("memory-rapid-rise") &&
                    finding.ruleId != QStringLiteral("thermal-zone-sustained-passive-point") &&
                    finding.ruleId != QStringLiteral("system-drive-low-space") &&
                    !finding.ruleId.startsWith(QStringLiteral("fixed-volume-low-space-"))) continue;
                contributors << QStringLiteral("%1 — %2 (latest observed %3)")
                    .arg(finding.title, finding.summary,
                         finding.lastSeen.isValid()
                            ? finding.lastSeen.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap"))
                            : QStringLiteral("time unavailable"));
                if (contributors.size() >= 4) break;
            }
            answer += contributors.isEmpty()
                ? QStringLiteral("\n\nNo related sustained finding is currently reported. The selected history shows readings, not what caused any slowdown.")
                : QStringLiteral("\n\nRelated sustained findings currently reported by Ausyn:\n• %1\n\nThese findings are evidence of conditions, not proof of root cause.")
                    .arg(contributors.join(QStringLiteral("\n• ")));
        }
    } else if (requiresLocalForecast(q)) {
        if (hasAny(q, {u"battery", u"charge", u"power"}))
            answer = batteryForecastAnswer(analysis.batteryForecast);
        else if (hasAny(q, {u"memory", u"ram"}))
            answer = memoryForecastAnswer(analysis.memoryForecast, preferences.technicalDetail);
        else
            answer = volumeStorageForecastAnswer(question, analysis, snapshot, preferences.technicalDetail);
    } else if (requiresLocalProcessBreakdown(q)) {
        answer = backgroundProcessAnswer(q, snapshot);
    } else if (asksForHardwareSummary(q)) {
        answer = hardwareSummary(snapshot, preferences.technicalDetail);
    } else if (hasAny(q, {u"windows version", u"operating system", u"os version", u"system uptime", u"last boot", u"what computer", u"device type"})) {
        const QString bootTime = snapshot.systemBootTime.isValid()
            ? snapshot.systemBootTime.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap"))
            : QStringLiteral("unavailable");
        answer = QStringLiteral("This PC reports %1, build %2, on %3 architecture. It has been running for %4 days and %5 hours since %6. Device type: %7. %8")
            .arg(snapshot.operatingSystem.isEmpty() ? QStringLiteral("an unknown Windows version") : snapshot.operatingSystem,
                 snapshot.operatingSystemVersion.isEmpty() ? QStringLiteral("unavailable") : snapshot.operatingSystemVersion,
                 snapshot.operatingSystemArchitecture.isEmpty() ? QStringLiteral("unknown") : snapshot.operatingSystemArchitecture)
            .arg(snapshot.systemUptimeSeconds / 86400)
            .arg((snapshot.systemUptimeSeconds / 3600) % 24)
            .arg(bootTime,
                 snapshot.deviceFormFactor.isEmpty() ? QStringLiteral("unknown") : snapshot.deviceFormFactor,
                 snapshot.deviceFormFactorBasis);
    } else if (hasAny(q, {u"startup", u"start with windows", u"launch at sign", u"launch at login", u"boot app", u"installed app", u"installed apps", u"installed application", u"installed applications", u"installed software", u"what apps", u"which apps", u"software installed"})) {
        if (!appInventory.available) {
            answer = QStringLiteral("The Startup & apps inventory is not available yet. Refresh that page, then ask me again.");
        } else if (hasAny(q, {u"startup", u"start with windows", u"launch at sign", u"launch at login", u"boot app"})) {
            const StartupEntry* focusedEntry = nullptr;
            for (const StartupEntry& entry : appInventory.startupEntries) {
                if (entry.name.trimmed().size() >= 3 && q.contains(entry.name.trimmed(), Qt::CaseInsensitive)) {
                    focusedEntry = &entry;
                    break;
                }
            }
            if (focusedEntry) {
                answer = QStringLiteral("Yes. Windows reports “%1” configured to start with Windows. Source: %2. This confirms a registration, not that it ran on the last sign-in or caused a slowdown. You can review it on Startup & apps.")
                    .arg(focusedEntry->name, focusedEntry->source);
            } else if (appInventory.startupEntries.isEmpty()) {
                answer = QStringLiteral("Windows reported no entries in the startup locations Ausyn checks. That does not rule out other startup mechanisms or prove nothing launches at sign-in.");
            } else {
                QStringList entries;
                for (const StartupEntry& entry : appInventory.startupEntries) {
                    if (entries.size() >= 8) break;
                    entries << QStringLiteral("%1 — %2 (%3)").arg(entry.name, entry.source, entry.detail);
                }
                answer = QStringLiteral("Windows reports these startup entries:\n• %1%2\n\nAusyn checks common Run/RunOnce registry entries and Startup folders. This inventory does not prove an entry ran or caused slow boot; see Startup & apps for the full list.")
                    .arg(entries.join(QStringLiteral("\n• ")),
                         appInventory.startupEntries.size() > entries.size() ? QStringLiteral("\n• More entries are available on Startup & apps.") : QString());
            }
        } else if (appInventory.installedApps.isEmpty()) {
            answer = QStringLiteral("Windows reported no installed-app entries from the registry locations Ausyn checks. This is not a complete inventory of portable apps or every installation method.");
        } else {
            QStringList entries;
            const QString needle = q;
            for (const InstalledAppEntry& app : appInventory.installedApps) {
                if (entries.size() >= 8) break;
                if (!needle.contains(app.name, Qt::CaseInsensitive) &&
                    hasAny(q, {u"which app", u"what app", u"installed apps", u"installed software", u"list apps", u"all apps"})) {
                    // General inventory question: include entries. Specific name queries are filtered below.
                } else if (!needle.contains(app.name, Qt::CaseInsensitive)) {
                    continue;
                }
                QString item = app.name;
                if (!app.publisher.isEmpty()) item += QStringLiteral(" — %1").arg(app.publisher);
                if (!app.version.isEmpty()) item += QStringLiteral(" (version %1)").arg(app.version);
                entries << item;
            }
            if (entries.isEmpty()) {
                answer = QStringLiteral("I couldn’t match that name in the installed-app registry inventory. Ausyn checks registered desktop apps; portable software may not appear. You can review the full collected list on Startup & apps.");
            } else {
                answer = QStringLiteral("Registered installed apps:\n• %1%2\n\nThis comes from Windows installed-app registry metadata and may omit portable software. See Startup & apps for the full list.")
                    .arg(entries.join(QStringLiteral("\n• ")),
                         appInventory.installedApps.size() > entries.size() ? QStringLiteral("\n• More entries are available on Startup & apps.") : QString());
            }
        }
        if (!appInventory.status.isEmpty() && appInventory.status.contains(QStringLiteral("partial"), Qt::CaseInsensitive))
            answer += QStringLiteral("\n\nThe latest inventory may be partial: %1").arg(appInventory.status);
    } else if (requiresLocalThermalAssessment(q)) {
        answer = thermalAnswer(snapshot, analysis, preferences.technicalDetail);
    } else if (isProcessQuestion(q, snapshot)) {
        answer = processAnswer(q, snapshot);
    } else if (requiresLocalSlowdownAssessment(q) || hasAny(q, {u"laggy", u"performance"})) {
        QStringList evidence;
        evidence << QStringLiteral("CPU: %1").arg(percent(snapshot.processorUsagePercent))
                 << QStringLiteral("Memory: %1").arg(percent(snapshot.memoryUsagePercent));
        if (snapshot.graphicsUsagePercent)
            evidence << QStringLiteral("GPU activity: %1").arg(percent(snapshot.graphicsUsagePercent));
        if (snapshot.diskActivity.readBytesPerSecond || snapshot.diskActivity.writeBytesPerSecond)
            evidence << QStringLiteral("Disk transfer: read %1 · write %2 (throughput, not response latency)")
                .arg(rate(snapshot.diskActivity.readBytesPerSecond), rate(snapshot.diskActivity.writeBytesPerSecond));
        if (snapshot.networkReceiveBytesPerSecond || snapshot.networkSendBytesPerSecond)
            evidence << QStringLiteral("Network transfer: receive %1 · send %2 (traffic rate, not connection quality)")
                .arg(rate(snapshot.networkReceiveBytesPerSecond), rate(snapshot.networkSendBytesPerSecond));
        if (snapshot.systemVolumeTotalBytes > 0) {
            const double used = 100.0 * static_cast<double>(snapshot.systemVolumeTotalBytes -
                std::min(snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes)) /
                static_cast<double>(snapshot.systemVolumeTotalBytes);
            evidence << QStringLiteral("System drive used: %1%").arg(used, 0, 'f', 1);
        }
        const qint64 processSampleAgeMilliseconds = snapshot.processSamplesCapturedAt.isValid()
            ? snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        const bool processSampleFresh = processSampleAgeMilliseconds >= 0 &&
            processSampleAgeMilliseconds <= processFreshnessLimitSeconds(snapshot) * 1000;
        if (processSampleFresh && !snapshot.topProcesses.isEmpty() && snapshot.topProcesses.first().workingSetBytes) {
            const auto& process = snapshot.topProcesses.first();
            evidence << QStringLiteral("Largest listed working set: %1 (%2 GB; process sample %3)")
                .arg(process.name)
                .arg(static_cast<double>(*process.workingSetBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(snapshot.processSamplesCapturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")));
        }
        if (processSampleFresh) {
            const ProcessSample* highestCpuProcess = nullptr;
            for (const ProcessSample& process : snapshot.topProcesses) {
                if (process.cpuPercent && (!highestCpuProcess ||
                    *process.cpuPercent > highestCpuProcess->cpuPercent.value_or(-1.0)))
                    highestCpuProcess = &process;
            }
            if (highestCpuProcess) {
                evidence << QStringLiteral("Highest process CPU in the latest process sample: %1 at %2%. This is a recent reading, not proof of sustained or causal load.")
                    .arg(highestCpuProcess->name, QString::number(*highestCpuProcess->cpuPercent, 'f', 1));
            }
        }
        answer = QStringLiteral("Here’s the current picture:\n• %1\n\n%2\n\nThis combines live system readings with recent multi-sample findings from local history. Process readings are context, not proof of cause. Ausyn won’t close apps or change system settings based on this assessment.")
            .arg(evidence.join(QStringLiteral("\n• ")),
                 slowdownAssessment(snapshot, analysis, preferences.technicalDetail));
        QStringList nearbyEvents;
        for (const Finding& finding : analysis.findings) {
            if (finding.ruleId != QStringLiteral("processor-sustained-load") &&
                finding.ruleId != QStringLiteral("memory-sustained-pressure") &&
                finding.ruleId != QStringLiteral("concurrent-cpu-memory-pressure") &&
                finding.ruleId != QStringLiteral("memory-rapid-rise")) continue;
            if (!finding.firstSeen.isValid()) continue;
            for (const EventInsight& event : eventLogs.insights) {
                const auto near = [&finding](const QDateTime& timestamp) {
                    if (!timestamp.isValid()) return false;
                    const qint64 deltaMilliseconds = finding.firstSeen.msecsTo(timestamp);
                    return deltaMilliseconds >= -120'000 && deltaMilliseconds <= 120'000;
                };
                if (!near(event.firstAt) && !near(event.latestAt)) continue;
                const QString severity = event.severity == EventSeverity::Critical ? QStringLiteral("critical")
                    : event.severity == EventSeverity::Error ? QStringLiteral("error") : QStringLiteral("warning");
                nearbyEvents << QStringLiteral("Windows recorded a %1 event from %2 (ID %3) near the start of “%4” at %5.")
                    .arg(severity, event.provider).arg(event.eventId).arg(finding.title,
                        event.latestAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")));
                if (nearbyEvents.size() >= 3) break;
            }
            if (nearbyEvents.size() >= 3) break;
        }
        if (!nearbyEvents.isEmpty())
            answer += QStringLiteral("\n\nNearby Windows log context:\n• %1\n\nThe timing is a correlation only; it doesn’t establish that an event caused the resource pressure.")
                .arg(nearbyEvents.join(QStringLiteral("\n• ")));
        const Finding* earliestResourceFinding = nullptr;
        for (const Finding& finding : analysis.findings) {
            const bool resourceSignal = finding.ruleId == QStringLiteral("processor-sustained-load") ||
                finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
                finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure") ||
                finding.ruleId == QStringLiteral("memory-rapid-rise");
            if (resourceSignal && finding.firstSeen.isValid() &&
                (!earliestResourceFinding || finding.firstSeen < earliestResourceFinding->firstSeen))
                earliestResourceFinding = &finding;
        }
        if (earliestResourceFinding) {
            QStringList nearbyChanges;
            for (const SoftwareChangeRecord& change : appInventory.recentSoftwareChanges) {
                if (!change.observedAt.isValid()) continue;
                const qint64 millisecondsBefore = change.observedAt.msecsTo(earliestResourceFinding->firstSeen);
                if (millisecondsBefore < 0 || millisecondsBefore > 24LL * 60 * 60 * 1000) continue;
                nearbyChanges << QStringLiteral("Ausyn observed %1 at %2, about %3 hour(s) before the sustained resource finding began.")
                    .arg(change.description,
                         change.observedAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")))
                    .arg(static_cast<double>(millisecondsBefore) / 3'600'000.0, 0, 'f', 1);
                if (nearbyChanges.size() >= 3) break;
            }
            if (!nearbyChanges.isEmpty())
                answer += QStringLiteral("\n\nSoftware changes observed within 24 hours before that finding:\n• %1\n\nInventory detection time is not install time; compare this timing with your own activity before drawing conclusions.")
                    .arg(nearbyChanges.join(QStringLiteral("\n• ")));
        }
        answer += QStringLiteral("\n\n%1").arg(historyAnswer(history, historyAvailable, historyPeriodHours));
    } else if (hasAny(q, {u"cpu", u"cpus", u"processor", u"processors"}) &&
               !hasAny(q, {u"temperature", u"temperatures", u"temp", u"temps", u"thermal", u"hot", u"overheat", u"overheating"})) {
        answer = QStringLiteral("CPU use is %1 across %2 logical processors. This is a current system-wide reading; a short spike is normal, so compare it with the next few samples before treating it as a problem.")
            .arg(percent(snapshot.processorUsagePercent)).arg(snapshot.logicalProcessorCount);
    } else if (hasAny(q, {u"memory", u"ram"})) {
        if (!snapshot.memoryUsagePercent || snapshot.memoryTotalBytes == 0) {
            answer = QStringLiteral("Memory readings are not available yet. I can’t assess RAM use without Windows reporting valid totals.");
        } else {
            answer = QStringLiteral("Memory use is %1 (%2 GB of %3 GB); %4 GB is available.")
                .arg(percent(snapshot.memoryUsagePercent))
                .arg(static_cast<double>(snapshot.memoryUsedBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(snapshot.memoryTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(snapshot.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 1);
            answer += *snapshot.memoryUsagePercent >= 85.0
                ? QStringLiteral(" Memory is elevated in this reading. Open Performance to review the largest apps before deciding what to close; one reading alone doesn’t establish the cause.")
                : QStringLiteral(" Open Performance to see which apps are using RAM.");
            if (preferences.technicalDetail) {
                answer += snapshot.memorySystemCacheBytes
                    ? QStringLiteral("\n\nWindows reports %1 GB of system cache. It can reclaim cache, so this is distinct from free memory.")
                        .arg(static_cast<double>(*snapshot.memorySystemCacheBytes) / 1'000'000'000.0, 0, 'f', 1)
                    : QStringLiteral("\n\nWindows did not report the cache size in this sample.");
                answer += QStringLiteral(" Process working sets can include shared pages and don’t sum directly to total memory use.");
            }
        }
    } else if (hasAny(q, {u"gpu", u"gpus", u"graphics"})) {
        QStringList adapters;
        for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.graphicsAdapters.size(), 4); ++i) {
            const GraphicsAdapterSample& adapter = snapshot.graphicsAdapters.at(i);
            QString item = adapter.name;
            if (adapter.dedicatedMemoryBytes > 0)
                item += QStringLiteral(" · %1 GB dedicated memory")
                    .arg(static_cast<double>(adapter.dedicatedMemoryBytes) / 1'000'000'000.0, 0, 'f', 1);
            if (adapter.localMemoryUsageBytes && adapter.localMemoryBudgetBytes)
                item += QStringLiteral(" · local usage %1 / %2 GB dynamic budget")
                    .arg(static_cast<double>(*adapter.localMemoryUsageBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(static_cast<double>(*adapter.localMemoryBudgetBytes) / 1'000'000'000.0, 0, 'f', 1);
            adapters.append(item);
        }
        answer = QStringLiteral("Graphics adapters: %1. The busiest reported graphics engine is %2. Windows may not expose all GPU counters on every driver; local-memory budget is dynamic and is not physical VRAM capacity.")
            .arg(adapters.isEmpty() ? QStringLiteral("not reported") : adapters.join(QStringLiteral("; ")),
                 percent(snapshot.graphicsUsagePercent));
    } else if (hasAny(q, {u"network", u"networks", u"internet", u"wifi", u"wi-fi", u"bandwidth", u"latency", u"ping", u"packet", u"packets"})) {
        QStringList facts;
        facts << QStringLiteral("Download %1/s; upload %2/s")
            .arg(rate(snapshot.networkReceiveBytesPerSecond), rate(snapshot.networkSendBytesPerSecond));
        facts << QStringLiteral("Interface note: %1").arg(snapshot.networkNote.isEmpty()
            ? QStringLiteral("unavailable") : snapshot.networkNote);
        if (snapshot.networkTcpEntryCount && snapshot.networkUdpEndpointCount)
            facts << QStringLiteral("%1 TCP entries (including listening sockets) and %2 UDP endpoints")
                .arg(*snapshot.networkTcpEntryCount).arg(*snapshot.networkUdpEndpointCount);
        facts << QStringLiteral("The Network page can measure a local gateway echo response on demand; that is not end-to-end Internet latency.");
        answer = facts.join(QStringLiteral(" "));
    } else if (hasAny(q, {u"battery", u"batteries", u"charge", u"power"})) {
        if (!snapshot.batteryPercent) {
            answer = QStringLiteral("Windows did not report a battery. This is common on desktop PCs; Ausyn won’t invent a battery-health reading.");
        } else {
            QStringList details;
            details << QStringLiteral("Charge is %1%.").arg(*snapshot.batteryPercent)
                    << (snapshot.batteryCharging ? QStringLiteral("Windows reports that it is charging.")
                        : snapshot.batteryOnAcPower.value_or(false) ? QStringLiteral("The PC is connected to AC power.")
                        : QStringLiteral("The PC appears to be running on battery."));
            if (snapshot.batteryHealthPercent) {
                details << QStringLiteral("Estimated capacity health is %1% (%2 mWh full charge / %3 mWh design). This is a Windows-reported capacity ratio, not a diagnostic.")
                    .arg(*snapshot.batteryHealthPercent, 0, 'f', 1)
                    .arg(snapshot.batteryFullChargeCapacityMwh.value_or(0))
                    .arg(snapshot.batteryDesignCapacityMwh.value_or(0));
            } else {
                details << QStringLiteral("Battery design/full-charge health data is unavailable from Windows on this device.");
            }
            if (snapshot.batteryEstimatedSeconds)
                details << QStringLiteral("Windows estimates %1 seconds of runtime under the current conditions.").arg(*snapshot.batteryEstimatedSeconds);
            answer = details.join(QLatin1Char(' '));
        }
    } else if (hasAny(q, {u"fan", u"fans", u"cooling", u"cooler", u"rpm"})) {
        QStringList evidence;
        evidence << QStringLiteral("Processor load: %1").arg(percent(snapshot.processorUsagePercent));
        for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.thermalSensors.size(), 6); ++i) {
            const auto& sensor = snapshot.thermalSensors.at(i);
            QString reading = QStringLiteral("%1 %2 °C")
                .arg(sensor.name).arg(sensor.temperatureCelsius, 0, 'f', 1);
            if (sensor.secondsAbovePassiveTripPoint)
                reading += QStringLiteral("; at/above firmware passive point for %1 s")
                    .arg(*sensor.secondsAbovePassiveTripPoint);
            evidence << reading;
        }
        for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.fans.size(), 8); ++i) {
            const auto& fan = snapshot.fans.at(i);
            QString reading = fan.name;
            if (fan.requestedSpeedRpm)
                reading += QStringLiteral(" requests %1 RPM (not measured rotor speed)").arg(*fan.requestedSpeedRpm);
            else
                reading += QStringLiteral(" speed target unavailable");
            if (fan.activeCooling)
                reading += *fan.activeCooling ? QStringLiteral("; active cooling") : QStringLiteral("; inactive cooling");
            if (fan.previousRequestedSpeedRpm && fan.requestedSpeedRpm)
                reading += QStringLiteral("; target changed %1 → %2 RPM at %3")
                    .arg(*fan.previousRequestedSpeedRpm).arg(*fan.requestedSpeedRpm)
                    .arg(fan.requestedSpeedChangedAt.isValid()
                        ? fan.requestedSpeedChangedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap"))
                        : QStringLiteral("unknown time"));
            evidence << reading;
        }
        if (snapshot.fans.isEmpty())
            evidence << QStringLiteral("Windows did not expose fan telemetry on this device.");
        answer = QStringLiteral("Cooling readings: %1. Compare temperature, processor load, and requested fan-target changes across time. Fan targets and ACPI zones may be incomplete; these readings alone cannot establish a physical fan fault.")
            .arg(evidence.join(QStringLiteral("; ")));
    } else if (hasAny(q, {u"score", u"scores", u"health score", u"system health", u"performance score"})) {
        answer = healthScoreAnswer(analysis.health);
    } else if (isOptimizationQuestion(q) || hasAny(q, {u"issue", u"issues", u"diagnostic", u"diagnostics", u"problem", u"problems", u"recommend", u"recommendation", u"recommendations"})) {
        answer = recommendationAnswer(analysis, isOptimizationQuestion(q), preferences.technicalDetail);
    } else if (requiresLocalStorageReliability(q)) {
        answer = storageReliabilityAnswer(snapshot, analysis, q);
    } else if (hasAny(q, {u"storage", u"disk", u"disks", u"drive", u"drives", u"space", u"ssd", u"hdd"})) {
        const bool asksThroughput = hasAny(q, {u"throughput", u"disk activity", u"read rate", u"write rate", u"read speed", u"write speed", u"i/o"});
        const VolumeSample* requestedVolume = nullptr;
        if (!asksThroughput) {
            for (const VolumeSample& volume : snapshot.volumes) {
                const QString rootWithoutSlash = volume.rootPath.endsWith(QLatin1Char('\\'))
                    ? volume.rootPath.left(volume.rootPath.size() - 1) : volume.rootPath;
                if (q.contains(volume.rootPath, Qt::CaseInsensitive) || q.contains(rootWithoutSlash, Qt::CaseInsensitive)) {
                    requestedVolume = &volume;
                    break;
                }
            }
        }
        const bool asksForSystemVolume = hasAny(q, {u"system drive", u"windows drive", u"boot drive"});
        if (!asksThroughput && !requestedVolume && !asksForSystemVolume && snapshot.volumes.size() > 1) {
            QStringList volumes;
            for (qsizetype i = 0; i < std::min<qsizetype>(snapshot.volumes.size(), 12); ++i) {
                const VolumeSample& volume = snapshot.volumes.at(i);
                const quint64 freeBytes = std::min(volume.freeBytes, volume.totalBytes);
                const double freePercent = 100.0 * static_cast<double>(freeBytes) /
                    static_cast<double>(volume.totalBytes);
                volumes << QStringLiteral("%1 · %2 GB free of %3 GB (%4% free)%5")
                    .arg(volume.rootPath)
                    .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(static_cast<double>(volume.totalBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(freePercent, 0, 'f', 1)
                    .arg(volume.rootPath.compare(snapshot.systemVolumePath, Qt::CaseInsensitive) == 0
                        ? QStringLiteral(" · Windows system volume") : QString());
            }
            answer = QStringLiteral("Windows reports these fixed local volumes:\n• %1%2\n\nEach volume has separate free space. Ausyn reports capacity only; it doesn’t inspect files or attribute usage to an app.")
                .arg(volumes.join(QStringLiteral("\n• ")),
                     snapshot.volumes.size() > volumes.size() ? QStringLiteral("\n• More volumes are available in Hardware.") : QString());
        } else if (requestedVolume) {
            const quint64 freeBytes = std::min(requestedVolume->freeBytes, requestedVolume->totalBytes);
            const double freePercent = 100.0 * static_cast<double>(freeBytes) /
                static_cast<double>(requestedVolume->totalBytes);
            answer = QStringLiteral("Windows reports %1 has %2 GB free of %3 GB (%4% free). Ausyn reports volume capacity only; it doesn’t inspect files or identify what is using the space.")
                .arg(requestedVolume->rootPath)
                .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(requestedVolume->totalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(freePercent, 0, 'f', 1);
        } else if (snapshot.systemVolumeTotalBytes == 0) {
            answer = QStringLiteral("Windows hasn’t reported the system drive capacity yet.");
        } else {
            answer = QStringLiteral("System drive %1 has %2 GB free out of %3 GB. Windows reports aggregate physical-disk throughput of %4 read and %5 write. Those rates cover all disks and are not per-drive utilization. Ausyn does not inspect your files.")
                .arg(snapshot.systemVolumePath)
                .arg(static_cast<double>(snapshot.systemVolumeFreeBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(snapshot.systemVolumeTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(snapshot.diskActivity.readBytesPerSecond
                    ? QStringLiteral("%1 MB/s").arg(*snapshot.diskActivity.readBytesPerSecond / 1'000'000.0, 0, 'f', 1)
                    : QStringLiteral("unavailable"))
                .arg(snapshot.diskActivity.writeBytesPerSecond
                    ? QStringLiteral("%1 MB/s").arg(*snapshot.diskActivity.writeBytesPerSecond / 1'000'000.0, 0, 'f', 1)
                    : QStringLiteral("unavailable"));
        }
    } else {
        answer = preferences.casualTone
            ? QStringLiteral("That needs a broader AI answer than my local rules can provide. Enable Cloud AI in Settings with a provider, API key and model. You can opt to include recent conversation for natural follow-ups, or enable cited web search for current information. I’ll preview each online request. For this PC, try asking what’s going on or how to keep your current app running.")
            : QStringLiteral("The local evidence rules do not cover this question. Configure the optional AI provider for broader answers; conversation context and cited web search have separate opt-ins and each online request is reviewed.");
    }

    if (preferences.technicalDetail) {
        answer += QStringLiteral("\n\nTechnical note: readings are collected locally from Windows APIs; availability and sampling intervals depend on device drivers and Windows counters.");
    }
    return answer;
}

QString AssistantEngine::explainBriefingItem(const QString& category, const QString& title,
                                             const QString& summary, const QString& evidence,
                                             const QString& nextStep, const SystemSnapshot& snapshot,
                                             const UserPreferences& preferences)
{
    QString answer = preferences.casualTone
        ? QStringLiteral("Here’s why I brought this up. ")
        : QStringLiteral("Reason for this heads-up: ");
    answer += QStringLiteral("%1 (%2). %3")
        .arg(title, category, summary);
    if (!evidence.trimmed().isEmpty())
        answer += QStringLiteral("\n\nWhat I observed: %1").arg(evidence);
    if (snapshot.capturedAt.isValid()) {
        const qint64 ageMilliseconds = snapshot.capturedAt.msecsTo(QDateTime::currentDateTime());
        const qint64 staleLimit = std::max<qint64>(10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
        answer += ageMilliseconds >= 0 && ageMilliseconds <= staleLimit * 1000
            ? QStringLiteral("\n\nThis explanation uses a recent local sample from %1.")
                .arg(snapshot.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
            : QStringLiteral("\n\nThe latest sample is %1 seconds old, so treat it as context rather than a live reading.")
                .arg(std::max<qint64>(0, ageMilliseconds / 1000));
    } else {
        answer += QStringLiteral("\n\nAusyn has not received a live system sample yet.");
    }
    if (!nextStep.trimmed().isEmpty())
        answer += QStringLiteral("\n\nA reasonable next step: %1").arg(nextStep);
    answer += QStringLiteral("\n\nThis is a measured heads-up, not proof of a root cause. You stay in control of any action.");
    return answer;
}

} // namespace Ausyn
