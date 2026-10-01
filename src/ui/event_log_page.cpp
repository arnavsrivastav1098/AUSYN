#include "event_log_page.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProcess>
#include <QSaveFile>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <algorithm>

namespace Ausyn {
namespace {
QFrame* makePanel(QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    return panel;
}

QString severityText(EventSeverity severity)
{
    switch (severity) {
    case EventSeverity::Critical: return QStringLiteral("Critical");
    case EventSeverity::Error: return QStringLiteral("Error");
    case EventSeverity::Warning: return QStringLiteral("Warning");
    }
    return QStringLiteral("Unknown");
}

QString signalText(const EventInsight& insight)
{
    if (insight.windowsBugCheck) return QStringLiteral("Blue-screen restart");
    if (insight.unexpectedShutdown) return QStringLiteral("Unexpected shutdown");
    if (insight.windowsHardwareError) return QStringLiteral("Hardware error report");
    if (insight.windowsStorageEvent) return QStringLiteral("Storage-driver event");
    if (insight.applicationHang) return QStringLiteral("App stopped responding");
    if (insight.likelyApplicationCrash) return QStringLiteral("Application crash");
    return QStringLiteral("Windows event");
}

QString recurrenceSpan(const EventInsight& insight)
{
    if (insight.occurrenceCount < 2 || !insight.firstAt.isValid() || !insight.latestAt.isValid())
        return {};
    const qint64 seconds = insight.firstAt.secsTo(insight.latestAt);
    if (seconds < 0) return {};
    if (seconds < 60) return QStringLiteral("under a minute");
    if (seconds < 60 * 60) return QStringLiteral("%1 min").arg(qMax<qint64>(1, seconds / 60));
    if (seconds < 24 * 60 * 60) return QStringLiteral("%1 hr %2 min").arg(seconds / 3600).arg((seconds % 3600) / 60);
    return QStringLiteral("%1 d %2 hr").arg(seconds / 86400).arg((seconds % 86400) / 3600);
}

QString csvField(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

} // namespace

EventLogPage::EventLogPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(15);

    auto* eyebrow = new QLabel(QStringLiteral("LOCAL WINDOWS DIAGNOSTICS"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Event intelligence"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("Ausyn groups recent Windows warnings and errors, including storage-driver reports when that log is available, so repeated patterns are easier to spot. Event messages are evidence, not a diagnosis."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* controls = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Search provider, event ID, or message…"));
    search_->setClearButtonEnabled(true);
    severityFilter_ = new QComboBox(this);
    severityFilter_->addItem(QStringLiteral("All levels"), -1);
    severityFilter_->addItem(QStringLiteral("Critical"), static_cast<int>(EventSeverity::Critical));
    severityFilter_->addItem(QStringLiteral("Errors"), static_cast<int>(EventSeverity::Error));
    severityFilter_->addItem(QStringLiteral("Warnings"), static_cast<int>(EventSeverity::Warning));
    refreshButton_ = new QPushButton(QStringLiteral("Refresh logs"), this);
    refreshButton_->setObjectName(QStringLiteral("secondaryButton"));
    exportButton_ = new QPushButton(QStringLiteral("Export visible"), this);
    exportButton_->setObjectName(QStringLiteral("secondaryButton"));
    exportButton_->setEnabled(false);
    reliabilityButton_ = new QPushButton(QStringLiteral("Reliability Monitor"), this);
    reliabilityButton_->setObjectName(QStringLiteral("secondaryButton"));
    reliabilityButton_->setToolTip(QStringLiteral("Open the Windows stability timeline. Ausyn will not change Windows settings."));
    controls->addWidget(search_, 1);
    controls->addWidget(severityFilter_);
    controls->addWidget(exportButton_);
    controls->addWidget(reliabilityButton_);
    controls->addWidget(refreshButton_);
    outer->addLayout(controls);
    connect(search_, &QLineEdit::textChanged, this, &EventLogPage::applyFilter);
    connect(severityFilter_, &QComboBox::currentIndexChanged, this, &EventLogPage::applyFilter);
    connect(refreshButton_, &QPushButton::clicked, this, &EventLogPage::refreshRequested);
    connect(exportButton_, &QPushButton::clicked, this, &EventLogPage::exportVisibleEvents);
    connect(reliabilityButton_, &QPushButton::clicked, this, &EventLogPage::openReliabilityMonitor);

