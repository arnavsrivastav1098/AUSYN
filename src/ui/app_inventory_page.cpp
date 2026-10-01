#include "app_inventory_page.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace Ausyn {
namespace {

struct StartupProcessMatches {
    QVector<const ProcessSample*> processes;
    bool exactPath = false;
};

QString startupExecutable(const QString& command)
{
    const QString trimmed = command.trimmed();
    QString executable;
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const qsizetype quoteEnd = trimmed.indexOf(QLatin1Char('"'), 1);
        if (quoteEnd > 1) executable = trimmed.mid(1, quoteEnd - 1);
    }
    if (executable.isEmpty()) {
        const qsizetype exeEnd = trimmed.indexOf(QStringLiteral(".exe"), 0, Qt::CaseInsensitive);
        if (exeEnd >= 0) executable = trimmed.left(exeEnd + 4).trimmed();
        else executable = trimmed.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
    }

    const std::wstring input(reinterpret_cast<const wchar_t*>(executable.utf16()));
    const DWORD required = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (required > 1) {
        std::wstring expanded(required, L'\0');
        const DWORD written = ExpandEnvironmentStringsW(input.c_str(), expanded.data(), required);
        if (written > 1 && written <= required) {
            expanded.resize(written - 1);
            executable = QString::fromWCharArray(expanded.data(), static_cast<qsizetype>(expanded.size()));
        }
    }
    return executable.trimmed();
}

StartupProcessMatches findStartupProcessMatches(const StartupEntry& entry,
                                                 const QVector<ProcessSample>& processes)
{
    StartupProcessMatches result;
    const QString executable = startupExecutable(entry.command);
    const QFileInfo executableInfo(executable);
    const QString expectedPath = executableInfo.isAbsolute()
        ? QDir::cleanPath(executableInfo.absoluteFilePath()).toCaseFolded() : QString{};
    const QString expectedName = executableInfo.fileName().isEmpty()
        ? entry.name.trimmed().toCaseFolded() : executableInfo.fileName().toCaseFolded();

    if (!expectedPath.isEmpty()) {
        for (const ProcessSample& process : processes) {
            if (process.executablePath.isEmpty()) continue;
            const QString processPath = QDir::cleanPath(
                QFileInfo(process.executablePath).absoluteFilePath()).toCaseFolded();
            if (processPath == expectedPath) result.processes.append(&process);
        }
        if (!result.processes.isEmpty()) {
            result.exactPath = true;
            return result;
        }
    }

    for (const ProcessSample& process : processes) {
        if (process.name.compare(expectedName, Qt::CaseInsensitive) == 0)
            result.processes.append(&process);
    }
    return result;
}

QString processUsageText(const ProcessSample& process)
{
    const QString cpu = process.cpuPercent
        ? QStringLiteral("%1% CPU").arg(*process.cpuPercent, 0, 'f', 1)
        : QStringLiteral("CPU sampling");
    const QString memory = process.workingSetBytes
        ? QStringLiteral("%1 MB RAM").arg(static_cast<double>(*process.workingSetBytes) / 1'000'000.0, 0, 'f', 0)
        : QStringLiteral("RAM unavailable");
    return QStringLiteral("%1 · PID %2 · %3 · %4")
        .arg(process.name, QString::number(process.processId), cpu, memory);
}

QString startupTrailKey(const StartupEntry& entry)
{
    return (entry.source + QLatin1Char('|') + entry.name + QLatin1Char('|') + entry.command).toCaseFolded();
}

bool hasHighCurrentResourceUse(const ProcessSample& process)
{
    constexpr quint64 kHighWorkingSetBytes = 512ULL * 1024ULL * 1024ULL;
    return (process.cpuPercent && *process.cpuPercent >= 15.0) ||
           (process.workingSetBytes && *process.workingSetBytes >= kHighWorkingSetBytes);
}

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QString csvField(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

void configureTable(QTableWidget* table, const QStringList& headers)
{
    table->setObjectName(QStringLiteral("processTable"));
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->setShowGrid(false);
}

} // namespace

