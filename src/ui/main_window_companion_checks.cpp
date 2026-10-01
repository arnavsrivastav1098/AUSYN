#include "main_window.h"
#include "notification_popup.h"
#include "live_visuals.h"
#include "page_hub.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <cmath>

namespace Ausyn {
bool MainWindow::runCompanionChecks(const QString& directory, QJsonArray& checks) {
    bool passed = true;
    const auto expect = [&checks, &passed](const QString& name, bool result) {
        checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), result}}); passed = passed && result;
    };
    const QDateTime base = QDateTime::currentDateTime().addSecs(-1200);
    SystemSnapshot s; s.samplingIntervalSeconds = 5; s.processorUsagePercent = 20; s.memoryUsagePercent = 60;
    s.memoryTotalBytes = 8ULL * 1024 * 1024 * 1024; s.memoryAvailableBytes = s.memoryTotalBytes * 4 / 10;
    s.foregroundProcessId = 501; s.foregroundProcessName = QStringLiteral("msedge.exe");
    ProcessSample edge; edge.name = s.foregroundProcessName; edge.processId = 501; edge.executablePath = QStringLiteral("C:/Apps/Edge/msedge.exe"); edge.cpuPercent = 10;
    ProcessSample background; background.name = QStringLiteral("render.exe"); background.processId = 502; background.executablePath = QStringLiteral("C:/Apps/Render/render.exe"); background.cpuPercent = 25;
    s.topProcesses = {edge, background};
    const auto at = [&s, base](int second) { s.capturedAt = base.addSecs(second); s.processSamplesCapturedAt = s.capturedAt; };
    const auto has = [](AutonomousCompanion& engine, const QString& key) { bool found = false; for (const auto& e : engine.takeEvents()) found = found || e.key.startsWith(key); return found; };
    UserPreferences p;
    AutonomousCompanion engine; engine.configure(p);
    at(0); engine.observe(s, false); at(5); engine.observe(s, false);
    expect(QStringLiteral("Automatic task context ignores a brief foreground visit"), engine.goal().isEmpty());
    for (int i = 10; i <= 35; i += 5) { at(i); engine.observe(s, false); }
    expect(QStringLiteral("Stable foreground use selects a task without a Keep button"), engine.goal() == edge.name);
    expect(QStringLiteral("Activity modes classify known browsers without claiming page contents"), engine.mode() == QStringLiteral("Browser session") && engine.detail().contains(QStringLiteral("cannot see")));
    expect(QStringLiteral("Game and communication modes are executable-based"), AutonomousCompanion::activityMode(QStringLiteral("GTA5.exe")) == QStringLiteral("Game session") && AutonomousCompanion::activityMode(QStringLiteral("zoom.exe")) == QStringLiteral("Communication app"));
    expect(QStringLiteral("Unknown apps receive a generic observed session"), AutonomousCompanion::activityMode(QStringLiteral("mystery.exe")) == QStringLiteral("App session"));
    expect(QStringLiteral("App patterns are not saved by default"), engine.state().value(QStringLiteral("profiles")).toArray().isEmpty() && engine.profileCount() == 0);
    const QString beforeOld = engine.status(); at(0); engine.observe(s, true);
    expect(QStringLiteral("Older timestamps cannot change the active task"), engine.status() == beforeOld);
    s.foregroundProcessName = QStringLiteral("code.exe"); s.foregroundProcessId = 700;
    for (int i = 40; i <= 65; i += 5) { at(i); engine.observe(s, false); }
    expect(QStringLiteral("Task switches produce a measured recap without a popup"), engine.goal() == s.foregroundProcessName && has(engine, QStringLiteral("recap/")));
    at(120); engine.observe(s, false);
    expect(QStringLiteral("A long gap restarts evidence instead of teaching unobserved use"), engine.detail().contains(QStringLiteral("forecast needs")) && has(engine, QStringLiteral("resume")));
    engine.resetSession(); expect(QStringLiteral("Stopping the companion clears its inferred task"), engine.goal().isEmpty());

    p.rememberActivity = true; engine.configure(p); s.foregroundProcessName = edge.name; s.foregroundProcessId = 501;
    for (int i = 0; i <= 210; i += 5) { at(i); engine.observe(s, false); }
    expect(QStringLiteral("Opted-in app baselines learn actual foreground system observations"), engine.profileCount() == 1 && engine.profileSummary().contains(QStringLiteral("readings")) && engine.detail().contains(QStringLiteral("usual system CPU")));
    s.processorUsagePercent = 85; s.memoryUsagePercent = 86; s.memoryAvailableBytes = s.memoryTotalBytes * 14 / 100;
    for (int i = 215; i <= 245; i += 5) { at(i); engine.observe(s, true); }
    expect(QStringLiteral("A sustained deviation from learned load generates contextual advice"), has(engine, QStringLiteral("unusual/")));
    at(250); engine.observe(s, false); at(255); engine.observe(s, true);
    at(260); engine.observe(s, false); at(265); engine.observe(s, true);
    expect(QStringLiteral("Repeated pressure episodes are counted once per transition"), has(engine, QStringLiteral("repeat/")) && engine.profileSummary().contains(QStringLiteral("3 pressure episodes")));
    const auto saved = engine.state(); AutonomousCompanion restored; restored.configure(p);
    expect(QStringLiteral("Local learning round trips with validated timestamps"), restored.restoreState(saved, base.addSecs(300)) && restored.profileCount() == 1);
    QJsonObject invalid = saved; invalid.insert(QStringLiteral("version"), 999);
    expect(QStringLiteral("Unknown learning file versions are rejected"), !restored.restoreState(invalid, base.addSecs(300)));
    QJsonArray profiles;
    for (int i = 0; i < 100; ++i) profiles.append(QJsonObject{{QStringLiteral("app"), QStringLiteral("app%1.exe").arg(i)}, {QStringLiteral("last"), base.toString(Qt::ISODate)}, {QStringLiteral("samples"), 40}, {QStringLiteral("cpu"), 20}, {QStringLiteral("memory"), 60}});
    invalid = {{QStringLiteral("version"), 1}, {QStringLiteral("profiles"), profiles}};
    restored.restoreState(invalid, base.addSecs(300)); expect(QStringLiteral("Stored app learning is bounded to 64 profiles"), restored.profileCount() == 64);
    restored.restoreState(invalid, base.addDays(31)); expect(QStringLiteral("App learning expires after 30 days"), restored.profileCount() == 0);
    QJsonArray corruptProfiles{QJsonObject{{QStringLiteral("app"), QStringLiteral("bad.exe")}, {QStringLiteral("last"), base.addDays(2).toString(Qt::ISODate)}, {QStringLiteral("samples"), 1}, {QStringLiteral("cpu"), 999}, {QStringLiteral("memory"), 60}}};
    invalid.insert(QStringLiteral("profiles"), corruptProfiles); restored.restoreState(invalid, base.addSecs(300));
    expect(QStringLiteral("Future or impossible learned readings are rejected"), restored.profileCount() == 0);
    const QString learningPath = QDir(directory).filePath(QStringLiteral("companion-fixture.json"));
    expect(QStringLiteral("Atomic companion persistence stays in the QA directory"), engine.save(learningPath, base.addSecs(300), true));
    restored.configure(p); expect(QStringLiteral("Companion file loads the same bounded profile"), restored.load(learningPath, base.addSecs(300)) && restored.profileCount() == 1);
    p.rememberActivity = false; restored.configure(p);
    expect(QStringLiteral("Turning learning off removes in-memory app patterns"), restored.profileCount() == 0 && restored.state().value(QStringLiteral("journal")).toArray().isEmpty());
    engine.forget(); expect(QStringLiteral("Forget clears learned profiles and the decision journal"), engine.profileCount() == 0 && engine.journalCount() == 0);
    at(300); engine.observe(s, true);
    for (int i = 0; i < 200; ++i) engine.beginAutomaticAction(background, s, edge.name);
    expect(QStringLiteral("Long decision histories remain bounded to 72 entries"), engine.journalCount() == 72 && engine.takeEvents().size() == 24);
    expect(QStringLiteral("Full decision journal exposes saved evidence as plain text"), engine.journalText().contains(QStringLiteral("Applied your background rule")));

    AutonomousCompanion forecast; forecast.configure(p);
    s.processorUsagePercent = 35;
    for (int i = 0; i <= 40; i += 5) {
        at(i); s.memoryAvailableBytes = static_cast<quint64>(1000 - i * 8) * 1024 * 1024;
        s.memoryUsagePercent = 100 * (1 - static_cast<double>(s.memoryAvailableBytes) / s.memoryTotalBytes); forecast.observe(s, false);
    }
    expect(QStringLiteral("Steady validated RAM decline produces a conditional short runway"), forecast.runway().startsWith(QStringLiteral("RAM headroom may")));
    s.memoryTotalBytes *= 2; at(45); forecast.observe(s, false);
    expect(QStringLiteral("Capacity changes invalidate the short RAM projection"), !forecast.runway().startsWith(QStringLiteral("RAM headroom may")));
    s.memoryUsagePercent.reset(); at(50); forecast.observe(s, false);
    expect(QStringLiteral("Missing memory evidence cannot produce a RAM forecast"), !forecast.runway().startsWith(QStringLiteral("RAM headroom may")));
    s.memoryTotalBytes /= 2; s.memoryUsagePercent = 60; s.memoryAvailableBytes = s.memoryTotalBytes * 4 / 10;
    s.batteryOnAcPower = false; s.batteryEstimatedSeconds = 1800; at(55); forecast.observe(s, false);
    expect(QStringLiteral("Battery session guard compares the saved work target with Windows estimates"), has(forecast, QStringLiteral("battery-goal")));
    s.batteryOnAcPower = true; s.batteryEstimatedSeconds = 30; at(60); forecast.observe(s, false);
    expect(QStringLiteral("Plugged-in sessions do not trigger the battery goal warning"), !has(forecast, QStringLiteral("battery-goal")));
    s.collectionDurationMicroseconds = 1100000;
    for (int i = 65; i <= 95; i += 5) { at(i); forecast.observe(s, false); }
    expect(QStringLiteral("Repeated Ausyn collection cost triggers economical operation"), forecast.economical());
    s.collectionDurationMicroseconds = 10000;
    for (int i = 100; i <= 160; i += 5) { at(i); forecast.observe(s, false); }
    expect(QStringLiteral("Repeated lower cost restores the chosen resource budget"), !forecast.economical());

    AutonomousCompanion attention; attention.configure(p);
    for (int i = 0; i < 3; ++i) attention.interrupted(base.addSecs(i), false);
    expect(QStringLiteral("Attention budget limits ordinary interruptions"), !attention.canInterrupt(base.addSecs(4), false));
    expect(QStringLiteral("Urgent pressure can bypass the ordinary attention budget"), attention.canInterrupt(base.addSecs(4), true));
    expect(QStringLiteral("Attention credits recover after ten minutes"), attention.canInterrupt(base.addSecs(605), false));
    attention.defer(QStringLiteral("x"), QStringLiteral("Deferred insight"));
    expect(QStringLiteral("Deferred alerts remain available in the digest"), attention.digest().contains(QStringLiteral("Deferred insight")));

    AutonomousCompanion automatic; p.automaticReliefEnabled = true; p.automaticReliefPaths = {background.executablePath}; automatic.configure(p);
    s.processorUsagePercent = 85; s.memoryUsagePercent = 85; s.memoryAvailableBytes = s.memoryTotalBytes * 15 / 100;
    for (int i = 0; i <= 35; i += 5) { at(i); automatic.observe(s, true); }
    expect(QStringLiteral("An approved busy background executable becomes eligible after sustained CPU pressure"), automatic.automaticCandidate(s, edge.name).has_value());
    expect(QStringLiteral("Exact path rules reject another executable with the same name"), !automatic.ruleAllows(QStringLiteral("C:/Different/render.exe")) && automatic.ruleAllows(QStringLiteral("C:\\Apps\\Render\\RENDER.EXE")));
    expect(QStringLiteral("Relative executable paths cannot authorize automatic actions"), !automatic.ruleAllows(QStringLiteral("render.exe")));
    expect(QStringLiteral("An explicitly protected background app is not eligible"), !automatic.automaticCandidate(s, background.name));
    s.processSamplesCapturedAt = s.capturedAt.addSecs(-60);
    expect(QStringLiteral("Stale process evidence blocks standing actions"), !automatic.automaticCandidate(s, edge.name));
    s.processSamplesCapturedAt = s.capturedAt;
    automatic.beginAutomaticAction(background, s, edge.name);
    for (int i = 40; i <= 70; i += 5) { at(i); automatic.observe(s, true); }
    expect(QStringLiteral("An action with no measured relief requests undo"), !automatic.takeUndoReason().isEmpty() && !automatic.actionPending());
    expect(QStringLiteral("Ineffective rules are cooled down before any retry"), !automatic.automaticCandidate(s, edge.name));
    automatic.endAutomaticAction(QStringLiteral("Fixture undo")); automatic.resetSession();
    for (int i = 100; i <= 135; i += 5) { at(i); automatic.observe(s, true); }
    automatic.beginAutomaticAction(background, s, edge.name); s.processorUsagePercent = 50; s.memoryUsagePercent = 80;
    for (int i = 140; i <= 170; i += 5) { at(i); automatic.observe(s, false); }
    expect(QStringLiteral("Measured lower CPU is recorded as an observed outcome, not proven causation"), automatic.takeUndoReason().isEmpty() && has(automatic, QStringLiteral("verification/")));
    automatic.beginAutomaticAction(background, s, edge.name); at(250); automatic.observe(s, false);
    expect(QStringLiteral("A telemetry gap requests undo instead of claiming successful verification"), !automatic.takeUndoReason().isEmpty());
    automatic.beginAutomaticAction(background, s, edge.name); s.foregroundProcessName = QStringLiteral("code.exe"); s.foregroundProcessId = 701; at(255); automatic.observe(s, false);
    expect(QStringLiteral("Foreground task changes end the earlier automatic action"), !automatic.takeUndoReason().isEmpty());
    p.automaticReliefEnabled = false; automatic.configure(p);
    expect(QStringLiteral("Standing actions are disabled when their toggle is off"), !automatic.ruleAllows(background.executablePath));
    p.autonomousCompanionEnabled = false; automatic.configure(p);
    expect(QStringLiteral("Disabling autonomous guidance clears its current goal"), automatic.goal().isEmpty());

    auto* popup = popupWindow(); popup->setAttribute(Qt::WA_DontShowOnScreen, true);
    popup->present(QStringLiteral("Live pressure test"), QStringLiteral("CPU 85% · RAM 90% · keeping your current app."), 1, false, false, false, true);
    QApplication::processEvents(); const QRect geometry = popup->geometry();
    auto* deadline = popup->findChild<QTimer*>(); const int remaining = deadline ? deadline->remainingTime() : -1;
    for (int i = 0; i < 120; ++i) {
        popup->present(QStringLiteral("Live pressure test"), QStringLiteral("Updated readings %1. ").arg(i) + QString(i % 2 ? 6000 : 30, QLatin1Char('x')), 1, false, false, true, true);
        QApplication::processEvents();
    }
    expect(QStringLiteral("120 live notification refreshes keep exactly the same size and position"), popup->geometry() == geometry);
    expect(QStringLiteral("Live refreshes do not extend the notification deadline"), deadline && deadline->remainingTime() <= remaining);
    expect(QStringLiteral("Notification content stays bounded and scrollable"), popup->findChild<QLabel*>(QStringLiteral("noticeBody"))->text().size() <= 8000 && popup->height() < 500);
    popup->present(QStringLiteral("Live pressure test"), QStringLiteral("CPU 82% · RAM 91% · 0.72 GB available.\n\nYou are continuing msedge.exe, so guidance follows that browser session. Approved background rules will be considered while your current app stays open.\n\nNo unrelated app will be closed. Fresh readings will verify any approved priority change."), 1, false, false, true, true);
    expect(QStringLiteral("Alert body is anchored at the top of its stable viewport"), popup->findChild<QLabel*>(QStringLiteral("noticeBody"))->alignment().testFlag(Qt::AlignTop));
    expect(QStringLiteral("Stable live popup screenshot saved"), popup->grab().save(QDir(directory).filePath(QStringLiteral("alert-stable-live.png")))); popup->hide();

    LiveActivityChart chart; chart.setAttribute(Qt::WA_DontShowOnScreen, true); chart.resize(780, 260); chart.setAnimationsEnabled(false); chart.show();
    s.foregroundProcessName = edge.name; s.foregroundProcessId = 501;
    for (int i = 0; i < 150; ++i) { at(i * 5); s.processorUsagePercent = 40 + 22 * std::sin(i / 8.0); s.memoryUsagePercent = 65 + 15 * std::sin(i / 24.0); s.graphicsUsagePercent = 12 + 10 * std::sin(i / 12.0); chart.addSnapshot(s); }
    QApplication::processEvents();
    expect(QStringLiteral("Live visual history remains bounded despite a long session"), chart.readingCount() <= 120);
    const int count = chart.readingCount(); chart.addSnapshot(s); expect(QStringLiteral("Repeated timestamps cannot invent additional chart readings"), chart.readingCount() == count);
    expect(QStringLiteral("Reduced motion suppresses chart arrival animations"), !chart.animating());
    const QPointF position(480, 120); QMouseEvent hover(QEvent::MouseMove, position, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier); QApplication::sendEvent(&chart, &hover);
    expect(QStringLiteral("Interactive actual-sample chart screenshot saved"), chart.grab().save(QDir(directory).filePath(QStringLiteral("live-chart-hover.png"))));
    chart.setAnimationsEnabled(true); at(755); chart.addSnapshot(s); chart.hide(); expect(QStringLiteral("Hidden charts stop animations immediately"), !chart.animating());
    preferences_.autonomousCompanionEnabled = true; companion_.configure(preferences_);
    s.capturedAt = QDateTime::currentDateTime(); s.processSamplesCapturedAt = s.capturedAt; latestSnapshot_ = s;
    companion_.observe(s, false); showHub(0); hubs_[0]->setDetailed(true); refreshCompanionPanel(); QApplication::processEvents();
    expect(QStringLiteral("Background companion panel renders with readable wrapped status"), companionPanel_->grab().save(QDir(directory).filePath(QStringLiteral("companion-panel.png"))) && companionStatus_->height() >= companionStatus_->heightForWidth(companionStatus_->width()));
    chatInput_->setText(QStringLiteral("brief me")); sendAssistantMessage();
    expect(QStringLiteral("Local background briefing answers preserve the user turn and clear the composer"), chatInput_->text().isEmpty() && chatMessages_.size() >= 2 && chatMessages_[chatMessages_.size() - 2].fromUser && chatMessages_[chatMessages_.size() - 2].text == QStringLiteral("brief me"));
    showHub(4); hubs_[4]->setDetailed(true); QApplication::processEvents();
    expect(QStringLiteral("Autonomous controls preserve five main sidebar destinations"), navigationButtons_.size() == 5 && companionEnabled_ && automaticReliefEnabled_ && rememberActivity_ && attentionBudget_);
    expect(QStringLiteral("Standing-rule settings screenshot saved"), companionEnabled_->parentWidget()->grab().save(QDir(directory).filePath(QStringLiteral("companion-settings.png"))));
    showHub(0); return passed;
}
}