    auto* panel = makePanel(this);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(13, 13, 13, 13);
    panelLayout->setSpacing(9);
    status_ = new QLabel(QStringLiteral("Preparing a local event scan…"), panel);
    status_->setObjectName(QStringLiteral("subtle"));
    status_->setWordWrap(true);
    table_ = new QTableWidget(panel);
    table_->setObjectName(QStringLiteral("processTable"));
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({QStringLiteral("Level"), QStringLiteral("Signal"),
        QStringLiteral("Source"), QStringLiteral("Event ID"), QStringLiteral("Log"),
        QStringLiteral("24h count"), QStringLiteral("Most recent")});
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    details_ = new QTextBrowser(panel);
    details_->setMaximumHeight(150);
    details_->setStyleSheet(QStringLiteral("QTextBrowser{background:#0e131d;border:1px solid #222e3e;border-radius:8px;color:#aeb9cb;padding:8px;}"));
    details_->setText(QStringLiteral("Select an event group to inspect its formatted Windows message and Ausyn’s explanation."));
    panelLayout->addWidget(status_);
    panelLayout->addWidget(table_, 1);
    panelLayout->addWidget(details_);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(panel, QStringLiteral("Windows event patterns"));
    auto* incidentPanel = makePanel(tabs);
    auto* incidentLayout = new QVBoxLayout(incidentPanel);
    incidentLayout->setContentsMargins(13, 13, 13, 13);
    incidentLayout->setSpacing(9);
    incidentStatus_ = new QLabel(QStringLiteral("Loading Ausyn's local incident history…"), incidentPanel);
    incidentStatus_->setObjectName(QStringLiteral("subtle"));
    incidentStatus_->setWordWrap(true);
    outcomeFilter_ = new QComboBox(incidentPanel);
    outcomeFilter_->addItem(QStringLiteral("All feedback"), 99);
    outcomeFilter_->addItem(QStringLiteral("Helped"), static_cast<int>(RecommendationOutcome::Improved));
    outcomeFilter_->addItem(QStringLiteral("No change"), static_cast<int>(RecommendationOutcome::NoChange));
    outcomeFilter_->addItem(QStringLiteral("Worse"), static_cast<int>(RecommendationOutcome::Worse));
    outcomeFilter_->addItem(QStringLiteral("Unsure"), static_cast<int>(RecommendationOutcome::Unsure));
    outcomeFilter_->addItem(QStringLiteral("Not rated"), 98);
    outcomeFilter_->setAccessibleName(QStringLiteral("Filter incident history by recommendation feedback"));
    outcomeFilter_->setToolTip(QStringLiteral("Show findings with a specific saved recommendation outcome."));
    incidents_ = new QTableWidget(incidentPanel);
    incidents_->setObjectName(QStringLiteral("processTable"));
    incidents_->setColumnCount(6);
    incidents_->setHorizontalHeaderLabels({QStringLiteral("State"), QStringLiteral("Level"),
        QStringLiteral("Finding"), QStringLiteral("Feedback"), QStringLiteral("First observed"),
        QStringLiteral("Last activity")});
    incidents_->setAlternatingRowColors(true);
    incidents_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    incidents_->setSelectionBehavior(QAbstractItemView::SelectRows);
    incidents_->setSelectionMode(QAbstractItemView::SingleSelection);
    incidents_->verticalHeader()->hide();
    incidents_->setShowGrid(false);
    incidents_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    incidents_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    incidents_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    incidents_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    incidents_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    incidents_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    auto* incidentControls = new QHBoxLayout;
    incidentControls->addWidget(incidentStatus_, 1);
    incidentControls->addWidget(new QLabel(QStringLiteral("Outcome"), incidentPanel));
    incidentControls->addWidget(outcomeFilter_);
    incidentDetails_ = new QTextBrowser(incidentPanel);
    incidentDetails_->setMaximumHeight(145);
    incidentDetails_->setStyleSheet(QStringLiteral("QTextBrowser{background:#0e131d;border:1px solid #222e3e;border-radius:8px;color:#aeb9cb;padding:8px;}"));
    incidentDetails_->setText(QStringLiteral("Select a finding to review its evidence and suggested next step."));
    incidentLayout->addLayout(incidentControls);
    incidentLayout->addWidget(incidents_, 1);
    incidentLayout->addWidget(incidentDetails_);
    tabs->addTab(incidentPanel, QStringLiteral("Incident replay & history"));
    outer->addWidget(tabs, 1);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &EventLogPage::showSelectedDetails);
    connect(incidents_, &QTableWidget::itemSelectionChanged,
            this, &EventLogPage::showSelectedIncidentDetails);
    connect(outcomeFilter_, &QComboBox::currentIndexChanged,
            this, &EventLogPage::applyFilter);
}