AppInventoryPage::AppInventoryPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(15);

    auto* eyebrow = new QLabel(QStringLiteral("APPLICATION AWARENESS"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Startup & apps"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("See what is configured to start with Windows and review the software inventory Windows exposes."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* controls = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Search apps, publishers, commands, or sources…"));
    search_->setClearButtonEnabled(true);
    refreshButton_ = new QPushButton(QStringLiteral("Refresh inventory"), this);
    refreshButton_->setObjectName(QStringLiteral("secondaryButton"));
    exportButton_ = new QPushButton(QStringLiteral("Export CSV"), this);
    exportButton_->setObjectName(QStringLiteral("secondaryButton"));
    controls->addWidget(search_, 1);
    controls->addWidget(exportButton_);
    controls->addWidget(refreshButton_);
    outer->addLayout(controls);
    connect(search_, &QLineEdit::textChanged, this, &AppInventoryPage::applyFilter);
    connect(refreshButton_, &QPushButton::clicked, this, &AppInventoryPage::refreshRequested);
    connect(exportButton_, &QPushButton::clicked, this, &AppInventoryPage::exportCurrentList);

    auto* inventoryPanel = panel(this);
    auto* inventoryLayout = new QVBoxLayout(inventoryPanel);
    inventoryLayout->setContentsMargins(13, 13, 13, 13);
    inventoryLayout->setSpacing(9);
    status_ = new QLabel(QStringLiteral("Preparing read-only inventory…"), inventoryPanel);
    status_->setObjectName(QStringLiteral("subtle"));
    status_->setWordWrap(true);
    tabs_ = new QTabWidget(inventoryPanel);
    startupTable_ = new QTableWidget(tabs_);
    configureTable(startupTable_, {QStringLiteral("Application"), QStringLiteral("Source"),
        QStringLiteral("Registration"), QStringLiteral("Live match"), QStringLiteral("Current use"),
        QStringLiteral("Signal")});
    startupTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    startupTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    startupTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    startupTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    startupTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    startupTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    appsTable_ = new QTableWidget(tabs_);
    configureTable(appsTable_, {QStringLiteral("Application"), QStringLiteral("Publisher"), QStringLiteral("Version"), QStringLiteral("Source")});
    appsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    appsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    appsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    appsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tabs_->addTab(startupTable_, QStringLiteral("Startup items"));
    tabs_->addTab(appsTable_, QStringLiteral("Installed apps"));
    auto* softwareChangesTab = new QWidget(tabs_);
    auto* softwareChangesLayout = new QVBoxLayout(softwareChangesTab);
    softwareChangesLayout->setContentsMargins(8, 8, 8, 8);
    softwareChangesLayout->setSpacing(8);
    auto* softwareChangeNote = new QLabel(QStringLiteral("Ausyn records apps and versions it first observes during inventory refreshes. Detection time is not install time; missing entries are not treated as removals. Software names stay on this PC."), softwareChangesTab);
    softwareChangeNote->setObjectName(QStringLiteral("subtle"));
    softwareChangeNote->setWordWrap(true);
    softwareChanges_ = new QListWidget(softwareChangesTab);
    softwareChanges_->setAccessibleName(QStringLiteral("Recent software inventory changes"));
    softwareChanges_->setAccessibleDescription(QStringLiteral("Recent locally stored first-observed software and version changes, newest first."));
    clearSoftwareChanges_ = new QPushButton(QStringLiteral("Clear history & baseline"), softwareChangesTab);
    clearSoftwareChanges_->setObjectName(QStringLiteral("secondaryButton"));
    softwareChangesLayout->addWidget(softwareChangeNote);
    softwareChangesLayout->addWidget(softwareChanges_, 1);
    softwareChangesLayout->addWidget(clearSoftwareChanges_, 0, Qt::AlignRight);
    tabs_->addTab(softwareChangesTab, QStringLiteral("Recent changes"));
    connect(clearSoftwareChanges_, &QPushButton::clicked, this, &AppInventoryPage::clearSoftwareHistoryRequested);
    auto* actionRow = new QHBoxLayout;
    auto* openSettings = new QPushButton(QStringLiteral("Manage startup in Windows Settings"), inventoryPanel);
    openSettings->setObjectName(QStringLiteral("secondaryButton"));
    auto* actionNote = new QLabel(QStringLiteral("Ausyn only inspects. Changes stay under your control."), inventoryPanel);
    actionNote->setObjectName(QStringLiteral("subtle"));
    actionRow->addWidget(openSettings);
    actionRow->addWidget(actionNote, 1);
    connect(openSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:startupapps")));
    });
    details_ = new QTextBrowser(inventoryPanel);
    details_->setMaximumHeight(125);
    details_->setStyleSheet(QStringLiteral("QTextBrowser{background:#0e131d;border:1px solid #222e3e;border-radius:8px;color:#aeb9cb;padding:8px;}"));
    details_->setText(QStringLiteral("Select an entry to see its local source and available details."));
    inventoryLayout->addWidget(status_);
    inventoryLayout->addWidget(tabs_, 1);
    inventoryLayout->addLayout(actionRow);
    inventoryLayout->addWidget(details_);
    outer->addWidget(inventoryPanel, 1);
    connect(startupTable_, &QTableWidget::itemSelectionChanged, this, &AppInventoryPage::showStartupDetails);
    connect(appsTable_, &QTableWidget::itemSelectionChanged, this, &AppInventoryPage::showInstalledDetails);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        const bool inventoryTab = index < 2;
        search_->setEnabled(inventoryTab);
        exportButton_->setEnabled(inventoryTab);
        applyFilter();
    });
}

