#include "troubleshooting_page.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QString percent(const std::optional<double>& value)
{
    return value ? QStringLiteral("%1%").arg(*value, 0, 'f', 1) : QStringLiteral("Unavailable");
}

QString findingSeverity(FindingSeverity severity)
{
    if (severity == FindingSeverity::Critical) return QStringLiteral("Critical");
    if (severity == FindingSeverity::Warning) return QStringLiteral("Warning");
    return QStringLiteral("Information");
}

QString rate(const std::optional<double>& bytesPerSecond)
{
    if (!bytesPerSecond) return QStringLiteral("Unavailable");
    if (*bytesPerSecond >= 1'000'000.0)
        return QStringLiteral("%1 MB/s").arg(*bytesPerSecond / 1'000'000.0, 0, 'f', 1);
    if (*bytesPerSecond >= 1'000.0)
        return QStringLiteral("%1 KB/s").arg(*bytesPerSecond / 1'000.0, 0, 'f', 0);
    return QStringLiteral("%1 B/s").arg(*bytesPerSecond, 0, 'f', 0);
}

} // namespace

TroubleshootingPage::TroubleshootingPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(13);
    auto* eyebrow = new QLabel(QStringLiteral("AUSYN GUIDED CHECKS"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Troubleshoot with evidence"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("Choose what feels wrong. Ausyn will line up the readings it has, explain what those readings can and cannot tell you, and offer safe checks you control."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* controls = new QHBoxLayout;
    auto* prompt = new QLabel(QStringLiteral("What are you noticing?"), this);
    prompt->setObjectName(QStringLiteral("metricName"));
    symptom_ = new QComboBox(this);
    symptom_->addItem(QStringLiteral("My PC feels slow"), 0);
    symptom_->addItem(QStringLiteral("The PC feels hot or fans are loud"), 1);
    symptom_->addItem(QStringLiteral("Memory use is high"), 2);
    symptom_->addItem(QStringLiteral("A drive is running out of space"), 3);
    symptom_->addItem(QStringLiteral("Battery drains faster than expected"), 4);
    symptom_->addItem(QStringLiteral("The network feels slow"), 5);
    symptom_->addItem(QStringLiteral("An app or game is crashing"), 6);
    symptom_->addItem(QStringLiteral("I want to prepare for a game"), 7);
    controls->addWidget(prompt);
    controls->addWidget(symptom_, 1);
    outer->addLayout(controls);

    auto* statePanel = panel(this);
    auto* stateLayout = new QVBoxLayout(statePanel);
    stateLayout->setContentsMargins(16, 14, 16, 14);
    stateLayout->setSpacing(6);
    status_ = new QLabel(QStringLiteral("Waiting for the first system sample…"), statePanel);
    status_->setObjectName(QStringLiteral("panelTitle"));
    summary_ = new QLabel(QStringLiteral("Ausyn will compare the selected symptom with its supported local readings."), statePanel);
    summary_->setObjectName(QStringLiteral("heroBody"));
    summary_->setWordWrap(true);
    stateLayout->addWidget(status_);
    stateLayout->addWidget(summary_);
    outer->addWidget(statePanel);

    auto* middle = new QHBoxLayout;
    middle->setSpacing(12);
    auto* evidencePanel = panel(this);
    auto* evidenceLayout = new QVBoxLayout(evidencePanel);
    evidenceLayout->setContentsMargins(14, 13, 14, 13);
    evidenceLayout->setSpacing(8);
    auto* evidenceTitle = new QLabel(QStringLiteral("What Ausyn can observe"), evidencePanel);
    evidenceTitle->setObjectName(QStringLiteral("panelTitle"));
    evidence_ = new QTableWidget(evidencePanel);
    evidence_->setObjectName(QStringLiteral("processTable"));
    evidence_->setColumnCount(3);
    evidence_->setHorizontalHeaderLabels({QStringLiteral("Signal"), QStringLiteral("Current evidence"), QStringLiteral("What it means")});
    evidence_->setAlternatingRowColors(true);
    evidence_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    evidence_->setSelectionMode(QAbstractItemView::NoSelection);
    evidence_->verticalHeader()->hide();
    evidence_->setShowGrid(false);
    evidence_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    evidence_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    evidence_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    evidenceLayout->addWidget(evidenceTitle);
    evidenceLayout->addWidget(evidence_, 1);

    auto* stepsPanel = panel(this);
    auto* stepsContainer = new QVBoxLayout(stepsPanel);
    stepsContainer->setContentsMargins(14, 13, 14, 13);
    stepsContainer->setSpacing(8);
    auto* stepsTitle = new QLabel(QStringLiteral("Safe checks you control"), stepsPanel);
    stepsTitle->setObjectName(QStringLiteral("panelTitle"));
    progress_ = new QLabel(QStringLiteral("0 steps marked done"), stepsPanel);
    progress_->setObjectName(QStringLiteral("subtle"));
    auto* stepsScroll = new QScrollArea(stepsPanel);
    stepsScroll->setWidgetResizable(true);
    stepsScroll->setFrameShape(QFrame::NoFrame);
    auto* stepContent = new QWidget(stepsScroll);
    stepsLayout_ = new QVBoxLayout(stepContent);
    stepsLayout_->setContentsMargins(0, 0, 0, 0);
    stepsLayout_->setSpacing(7);
    stepsScroll->setWidget(stepContent);
    stepsContainer->addWidget(stepsTitle);
    stepsContainer->addWidget(progress_);
    stepsContainer->addWidget(stepsScroll, 1);
    middle->addWidget(evidencePanel, 6);
    middle->addWidget(stepsPanel, 5);
    outer->addLayout(middle, 1);

    auto* footer = panel(this);
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(14, 11, 14, 11);
    limitations_ = new QLabel(QStringLiteral("Ausyn reports evidence and safe checks. It does not change settings, close apps, or diagnose hardware failure."), footer);
    limitations_->setObjectName(QStringLiteral("subtle"));
    limitations_->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    auto* review = new QPushButton(QStringLiteral("Open related page"), footer);
    review->setObjectName(QStringLiteral("secondaryButton"));
    auto* explain = new QPushButton(QStringLiteral("Ask Ausyn to explain"), footer);
    explain->setObjectName(QStringLiteral("primaryButton"));
    auto* copy = new QPushButton(QStringLiteral("Copy review summary"), footer);
    copy->setObjectName(QStringLiteral("secondaryButton"));
    buttons->addWidget(review);
    buttons->addWidget(explain);
    buttons->addStretch();
    buttons->addWidget(copy);
    footerLayout->addWidget(limitations_);
    footerLayout->addLayout(buttons);
    outer->addWidget(footer);

    connect(symptom_, &QComboBox::currentIndexChanged, this, &TroubleshootingPage::render);
    connect(review, &QPushButton::clicked, this, [this] {
        const int symptom = symptom_->currentData().toInt();
        emit reviewRequested(relatedPageFor(symptom), symptom_->currentText());
    });
    connect(explain, &QPushButton::clicked, this, [this] {
        const int symptom = symptom_->currentData().toInt();
        QStringList evidenceLines;
        const auto rows = evidenceFor(symptom);
        for (const EvidenceRow& row : rows)
            evidenceLines << QStringLiteral("%1: %2").arg(row.signal, row.observation);
        emit askAboutRequested(QStringLiteral("Guided troubleshooting"), titleFor(symptom),
            summary_->text(), evidenceLines.join(QStringLiteral("; ")),
            stepsFor(symptom).isEmpty() ? QString{} : stepsFor(symptom).first());
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const int symptom = symptom_->currentData().toInt();
        QStringList lines{QStringLiteral("Ausyn guided review · %1").arg(titleFor(symptom)), summary_->text(),
                          QStringLiteral("Sample status: %1").arg(status_->text())};
        for (const EvidenceRow& row : evidenceFor(symptom))
            lines << QStringLiteral("%1: %2 — %3").arg(row.signal, row.observation, row.interpretation);
        lines << QStringLiteral("Limits: %1").arg(limitsFor(symptom));
        QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        status_->setToolTip(QStringLiteral("A review summary was copied to the clipboard at your request."));
    });
}

void TroubleshootingPage::setContext(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis,
                                    const EventLogUpdate& eventLogs)
{
    snapshot_ = snapshot;
    analysis_ = analysis;
    eventLogs_ = eventLogs;
    render();
}

QString TroubleshootingPage::titleFor(int symptom) const
{
    switch (symptom) {
    case 0: return QStringLiteral("Performance feels slow");
    case 1: return QStringLiteral("Heat or loud fans");
    case 2: return QStringLiteral("High memory use");
    case 3: return QStringLiteral("Low drive space");
    case 4: return QStringLiteral("Battery drains quickly");
    case 5: return QStringLiteral("Network feels slow");
    case 6: return QStringLiteral("App or game crashes");
    case 7: return QStringLiteral("Prepare for a game");
    default: return QStringLiteral("Guided PC check");
    }
}

QString TroubleshootingPage::summaryFor(int symptom) const
{
    const qint64 ageMilliseconds = snapshot_.capturedAt.isValid()
        ? snapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 freshLimit = std::max<qint64>(10, std::clamp(snapshot_.samplingIntervalSeconds, 1, 10) * 3);
    const bool fresh = ageMilliseconds >= 0 && ageMilliseconds <= freshLimit * 1000;
    switch (symptom) {
    case 0:
        if (!fresh) return QStringLiteral("Ausyn’s latest reading is not fresh enough for a live performance assessment. Review the stored evidence as context and wait for monitoring to recover before making a current diagnosis.");
        return QStringLiteral("Ausyn compares repeated CPU and memory pressure with current disk and graphics activity. A busy process or a single high reading can be context, but neither proves a cause.");
    case 1:
        return QStringLiteral("Ausyn checks thermal-zone readings Windows exposes and whether a sensor reports sustained time above its passive trip point. Many laptops do not expose CPU package temperature or fan RPM through these interfaces.");
    case 2:
        return QStringLiteral("Memory pressure is based on Windows-reported system memory use and repeated measurements. High use alone does not identify a leak or tell Ausyn which app is safe to close.");
    case 3:
        return QStringLiteral("Ausyn checks free capacity for each fixed local volume and can show a trend when enough daily history exists. It does not inspect files or identify what used the space.");
    case 4:
        return QStringLiteral("Charge and discharge estimates depend on Windows and battery firmware. Workload, brightness, radio use, and recalibration can change the actual runtime.");
    case 5:
        return QStringLiteral("Ausyn measures traffic rates and adapter link speeds. Traffic volume does not measure latency, Wi-Fi signal quality, packet loss, or your internet provider’s service.");
    case 6:
        return QStringLiteral("Ausyn can summarize readable recent Windows warning/error records, including some application crash reports. Event timing and faulting-module text do not prove the root cause.");
    case 7:
        return QStringLiteral("Ausyn can compare RAM, dedicated graphics memory, and free install-drive space with minimum requirements you enter. It cannot guarantee compatibility or predict frame rate.");
    }
    return QStringLiteral("Choose a symptom to see the related local measurements.");
}

QString TroubleshootingPage::limitsFor(int symptom) const
{
    switch (symptom) {
    case 0: return QStringLiteral("A slowdown can come from a workload, driver, application, storage latency, thermal behavior, or other causes Ausyn cannot observe. Disk throughput is not disk latency.");
    case 1: return QStringLiteral("Thermal-zone sensors may be unavailable or represent a board zone rather than a CPU/GPU core. Do not use this page as a temperature safety rating.");
    case 2: return QStringLiteral("The working set of a process is not the same as reclaimable memory. Ausyn never ends a process automatically.");
    case 3: return QStringLiteral("Free-space forecasts are conditional estimates, not a promise. Ausyn never deletes files.");
    case 4: return QStringLiteral("Battery runtime is an estimate, not a guarantee or hardware health diagnosis.");
    case 5: return QStringLiteral("Per-process bandwidth and internet quality are not measured by this page.");
    case 6: return QStringLiteral("Ausyn cannot infer a cause from a crash event alone and does not run repair commands.");
    case 7: return QStringLiteral("A minimum-requirements comparison is not an FPS estimate or compatibility guarantee.");
    }
    return QStringLiteral("Observations can be incomplete between samples.");
}

int TroubleshootingPage::relatedPageFor(int symptom) const
{
    switch (symptom) {
    case 0: case 2: return 2;
    case 1: return 3;
    case 3: case 7: return symptom == 7 ? 5 : 10;
    case 4: return 4;
    case 5: return 16;
    case 6: return 11;
    }
    return 6;
}

QVector<TroubleshootingPage::EvidenceRow> TroubleshootingPage::evidenceFor(int symptom) const
{
    QVector<EvidenceRow> rows;
    auto add = [&rows](QString signal, QString observation, QString interpretation) {
        rows.append({std::move(signal), std::move(observation), std::move(interpretation)});
    };
    auto addRelevantFindings = [&rows, this](const QStringList& ruleIds, bool includeVolumeRules = false) {
        for (const Finding& finding : analysis_.findings) {
            const bool matched = ruleIds.contains(finding.ruleId) ||
                (includeVolumeRules && finding.ruleId.startsWith(QStringLiteral("fixed-volume-low-space-")));
            if (!matched) continue;
            rows.append({findingSeverity(finding.severity) + QStringLiteral(" · ") + finding.title,
                finding.evidence, finding.summary + (finding.recommendation.isEmpty()
                    ? QString{} : QStringLiteral(" Suggested: ") + finding.recommendation)});
        }
    };
    switch (symptom) {
    case 0:
        add(QStringLiteral("Processor"), percent(snapshot_.processorUsagePercent), QStringLiteral("One current system-wide sample; repeated findings are stronger evidence."));
        add(QStringLiteral("Memory"), percent(snapshot_.memoryUsagePercent), QStringLiteral("System use; not an app-level attribution."));
        add(QStringLiteral("Disk transfer"), QStringLiteral("%1 read · %2 write")
            .arg(rate(snapshot_.diskActivity.readBytesPerSecond), rate(snapshot_.diskActivity.writeBytesPerSecond)),
            QStringLiteral("Aggregate throughput is not latency or a per-drive measure."));
        addRelevantFindings({QStringLiteral("processor-sustained-load"), QStringLiteral("memory-sustained-pressure"),
            QStringLiteral("concurrent-cpu-memory-pressure"), QStringLiteral("personal-baseline-resource-shift"),
            QStringLiteral("thermal-zone-sustained-passive-point")});
        break;
    case 1:
        if (snapshot_.thermalSensors.isEmpty())
            add(QStringLiteral("Thermal zones"), QStringLiteral("Unavailable from supported Windows sensors"), QStringLiteral("This does not mean the computer is cool."));
        for (const ThermalSensorSample& sensor : snapshot_.thermalSensors) {
            add(QStringLiteral("%1 temperature").arg(sensor.name), QStringLiteral("%1 °C").arg(sensor.temperatureCelsius, 0, 'f', 1),
                sensor.passiveTripPointCelsius
                    ? QStringLiteral("Passive trip point %1 °C; sensor meaning depends on firmware.").arg(*sensor.passiveTripPointCelsius, 0, 'f', 1)
                    : QStringLiteral("No passive trip point was exposed by this sensor."));
        }
        if (snapshot_.fans.isEmpty())
            add(QStringLiteral("Fan telemetry"), QStringLiteral("Unavailable"), QStringLiteral("Windows did not expose a usable fan speed sample."));
        addRelevantFindings({QStringLiteral("thermal-zone-sustained-passive-point"), QStringLiteral("thermal-zone-high-temperature")});
        break;
    case 2:
        add(QStringLiteral("System memory"), percent(snapshot_.memoryUsagePercent),
            QStringLiteral("Windows memory pressure is system-wide; it cannot identify safe processes to close."));
        add(QStringLiteral("Used / total"), snapshot_.memoryTotalBytes
            ? QStringLiteral("%1 / %2 GB").arg(snapshot_.memoryUsedBytes / 1'000'000'000.0, 0, 'f', 1)
                .arg(snapshot_.memoryTotalBytes / 1'000'000'000.0, 0, 'f', 1)
            : QStringLiteral("Unavailable"), QStringLiteral("Physical memory reported by Windows."));
        if (analysis_.memoryForecast.hasEstimate)
            add(QStringLiteral("Trend"), QStringLiteral("Could reach 90% in about %1 minute(s)").arg(static_cast<int>(std::lround(analysis_.memoryForecast.minutesUntil90Percent))),
                QStringLiteral("Conditional projection of recent average use; not a leak diagnosis."));
        addRelevantFindings({QStringLiteral("memory-sustained-pressure"), QStringLiteral("concurrent-cpu-memory-pressure"), QStringLiteral("memory-rapid-rise")});
        break;
    case 3:
        if (snapshot_.volumes.isEmpty())
            add(QStringLiteral("Fixed local volumes"), QStringLiteral("Unavailable"), QStringLiteral("Windows did not provide current fixed-volume capacity."));
        for (const VolumeSample& volume : snapshot_.volumes) {
            const double freePercent = volume.totalBytes
                ? 100.0 * static_cast<double>(std::min(volume.freeBytes, volume.totalBytes)) / static_cast<double>(volume.totalBytes) : 0.0;
            add(volume.label.isEmpty() ? volume.rootPath : QStringLiteral("%1 · %2").arg(volume.label, volume.rootPath),
                volume.totalBytes ? QStringLiteral("%1 GB free · %2%")
                    .arg(static_cast<double>(volume.freeBytes) / 1'000'000'000.0, 0, 'f', 1).arg(freePercent, 0, 'f', 1)
                    : QStringLiteral("Capacity unavailable"),
                QStringLiteral("Current capacity; Ausyn does not inspect files."));
        }
        addRelevantFindings({QStringLiteral("system-drive-low-space")}, true);
        break;
    case 4:
        add(QStringLiteral("Charge"), snapshot_.batteryPercent ? QStringLiteral("%1%").arg(*snapshot_.batteryPercent) : QStringLiteral("No battery reported"),
            snapshot_.batteryOnAcPower.value_or(false) ? QStringLiteral("Connected to AC power.") : QStringLiteral("Windows power-state report."));
        add(QStringLiteral("Discharge rate"), snapshot_.batteryRateMilliwatts
            ? QStringLiteral("%1 W").arg(std::abs(static_cast<double>(*snapshot_.batteryRateMilliwatts)) / 1000.0, 0, 'f', 1)
            : QStringLiteral("Unavailable"), QStringLiteral("Firmware-reported rate can vary with workload."));
        if (analysis_.batteryForecast.hasEstimate)
            add(QStringLiteral("Recent trend"), QStringLiteral("15% in about %1 minute(s)").arg(static_cast<int>(std::lround(analysis_.batteryForecast.minutesUntil15Percent))),
                QStringLiteral("Conditional estimate; actual runtime varies."));
        addRelevantFindings({QStringLiteral("battery-low-charge"), QStringLiteral("battery-capacity-estimate-decline")});
        break;
    case 5:
        add(QStringLiteral("Receive rate"), rate(snapshot_.networkReceiveBytesPerSecond), QStringLiteral("Aggregate adapter traffic, not speed or signal quality."));
        add(QStringLiteral("Send rate"), rate(snapshot_.networkSendBytesPerSecond), QStringLiteral("Aggregate adapter traffic, not per-app bandwidth."));
        add(QStringLiteral("Active adapters"), QString::number(snapshot_.activeNetworkAdapters.size()), QStringLiteral("Windows-reported adapter count; link speed does not show internet quality."));
        break;
    case 6: {
        const qint64 scanAgeMilliseconds = eventLogs_.checkedAt.isValid()
            ? eventLogs_.checkedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        add(QStringLiteral("Windows event scan"), !eventLogs_.available ? QStringLiteral("Unavailable")
            : (scanAgeMilliseconds < 0 || scanAgeMilliseconds > 30LL * 60 * 1000
                ? QStringLiteral("Stale") : QStringLiteral("Updated %1").arg(eventLogs_.checkedAt.toLocalTime().toString(QStringLiteral("h:mm ap")))),
            QStringLiteral("Recent local System/Application event groups; coverage may be partial."));
        int crashCount = 0;
        for (const EventInsight& event : eventLogs_.insights) {
            if (!event.likelyApplicationCrash) continue;
            ++crashCount;
            add(QStringLiteral("Crash report · %1").arg(event.provider),
                QStringLiteral("Event %1 · %2 occurrence(s)").arg(event.eventId).arg(event.occurrenceCount),
                event.explanation);
            if (crashCount >= 4) break;
        }
        if (crashCount == 0)
            add(QStringLiteral("Application crash reports"), QStringLiteral("No readable crash report in this scan"), QStringLiteral("A clean scan does not prove the app had no issue."));
        break;
    }
    case 7:
        add(QStringLiteral("Memory"), snapshot_.memoryTotalBytes
            ? QStringLiteral("%1 GB total").arg(snapshot_.memoryTotalBytes / 1'000'000'000.0, 0, 'f', 1)
            : QStringLiteral("Unavailable"), QStringLiteral("Compare with the game's stated minimum and recommended requirements."));
        add(QStringLiteral("Dedicated graphics memory"), snapshot_.graphicsMemoryBytes
            ? QStringLiteral("%1 GB").arg(snapshot_.graphicsMemoryBytes / 1'000'000'000.0, 0, 'f', 1)
            : QStringLiteral("Unavailable"), QStringLiteral("Shared graphics memory is not the same as dedicated VRAM."));
        add(QStringLiteral("Install-drive space"), snapshot_.systemVolumeTotalBytes
            ? QStringLiteral("%1 GB free on %2").arg(snapshot_.systemVolumeFreeBytes / 1'000'000'000.0, 0, 'f', 1).arg(snapshot_.systemVolumePath)
            : QStringLiteral("Check the target volume in Hardware"), QStringLiteral("Only the system drive is shown here; choose the actual install drive."));
        break;
    }
    if (rows.isEmpty())
        add(QStringLiteral("Ausyn findings"), QStringLiteral("No related sustained finding right now"), QStringLiteral("A short symptom can happen between Ausyn samples."));
    return rows;
}

QStringList TroubleshootingPage::stepsFor(int symptom) const
{
    switch (symptom) {
    case 0: return {QStringLiteral("Wait for several fresh samples; check whether the pressure stays elevated."),
        QStringLiteral("Open Performance and review the current CPU, memory, and process readings."),
        QStringLiteral("Save work, then close only apps you recognize and no longer need."),
        QStringLiteral("Compare readings again after a minute; a change is an observation, not proof of cause.")};
    case 1: return {QStringLiteral("Place the laptop on a hard, clear surface and check that vents are unobstructed."),
        QStringLiteral("Review Hardware for any Windows-exposed thermal-zone readings."),
        QStringLiteral("If temperature or fan behavior seems unsafe, save work and follow the device maker’s guidance."),
        QStringLiteral("Do not open the device or alter firmware settings based on an Ausyn reading.")};
    case 2: return {QStringLiteral("Save active work before changing anything."),
        QStringLiteral("Open Performance and review current working sets for apps you recognize."),
        QStringLiteral("Close only known apps you are finished using; avoid ending unfamiliar system processes."),
        QStringLiteral("Watch whether system pressure changes over several fresh samples.")};
    case 3: return {QStringLiteral("Confirm which drive is low; install and download locations may differ."),
        QStringLiteral("Review Storage settings or File Explorer for files you recognize."),
        QStringLiteral("Back up anything important before deleting or moving files."),
        QStringLiteral("Never remove unfamiliar Windows or application files just to free space.")};
    case 4: return {QStringLiteral("Connect power if you need to preserve current work."),
        QStringLiteral("Review Battery & power for charge and recent-rate estimates."),
        QStringLiteral("Check Windows brightness, radios, and running workloads if you choose."),
        QStringLiteral("Compare over a similar workload; battery estimates vary with use.")};
    case 5: return {QStringLiteral("Check whether the issue affects one app or all network activity."),
        QStringLiteral("Compare Ausyn’s traffic rates while the issue is happening."),
        QStringLiteral("Check Wi-Fi signal and router status using Windows and your network equipment."),
        QStringLiteral("Avoid sharing credentials or running unknown network-repair scripts.")};
    case 6: return {QStringLiteral("Save work and note the app, time, and what you were doing."),
        QStringLiteral("Review Event intelligence for a matching timestamp and formatted Windows message."),
        QStringLiteral("Check the app publisher’s supported updates and release notes."),
        QStringLiteral("Do not delete logs or install a driver from an unverified source.")};
    case 7: return {QStringLiteral("Enter the game’s minimum and recommended RAM, VRAM, and storage requirements."),
        QStringLiteral("Select the volume where you actually plan to install the game."),
        QStringLiteral("Review each Met, Not met, or Unknown result in Gaming readiness."),
        QStringLiteral("Check the publisher’s supported OS and GPU requirements; Ausyn cannot estimate FPS.")};
    }
    return {};
}

void TroubleshootingPage::render()
{
    const int symptom = symptom_->currentData().toInt();
    const qint64 ageMilliseconds = snapshot_.capturedAt.isValid()
        ? snapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 freshness = std::max<qint64>(10, std::clamp(snapshot_.samplingIntervalSeconds, 1, 10) * 3);
    const bool fresh = ageMilliseconds >= 0 && ageMilliseconds <= freshness * 1000;
    status_->setText(!snapshot_.capturedAt.isValid()
        ? QStringLiteral("Waiting for monitoring")
        : (fresh ? QStringLiteral("Live evidence · sample %1").arg(snapshot_.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
                 : ageMilliseconds < 0 ? QStringLiteral("Sample timestamp is ahead of this PC’s clock · not live")
                 : QStringLiteral("Last sample %1 seconds ago · stale").arg(ageMilliseconds / 1000)));
    summary_->setText(summaryFor(symptom));
    limitations_->setText(limitsFor(symptom));

    const QVector<EvidenceRow> rows = evidenceFor(symptom);
    bool evidenceChanged = rows.size() != renderedEvidence_.size();
    if (!evidenceChanged) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows.at(i).signal != renderedEvidence_.at(i).signal ||
                rows.at(i).observation != renderedEvidence_.at(i).observation ||
                rows.at(i).interpretation != renderedEvidence_.at(i).interpretation) {
                evidenceChanged = true;
                break;
            }
        }
    }
    if (evidenceChanged) {
        evidence_->setRowCount(rows.size());
        for (qsizetype row = 0; row < rows.size(); ++row) {
            const QStringList columns{rows.at(row).signal, rows.at(row).observation, rows.at(row).interpretation};
            for (int column = 0; column < columns.size(); ++column) {
                auto* item = new QTableWidgetItem(columns.at(column));
                item->setToolTip(columns.at(column));
                evidence_->setItem(static_cast<int>(row), column, item);
            }
        }
        renderedEvidence_ = rows;
    }

    const QStringList steps = stepsFor(symptom);
    int complete = 0;
    for (qsizetype index = 0; index < steps.size(); ++index) {
        if (completedSteps_.contains(QStringLiteral("%1:%2").arg(symptom).arg(index))) ++complete;
    }
    if (renderedStepsSymptom_ != symptom) {
        while (QLayoutItem* item = stepsLayout_->takeAt(0)) {
            if (QWidget* widget = item->widget()) widget->deleteLater();
            delete item;
        }
        for (qsizetype index = 0; index < steps.size(); ++index) {
            const QString key = QStringLiteral("%1:%2").arg(symptom).arg(index);
            auto* stepRow = new QWidget(this);
            auto* stepRowLayout = new QHBoxLayout(stepRow);
            stepRowLayout->setContentsMargins(0, 2, 0, 2);
            stepRowLayout->setSpacing(8);
            auto* check = new QCheckBox(stepRow);
            check->setChecked(completedSteps_.contains(key));
            auto* stepText = new QLabel(steps.at(index), stepRow);
            stepText->setObjectName(QStringLiteral("subtle"));
            stepText->setWordWrap(true);
            stepRowLayout->addWidget(check, 0, Qt::AlignTop);
            stepRowLayout->addWidget(stepText, 1);
            connect(check, &QCheckBox::toggled, this, [this, key, steps, symptom](bool checked) {
                if (checked) completedSteps_.insert(key);
                else completedSteps_.remove(key);
                int done = 0;
                for (qsizetype i = 0; i < steps.size(); ++i)
                    if (completedSteps_.contains(QStringLiteral("%1:%2").arg(symptom).arg(i))) ++done;
                progress_->setText(QStringLiteral("%1 of %2 steps marked done · session only").arg(done).arg(steps.size()));
            });
            stepsLayout_->addWidget(stepRow);
        }
        stepsLayout_->addStretch(1);
        renderedStepsSymptom_ = symptom;
    }
    progress_->setText(QStringLiteral("%1 of %2 steps marked done · session only").arg(complete).arg(steps.size()));
}

} // namespace Ausyn