void EventLogPage::setBusy(bool busy)
{
    refreshButton_->setEnabled(!busy);
    if (busy) {
        refreshButton_->setText(QStringLiteral("Reading…"));
        if (insights_.isEmpty()) status_->setText(QStringLiteral("Reading recent Windows system, application, and storage-driver events in the background…"));
    } else {
        refreshButton_->setText(QStringLiteral("Refresh logs"));
    }
}

void EventLogPage::setUpdate(const EventLogUpdate& update)
{
    insights_ = update.insights;
    QString state = update.status;
    if (update.available) {
        state.prepend(QStringLiteral("Updated %1 · %2 grouped patterns\n")
            .arg(update.checkedAt.toString(QStringLiteral("h:mm:ss ap"))).arg(insights_.size()));
    }
    status_->setText(state);
    applyFilter();
    if (insights_.isEmpty() && update.available) {
        details_->setText(QStringLiteral("No warnings or errors were found in the recent event window."));
    }
}

void EventLogPage::setIncidentHistory(const QVector<Finding>& findings, bool available, const QString& message)
{
    incidentFindings_ = findings;
    incidentStatus_->setText(available
        ? QStringLiteral("%1 locally stored finding(s) · bounded by the selected history period · newest activity first")
              .arg(findings.size())
        : (message.isEmpty() ? QStringLiteral("Ausyn's local incident history is unavailable.") : message));
    applyFilter();
}