void AppInventoryPage::setBusy(bool busy)
{
    refreshButton_->setEnabled(!busy);
    refreshButton_->setText(busy ? QStringLiteral("Scanning…") : QStringLiteral("Refresh inventory"));
    if (busy && startupEntries_.isEmpty() && installedApps_.isEmpty())
        status_->setText(QStringLiteral("Reading startup registrations and installed-app metadata in the background…"));
}

void AppInventoryPage::exportCurrentList()
{
    if (tabs_->currentIndex() >= 2) return;
    const bool startup = tabs_->currentWidget() == startupTable_;
    const QString kind = startup ? QStringLiteral("startup-items") : QStringLiteral("installed-apps");
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export %1").arg(startup
        ? QStringLiteral("startup items") : QStringLiteral("installed apps")),
        QDir::home().filePath(QStringLiteral("Ausyn-%1.csv").arg(kind)),
        QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    QStringList lines;
    if (startup) {
        lines << QStringList{QStringLiteral("Application"), QStringLiteral("Source"),
            QStringLiteral("Command or file"), QStringLiteral("Details")}.join(QLatin1Char(','));
        for (const StartupEntry& entry : visibleStartup_)
            lines << QStringList{csvField(entry.name), csvField(entry.source),
                csvField(entry.command), csvField(entry.detail)}.join(QLatin1Char(','));
    } else {
        lines << QStringList{QStringLiteral("Application"), QStringLiteral("Publisher"),
            QStringLiteral("Version"), QStringLiteral("Install date"), QStringLiteral("Source")}.join(QLatin1Char(','));
        for (const InstalledAppEntry& app : visibleApps_)
            lines << QStringList{csvField(app.name), csvField(app.publisher), csvField(app.version),
                csvField(app.installDate), csvField(app.source)}.join(QLatin1Char(','));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        status_->setText(QStringLiteral("Could not create export: %1").arg(file.errorString()));
        return;
    }
    const QByteArray contents = QByteArray::fromHex("efbbbf") + lines.join(QStringLiteral("\r\n")).toUtf8() + QByteArrayLiteral("\r\n");
    if (file.write(contents) != contents.size() || !file.commit()) {
        status_->setText(QStringLiteral("Export failed: %1").arg(file.errorString()));
        return;
    }
    status_->setText(QStringLiteral("Exported %1 visible %2 to %3")
        .arg(startup ? visibleStartup_.size() : visibleApps_.size())
        .arg(startup ? QStringLiteral("startup items") : QStringLiteral("installed apps"), path));
}

void AppInventoryPage::setUpdate(const AppInventoryUpdate& update)
{
    startupEntries_ = update.startupEntries;
    installedApps_ = update.installedApps;
    consecutiveHighResourceSamples_.clear();
    lastStartupSampleAt_ = runningProcessesSampledAt_;
    status_->setText(QStringLiteral("Checked %1 · %2 startup entries · %3 installed apps\n%4")
        .arg(update.checkedAt.toString(QStringLiteral("h:mm:ss ap")))
        .arg(startupEntries_.size()).arg(installedApps_.size()).arg(update.status));
    applyFilter();
}

