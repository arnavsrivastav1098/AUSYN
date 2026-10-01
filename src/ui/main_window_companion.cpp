#include "main_window.h"
#include "notification_popup.h"
#include "live_visuals.h"
#include "../monitoring/background_relief.h"
#include "../monitoring/telemetry_service.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTextBrowser>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

namespace Ausyn {
QWidget* MainWindow::makeCompanionPanel() {
    auto* panel = new QFrame; panel->setObjectName(QStringLiteral("panel"));
    panel->setAccessibleName(QStringLiteral("Background companion"));
    auto* layout = new QVBoxLayout(panel); layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(10);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("Your background companion"), panel); title->setObjectName(QStringLiteral("panelTitle")); heading->addWidget(title, 1);
    auto* briefing = new QPushButton(QStringLiteral("Brief me"), panel); briefing->setObjectName(QStringLiteral("secondaryButton")); heading->addWidget(briefing); layout->addLayout(heading);
    companionStatus_ = new QLabel(panel); companionStatus_->setObjectName(QStringLiteral("companionStatus")); companionStatus_->setWordWrap(true); layout->addWidget(companionStatus_);
    auto* description = new QLabel(QStringLiteral("I follow the task you keep in front, prepare guidance and check approved background rules. You can keep working."), panel);
    description->setWordWrap(true); description->setObjectName(QStringLiteral("subtle")); layout->addWidget(description);
    companionDetails_ = new QLabel(panel); companionDetails_->setObjectName(QStringLiteral("subtle")); companionDetails_->setWordWrap(true); companionDetails_->setProperty("detailOnly", true); layout->addWidget(companionDetails_);
    companionJournal_ = new QLabel(panel); companionJournal_->setWordWrap(true); companionJournal_->setProperty("detailOnly", true); layout->addWidget(companionJournal_);
    connect(briefing, &QPushButton::clicked, this, &MainWindow::showCompanionBriefing);
    for (auto* label : panel->findChildren<QLabel*>()) label->setTextFormat(Qt::PlainText);
    return panel;
}
QWidget* MainWindow::makeCompanionSettings() {
    auto* panel = new QFrame; panel->setObjectName(QStringLiteral("panel"));
    auto* layout = new QVBoxLayout(panel); layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Background decisions & standing rules"), panel); title->setObjectName(QStringLiteral("panelTitle")); layout->addWidget(title);
    const auto checkbox = [panel, layout](const QString& text, bool checked) {
        auto* box = new QCheckBox(text, panel); box->setChecked(checked); layout->addWidget(box); return box;
    };
    companionEnabled_ = checkbox(QStringLiteral("Follow my current task and prepare help automatically"), preferences_.autonomousCompanionEnabled);
    rememberActivity_ = checkbox(QStringLiteral("Remember app patterns and the local decision journal"), preferences_.rememberActivity);
    auto* privacy = new QLabel(QStringLiteral("Optional learning stores up to 64 app profiles for 30 days and 72 journal entries for 7 days on this PC. It uses foreground executable names and resource readings. No screen contents, window titles, keystrokes or browsing history. Turning it off removes the saved companion file."), panel);
    privacy->setWordWrap(true); privacy->setObjectName(QStringLiteral("subtle")); layout->addWidget(privacy);
    smartAttention_ = checkbox(QStringLiteral("Group ordinary alerts to avoid repeated interruptions"), preferences_.smartAttentionEnabled);
    const auto choice = [panel, layout](const QString& label, const QList<int>& values, int current, const QString& unit) {
        auto* row = new QHBoxLayout; auto* caption = new QLabel(label, panel); caption->setWordWrap(true); row->addWidget(caption, 1);
        auto* combo = new QComboBox(panel); combo->setAccessibleName(label);
        for (const int value : values) combo->addItem(QStringLiteral("%1 %2").arg(value).arg(unit), value);
        combo->setCurrentIndex(std::max(0, combo->findData(current))); row->addWidget(combo); layout->addLayout(row); return combo;
    };
    attentionBudget_ = choice(QStringLiteral("Ordinary desktop alerts per 10 minutes"), {1, 2, 3, 4, 6}, preferences_.attentionBudget, QStringLiteral("alerts"));
    workSessionMinutes_ = choice(QStringLiteral("My usual work session (for battery guidance)"), {15, 30, 60, 90, 120, 180, 240}, preferences_.workSessionMinutes, QStringLiteral("minutes"));
    automaticReliefEnabled_ = checkbox(QStringLiteral("Apply the executable rules I approve below automatically"), preferences_.automaticReliefEnabled);
    auto* scope = new QLabel(QStringLiteral("Under sustained CPU pressure, an approved busy background process can use Below Normal CPU priority for up to 10 minutes. Ausyn checks the result and requests undo when it cannot measure relief. This can slow background work; it does not free RAM. The foreground app, system processes and other users’ processes are protected. No file deletion, app closure, High/Realtime priority or remote AI execution."), panel);
    scope->setWordWrap(true); scope->setObjectName(QStringLiteral("subtle")); layout->addWidget(scope);
    companionRules_ = new QLabel(panel); companionRules_->setWordWrap(true); companionRules_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(companionRules_);
    auto* actions = new QHBoxLayout;
    auto* approve = new QPushButton(QStringLiteral("Approve an executable…"), panel);
    auto* clearRules = new QPushButton(QStringLiteral("Revoke all rules"), panel);
    for (auto* button : {approve, clearRules}) button->setObjectName(QStringLiteral("secondaryButton"));
    actions->addWidget(approve); actions->addWidget(clearRules); layout->addLayout(actions);
    companionLearning_ = new QLabel(panel); companionLearning_->setWordWrap(true); companionLearning_->setProperty("detailOnly", true); layout->addWidget(companionLearning_);
    auto* forget = new QPushButton(QStringLiteral("Forget learned patterns && journal"), panel); forget->setObjectName(QStringLiteral("secondaryButton")); layout->addWidget(forget, 0, Qt::AlignLeft);
    connect(approve, &QPushButton::clicked, this, [this] {
        if (uiCheck_) return;
        const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a background executable to approve"), {}, QStringLiteral("Windows executable (*.exe)"));
        const QString path = AutonomousCompanion::normalizedPath(selected);
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        if (preferences_.automaticReliefPaths.size() >= 8) { QMessageBox::information(this, QStringLiteral("Rule limit"), QStringLiteral("Up to eight exact executable paths are supported. Revoke rules before choosing a new set.")); return; }
        QMessageBox review(this); review.setWindowTitle(QStringLiteral("Approve a standing background rule"));
        review.setText(QStringLiteral("Allow Ausyn to lower CPU priority for %1 automatically during sustained pressure?").arg(QFileInfo(path).fileName()));
        review.setInformativeText(QStringLiteral("Only processes at the exact path below, owned by your Windows account and currently at Normal priority, are eligible. The app must be in the background while another stable foreground task is busy. Original priority is restored on focus change, failed verification, rule disable, pause, normal exit or 10-minute expiry. A crash may leave Below Normal priority until that app restarts. Approval persists across launches; you can revoke it here. Enabling the checkbox activates approved rules."));
        review.setDetailedText(path); review.setStandardButtons(QMessageBox::Apply | QMessageBox::Cancel); review.setDefaultButton(QMessageBox::Cancel);
        if (review.exec() != QMessageBox::Apply) return;
        if (!preferences_.automaticReliefPaths.contains(path, Qt::CaseInsensitive)) preferences_.automaticReliefPaths.append(path);
        savePreferences(); recordActivity(QStringLiteral("Standing background rule approved"), path, 9); refreshCompanionPanel();
    });
    connect(clearRules, &QPushButton::clicked, this, [this] {
        if (backgroundRelief_) backgroundRelief_->restore(); automaticLease_ = false;
        companion_.endAutomaticAction(QStringLiteral("All executable rules were revoked."));
        preferences_.automaticReliefPaths.clear(); automaticReliefEnabled_->setChecked(false); savePreferences(); refreshCompanionPanel();
        recordActivity(QStringLiteral("Standing rules revoked"), QStringLiteral("No executable remains approved for automatic background relief."), 9);
    });
    connect(forget, &QPushButton::clicked, this, [this] {
        companion_.forget(); if (!uiCheck_ && !companionStatePath_.isEmpty() && QFile::exists(companionStatePath_) && !QFile::remove(companionStatePath_))
            QMessageBox::warning(this, QStringLiteral("Saved learning could not be removed"), QStringLiteral("The in-memory patterns were cleared, but Windows did not allow removal of the saved file. Close other Ausyn instances and try again."));
        refreshCompanionPanel(); recordActivity(QStringLiteral("Companion learning cleared"), QStringLiteral("App patterns, action outcomes and the companion journal were cleared."), 9);
    });
    for (auto* box : {companionEnabled_, rememberActivity_, automaticReliefEnabled_, smartAttention_}) connect(box, &QCheckBox::toggled, this, [this] { savePreferences(); });
    for (auto* combo : {attentionBudget_, workSessionMinutes_}) connect(combo, &QComboBox::currentIndexChanged, this, [this] { savePreferences(); });
    for (auto* label : panel->findChildren<QLabel*>()) label->setTextFormat(Qt::PlainText);
    return panel;
}
QWidget* MainWindow::makeCompanionJournal() {
    auto* panel = new QFrame; panel->setObjectName(QStringLiteral("panel")); panel->setProperty("detailOnly", true);
    auto* layout = new QVBoxLayout(panel); layout->setContentsMargins(16, 14, 16, 14);
    auto* heading = new QLabel(QStringLiteral("Background decision journal"), panel); heading->setObjectName(QStringLiteral("panelTitle")); layout->addWidget(heading);
    companionSavedJournal_ = new QTextBrowser(panel); companionSavedJournal_->setAccessibleName(QStringLiteral("Local background decision history")); companionSavedJournal_->setOpenExternalLinks(false); companionSavedJournal_->setMinimumHeight(240); companionSavedJournal_->setMaximumHeight(320); layout->addWidget(companionSavedJournal_);
    return panel;
}
void MainWindow::configureCompanion() {
    if (companionStatePath_.isEmpty() && !uiCheck_) {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!directory.isEmpty()) {
            companionStatePath_ = QDir(directory).filePath(QStringLiteral("companion.json"));
            companion_.configure(preferences_); companion_.load(companionStatePath_, QDateTime::currentDateTime());
        }
    }
    companion_.configure(preferences_);
    if ((!preferences_.automaticReliefEnabled || !preferences_.autonomousCompanionEnabled || !preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled) && automaticLease_) {
        if (backgroundRelief_) backgroundRelief_->restore(); automaticLease_ = false;
        companion_.endAutomaticAction(QStringLiteral("The automatic rule or monitoring was disabled."));
    }
    if (!preferences_.rememberActivity && !uiCheck_ && !companionStatePath_.isEmpty() && QFile::exists(companionStatePath_)) {
        if (!QFile::remove(companionStatePath_)) recordActivity(QStringLiteral("Saved companion file could not be removed"), QStringLiteral("Windows denied removal. Learning is off; the file is no longer read or updated. Use Forget learned patterns to retry."), 9);
    }
    const bool economical = preferences_.autonomousCompanionEnabled && preferences_.monitoringEnabled && companion_.economical();
    if (telemetry_) telemetry_->setSamplingIntervalSeconds(economical ? 10 : preferences_.samplingIntervalSeconds);
    if (dashboardChart_) dashboardChart_->setAnimationsEnabled(preferences_.animationsEnabled && preferences_.monitoringEnabled && !economical);
    if (performanceChart_) performanceChart_->setAnimationsEnabled(preferences_.animationsEnabled && preferences_.monitoringEnabled && !economical);
    companionEconomical_ = economical;
    refreshCompanionPanel();
}
void MainWindow::updateCompanion(const SystemSnapshot& s) {
    if (automaticLease_ && backgroundRelief_ && !backgroundRelief_->active()) {
        automaticLease_ = false; companion_.endAutomaticAction(backgroundRelief_->status());
    }
    companion_.observe(s, earlyPressureActive_);
    if (automaticLease_ && backgroundRelief_) {
        const QString reason = companion_.takeUndoReason();
        const bool taskChanged = !companion_.actionGoal().isEmpty() && s.foregroundProcessName.compare(companion_.actionGoal(), Qt::CaseInsensitive) != 0 && s.foregroundProcessName.compare(QStringLiteral("ausyn.exe"), Qt::CaseInsensitive) != 0;
        if (!reason.isEmpty() || taskChanged || !backgroundRelief_->active()) {
            backgroundRelief_->restore(); automaticLease_ = false;
            companion_.endAutomaticAction((!reason.isEmpty() ? reason : taskChanged ? QStringLiteral("The foreground task changed.") : QStringLiteral("The temporary lease ended or the target left the background.")) + QLatin1Char(' ') + backgroundRelief_->status());
        }
    }
    const QString protectedApp = workloadCoach_.confirmed() ? workloadCoach_.appName() : companion_.goal();
    const auto candidate = companion_.automaticCandidate(s, protectedApp);
    if (!uiCheck_ && backgroundRelief_ && !backgroundRelief_->active() && candidate &&
        (!lastAutomaticAttempt_.isValid() || lastAutomaticAttempt_.secsTo(s.capturedAt) >= 60)) {
        lastAutomaticAttempt_ = s.capturedAt; QString error;
        if (backgroundRelief_->apply(*candidate, s, protectedApp, &error)) {
            automaticLease_ = true; companion_.beginAutomaticAction(*candidate, s, protectedApp);
        } else recordActivity(QStringLiteral("Approved background action skipped"), error, 0);
    }
    for (const auto& e : companion_.takeEvents()) {
        recordActivity(e.title, e.body, 0);
        if (e.notify) showDesktopNotification(NotificationCategory::SystemFinding, e.title, e.body, QSystemTrayIcon::Information, false, e.resource, QStringLiteral("companion/") + e.key);
    }
    if (companionEconomical_ != companion_.economical()) configureCompanion();
    if (!uiCheck_ && !companionStatePath_.isEmpty() && !companion_.save(companionStatePath_, s.capturedAt))
        statusBar()->showMessage(QStringLiteral("The local companion journal could not be saved. Session monitoring continues."), 5000);
    refreshCompanionPanel();
}
void MainWindow::refreshCompanionPanel() {
    const qint64 age = latestSnapshot_.capturedAt.isValid() ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const bool fresh = age >= 0 && age <= std::max(15, latestSnapshot_.samplingIntervalSeconds * 3) * 1000LL;
    const QString status = !preferences_.monitoringEnabled ? QStringLiteral("Paused · no background decisions") : !fresh ? QStringLiteral("Waiting for fresh readings · earlier activity is history") : companion_.status();
    if (companionStatus_ && companionPanel_->isVisible()) {
        companionStatus_->setText(status);
        if (companionDetails_->isVisible()) companionDetails_->setText(companion_.detail());
        if (companionJournal_->isVisible()) companionJournal_->setText(companion_.digest());
    }
    if (companionRules_) {
        QStringList names; for (const auto& path : preferences_.automaticReliefPaths) names << QFileInfo(path).fileName();
        companionRules_->setText(names.isEmpty() ? QStringLiteral("No executable approved. Automatic OS actions are inactive.") : QStringLiteral("Approved exact paths (%1): %2").arg(preferences_.automaticReliefPaths.size()).arg(names.join(QStringLiteral(" · "))));
        companionRules_->setToolTip(preferences_.automaticReliefPaths.join(QLatin1Char('\n')));
    }
    if (companionLearning_ && companionLearning_->isVisible()) companionLearning_->setText(companion_.profileSummary());
    if (companionSavedJournal_ && companionSavedJournal_->isVisible()) {
        const QString text = companion_.journalText();
        if (text != companionRenderedJournal_) { companionRenderedJournal_ = text; companionSavedJournal_->setPlainText(text); }
    }
    if (trayCompanionStatus_) trayCompanionStatus_->setText(status.left(110));
    if (trayIcon_) trayIcon_->setToolTip(QStringLiteral("Ausyn · %1").arg(status.left(110)));
}
void MainWindow::showCompanionBriefing() {
    const qint64 age = latestSnapshot_.capturedAt.isValid() ? latestSnapshot_.capturedAt.secsTo(QDateTime::currentDateTime()) : -1;
    const QString freshness = !preferences_.monitoringEnabled ? QStringLiteral("Monitoring is paused. Entries below are historical.\n\n") : age < 0 || age > std::max(15, latestSnapshot_.samplingIntervalSeconds * 3) ? QStringLiteral("Waiting for fresh readings. Entries below are historical.\n\n") : QString{};
    popupWindow()->present(QStringLiteral("Your background briefing"), freshness + companion_.digest(), 1, false, preferences_.lightTheme);
}
void MainWindow::installCompanionTray(QMenu* menu) {
    trayCompanionStatus_ = menu->addAction(QStringLiteral("Observing your system…")); trayCompanionStatus_->setEnabled(false);
    auto* brief = menu->addAction(QStringLiteral("Brief me here")); connect(brief, &QAction::triggered, this, &MainWindow::showCompanionBriefing);
    auto* pause = menu->addAction(QStringLiteral("Pause / resume monitoring")); connect(pause, &QAction::triggered, this, [this] { monitoringEnabled_->setChecked(!monitoringEnabled_->isChecked()); });
    auto* quiet = menu->addAction(QStringLiteral("Quiet for 15 minutes")); connect(quiet, &QAction::triggered, this, [this] { desktopSnoozedUntil_ = QDateTime::currentDateTimeUtc().addSecs(900); if (notificationPopup_) notificationPopup_->hide(); recordActivity(QStringLiteral("Desktop alerts snoozed from tray"), QStringLiteral("Monitoring continues; desktop alerts are quiet for 15 minutes."), 9); });
    auto* undo = menu->addAction(QStringLiteral("Undo temporary background priority")); connect(undo, &QAction::triggered, this, [this] { if (backgroundRelief_) backgroundRelief_->restore(); automaticLease_ = false; companion_.endAutomaticAction(QStringLiteral("You requested undo from the notification area.")); });
    connect(menu, &QMenu::aboutToShow, this, [this, undo] { refreshCompanionPanel(); undo->setEnabled(backgroundRelief_ && backgroundRelief_->active()); });
}
}