void EventLogPage::applyFilter()
{
    const QString query = search_->text().trimmed();
    const int severity = severityFilter_->currentData().toInt();
    details_->setText(QStringLiteral("Select a visible event group to inspect its formatted Windows message and Ausyn’s explanation."));
    incidentDetails_->setText(QStringLiteral("Select a visible finding to review its evidence, suggested step, and saved outcome."));
    table_->setRowCount(0);
    for (qsizetype index = 0; index < insights_.size(); ++index) {
        const EventInsight& insight = insights_.at(index);
        if (severity >= 0 && static_cast<int>(insight.severity) != severity) continue;
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(signalText(insight), insight.provider).arg(insight.eventId).arg(insight.channel)
            .arg(insight.message, insight.explanation);
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QStringList columns{
            severityText(insight.severity), signalText(insight), insight.provider, QString::number(insight.eventId),
            insight.channel, QString::number(insight.occurrenceCount),
            insight.latestAt.isValid() ? insight.latestAt.toString(QStringLiteral("MMM d, h:mm ap"))
                                       : QStringLiteral("Time unavailable")
        };
        for (int column = 0; column < columns.size(); ++column) {
            auto* item = new QTableWidgetItem(columns.at(column));
            item->setData(Qt::UserRole, index);
            item->setToolTip(column == 1 ? insight.explanation : columns.at(column));
            table_->setItem(row, column, item);
        }
    }
    exportButton_->setEnabled(table_->rowCount() > 0);
    incidents_->setRowCount(0);
    const int outcomeFilter = outcomeFilter_->currentData().toInt();
    for (qsizetype index = 0; index < incidentFindings_.size(); ++index) {
        const Finding& finding = incidentFindings_.at(index);
        const int findingLevel = finding.severity == FindingSeverity::Critical ? 3
            : (finding.severity == FindingSeverity::Warning ? 1 : 0);
        if (severity >= 0 && findingLevel != severity) continue;
        QString feedback = QStringLiteral("Not rated");
        if (finding.recommendationOutcome) {
            switch (*finding.recommendationOutcome) {
            case RecommendationOutcome::Worse: feedback = QStringLiteral("Worse"); break;
            case RecommendationOutcome::NoChange: feedback = QStringLiteral("No change"); break;
            case RecommendationOutcome::Improved: feedback = QStringLiteral("Helped"); break;
            case RecommendationOutcome::Unsure: feedback = QStringLiteral("Unsure"); break;
            }
        }
        if ((outcomeFilter == 98 && finding.recommendationOutcome) ||
            (outcomeFilter != 98 && outcomeFilter != 99 &&
             (!finding.recommendationOutcome ||
              static_cast<int>(*finding.recommendationOutcome) != outcomeFilter))) continue;
        const QString haystack = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(finding.title, finding.summary, finding.evidence, finding.recommendation,
                 finding.ruleId, feedback);
        if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
        const int row = incidents_->rowCount();
        incidents_->insertRow(row);
        const QString lastActivity = finding.resolvedAt.isValid()
            ? finding.resolvedAt.toString(QStringLiteral("MMM d, h:mm ap"))
            : finding.lastSeen.toString(QStringLiteral("MMM d, h:mm ap"));
        const QStringList columns{
            finding.resolvedAt.isValid() ? QStringLiteral("Resolved") : QStringLiteral("Active"),
            finding.severity == FindingSeverity::Critical ? QStringLiteral("Critical")
                : (finding.severity == FindingSeverity::Warning ? QStringLiteral("Warning") : QStringLiteral("Info")),
            finding.title,
            feedback,
            finding.firstSeen.isValid() ? finding.firstSeen.toString(QStringLiteral("MMM d, h:mm ap"))
                                        : QStringLiteral("Time unavailable"),
            lastActivity.isEmpty() ? QStringLiteral("Time unavailable") : lastActivity
        };
        for (int column = 0; column < columns.size(); ++column) {
            auto* item = new QTableWidgetItem(columns.at(column));
            item->setData(Qt::UserRole, index);
            incidents_->setItem(row, column, item);
        }
    }
    if (incidents_->rowCount() == 0 && !incidentFindings_.isEmpty())
        incidentDetails_->setText(QStringLiteral("No local findings match these filters."));
    if (table_->rowCount() == 0 && !insights_.isEmpty()) {
        details_->setText(QStringLiteral("No event groups match these filters."));
    }
}