void AppInventoryPage::setSoftwareChangeHistory(const QStringList& changes)
{
    softwareChanges_->clear();
    for (const QString& change : changes) softwareChanges_->addItem(change);
    if (changes.isEmpty())
        softwareChanges_->addItem(QStringLiteral("No software inventory changes have been recorded yet."));
}

void AppInventoryPage::setRunningProcesses(const QVector<ProcessSample>& processes,
                                           const QDateTime& sampledAt)
{
    runningProcesses_ = processes;
    runningProcessesSampledAt_ = sampledAt;
    updateStartupActivity();
}

QVector<StartupResourceSignal> AppInventoryPage::proactiveReviewSignals() const
{
    QVector<StartupResourceSignal> resourceSignals;
    for (const StartupEntry& entry : startupEntries_) {
        const QString key = startupTrailKey(entry);
        if (consecutiveHighResourceSamples_.value(key) < 3) continue;
        const StartupProcessMatches matches = findStartupProcessMatches(entry, runningProcesses_);
        if (!matches.exactPath || matches.processes.size() != 1) continue;
        resourceSignals.append({key, entry.name, processUsageText(*matches.processes.first())});
    }
    return resourceSignals;
}

void AppInventoryPage::applyFilter()
{
    const QString query = search_->text().trimmed();
    visibleStartup_.clear();
    visibleApps_.clear();
    startupTable_->setRowCount(0);
    for (const auto& entry : startupEntries_) {
        const QString haystack = entry.name + QLatin1Char(' ') + entry.command + QLatin1Char(' ') + entry.source;
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        const int row = startupTable_->rowCount();
        startupTable_->insertRow(row);
        visibleStartup_.append(entry);
        startupTable_->setItem(row, 0, new QTableWidgetItem(entry.name));
        startupTable_->setItem(row, 1, new QTableWidgetItem(entry.source));
        startupTable_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("Configured")));
        startupTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("Checking…")));
        startupTable_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("—")));
        startupTable_->setItem(row, 5, new QTableWidgetItem(QStringLiteral("—")));
    }

    appsTable_->setRowCount(0);
    for (const auto& app : installedApps_) {
        const QString haystack = app.name + QLatin1Char(' ') + app.publisher + QLatin1Char(' ') +
                                 app.version + QLatin1Char(' ') + app.source;
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        const int row = appsTable_->rowCount();
        appsTable_->insertRow(row);
        visibleApps_.append(app);
        appsTable_->setItem(row, 0, new QTableWidgetItem(app.name));
        appsTable_->setItem(row, 1, new QTableWidgetItem(app.publisher.isEmpty() ? QStringLiteral("Not reported") : app.publisher));
        appsTable_->setItem(row, 2, new QTableWidgetItem(app.version.isEmpty() ? QStringLiteral("Not reported") : app.version));
        appsTable_->setItem(row, 3, new QTableWidgetItem(app.source));
    }
    details_->setText(QStringLiteral("Select an entry to see its local source and available details."));
    updateStartupActivity();
}

