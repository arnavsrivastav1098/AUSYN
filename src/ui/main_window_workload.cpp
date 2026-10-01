#include "main_window.h"
#include "resource_inspector.h"
#include "notification_popup.h"
#include "../monitoring/background_relief.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QVBoxLayout>
#include <algorithm>
namespace Ausyn {
QWidget* MainWindow::makeWorkloadPanel() {
    auto* panel = new QFrame;
    panel->setObjectName(QStringLiteral("panel"));
    panel->setAccessibleName(QStringLiteral("Workload companion"));
    panel->setProperty("detailOnly", true);
    auto* layout = new QVBoxLayout(panel); layout->setContentsMargins(20, 18, 20, 18); layout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Keep your work moving"), panel); title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);
    workloadSummary_ = new QLabel(panel); workloadSummary_->setWordWrap(true); workloadSummary_->setObjectName(QStringLiteral("workloadSummary")); layout->addWidget(workloadSummary_);
    workloadPlan_ = new QLabel(panel); workloadPlan_->setWordWrap(true); workloadPlan_->setObjectName(QStringLiteral("subtle")); layout->addWidget(workloadPlan_);
    auto* selection = new QHBoxLayout;
    workloadAppChoice_ = new QComboBox(panel); workloadAppChoice_->setAccessibleName(QStringLiteral("App to keep running"));
    workloadAppChoice_->setMinimumContentsLength(15); workloadAppChoice_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    keepWorkloadApp_ = new QPushButton(QStringLiteral("Keep this app"), panel); keepWorkloadApp_->setObjectName(QStringLiteral("secondaryButton"));
    auto* release = new QPushButton(QStringLiteral("End session"), panel); release->setObjectName(QStringLiteral("secondaryButton"));
    selection->addWidget(workloadAppChoice_, 1); selection->addWidget(keepWorkloadApp_); selection->addWidget(release); layout->addLayout(selection);
    auto* actions = new QGridLayout;
    compareWorkload_ = new QPushButton(QStringLiteral("Compare my change"), panel);
    relieveWorkload_ = new QPushButton(QStringLiteral("Review background relief"), panel);
    restoreWorkload_ = new QPushButton(QStringLiteral("Restore priority"), panel);
    for (auto* button : {compareWorkload_, relieveWorkload_, restoreWorkload_}) { button->setObjectName(QStringLiteral("secondaryButton")); button->setMinimumHeight(36); }
    compareWorkload_->setToolTip(QStringLiteral("Start just before reducing video quality or another workload. Compare the next 30 seconds; no settings are changed by this button."));
    actions->addWidget(compareWorkload_, 0, 0); actions->addWidget(relieveWorkload_, 0, 1); actions->addWidget(restoreWorkload_, 1, 0); layout->addLayout(actions);
    workloadComparison_ = new QLabel(panel); workloadComparison_->setWordWrap(true); workloadComparison_->setObjectName(QStringLiteral("workloadComparison")); layout->addWidget(workloadComparison_);
    workloadActionStatus_ = new QLabel(panel); workloadActionStatus_->setWordWrap(true); workloadActionStatus_->setObjectName(QStringLiteral("subtle")); layout->addWidget(workloadActionStatus_);
    connect(keepWorkloadApp_, &QPushButton::clicked, this, [this] {
        if (!preferences_.monitoringEnabled || !workloadCoach_.fresh(QDateTime::currentDateTime()) || !workloadCoach_.keepApp(workloadAppChoice_->currentData().toString())) {
            statusBar()->showMessage(QStringLiteral("Wait for fresh readings from a running app."), 5000); return;
        }
        if (backgroundRelief_) backgroundRelief_->guard(workloadCoach_.appName());
        recordActivity(QStringLiteral("Work session priority selected"), workloadCoach_.summary(), 0);
        refreshWorkloadPanel();
    });
    connect(release, &QPushButton::clicked, this, [this] {
        if (backgroundRelief_) backgroundRelief_->restore();
        workloadCoach_.releaseApp(); refreshWorkloadPanel();
        recordActivity(QStringLiteral("Work session ended"), QStringLiteral("The app preference was cleared and any temporary background priority change was ended."), 0);
    });
    connect(compareWorkload_, &QPushButton::clicked, this, [this] {
        if (!preferences_.monitoringEnabled || !workloadCoach_.fresh(QDateTime::currentDateTime())) return;
        workloadCoach_.beginComparison(); refreshWorkloadPanel();
        recordActivity(QStringLiteral("Comparing your workload change"), QStringLiteral("Captured a baseline; the next 30 seconds of readings will be compared. Ausyn has not changed any app settings."), 0);
    });
    connect(relieveWorkload_, &QPushButton::clicked, this, &MainWindow::reviewBackgroundRelief);
    connect(restoreWorkload_, &QPushButton::clicked, backgroundRelief_, &BackgroundRelief::restore);
    return panel;
}
void MainWindow::refreshWorkloadPanel() {
    if (!workloadSummary_ || !workloadPanel_ || (!workloadPanel_->isVisible() && isVisible())) return;
    const bool fresh = preferences_.monitoringEnabled && workloadCoach_.fresh(QDateTime::currentDateTime());
    workloadSummary_->setText(!preferences_.monitoringEnabled ? QStringLiteral("Monitoring paused. Resume to understand your current task.")
        : !fresh ? QStringLiteral("Waiting for fresh activity readings. Earlier context is not a live diagnosis.") : workloadCoach_.summary());
    workloadPlan_->setText(fresh ? workloadCoach_.plan() : QString());
    workloadPlan_->setVisible(fresh);
    workloadComparison_->setText(workloadCoach_.comparison()); workloadComparison_->setVisible(!workloadComparison_->text().isEmpty());
    workloadActionStatus_->setVisible(!workloadActionStatus_->text().isEmpty());
    keepWorkloadApp_->setEnabled(fresh); compareWorkload_->setEnabled(fresh);
    relieveWorkload_->setEnabled(fresh && workloadCoach_.confirmed() && workloadCoach_.backgroundCandidate().has_value() && !uiCheck_);
    restoreWorkload_->setVisible(backgroundRelief_ && backgroundRelief_->active());
    QStringList names;
    if (fresh) for (const auto& p : latestSnapshot_.topProcesses) {
        if (!p.name.isEmpty() && !p.executablePath.isEmpty() && p.name.compare(QStringLiteral("ausyn.exe"), Qt::CaseInsensitive) != 0 && !names.contains(p.name, Qt::CaseInsensitive)) names << p.name;
    }
    names.sort(Qt::CaseInsensitive);
    QStringList current;
    for (int i = 0; i < workloadAppChoice_->count(); ++i) current << workloadAppChoice_->itemData(i).toString();
    if (current != names) {
        const QString selected = workloadAppChoice_->currentData().toString();
        workloadAppChoice_->clear();
        for (const auto& name : names) workloadAppChoice_->addItem(name, name);
        int index = workloadAppChoice_->findData(selected);
        if (index < 0) index = workloadAppChoice_->findData(workloadCoach_.appName());
        if (index >= 0) workloadAppChoice_->setCurrentIndex(index);
    }
}
void MainWindow::updateWorkloadContext(const SystemSnapshot& snapshot) {
    const qint64 processAge = snapshot.processSamplesCapturedAt.isValid() ? snapshot.processSamplesCapturedAt.msecsTo(snapshot.capturedAt) : -1;
    if (backgroundRelief_) backgroundRelief_->guard(workloadCoach_.confirmed() ? workloadCoach_.appName() : QString(), processAge >= 0 && processAge <= 30'000 ? snapshot.foregroundProcessName : QString());
    if (!preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled) return;
    const qint64 age = snapshot.capturedAt.isValid() ? snapshot.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (age < 0 || age > std::max(15, snapshot.samplingIntervalSeconds * 3) * 1000LL) return;
    workloadCoach_.observe(snapshot, earlyPressureActive_);
    updateCompanion(snapshot);
    const auto notice = workloadCoach_.notice();
    if (notice) {
        const bool changed = workloadEpisodeKey_ != notice->key;
        if (changed) { workloadEpisodeKey_ = notice->key; dismissedAdviceTitle_.clear(); }
        workloadBannerTitle_ = notice->title;
        showAdviceBanner(notice->title, notice->body, notice->resource);
        if (showDesktopNotification(NotificationCategory::SystemFinding, notice->title, notice->body,
            notice->critical ? QSystemTrayIcon::Critical : earlyPressureActive_ ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
            false, notice->resource, notice->key)) {
            workloadCoach_.markDelivered(notice->key);
            if (earlyPressureActive_) deliveredFindingSeverity_.insert(QStringLiteral("live-sustained-resource-load"), static_cast<int>(earlyPressureFinding_.severity));
        }
    } else if (earlyPressureActive_ && adviceBannerTitle_ && adviceBannerTitle_->text() == workloadBannerTitle_) {
        // Refresh an existing episode card without opening or extending a popup.
        adviceBannerBody_->setText((workloadCoach_.summary() + QLatin1Char(' ') + workloadCoach_.plan()).left(500));
    }
    if (notificationPopup_ && notificationPopup_->isVisible() && lastDesktopAlertOccurrenceKey_.startsWith(QStringLiteral("workload/"))) {
        const auto pct = [](const std::optional<double>& v) { return v ? QStringLiteral("%1%").arg(*v, 0, 'f', 0) : QStringLiteral("unavailable"); };
        const QString readings = QStringLiteral("CPU %1 · RAM %2 · %3 available. ")
            .arg(pct(snapshot.processorUsagePercent), pct(snapshot.memoryUsagePercent), snapshot.memoryTotalBytes > 0
                ? QStringLiteral("%1 GB").arg(static_cast<double>(snapshot.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 2) : QStringLiteral("RAM reading unavailable"));
        const QString title = notificationPopup_->findChild<QLabel*>(QStringLiteral("noticeTitle"))->text();
        notificationPopup_->present(title, workloadCoach_.summary() + QLatin1Char(' ') + readings + workloadCoach_.plan(),
            lastDesktopAlertResource_, lastDesktopAlertIcon_ == QSystemTrayIcon::Critical, preferences_.lightTheme, true, true);
    }
    if (isVisible() && activeLegacyPage_ == 0) refreshWorkloadPanel();
}
void MainWindow::reviewBackgroundRelief() {
    if (uiCheck_ || !preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled || !workloadCoach_.confirmed() || !workloadCoach_.fresh(QDateTime::currentDateTime())) return;
    const auto candidate = workloadCoach_.backgroundCandidate();
    if (!candidate) { statusBar()->showMessage(QStringLiteral("No eligible busy background app is visible in the latest readings."), 6000); return; }
    const ProcessSample target = *candidate;
    const SystemSnapshot before = latestSnapshot_;
    QMessageBox review(this); review.setWindowTitle(QStringLiteral("Review temporary background relief"));
    review.setText(QStringLiteral("Temporarily lower CPU priority for %1 (PID %2), keeping %3 as your chosen task?").arg(target.name).arg(target.processId).arg(workloadCoach_.appName()));
    review.setInformativeText(QStringLiteral("Only this process changes from Normal to Below Normal for up to 10 minutes. It may finish work more slowly; this does not free RAM or guarantee smoother performance. Ausyn restores it on expiry, when it next observes the app in front, or when you end the session or quit normally. If Ausyn crashes, restart the affected app to reset its priority. No app is closed."));
    review.setDetailedText(QStringLiteral("Executable: %1\nLatest CPU sample: %2%\nProcess readings: %3\nThe process identity, owner, current priority and foreground state are checked again before applying.").arg(target.executablePath).arg(target.cpuPercent.value_or(0), 0, 'f', 1).arg(before.processSamplesCapturedAt.toLocalTime().toString(Qt::ISODate)));
    review.setStandardButtons(QMessageBox::Apply | QMessageBox::Cancel); review.setDefaultButton(QMessageBox::Cancel);
    if (review.exec() != QMessageBox::Apply) return;
    QString error;
    if (!backgroundRelief_->apply(target, before, workloadCoach_.appName(), &error)) {
        QMessageBox::warning(this, QStringLiteral("Priority was not changed"), error); return;
    }
    workloadCoach_.beginComparison(); refreshWorkloadPanel();
}
}