void EventLogPage::exportVisibleEvents()
{
    if (table_->rowCount() == 0) return;
    const QMessageBox::StandardButton choice = QMessageBox::warning(this,
        QStringLiteral("Export Windows event details"),
        QStringLiteral("Windows event messages can contain file paths, account names, or device details. The CSV will be saved locally to the location you choose; review it before sharing. Continue?"),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Save) return;

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export visible Windows events"),
        QDir::home().filePath(QStringLiteral("Ausyn-Windows-events.csv")),
        QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    QStringList lines;
    lines << QStringList{QStringLiteral("Severity"), QStringLiteral("Signal"),
        QStringLiteral("Provider"), QStringLiteral("Event ID"), QStringLiteral("Channel"),
        QStringLiteral("Occurrences"), QStringLiteral("First seen (local)"),
        QStringLiteral("Most recent (local)"), QStringLiteral("Application"),
        QStringLiteral("Faulting module"), QStringLiteral("Exception code"),
        QStringLiteral("Windows message"), QStringLiteral("Ausyn explanation")}.join(QLatin1Char(','));
    for (int row = 0; row < table_->rowCount(); ++row) {
        const int index = table_->item(row, 0)->data(Qt::UserRole).toInt();
        if (index < 0 || index >= insights_.size()) continue;
        const EventInsight& event = insights_.at(index);
        const auto localTime = [](const QDateTime& time) {
            return time.isValid() ? time.toLocalTime().toString(Qt::ISODate) : QString();
        };
        lines << QStringList{
            csvField(severityText(event.severity)), csvField(signalText(event)),
            csvField(event.provider), csvField(QString::number(event.eventId)),
            csvField(event.channel), csvField(QString::number(event.occurrenceCount)),
            csvField(localTime(event.firstAt)), csvField(localTime(event.latestAt)),
            csvField(event.affectedApplication), csvField(event.faultingModule),
            csvField(event.exceptionCode), csvField(event.message), csvField(event.explanation)
        }.join(QLatin1Char(','));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        status_->setText(QStringLiteral("Could not create event export: %1").arg(file.errorString()));
        return;
    }
    const QByteArray contents = QByteArray::fromHex("efbbbf") +
        lines.join(QStringLiteral("\r\n")).toUtf8() + QByteArrayLiteral("\r\n");
    if (file.write(contents) != contents.size() || !file.commit()) {
        status_->setText(QStringLiteral("Event export failed: %1").arg(file.errorString()));
        return;
    }
    status_->setText(QStringLiteral("Exported %1 visible Windows event group(s) to %2")
        .arg(table_->rowCount()).arg(path));
}

void EventLogPage::openReliabilityMonitor()
{
    if (!QProcess::startDetached(QStringLiteral("perfmon.exe"), {QStringLiteral("/rel")})) {
        QMessageBox::warning(this, QStringLiteral("Reliability Monitor unavailable"),
            QStringLiteral("Windows Reliability Monitor could not be opened. You can search for “View reliability history” from the Windows Start menu."));
    }
}