void AppInventoryPage::updateStartupActivity()
{
    const bool newSample = runningProcessesSampledAt_.isValid() &&
        runningProcessesSampledAt_ != lastStartupSampleAt_;
    if (newSample) {
        lastStartupSampleAt_ = runningProcessesSampledAt_;
        for (const StartupEntry& entry : startupEntries_) {
            const StartupProcessMatches matches = findStartupProcessMatches(entry, runningProcesses_);
            const QString key = startupTrailKey(entry);
            if (matches.exactPath && matches.processes.size() == 1 &&
                hasHighCurrentResourceUse(*matches.processes.first())) {
                consecutiveHighResourceSamples_[key] = std::min(3, consecutiveHighResourceSamples_.value(key) + 1);
            } else {
                consecutiveHighResourceSamples_.remove(key);
            }
        }
    }

    if (!isVisible()) return;

    for (qsizetype row = 0; row < visibleStartup_.size(); ++row) {
        const StartupEntry& entry = visibleStartup_.at(row);
        const StartupProcessMatches matches = findStartupProcessMatches(entry, runningProcesses_);
        const QString status = matches.processes.isEmpty()
            ? QStringLiteral("No visible match")
            : QStringLiteral("%1 match · %2")
                  .arg(matches.processes.size())
                  .arg(matches.exactPath ? QStringLiteral("path") : QStringLiteral("name only"));
        startupTable_->setItem(static_cast<int>(row), 3, new QTableWidgetItem(status));

        QString usage = QStringLiteral("Unavailable");
        if (matches.processes.size() == 1) usage = processUsageText(*matches.processes.first());
        else if (matches.processes.size() > 1)
            usage = QStringLiteral("%1 possible processes").arg(matches.processes.size());
        startupTable_->setItem(static_cast<int>(row), 4, new QTableWidgetItem(usage));
        const int highSamples = consecutiveHighResourceSamples_.value(startupTrailKey(entry));
        const QString signal = matches.processes.isEmpty() ? QStringLiteral("No signal")
            : (matches.exactPath && matches.processes.size() == 1
                ? (highSamples >= 3 ? QStringLiteral("Review") : QStringLiteral("Watching"))
                : QStringLiteral("Uncertain match"));
        startupTable_->setItem(static_cast<int>(row), 5, new QTableWidgetItem(signal));
    }
    showStartupDetails();
}

void AppInventoryPage::showStartupDetails()
{
    if (tabs_->currentWidget() != startupTable_) return;
    const int row = startupTable_->currentRow();
    if (row < 0 || row >= visibleStartup_.size()) return;
    const auto& entry = visibleStartup_.at(row);
    const StartupProcessMatches matches = findStartupProcessMatches(entry, runningProcesses_);
    QString activity;
    QString recommendation = QStringLiteral("No startup review suggestion is supported by the current evidence.");
    if (matches.processes.isEmpty()) {
        activity = QStringLiteral("No visible process currently matches this command. A missing match does not prove the application is not running.");
    } else {
        activity = matches.exactPath
            ? QStringLiteral("Executable-path match detected; this does not prove this startup entry launched the process.")
            : QStringLiteral("Executable-name match only; it may be unrelated or manually launched.");
        for (qsizetype index = 0; index < std::min<qsizetype>(matches.processes.size(), 5); ++index)
            activity += QStringLiteral("\n• %1").arg(processUsageText(*matches.processes.at(index)));
        if (matches.exactPath && matches.processes.size() == 1) {
            const int sustainedSamples = consecutiveHighResourceSamples_.value(startupTrailKey(entry));
            if (sustainedSamples >= 3) {
                recommendation = QStringLiteral("Consider reviewing this startup registration in Windows Settings if you do not need it at sign-in. The exact executable path has matched for at least three process samples and the current process has shown at least 15% CPU or 512 MiB working set across those samples. This does not prove the registration launched the process or caused a slow boot.");
            } else if (hasHighCurrentResourceUse(*matches.processes.first())) {
                recommendation = QStringLiteral("The current reading crossed Ausyn's observe threshold (15% CPU or 512 MiB working set). Ausyn is waiting for three consecutive process samples before suggesting a startup review; a short spike may be temporary.");
            }
        }
    }
    details_->setPlainText(QStringLiteral("%1\nSource: %2\nCommand or file: %3\n\nLive activity: %4\n\nEvidence-based next step: %5\n\n%6")
        .arg(entry.name, entry.source, entry.command, activity, recommendation, entry.detail));
}

void AppInventoryPage::showInstalledDetails()
{
    if (tabs_->currentWidget() != appsTable_) return;
    const int row = appsTable_->currentRow();
    if (row < 0 || row >= visibleApps_.size()) return;
    const auto& app = visibleApps_.at(row);
    details_->setPlainText(QStringLiteral("%1\nPublisher: %2\nVersion: %3\nInstall date: %4\nInventory source: %5")
        .arg(app.name,
             app.publisher.isEmpty() ? QStringLiteral("Not reported") : app.publisher,
             app.version.isEmpty() ? QStringLiteral("Not reported") : app.version,
             app.installDate.isEmpty() ? QStringLiteral("Not reported") : app.installDate,
             app.source));
}

} // namespace Ausyn
