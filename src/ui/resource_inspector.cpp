#include "resource_inspector.h"
#include "page_hub.h"
#include "live_visuals.h"
#include <QDir>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace Ausyn {
namespace {
QString gb(quint64 bytes) { return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / 1e9, 0, 'f', 1); }
QString percent(std::optional<double> value) { return value && std::isfinite(*value) ? QStringLiteral("%1%").arg(*value, 0, 'f', 1) : QStringLiteral("Unavailable"); }
QString rate(std::optional<double> value) { return value && std::isfinite(*value) ? QStringLiteral("%1 MB/s").arg(*value / 1e6, 0, 'f', 3) : QStringLiteral("Unavailable"); }
QString health(std::optional<unsigned int> value) {
    if (!value) return QStringLiteral("Not reported");
    switch (*value) {
    case 0: return QStringLiteral("Healthy");
    case 1: return QStringLiteral("Warning");
    case 2: return QStringLiteral("Unhealthy");
    default: return QStringLiteral("Unknown (%1)").arg(*value);
    }
}
void cell(QTableWidget* table, int row, int column, const QString& text) {
    auto* item = table->item(row, column);
    if (!item) { item = new QTableWidgetItem; table->setItem(row, column, item); }
    if (item->text() != text) item->setText(text);
    item->setToolTip(text);
}
QTableWidget* table(QWidget* parent, const QStringList& names) {
    auto* result = new QTableWidget(0, static_cast<int>(names.size()), parent);
    result->setHorizontalHeaderLabels(names); result->verticalHeader()->hide();
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setSelectionBehavior(QAbstractItemView::SelectRows); result->setShowGrid(false);
    result->setAlternatingRowColors(true); result->setWordWrap(true);
    result->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < names.size(); ++i) result->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    return result;
}
}
QString resourceTitle(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Cpu: return QStringLiteral("Processor");
    case ResourceKind::Memory: return QStringLiteral("Memory");
    case ResourceKind::Graphics: return QStringLiteral("Graphics");
    case ResourceKind::Storage: return QStringLiteral("Storage");
    case ResourceKind::Battery: return QStringLiteral("Battery");
    case ResourceKind::Network: return QStringLiteral("Network");
    case ResourceKind::Processes: return QStringLiteral("Running apps");
    }
    return QString();
}
ResourceCard::ResourceCard(ResourceKind kind, QWidget* parent) : QFrame(parent), kind_(kind) {
    setObjectName(QStringLiteral("panel")); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
    setAccessibleName(QStringLiteral("Open %1 details in a separate window").arg(resourceTitle(kind)));
    setToolTip(accessibleName()); setProperty("resourceKind", static_cast<int>(kind));
}
void ResourceCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) { emit activated(static_cast<int>(kind_)); event->accept(); }
    else QFrame::mouseReleaseEvent(event);
}
void ResourceCard::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) { emit activated(static_cast<int>(kind_)); event->accept(); }
    else QFrame::keyPressEvent(event);
}
void ResourceCard::paintEvent(QPaintEvent* event) {
    QFrame::paintEvent(event);
    if (hasFocus()) { QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor(QStringLiteral("#9690ff")), 2)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(rect().adjusted(2,2,-2,-2), 12,12); }
}
ResourceInspector::ResourceInspector(ResourceKind kind, QWidget* parent) : FramelessWindow(parent), kind_(kind) {
    setObjectName(QStringLiteral("resourceInspector"));
    setWindowTitle(QStringLiteral("Ausyn · %1").arg(resourceTitle(kind)));
    const QRect available = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0,0,1280,720);
    setMinimumSize(std::min(620, available.width() - 32), std::min(460, available.height() - 48));
    resize(QSize(980, 760).boundedTo(available.size() - QSize(48, 64)));
    auto* root = new QWidget(this); root->setObjectName(QStringLiteral("appRoot"));
    auto* outer = new QVBoxLayout(root); outer->setContentsMargins(1,1,1,1); outer->setSpacing(0);
    auto* title = new WindowTitleBar(this, root); title->setObjectName(QStringLiteral("topBar"));
    auto* caption = new QLabel(QStringLiteral("AUSYN  /  %1").arg(resourceTitle(kind).toUpper()), title);
    caption->setObjectName(QStringLiteral("panelTitle"));
    title->contentLayout()->addWidget(caption, 1); title->contentLayout()->addWidget(new WindowControls(this, title));
    outer->addWidget(title);
    auto* scroll = new QScrollArea(root); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll); content->setObjectName(QStringLiteral("inspectorBody")); content->setAutoFillBackground(false);
    auto* column = new QVBoxLayout(content); column->setContentsMargins(24,20,24,24); column->setSpacing(14);
    auto* top = new QHBoxLayout;
    auto* heading = new QLabel(resourceTitle(kind), content); heading->setObjectName(QStringLiteral("hubTitle"));
    details_ = new DetailSwitch(content); details_->setChecked(true);
    top->addWidget(heading, 1); top->addWidget(details_); column->addLayout(top);
    value_ = new QLabel(content); value_->setObjectName(QStringLiteral("inspectorValue")); value_->setWordWrap(true);
    summary_ = new QLabel(content); summary_->setObjectName(QStringLiteral("subtle")); summary_->setWordWrap(true);
    advice_ = new QLabel(content); advice_->setObjectName(QStringLiteral("inspectorAdvice")); advice_->setWordWrap(true);
    source_ = new QLabel(content); source_->setObjectName(QStringLiteral("subtle")); source_->setWordWrap(true);
    source_->setObjectName(QStringLiteral("inspectorFreshness"));
    column->addWidget(value_); column->addWidget(summary_);
    level_ = new QProgressBar(content); level_->setRange(0,100); level_->setTextVisible(false); level_->setFixedHeight(9); column->addWidget(level_);
    level_->setObjectName(QStringLiteral("inspectorLevel"));
    column->addWidget(advice_); column->addWidget(source_);
    chart_ = new LiveActivityChart(content);
    chart_->setMinimumHeight(160);
    chart_->setSeries(kind == ResourceKind::Cpu ? 0 : kind == ResourceKind::Memory ? 1 : 2);
    chart_->setVisible(kind == ResourceKind::Cpu || kind == ResourceKind::Memory || kind == ResourceKind::Graphics);
    column->addWidget(chart_);
    facts_ = table(content, {QStringLiteral("Reading"), QStringLiteral("Windows evidence")});
    facts_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    facts_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    column->addWidget(facts_);
    filter_ = new QLineEdit(content); filter_->setPlaceholderText(QStringLiteral("Filter current apps…"));
    filter_->setVisible(kind == ResourceKind::Cpu || kind == ResourceKind::Memory || kind == ResourceKind::Processes);
    column->addWidget(filter_);
    processes_ = table(content, {QStringLiteral("App"), QStringLiteral("CPU"), QStringLiteral("RAM working set"), QStringLiteral("PID")});
    processes_->setObjectName(QStringLiteral("inspectorProcesses")); processes_->setMinimumHeight(180); processes_->setMaximumHeight(450);
    processes_->setVisible(filter_->isVisibleTo(content)); column->addWidget(processes_);
    auto* actions = new QHBoxLayout;
    auto* history = new QPushButton(QStringLiteral("View saved history"), content); history->setObjectName(QStringLiteral("secondaryButton"));
    auto* taskManager = new QPushButton(QStringLiteral("Open Windows Task Manager"), content); taskManager->setObjectName(QStringLiteral("secondaryButton"));
    actions->addWidget(history); actions->addWidget(taskManager); actions->addStretch(); column->addLayout(actions); column->addStretch();
    connect(history, &QPushButton::clicked, this, [this] { emit toolRequested(8); });
    connect(taskManager, &QPushButton::clicked, this, [this] {
        const QString path = QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/Taskmgr.exe"));
        if (!QProcess::startDetached(path, QStringList{})) QMessageBox::information(this, QStringLiteral("Task Manager"), QStringLiteral("Windows could not open Task Manager."));
    });
    connect(details_, &QAbstractButton::toggled, this, [this] { render(); });
    connect(filter_, &QLineEdit::textChanged, this, [this] { render(); });
    scroll->setWidget(content); scroll->viewport()->setAutoFillBackground(false); outer->addWidget(scroll, 1); setCentralWidget(root);
}
void ResourceInspector::seedChart(const LiveActivityChart& source) { chart_->copyHistoryFrom(source); }
QString ResourceInspector::summaryText() const { return advice_->text(); }
void ResourceInspector::setContext(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis, bool monitoring, bool force) {
    const bool newReading = snapshot.capturedAt != snapshot_.capturedAt;
    snapshot_ = snapshot; analysis_ = analysis; monitoring_ = monitoring;
    if (newReading) chart_->addSnapshot(snapshot);
    if (!isVisible() && !force) return;
    if (!force && renderedAt_.isValid() && newReading && renderedAt_.msecsTo(snapshot.capturedAt) < 2000) return;
    renderedAt_ = snapshot.capturedAt; render();
}
void ResourceInspector::updateFreshness(bool monitoring) {
    monitoring_ = monitoring;
    const qint64 age = snapshot_.capturedAt.isValid() ? snapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const bool stale = age < 0 || age > std::max(10, snapshot_.samplingIntervalSeconds * 3) * 1000LL;
    source_->setText(!monitoring ? QStringLiteral("Monitoring paused · these are saved readings")
        : !snapshot_.capturedAt.isValid() ? QStringLiteral("Waiting for Windows readings")
        : QStringLiteral("%1 · captured %2 · %3")
            .arg(stale ? QStringLiteral("Earlier readings; wait for an update") : QStringLiteral("Live Windows evidence"),
                snapshot_.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")),
                QStringLiteral("process list captured %1").arg(snapshot_.processSamplesCapturedAt.isValid()
                    ? snapshot_.processSamplesCapturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")) : QStringLiteral("not available"))));
    if (property("usingFixture").toBool()) source_->setText(QStringLiteral("UI sample data · ") + source_->text());
}
void ResourceInspector::render() {
    const bool detailed = details_->isChecked();
    chart_->setDetailed(detailed);
    QVector<QPair<QString,QString>> facts;
    const auto add = [&facts](const QString& name, const QString& reading) { facts.append({name, reading}); };
    std::optional<double> usage;
    QString value, summary, advice;
    const auto& s = snapshot_;
    switch (kind_) {
    case ResourceKind::Cpu:
        usage = s.processorUsagePercent; value = percent(usage); summary = s.processorName;
        advice = usage.value_or(0) >= 75 ? QStringLiteral("Processor load is elevated in this reading. Review the busiest apps below. Video, games and builds can raise CPU use; compare several readings before identifying a cause.") : QStringLiteral("Use the chart to see whether a spike settles. The app list shows recent readings, rather than proof of what caused system load.");
        add(QStringLiteral("Logical processors"), s.logicalProcessorCount ? QString::number(s.logicalProcessorCount) : QStringLiteral("Not reported"));
        if (detailed) {
            for (const auto& core : s.processorCores) add(core.name, percent(core.utilizationPercent));
            add(QStringLiteral("Temperature sensor coverage"), s.thermalSensorStatus.isEmpty() ? QStringLiteral("Windows has not reported a temperature sensor") : s.thermalSensorStatus);
            for (const auto& sensor : s.thermalSensors) {
                add(sensor.name, QStringLiteral("%1 °C · %2").arg(sensor.temperatureCelsius, 0, 'f', 1).arg(sensor.source));
                if (sensor.passiveTripPointCelsius) add(QStringLiteral("Firmware passive trip point"), QStringLiteral("%1 °C").arg(*sensor.passiveTripPointCelsius, 0, 'f', 1));
            }
        }
        break;
    case ResourceKind::Memory:
        usage = s.memoryUsagePercent; value = percent(usage); summary = QStringLiteral("%1 in use · %2 available · %3 total").arg(gb(s.memoryUsedBytes), gb(s.memoryAvailableBytes), gb(s.memoryTotalBytes));
        advice = usage.value_or(0) >= 95 ? QStringLiteral("RAM headroom is very low. Save your work, then review unused tabs and apps below. Reducing a demanding video or game workload may help. Ausyn won’t close anything automatically.")
            : usage.value_or(0) >= 85 ? QStringLiteral("Memory use is elevated. Review the largest working sets below and compare the chart. Cache and shared pages mean these process values don’t add directly to total RAM use.")
            : QStringLiteral("Windows has memory available for new work. Process working sets include shared pages; their sum is not an exact total of system RAM.");
        add(QStringLiteral("Available for work"), gb(s.memoryAvailableBytes)); add(QStringLiteral("Physical memory in use"), gb(s.memoryUsedBytes));
        if (detailed) add(QStringLiteral("Reclaimable system cache"), s.memorySystemCacheBytes ? gb(*s.memorySystemCacheBytes) : QStringLiteral("Not reported by Windows"));
        if (!s.memoryUsagePercent || !s.memoryTotalBytes) { usage.reset(); value = QStringLiteral("Unavailable"); summary = QStringLiteral("Windows memory totals are not available"); advice = QStringLiteral("I can’t assess RAM headroom without a valid Windows memory reading."); }
        break;
    case ResourceKind::Graphics:
        usage = s.graphicsUsagePercent; value = percent(usage); summary = s.graphicsName;
        advice = QStringLiteral("This is activity on the busiest Windows graphics engine. It does not measure game frame rate. Dedicated VRAM and the driver’s current local-memory budget are different readings.");
        for (const auto& adapter : s.graphicsAdapters) {
            add(adapter.name, adapter.dedicatedMemoryBytes ? QStringLiteral("%1 dedicated VRAM").arg(gb(adapter.dedicatedMemoryBytes)) : QStringLiteral("Dedicated VRAM not reported"));
            if (detailed) add(QStringLiteral("Local use / driver budget"), QStringLiteral("%1 / %2").arg(adapter.localMemoryUsageBytes ? gb(*adapter.localMemoryUsageBytes) : QStringLiteral("Not reported"), adapter.localMemoryBudgetBytes ? gb(*adapter.localMemoryBudgetBytes) : QStringLiteral("Not reported")));
        }
        break;
    case ResourceKind::Storage:
        if (s.systemVolumeTotalBytes) usage = 100.0 * (1 - static_cast<double>(s.systemVolumeFreeBytes) / static_cast<double>(s.systemVolumeTotalBytes));
        value = QStringLiteral("%1 free on %2").arg(gb(s.systemVolumeFreeBytes), s.systemVolumePath); summary = QStringLiteral("System-drive space used: %1").arg(percent(usage));
        advice = s.systemVolumeTotalBytes && s.systemVolumeFreeBytes < s.systemVolumeTotalBytes / 10 ? QStringLiteral("This drive has little free space. Review large downloads and Windows Storage settings; back up important files before deleting anything.") : QStringLiteral("Capacity and transfer rate are separate signals. Ausyn reports Windows drive readings and does not inspect your files or infer disk latency.");
        for (const auto& volume : s.volumes) add(volume.rootPath + volume.label, QStringLiteral("%1 free of %2").arg(gb(volume.freeBytes), gb(volume.totalBytes)));
        add(QStringLiteral("Read / write throughput"), rate(s.diskActivity.readBytesPerSecond) + QStringLiteral(" / ") + rate(s.diskActivity.writeBytesPerSecond));
        if (detailed) for (const auto& disk : s.physicalDisks) {
            add(disk.model, QStringLiteral("Windows health: %1 · temperature: %2 · wear: %3")
                .arg(health(disk.windowsHealthStatus), disk.temperatureCelsius ? QStringLiteral("%1 °C").arg(*disk.temperatureCelsius) : QStringLiteral("Not reported"), disk.wearPercent ? QStringLiteral("%1%").arg(*disk.wearPercent) : QStringLiteral("Not reported")));
            if (disk.powerOnHours) add(QStringLiteral("Power-on hours"), QString::number(*disk.powerOnHours));
            if (disk.uncorrectedReadErrors || disk.uncorrectedWriteErrors) add(QStringLiteral("Uncorrected read / write errors"),
                QStringLiteral("%1 / %2").arg(disk.uncorrectedReadErrors ? QString::number(*disk.uncorrectedReadErrors) : QStringLiteral("Not reported"),
                disk.uncorrectedWriteErrors ? QString::number(*disk.uncorrectedWriteErrors) : QStringLiteral("Not reported")));
        }
        if (!s.systemVolumeTotalBytes) { value = QStringLiteral("Capacity unavailable"); summary = QStringLiteral("Windows has not reported system-drive totals"); }
        break;
    case ResourceKind::Battery:
        if (s.batteryPercent) usage = *s.batteryPercent;
        value = percent(usage); summary = s.batteryOnAcPower ? (*s.batteryOnAcPower ? QStringLiteral("Connected to power") : QStringLiteral("Running on battery")) : QStringLiteral("Power source not reported");
        advice = usage && *usage <= 20 && s.batteryOnAcPower && !*s.batteryOnAcPower ? QStringLiteral("Save your work and connect power soon. Remaining time depends on the workload.") : QStringLiteral("Capacity estimates vary with workload and Windows sensor support. Charge percentage alone does not establish battery condition.");
        add(QStringLiteral("Full-charge / design capacity"), QStringLiteral("%1 / %2").arg(s.batteryFullChargeCapacityMwh ? QStringLiteral("%1 mWh").arg(*s.batteryFullChargeCapacityMwh) : QStringLiteral("Not reported"), s.batteryDesignCapacityMwh ? QStringLiteral("%1 mWh").arg(*s.batteryDesignCapacityMwh) : QStringLiteral("Not reported")));
        if (detailed) add(QStringLiteral("Capacity ratio"), percent(s.batteryHealthPercent));
        break;
    case ResourceKind::Network:
        value = rate(s.networkReceiveBytesPerSecond); summary = QStringLiteral("Receiving · sending %1").arg(rate(s.networkSendBytesPerSecond));
        advice = QStringLiteral("This is traffic rate, rather than internet speed or connection quality. Low traffic during a video can mean the video has already buffered; Ausyn does not run a speed test.");
        for (const auto& adapter : s.activeNetworkAdapters) add(adapter.name, adapter.description);
        if (detailed) add(QStringLiteral("Windows network note"), s.networkNote.isEmpty() ? QStringLiteral("No extra driver note") : s.networkNote);
        break;
    case ResourceKind::Processes:
        value = QStringLiteral("%1 readable apps").arg(s.topProcesses.size()); summary = QStringLiteral("Search the current process sample below");
        advice = QStringLiteral("Review what is using resources before closing anything. Shared working sets can overlap, and short CPU activity does not prove an app caused a slowdown.");
        add(QStringLiteral("Collection status"), s.processCollectionStatus.isEmpty() ? QStringLiteral("Recent process sample") : s.processCollectionStatus);
        break;
    }
    if (detailed) {
        const Finding* mostRelevant = nullptr;
        for (const auto& finding : analysis_.findings) {
            const QString text = finding.ruleId + QLatin1Char(' ') + finding.title;
            const bool related = kind_ == ResourceKind::Memory ? text.contains(QStringLiteral("memory"), Qt::CaseInsensitive)
                : kind_ == ResourceKind::Cpu ? text.contains(QStringLiteral("cpu"), Qt::CaseInsensitive) || text.contains(QStringLiteral("thermal"), Qt::CaseInsensitive)
                : kind_ == ResourceKind::Storage ? text.contains(QStringLiteral("storage"), Qt::CaseInsensitive) || text.contains(QStringLiteral("disk"), Qt::CaseInsensitive)
                : kind_ == ResourceKind::Battery && text.contains(QStringLiteral("battery"), Qt::CaseInsensitive);
            if (related && (!mostRelevant || finding.severity > mostRelevant->severity)) mostRelevant = &finding;
        }
        if (mostRelevant) {
            add(QStringLiteral("Current monitored finding"), mostRelevant->title + QStringLiteral(" · ") + mostRelevant->summary);
            add(QStringLiteral("Evidence behind this finding"), mostRelevant->evidence);
            if (!mostRelevant->recommendation.isEmpty()) advice = mostRelevant->recommendation;
        }
    }
    if (!s.capturedAt.isValid()) { value = QStringLiteral("Waiting for readings"); advice = QStringLiteral("This window uses Ausyn’s existing monitor. It does not start another polling loop."); }
    value_->setText(value); summary_->setText(summary); advice_->setText(advice);
    level_->setVisible(usage.has_value()); if (usage) level_->setValue(static_cast<int>(std::clamp(*usage, 0.0, 100.0)));
    facts_->setUpdatesEnabled(false); facts_->setRowCount(static_cast<int>(facts.size()));
    for (int i = 0; i < facts.size(); ++i) { cell(facts_, i, 0, facts[i].first); cell(facts_, i, 1, facts[i].second); }
    facts_->resizeRowsToContents(); facts_->setFixedHeight(std::min(360, std::max(80, facts_->horizontalHeader()->height() + facts_->verticalHeader()->length() + 8))); facts_->setUpdatesEnabled(true);
    if (kind_ == ResourceKind::Cpu || kind_ == ResourceKind::Memory || kind_ == ResourceKind::Processes) {
        QVector<ProcessSample> rows;
        for (const auto& process : s.topProcesses) if (filter_->text().isEmpty() || process.name.contains(filter_->text(), Qt::CaseInsensitive)) rows.append(process);
        std::stable_sort(rows.begin(), rows.end(), [this](const ProcessSample& a, const ProcessSample& b) {
            return kind_ == ResourceKind::Cpu ? a.cpuPercent.value_or(-1) > b.cpuPercent.value_or(-1) : a.workingSetBytes.value_or(0) > b.workingSetBytes.value_or(0);
        });
        if (kind_ != ResourceKind::Processes) rows.resize(std::min<qsizetype>(rows.size(), detailed ? 15 : 5));
        processes_->setUpdatesEnabled(false); processes_->setRowCount(static_cast<int>(rows.size())); processes_->setColumnHidden(3, !detailed);
        for (int i = 0; i < rows.size(); ++i) { const auto& process = rows[i]; cell(processes_, i, 0, process.name); cell(processes_, i, 1, percent(process.cpuPercent)); cell(processes_, i, 2, process.workingSetBytes ? gb(*process.workingSetBytes) : QStringLiteral("Not reported")); cell(processes_, i, 3, QString::number(process.processId)); }
        processes_->setUpdatesEnabled(true);
    }
    updateFreshness(monitoring_);
}
}