void EventLogPage::showSelectedIncidentDetails()
{
    const int row = incidents_->currentRow();
    if (row < 0 || row >= incidents_->rowCount()) return;
    const int index = incidents_->item(row, 0)->data(Qt::UserRole).toInt();
    if (index < 0 || index >= incidentFindings_.size()) return;
    const Finding& finding = incidentFindings_.at(index);
    QString text = QStringLiteral("%1\n\n%2\n\nEvidence\n%3\n\nSuggested next step\n%4")
        .arg(finding.title, finding.summary, finding.evidence, finding.recommendation);
    if (finding.resolvedAt.isValid())
        text += QStringLiteral("\n\nResolved: %1. This means Ausyn no longer observed the rule; it does not prove what resolved it.")
            .arg(finding.resolvedAt.toString(QStringLiteral("MMM d, yyyy h:mm:ss ap")));
    else
        text += QStringLiteral("\n\nThis finding remains active in the latest saved analysis.");
    if (finding.recommendationOutcome) {
        QString outcome;
        switch (*finding.recommendationOutcome) {
        case RecommendationOutcome::Worse: outcome = QStringLiteral("Things felt worse"); break;
        case RecommendationOutcome::NoChange: outcome = QStringLiteral("No noticeable change"); break;
        case RecommendationOutcome::Improved: outcome = QStringLiteral("Things improved"); break;
        case RecommendationOutcome::Unsure: outcome = QStringLiteral("Outcome was unclear"); break;
        }
        text += QStringLiteral("\n\nYour feedback: %1%2. This records your report; it does not establish that the recommendation caused the result.")
            .arg(outcome, finding.recommendationOutcomeAt.isValid()
                ? QStringLiteral(" · %1").arg(finding.recommendationOutcomeAt.toString(QStringLiteral("MMM d, h:mm ap")))
                : QString());
    }
    if (finding.verificationCheckedAt.isValid()) {
        QStringList compared;
        const auto addChange = [&compared](const QString& metric,
                                           const std::optional<double>& before,
                                           const std::optional<double>& after) {
            if (!before || !after) return;
            const double delta = *after - *before;
            compared << QStringLiteral("%1 %2% → %3% (%4%5 percentage points)")
                .arg(metric).arg(*before, 0, 'f', 1).arg(*after, 0, 'f', 1)
                .arg(delta > 0.0 ? QStringLiteral("+") : QString())
                .arg(delta, 0, 'f', 1);
        };
        addChange(QStringLiteral("CPU"), finding.verificationCpuBefore, finding.verificationCpuAfter);
        addChange(QStringLiteral("Memory"), finding.verificationMemoryBefore, finding.verificationMemoryAfter);
        if (!compared.isEmpty())
            text += QStringLiteral("\n\nSaved before-and-after check (%1): %2. This is observational and does not prove cause.")
                .arg(finding.verificationCheckedAt.toString(QStringLiteral("MMM d, h:mm ap")),
                     compared.join(QStringLiteral("; ")));
    }
    if (finding.firstSeen.isValid()) {
        text += QStringLiteral("\n\nTimeline\nFirst observed: %1\nLast observed: %2")
            .arg(finding.firstSeen.toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm:ss ap")),
                 finding.lastSeen.isValid()
                    ? finding.lastSeen.toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm:ss ap"))
                    : QStringLiteral("Unavailable"));
        if (finding.resolvedAt.isValid())
            text += QStringLiteral("\nNo longer observed: %1")
                .arg(finding.resolvedAt.toLocalTime().toString(QStringLiteral("MMM d, yyyy h:mm:ss ap")));
    }
    const QDateTime anchor = finding.lastSeen.isValid() ? finding.lastSeen : finding.firstSeen;
    QVector<const EventInsight*> nearbyEvents;
    if (anchor.isValid()) {
        for (const EventInsight& event : insights_) {
            if (!event.latestAt.isValid()) continue;
            const qint64 deltaSeconds = qAbs(anchor.secsTo(event.latestAt));
            if (deltaSeconds <= 15 * 60) nearbyEvents.append(&event);
        }
    }
    std::sort(nearbyEvents.begin(), nearbyEvents.end(), [anchor](const EventInsight* left, const EventInsight* right) {
        return qAbs(anchor.secsTo(left->latestAt)) < qAbs(anchor.secsTo(right->latestAt));
    });
    if (nearbyEvents.isEmpty()) {
        text += QStringLiteral("\n\nWindows event replay: no loaded Windows event group was recorded within 15 minutes of the latest observation. This is not proof that no related event exists; the event scan has its own coverage and time window.");
    } else {
        text += QStringLiteral("\n\nWindows event replay · within 15 minutes of the latest observation:");
        const qsizetype shown = qMin<qsizetype>(nearbyEvents.size(), 6);
        for (qsizetype i = 0; i < shown; ++i) {
            const EventInsight& event = *nearbyEvents.at(i);
            text += QStringLiteral("\n%1 · %2 · %3, ID %4 · %5")
                .arg(event.latestAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")),
                     severityText(event.severity), event.provider).arg(event.eventId).arg(signalText(event));
        }
        text += QStringLiteral("\nTiming can help guide investigation, but it does not establish a shared cause.");
    }
    text += QStringLiteral("\n\nFinding ID: %1").arg(finding.ruleId);
    incidentDetails_->setPlainText(text);
}

void EventLogPage::showSelectedDetails()
{
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount()) return;
    const QString provider = table_->item(row, 2)->text();
    const quint32 id = table_->item(row, 3)->text().toUInt();
    const QString channel = table_->item(row, 4)->text();
    const auto it = std::find_if(insights_.cbegin(), insights_.cend(), [&](const EventInsight& item) {
        return item.provider == provider && item.eventId == id && item.channel == channel;
    });
    if (it == insights_.cend()) return;
    QString details = it->explanation;
    const QString span = recurrenceSpan(*it);
    if (!span.isEmpty()) {
        details += QStringLiteral("\n\nRecurrence: %1 occurrences across %2 (from %3 to %4). This describes timing only; it does not show that the events share a cause.")
            .arg(it->occurrenceCount).arg(span)
            .arg(it->firstAt.toString(QStringLiteral("MMM d, h:mm:ss ap")))
            .arg(it->latestAt.toString(QStringLiteral("MMM d, h:mm:ss ap")));
    }
    if (it->likelyApplicationCrash) {
        details += QStringLiteral("\n\nAusyn classification: Windows Application Error crash report (Event ID 1000). This record does not establish why the application failed.");
        if (!it->affectedApplication.isEmpty())
            details += QStringLiteral("\nWindows-reported application: %1").arg(it->affectedApplication);
        if (!it->faultingModule.isEmpty())
            details += QStringLiteral("\nWindows-reported faulting module: %1").arg(it->faultingModule);
        if (!it->exceptionCode.isEmpty())
            details += QStringLiteral("\nWindows-reported exception code: %1").arg(it->exceptionCode);
        if (!it->faultingModule.isEmpty() || !it->exceptionCode.isEmpty())
            details += QStringLiteral("\nThese fields are diagnostic clues from Windows, not proof that the named module is defective or the root cause.");
    }
    if (it->unexpectedShutdown) {
        details += QStringLiteral("\n\nAusyn classification: Windows unexpected-shutdown signal. Event ID 41 or 6008 can be recorded after power loss, a crash, or a forced shutdown; it does not identify the cause or prove hardware failure.");
    }
    if (it->windowsBugCheck) {
        details += QStringLiteral("\n\nAusyn classification: Windows bugcheck/restart record (System event 1001). Review the formatted Windows message below for any reported stop code; the record cannot identify the faulty component.");
    }
    if (it->windowsHardwareError) {
        details += QStringLiteral("\n\nAusyn classification: Windows Hardware Error Architecture (WHEA) report. Review its Windows message and recurrence. This record is evidence for investigation; it does not diagnose a component or prove that hardware is failing.");
    }
    if (it->windowsStorageEvent) {
        details += QStringLiteral("\n\nAusyn classification: Windows storage-driver event. Review the Windows message and recurrence. This event is evidence to investigate; it does not diagnose a drive or prove a storage component is failing.");
    }
    if (it->applicationHang) {
        details += QStringLiteral("\n\nAusyn classification: Windows Application Hang (event 1002). This records that an application stopped responding, not why it happened.");
    }
    if (it->latestAt.isValid()) {
        QVector<const EventInsight*> nearby;
        for (const EventInsight& candidate : insights_) {
            if (&candidate == &*it || !candidate.latestAt.isValid()) continue;
            const qint64 deltaSeconds = qAbs(it->latestAt.secsTo(candidate.latestAt));
            if (deltaSeconds <= 120) nearby.append(&candidate);
        }
        std::sort(nearby.begin(), nearby.end(), [&](const EventInsight* left, const EventInsight* right) {
            return qAbs(it->latestAt.secsTo(left->latestAt)) < qAbs(it->latestAt.secsTo(right->latestAt));
        });
        if (!nearby.isEmpty()) {
            details += QStringLiteral("\n\nOther event groups recorded within two minutes:");
            const qsizetype shown = qMin<qsizetype>(nearby.size(), 4);
            for (qsizetype index = 0; index < shown; ++index) {
                const EventInsight& event = *nearby.at(index);
                details += QStringLiteral("\n• %1 · %2, ID %3 · %4 · %5")
                    .arg(severityText(event.severity), event.provider)
                    .arg(event.eventId).arg(event.channel)
                    .arg(event.latestAt.toString(QStringLiteral("h:mm:ss ap")));
            }
            if (nearby.size() > shown)
                details += QStringLiteral("\n…and %1 more nearby group(s).").arg(nearby.size() - shown);
            details += QStringLiteral("\n\nThese records occurred near the same time; timing alone does not show a shared cause.");
        }
    }
    if (!it->message.isEmpty()) details += QStringLiteral("\n\nWindows message:\n%1").arg(it->message);
    else details += QStringLiteral("\n\nWindows did not provide a formatted message for this event.");
    details_->setPlainText(details);
}

} // namespace Ausyn
