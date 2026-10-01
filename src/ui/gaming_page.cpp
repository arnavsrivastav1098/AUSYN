#include "gaming_page.h"

#include "../intelligence/gaming_readiness.h"

#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace Ausyn {
namespace {
QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QDoubleSpinBox* gigabyteInput(double max, QWidget* parent)
{
    auto* input = new QDoubleSpinBox(parent);
    input->setRange(0.0, max);
    input->setDecimals(1);
    input->setSingleStep(1.0);
    input->setSpecialValueText(QStringLiteral("Not included"));
    input->setSuffix(QStringLiteral(" GB"));
    input->setMinimumWidth(150);
    return input;
}
}

GamingPage::GamingPage(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 24, 30, 24);
    outer->setSpacing(13);
    auto* eyebrow = new QLabel(QStringLiteral("LOCAL REQUIREMENTS CHECK"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* title = new QLabel(QStringLiteral("Gaming readiness"), this);
    title->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("Compare detected resources with a game's minimum requirements. Enter values from the publisher; Ausyn does not guess requirements or promise frame rates."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(title);
    outer->addWidget(intro);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 8, 0);
    contentLayout->setSpacing(13);

    auto* hardware = panel(content);
    auto* hardwareLayout = new QVBoxLayout(hardware);
    hardwareLayout->setContentsMargins(18, 15, 18, 15);
    auto* hardwareTitle = new QLabel(QStringLiteral("Detected hardware"), hardware);
    hardwareTitle->setObjectName(QStringLiteral("panelTitle"));
    hardwareSummary_ = new QLabel(QStringLiteral("Waiting for the first Windows snapshot…"), hardware);
    hardwareSummary_->setObjectName(QStringLiteral("subtle"));
    hardwareSummary_->setWordWrap(true);
    hardwareLayout->addWidget(hardwareTitle);
    hardwareLayout->addWidget(hardwareSummary_);
    contentLayout->addWidget(hardware);

    auto* session = panel(content);
    auto* sessionLayout = new QVBoxLayout(session);
    sessionLayout->setContentsMargins(18, 15, 18, 15);
    auto* sessionHeader = new QHBoxLayout;
    auto* sessionTitle = new QLabel(QStringLiteral("Gaming session"), session);
    sessionTitle->setObjectName(QStringLiteral("panelTitle"));
    sessionButton_ = new QPushButton(QStringLiteral("Start session"), session);
    sessionButton_->setObjectName(QStringLiteral("primaryButton"));
    sessionHeader->addWidget(sessionTitle);
    sessionHeader->addStretch();
    sessionHeader->addWidget(sessionButton_);
    sessionSummary_ = new QLabel(QStringLiteral("Start a session before launching a game to summarize the live system readings while you play. Samples stay in memory and are cleared when you reset or close Ausyn."), session);
    sessionSummary_->setObjectName(QStringLiteral("subtle"));
    sessionSummary_->setWordWrap(true);
    sessionLayout->addLayout(sessionHeader);
    sessionLayout->addWidget(sessionSummary_);
    contentLayout->addWidget(session);

    auto* scan = panel(content);
    auto* scanLayout = new QVBoxLayout(scan);
    scanLayout->setContentsMargins(18, 15, 18, 15);
    auto* scanHeader = new QHBoxLayout;
    auto* scanTitle = new QLabel(QStringLiteral("Pre-game system scan"), scan);
    scanTitle->setObjectName(QStringLiteral("panelTitle"));
    preGameScanButton_ = new QPushButton(QStringLiteral("Scan before gaming"), scan);
    preGameScanButton_->setObjectName(QStringLiteral("primaryButton"));
    scanHeader->addWidget(scanTitle);
    scanHeader->addStretch();
    scanHeader->addWidget(preGameScanButton_);
    preGameLaunchButton_ = new QPushButton(QStringLiteral("Check & launch game"), scan);
    preGameLaunchButton_->setObjectName(QStringLiteral("primaryButton"));
    scanHeader->addWidget(preGameLaunchButton_);
    preGameScanSummary_ = new QLabel(QStringLiteral("Run a quick check before launching a game. The scan uses current Windows readings and never changes system settings."), scan);
    preGameScanSummary_->setObjectName(QStringLiteral("subtle"));
    preGameScanSummary_->setWordWrap(true);
    scanLayout->addLayout(scanHeader);
    scanLayout->addWidget(preGameScanSummary_);
    contentLayout->addWidget(scan);

    auto* requirementsPanel = panel(content);
    auto* requirementsLayout = new QVBoxLayout(requirementsPanel);
    requirementsLayout->setContentsMargins(18, 16, 18, 17);
    auto* requirementsTitle = new QLabel(QStringLiteral("Game requirements"), requirementsPanel);
    requirementsTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* helper = new QLabel(QStringLiteral("Choose a built-in profile or enter published values. Ausyn checks known resource thresholds, then adapts settings guidance to live system headroom."), requirementsPanel);
    helper->setObjectName(QStringLiteral("subtle"));
    helper->setWordWrap(true);
    auto* catalogRow = new QHBoxLayout;
    catalogProfile_ = new QComboBox(requirementsPanel);
    catalogProfile_->addItem(QStringLiteral("Auto-detect / custom profile"), QString{});
    for (const GameCatalogProfile& profile : GameCatalog::profiles())
        catalogProfile_->addItem(profile.title, profile.id);
    adaptiveMode_ = new QComboBox(requirementsPanel);
    adaptiveMode_->addItem(QStringLiteral("Ausyn Auto · adapt to this PC"), QStringLiteral("auto"));
    adaptiveMode_->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
    adaptiveMode_->addItem(QStringLiteral("Performance first"), QStringLiteral("performance"));
    adaptiveMode_->addItem(QStringLiteral("Visual quality first"), QStringLiteral("quality"));
    catalogRow->addWidget(catalogProfile_, 2);
    catalogRow->addWidget(adaptiveMode_, 1);
    launchStatus_ = new QLabel(QStringLiteral("Ausyn watches for supported game processes and reports shortly after one starts. This build cannot block a game before launch."), requirementsPanel);
    launchStatus_->setObjectName(QStringLiteral("subtle"));
    launchStatus_->setWordWrap(true);
    catalogDetails_ = new QLabel(QStringLiteral("Built-in profiles are sourced from publisher requirements. Exact CPU/GPU performance and gameplay cannot be inferred from component names alone."), requirementsPanel);
    catalogDetails_->setObjectName(QStringLiteral("subtle"));
    catalogDetails_->setWordWrap(true);
    catalogDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    auto* autoPanel = panel(requirementsPanel);
    auto* autoLayout = new QVBoxLayout(autoPanel);
    autoLayout->setContentsMargins(14, 11, 14, 11);
    autoLayout->setSpacing(5);
    auto* autoTitle = new QLabel(QStringLiteral("Live readiness & settings guidance"), autoPanel);
    autoTitle->setObjectName(QStringLiteral("panelTitle"));
    autoRecommendation_ = new QLabel(QStringLiteral("Select a game profile or use Auto. Recommendations are advisory; Ausyn never edits game settings."), autoPanel);
    autoRecommendation_->setObjectName(QStringLiteral("subtle"));
    autoRecommendation_->setWordWrap(true);
    autoLayout->addWidget(autoTitle);
    autoLayout->addWidget(autoRecommendation_);
    auto* form = new QGridLayout;
    form->setHorizontalSpacing(15);
    form->setVerticalSpacing(10);
    gameTitle_ = new QLineEdit(requirementsPanel);
    gameTitle_->setPlaceholderText(QStringLiteral("Game name (optional)"));
    requirementsSource_ = new QLineEdit(requirementsPanel);
    requirementsSource_->setPlaceholderText(QStringLiteral("Publisher requirements link or source note (optional)"));
    customGameExecutable_ = new QLineEdit(requirementsPanel);
    customGameExecutable_->setPlaceholderText(QStringLiteral("Optional: game executable name, e.g. ExampleGame.exe"));
    minimumRam_ = gigabyteInput(512.0, requirementsPanel);
    minimumVram_ = gigabyteInput(128.0, requirementsPanel);
    storageRequired_ = gigabyteInput(2048.0, requirementsPanel);
    installDrive_ = new QComboBox(requirementsPanel);
    installDrive_->setMinimumWidth(150);
    const auto addField = [form, requirementsPanel](int row, const QString& label, QWidget* field) {
        auto* text = new QLabel(label, requirementsPanel);
        form->addWidget(text, row, 0);
        form->addWidget(field, row, 1);
    };
    addField(0, QStringLiteral("System memory"), minimumRam_);
    addField(1, QStringLiteral("Dedicated graphics memory"), minimumVram_);
    addField(2, QStringLiteral("Free installation space"), storageRequired_);
    addField(3, QStringLiteral("Install drive"), installDrive_);
    addField(4, QStringLiteral("Requirements source"), requirementsSource_);
    addField(5, QStringLiteral("Auto-detect executable"), customGameExecutable_);
    auto* compare = new QPushButton(QStringLiteral("Compare requirements"), requirementsPanel);
    compare->setObjectName(QStringLiteral("primaryButton"));
    compare->setMinimumHeight(40);
    requirementsLayout->addWidget(requirementsTitle);
    requirementsLayout->addWidget(helper);
    requirementsLayout->addLayout(catalogRow);
    requirementsLayout->addWidget(launchStatus_);
    requirementsLayout->addWidget(catalogDetails_);
    requirementsLayout->addWidget(autoPanel);
    requirementsLayout->addLayout(form);
    requirementsLayout->addWidget(compare, 0, Qt::AlignLeft);
    auto* profileControls = new QHBoxLayout;
    savedProfiles_ = new QComboBox(requirementsPanel);
    savedProfiles_->setMinimumWidth(220);
    savedProfiles_->setPlaceholderText(QStringLiteral("Saved local game profiles"));
    auto* loadProfile = new QPushButton(QStringLiteral("Load"), requirementsPanel);
    auto* saveProfile = new QPushButton(QStringLiteral("Save current"), requirementsPanel);
    auto* removeProfile = new QPushButton(QStringLiteral("Delete"), requirementsPanel);
    loadProfile->setObjectName(QStringLiteral("secondaryButton"));
    saveProfile->setObjectName(QStringLiteral("secondaryButton"));
    removeProfile->setObjectName(QStringLiteral("secondaryButton"));
    profileControls->addWidget(savedProfiles_, 1);
    profileControls->addWidget(loadProfile);
    profileControls->addWidget(saveProfile);
    profileControls->addWidget(removeProfile);
    profileStatus_ = new QLabel(QStringLiteral("Profiles are kept on this PC. Requirements and sources are user-entered and are not checked by Ausyn."), requirementsPanel);
    profileStatus_->setObjectName(QStringLiteral("subtle"));
    profileStatus_->setWordWrap(true);
    requirementsLayout->addLayout(profileControls);
    requirementsLayout->addWidget(profileStatus_);
    contentLayout->addWidget(requirementsPanel);

    auto* result = panel(content);
    auto* resultLayout = new QVBoxLayout(result);
    resultLayout->setContentsMargins(18, 16, 18, 16);
    auto* resultHeader = new QHBoxLayout;
    resultTitle_ = new QLabel(QStringLiteral("Readiness report"), result);
    resultTitle_->setObjectName(QStringLiteral("panelTitle"));
    resultConfidence_ = new QLabel(QStringLiteral("AWAITING REQUIREMENTS"), result);
    resultConfidence_->setStyleSheet(QStringLiteral("color:#a9a8ff;background:#20253a;border:1px solid #363957;border-radius:8px;padding:6px 9px;font-size:10px;font-weight:700;letter-spacing:1px"));
    resultHeader->addWidget(resultTitle_);
    resultHeader->addStretch();
    resultHeader->addWidget(resultConfidence_);
    resultSummary_ = new QLabel(QStringLiteral("Enter minimum requirements to compare them with this PC."), result);
    resultSummary_->setWordWrap(true);
    resultSummary_->setStyleSheet(QStringLiteral("color:#aeb9cb;font-size:13px"));
    checksContainer_ = new QWidget(result);
    auto* checksLayout = new QVBoxLayout(checksContainer_);
    checksLayout->setContentsMargins(0, 5, 0, 0);
    resultLayout->addLayout(resultHeader);
    resultLayout->addWidget(resultSummary_);
    resultLayout->addWidget(checksContainer_);
    contentLayout->addWidget(result);

    auto* footnote = new QLabel(QStringLiteral("This check compares memory capacity, reported dedicated VRAM, and free space only. It does not inspect publisher APIs, CPU instruction compatibility, anti-cheat support, game settings, temperatures, or expected FPS."), content);
    footnote->setObjectName(QStringLiteral("subtle"));
    footnote->setWordWrap(true);
    contentLayout->addWidget(footnote);
    contentLayout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    connect(compare, &QPushButton::clicked, this, &GamingPage::refreshAssessment);
    connect(loadProfile, &QPushButton::clicked, this, &GamingPage::loadSelectedProfile);
    connect(saveProfile, &QPushButton::clicked, this, &GamingPage::saveCurrentProfile);
    connect(removeProfile, &QPushButton::clicked, this, &GamingPage::deleteSelectedProfile);
    connect(catalogProfile_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) applyCatalogProfile(catalogProfile_->itemData(index).toString());
    });
    connect(adaptiveMode_, qOverload<int>(&QComboBox::currentIndexChanged), this, &GamingPage::updateAdaptiveGuidance);
    connect(savedProfiles_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, loadProfile, removeProfile](int index) {
        const bool selected = index >= 0;
        loadProfile->setEnabled(selected);
        removeProfile->setEnabled(selected);
    });
    reloadProfiles();
    connect(preGameScanButton_, &QPushButton::clicked, this, &GamingPage::runPreGameScan);
    connect(preGameLaunchButton_, &QPushButton::clicked,
            this, &GamingPage::launchGameWithReadinessCheck);
    connect(sessionButton_, &QPushButton::clicked, this, [this] {
        if (sessionActive_) {
            sessionActive_ = false;
            sessionButton_->setText(QStringLiteral("Start new session"));
        } else {
            sessionActive_ = true;
            sessionStartedAt_ = QDateTime::currentDateTime();
            lastSessionSampleAt_ = {};
            cpuSum_ = memorySum_ = gpuSum_ = 0.0;
            cpuPeak_ = memoryPeak_ = gpuPeak_ = 0.0;
            cpuSampleCount_ = memorySampleCount_ = gpuSampleCount_ = 0;
            thermalPeakCelsius_ = 0.0;
            thermalPeakSensor_.clear();
            thermalSampleCount_ = thermalNearPassiveCount_ = 0;
            sessionButton_->setText(QStringLiteral("End session"));
        }
        updateSessionSummary();
    });
    const auto clearCatalogForEdits = [this] {
        if (!activeGameProfile_) return;
        activeGameProfile_ = nullptr;
        const QSignalBlocker blocker(catalogProfile_);
        catalogProfile_->setCurrentIndex(0);
        catalogDetails_->setText(QStringLiteral("This is now a custom profile based on edited values. Check the publisher source yourself."));
    };
    connect(gameTitle_, &QLineEdit::textChanged, this, [this, clearCatalogForEdits] {
        clearCatalogForEdits();
        refreshAssessment();
    });
    connect(minimumRam_, &QDoubleSpinBox::valueChanged, this, [this, clearCatalogForEdits] { clearCatalogForEdits(); refreshAssessment(); });
    connect(minimumVram_, &QDoubleSpinBox::valueChanged, this, [this, clearCatalogForEdits] { clearCatalogForEdits(); refreshAssessment(); });
    connect(storageRequired_, &QDoubleSpinBox::valueChanged, this, [this, clearCatalogForEdits] { clearCatalogForEdits(); refreshAssessment(); });
    connect(requirementsSource_, &QLineEdit::textChanged, this, [clearCatalogForEdits] { clearCatalogForEdits(); });
    connect(installDrive_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { refreshAssessment(); });
}

GameRequirements GamingPage::requirements() const
{
    GameRequirements value;
    value.title = gameTitle_->text();
    value.minimumRamGb = minimumRam_->value();
    value.minimumVramGb = minimumVram_->value();
    value.requiredFreeStorageGb = storageRequired_->value();
    value.driveRoot = installDrive_->currentData().toString();
    return value;
}

void GamingPage::reloadProfiles()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("GameProfiles"));
    const QStringList names = settings.childGroups();
    customGameExeToProfile_.clear();
    for (const QString& name : names) {
        settings.beginGroup(name);
        const QString executable = settings.value(QStringLiteral("executable")).toString().trimmed().toCaseFolded();
        settings.endGroup();
        if (!executable.isEmpty()) customGameExeToProfile_.insert(executable, name);
    }
    settings.endGroup();
    const QSignalBlocker blocker(savedProfiles_);
    savedProfiles_->clear();
    for (const QString& name : names) savedProfiles_->addItem(name, name);
}

void GamingPage::loadSelectedProfile()
{
    const QString name = savedProfiles_->currentData().toString();
    if (name.isEmpty()) return;
    QSettings settings;
    settings.beginGroup(QStringLiteral("GameProfiles/%1").arg(name));
    gameTitle_->setText(name);
    minimumRam_->setValue(settings.value(QStringLiteral("minimumRamGb")).toDouble());
    minimumVram_->setValue(settings.value(QStringLiteral("minimumVramGb")).toDouble());
    storageRequired_->setValue(settings.value(QStringLiteral("requiredFreeStorageGb")).toDouble());
    const QString drive = settings.value(QStringLiteral("driveRoot")).toString();
    const int driveIndex = installDrive_->findData(drive);
    if (driveIndex >= 0) installDrive_->setCurrentIndex(driveIndex);
    requirementsSource_->setText(settings.value(QStringLiteral("source")).toString());
    customGameExecutable_->setText(settings.value(QStringLiteral("executable")).toString());
    const QString savedAt = settings.value(QStringLiteral("savedAt")).toString();
    settings.endGroup();
    activeGameProfile_ = nullptr;
    catalogProfile_->setCurrentIndex(0);
    catalogDetails_->setText(QStringLiteral("Loaded your saved local requirements profile. Ausyn has not checked the entered source or requirements against the publisher."));
    profileStatus_->setText(QStringLiteral("Loaded local profile · saved %1. Ausyn has not checked whether its requirements or source are current.").arg(savedAt));
    refreshAssessment();
}

void GamingPage::saveCurrentProfile()
{
    const QString name = gameTitle_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Name this profile"),
            QStringLiteral("Enter a game name before saving its requirements."));
        gameTitle_->setFocus();
        return;
    }
    static const QRegularExpression profileNamePattern(QStringLiteral("^[^/\\\\]{1,80}$"));
    if (!profileNamePattern.match(name).hasMatch() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        QMessageBox::information(this, QStringLiteral("Check profile name"),
            QStringLiteral("Use a profile name up to 80 characters without slashes."));
        gameTitle_->setFocus();
        return;
    }
    const QString executable = customGameExecutable_->text().trimmed();
    static const QRegularExpression executablePattern(
        QStringLiteral("^[A-Za-z0-9_-][A-Za-z0-9._ -]{0,126}\\.exe$"),
        QRegularExpression::CaseInsensitiveOption);
    if (!executable.isEmpty() && !executablePattern.match(executable).hasMatch()) {
        QMessageBox::information(this, QStringLiteral("Check executable name"),
            QStringLiteral("Enter only the executable filename, such as ExampleGame.exe. Paths and command-line arguments are not accepted."));
        customGameExecutable_->setFocus();
        return;
    }
    if (!executable.isEmpty()) {
        const QString owner = customGameExeToProfile_.value(executable.toCaseFolded());
        if (!owner.isEmpty() && owner != name) {
            QMessageBox::information(this, QStringLiteral("Executable already assigned"),
                QStringLiteral("%1 already identifies the saved profile “%2”. Delete or edit that profile before assigning the executable elsewhere.")
                    .arg(executable, owner));
            customGameExecutable_->setFocus();
            return;
        }
    }
    QSettings settings;
    settings.beginGroup(QStringLiteral("GameProfiles/%1").arg(name));
    settings.setValue(QStringLiteral("minimumRamGb"), minimumRam_->value());
    settings.setValue(QStringLiteral("minimumVramGb"), minimumVram_->value());
    settings.setValue(QStringLiteral("requiredFreeStorageGb"), storageRequired_->value());
    settings.setValue(QStringLiteral("driveRoot"), installDrive_->currentData().toString());
    settings.setValue(QStringLiteral("source"), requirementsSource_->text().trimmed());
    settings.setValue(QStringLiteral("executable"), executable);
    settings.setValue(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        profileStatus_->setText(QStringLiteral("Could not save the profile to local settings. Check the Windows user profile's available storage and permissions."));
        return;
    }
    reloadProfiles();
    savedProfiles_->setCurrentIndex(savedProfiles_->findData(name));
    profileStatus_->setText(QStringLiteral("Saved “%1” locally. Source and requirements are user-provided; check the publisher page for changes.").arg(name));
}

void GamingPage::deleteSelectedProfile()
{
    const QString name = savedProfiles_->currentData().toString();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("Delete game profile"),
        QStringLiteral("Delete the saved local profile “%1”? This cannot be undone.").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    QSettings settings;
    settings.remove(QStringLiteral("GameProfiles/%1").arg(name));
    reloadProfiles();
    profileStatus_->setText(QStringLiteral("Deleted the local profile “%1”.").arg(name));
}

GameReadiness GamingPage::currentAssessment() const
{
    return GamingReadinessEngine::analyze(requirements(), snapshot_);
}

bool GamingPage::selectCatalogProfileForQuestion(const QString& question)
{
    const QString q = question.toCaseFolded();
    QString profileId;
    if (q.contains(QStringLiteral("gta5")) || q.contains(QStringLiteral("gta 5")) ||
        q.contains(QStringLiteral("gta v")) || q.contains(QStringLiteral("grand theft auto v")) ||
        q.contains(QStringLiteral("grand theft auto 5"))) {
        profileId = q.contains(QStringLiteral("enhanced"))
            ? QStringLiteral("gtav_enhanced") : QStringLiteral("gtav_legacy");
    } else if (q.contains(QStringLiteral("fortnite"))) {
        profileId = QStringLiteral("fortnite");
    } else if (q.contains(QStringLiteral("cyberpunk"))) {
        profileId = QStringLiteral("cyberpunk2077");
    } else if (q.contains(QStringLiteral("apex legends"))) {
        profileId = QStringLiteral("apex_legends");
    } else if (q.contains(QStringLiteral("counter-strike 2")) || q.contains(QStringLiteral("counter strike 2")) ||
               q.contains(QStringLiteral("cs2"))) {
        profileId = QStringLiteral("counter_strike_2");
    }
    const int index = catalogProfile_->findData(profileId);
    if (profileId.isEmpty() || index <= 0) return false;
    if (installDrive_->count() == 0 && !snapshot_.volumes.isEmpty()) {
        const QSignalBlocker blocker(installDrive_);
        for (const VolumeSample& volume : snapshot_.volumes) {
            installDrive_->addItem(QStringLiteral("%1 · %2 GB free").arg(volume.rootPath)
                .arg(static_cast<double>(volume.freeBytes) / 1'000'000'000.0, 0, 'f', 1), volume.rootPath);
        }
        const int systemDrive = installDrive_->findData(snapshot_.systemVolumePath);
        if (systemDrive >= 0) installDrive_->setCurrentIndex(systemDrive);
    }
    if (catalogProfile_->currentIndex() != index) catalogProfile_->setCurrentIndex(index);
    return true;
}

void GamingPage::setSnapshot(const SystemSnapshot& snapshot)
{
    snapshot_ = snapshot;
    appendSessionSample(snapshot);
    const QDateTime now = QDateTime::currentDateTime();
    if (snapshot.processSamplesCapturedAt.isValid() && snapshot.processSamplesCapturedAt != lastAdaptiveSampleAt_ &&
        snapshot.processSamplesCapturedAt.msecsTo(now) >= 0 && snapshot.processSamplesCapturedAt.msecsTo(now) <= 20'000) {
        lastAdaptiveSampleAt_ = snapshot.processSamplesCapturedAt;
        const auto appendRecent = [](QVector<double>& series, const std::optional<double>& value) {
            if (!value || !std::isfinite(*value) || *value < 0.0 || *value > 100.0) return;
            series.append(*value);
            while (series.size() > 6) series.removeFirst();
        };
        appendRecent(recentCpuLoad_, snapshot.processorUsagePercent);
        appendRecent(recentMemoryLoad_, snapshot.memoryUsagePercent);
        appendRecent(recentGpuLoad_, snapshot.graphicsUsagePercent);
        double thermalRatio = 0.0;
        for (const ThermalSensorSample& sensor : snapshot.thermalSensors) {
            if (sensor.passiveTripPointCelsius && *sensor.passiveTripPointCelsius > 0.0 &&
                std::isfinite(sensor.temperatureCelsius))
                thermalRatio = std::max(thermalRatio, sensor.temperatureCelsius / *sensor.passiveTripPointCelsius);
        }
        if (thermalRatio > 0.0) {
            recentThermalTripRatio_.append(thermalRatio);
            while (recentThermalTripRatio_.size() > 6) recentThermalTripRatio_.removeFirst();
        }
    }
    if (launchMonitoringEnabled_) detectLaunchedGames();
    if (!isVisible()) return;
    updateAdaptiveGuidance();
    bool driveListChanged = installDrive_->count() != snapshot.volumes.size();
    for (qsizetype i = 0; !driveListChanged && i < snapshot.volumes.size(); ++i) {
        const auto& volume = snapshot.volumes.at(i);
        const QString text = QStringLiteral("%1 · %2 GB free").arg(volume.rootPath)
            .arg(static_cast<double>(volume.freeBytes) / 1'000'000'000.0, 0, 'f', 1);
        driveListChanged = installDrive_->itemData(static_cast<int>(i)).toString() != volume.rootPath ||
                           installDrive_->itemText(static_cast<int>(i)) != text;
    }
    if (driveListChanged) {
        const QString previous = installDrive_->currentData().toString();
        installDrive_->blockSignals(true);
        installDrive_->clear();
        for (const auto& volume : snapshot.volumes) {
            installDrive_->addItem(QStringLiteral("%1 · %2 GB free").arg(volume.rootPath)
                .arg(static_cast<double>(volume.freeBytes) / 1'000'000'000.0, 0, 'f', 1), volume.rootPath);
        }
        const int index = installDrive_->findData(previous);
        if (index >= 0) installDrive_->setCurrentIndex(index);
        installDrive_->blockSignals(false);
    }
    const QString cpu = snapshot.processorName.isEmpty() ? QStringLiteral("not reported") : snapshot.processorName;
    const QString gpu = snapshot.graphicsName.isEmpty() ? QStringLiteral("not reported") : snapshot.graphicsName;
    hardwareSummary_->setText(QStringLiteral("CPU: %1\nGraphics: %2\nInstalled memory: %3 GB · Reported dedicated graphics memory: %4 GB")
        .arg(cpu, gpu)
        .arg(static_cast<double>(snapshot.memoryTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
        .arg(static_cast<double>(snapshot.graphicsMemoryBytes) / 1'000'000'000.0, 0, 'f', 1));
    refreshAssessment();
}

void GamingPage::applyCatalogProfile(const QString& id)
{
    activeGameProfile_ = GameCatalog::find(id);
    if (!activeGameProfile_) {
        catalogDetails_->setText(QStringLiteral("Auto-detect watches for supported games. You can also enter a custom profile below. Game recognition uses executable names, not file contents."));
        updateAdaptiveGuidance();
        refreshAssessment();
        return;
    }
    const QSignalBlocker titleBlocker(gameTitle_);
    const QSignalBlocker ramBlocker(minimumRam_);
    const QSignalBlocker vramBlocker(minimumVram_);
    const QSignalBlocker storageBlocker(storageRequired_);
    const QSignalBlocker sourceBlocker(requirementsSource_);
    gameTitle_->setText(activeGameProfile_->title);
    minimumRam_->setValue(activeGameProfile_->minimumRamGb);
    minimumVram_->setValue(activeGameProfile_->minimumVramGb);
    storageRequired_->setValue(activeGameProfile_->minimumStorageGb);
    requirementsSource_->setText(activeGameProfile_->sourceUrl);
    const QString recRam = activeGameProfile_->recommendedRamGb > 0.0
        ? QStringLiteral("%1 GB RAM").arg(activeGameProfile_->recommendedRamGb, 0, 'f', 0) : QStringLiteral("not stated");
    const QString recVram = activeGameProfile_->recommendedVramGb > 0.0
        ? QStringLiteral("%1 GB dedicated VRAM").arg(activeGameProfile_->recommendedVramGb, 0, 'f', 0) : QStringLiteral("not stated");
    const QStringList details{
        QStringLiteral("Publisher minimum: %1 GB RAM · %2 VRAM · %3 GB storage")
            .arg(activeGameProfile_->minimumRamGb, 0, 'f', 0)
            .arg(activeGameProfile_->minimumVramGb > 0.0
                ? QStringLiteral("%1 GB").arg(activeGameProfile_->minimumVramGb, 0, 'f', 0) : QStringLiteral("not specified"))
            .arg(activeGameProfile_->minimumStorageGb, 0, 'f', 0),
        QStringLiteral("Publisher recommended: %1 · %2").arg(recRam, recVram),
        QStringLiteral("CPU minimum: %1 · recommended: %2").arg(activeGameProfile_->minimumCpu, activeGameProfile_->recommendedCpu),
        QStringLiteral("GPU minimum: %1 · recommended: %2").arg(activeGameProfile_->minimumGpu, activeGameProfile_->recommendedGpu),
        QStringLiteral("Source checked %1: %2").arg(activeGameProfile_->sourceCheckedDate.toString(Qt::ISODate), activeGameProfile_->sourceUrl),
        QStringLiteral("GPU/CPU model equivalence is not automatically scored; meeting measurable resource values does not guarantee playability or FPS.")
    };
    catalogDetails_->setText(details.join(QLatin1Char('\n')));
    refreshAssessment();
    updateAdaptiveGuidance();
}

void GamingPage::detectLaunchedGames()
{
    if (!snapshot_.processSamplesCapturedAt.isValid() ||
        snapshot_.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) > 20'000 ||
        snapshot_.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) < 0) return;

    QSet<quint32> currentIds;
    QSet<QString> currentGameIds;
    const GameCatalogProfile* newlyLaunched = nullptr;
    QString newlyLaunchedCustomProfile;
    QString newlyLaunchedTitle;
    for (const ProcessSample& process : snapshot_.topProcesses) {
        const GameCatalogProfile* profile = GameCatalog::findExecutable(process.name);
        const QString customName = customGameExeToProfile_.value(process.name.trimmed().toCaseFolded());
        if ((!profile && customName.isEmpty()) || process.processId == 0) continue;
        currentIds.insert(process.processId);
        const QString gameId = profile ? profile->id : QStringLiteral("custom:%1").arg(customName);
        currentGameIds.insert(gameId);
        if (!observedGameProcessIds_.contains(process.processId) && !activeCatalogGameIds_.contains(gameId)) {
            newlyLaunched = profile;
            newlyLaunchedCustomProfile = profile ? QString{} : customName;
            newlyLaunchedTitle = profile ? profile->title : customName;
        }
    }
    observedGameProcessIds_ = std::move(currentIds);
    activeCatalogGameIds_ = std::move(currentGameIds);
    if (!newlyLaunched && newlyLaunchedCustomProfile.isEmpty()) return;

    if (newlyLaunched) {
        const int profileIndex = catalogProfile_->findData(newlyLaunched->id);
        if (profileIndex >= 0 && catalogProfile_->currentIndex() != profileIndex)
            catalogProfile_->setCurrentIndex(profileIndex);
    } else {
        const int profileIndex = savedProfiles_->findData(newlyLaunchedCustomProfile);
        if (profileIndex >= 0) {
            savedProfiles_->setCurrentIndex(profileIndex);
            loadSelectedProfile();
        }
    }
    const GameReadiness readiness = currentAssessment();
    QString risk;
    if (readiness.failedChecks > 0) {
        risk = QStringLiteral("One or more measured resource minimums are below the publisher values; the game may run poorly or fail to launch.");
    } else if (readiness.knownChecks < readiness.checks.size() || readiness.checks.isEmpty()) {
        risk = QStringLiteral("Ausyn cannot measure enough listed minimums to give a complete readiness result. CPU/GPU model equivalence is also not assessed.");
    } else {
        risk = QStringLiteral("Measured RAM/VRAM/storage checks meet the listed minimums, but CPU/GPU model equivalence is not assessed, so this is not a guarantee it can run well.");
    }
    if (newlyLaunched && snapshot_.memoryTotalBytes > 0 && newlyLaunched->recommendedRamGb > 0.0 &&
        static_cast<double>(snapshot_.memoryTotalBytes) / 1'000'000'000.0 < newlyLaunched->recommendedRamGb)
        risk += QStringLiteral(" Installed RAM is below the publisher's recommended tier, so lower settings or stutter may be more likely.");
    if (newlyLaunched && snapshot_.graphicsMemoryBytes > 0 && newlyLaunched->recommendedVramGb > 0.0 &&
        static_cast<double>(snapshot_.graphicsMemoryBytes) / 1'000'000'000.0 < newlyLaunched->recommendedVramGb)
        risk += QStringLiteral(" Reported dedicated graphics memory is below the publisher's recommended tier.");
    QStringList livePressure;
    if (snapshot_.processorUsagePercent && *snapshot_.processorUsagePercent >= 85.0)
        livePressure << QStringLiteral("whole-system CPU is %1% busy").arg(*snapshot_.processorUsagePercent, 0, 'f', 0);
    if (snapshot_.memoryUsagePercent && *snapshot_.memoryUsagePercent >= 85.0)
        livePressure << QStringLiteral("memory use is %1%").arg(*snapshot_.memoryUsagePercent, 0, 'f', 0);
    if (snapshot_.graphicsUsagePercent && *snapshot_.graphicsUsagePercent >= 85.0)
        livePressure << QStringLiteral("graphics engine use is %1%").arg(*snapshot_.graphicsUsagePercent, 0, 'f', 0);
    if (!livePressure.isEmpty())
        risk += QStringLiteral(" Current system pressure (%1) raises the chance of stutter or low frame rates.").arg(livePressure.join(QStringLiteral("; ")));
    const QString message = QStringLiteral("%1 %2 Auto settings guidance is on the Gaming readiness page.").arg(newlyLaunchedTitle, risk);
    launchStatus_->setText(QStringLiteral("Detected just after launch · %1\n%2\nIf you keep playing, this PC may have extra heat/fan noise, stutter, or low frame rates under sustained load. These are conditional risks, not promises. Ausyn only has direct temperature evidence when Windows exposes a usable sensor.")
        .arg(newlyLaunchedTitle, risk));
    emit gameLaunchDetected(newlyLaunchedTitle, message);
}

void GamingPage::updateAdaptiveGuidance()
{
    if (!autoRecommendation_) return;
    const QString mode = adaptiveMode_ ? adaptiveMode_->currentData().toString() : QStringLiteral("auto");
    const double cpu = snapshot_.processorUsagePercent.value_or(-1.0);
    const double memory = snapshot_.memoryUsagePercent.value_or(-1.0);
    const double gpu = snapshot_.graphicsUsagePercent.value_or(-1.0);
    const int cpuCount = cpu >= 0.0 ? recentCpuLoad_.size() : 0;
    const double cpuAverage = recentCpuLoad_.isEmpty() ? cpu :
        std::accumulate(recentCpuLoad_.cbegin(), recentCpuLoad_.cend(), 0.0) / recentCpuLoad_.size();
    const double memoryAverage = recentMemoryLoad_.isEmpty() ? memory :
        std::accumulate(recentMemoryLoad_.cbegin(), recentMemoryLoad_.cend(), 0.0) / recentMemoryLoad_.size();
    const double gpuAverage = recentGpuLoad_.isEmpty() ? gpu :
        std::accumulate(recentGpuLoad_.cbegin(), recentGpuLoad_.cend(), 0.0) / recentGpuLoad_.size();
    const bool thermalRisk = std::any_of(recentThermalTripRatio_.cbegin(), recentThermalTripRatio_.cend(),
        [](double ratio) { return ratio >= 0.9; });
    const bool belowMeasuredMinimum = activeGameProfile_ && currentAssessment().failedChecks > 0;
    const bool belowRecommendedMemory = activeGameProfile_ && snapshot_.memoryTotalBytes > 0 &&
        static_cast<double>(snapshot_.memoryTotalBytes) / 1'000'000'000.0 < activeGameProfile_->recommendedRamGb;
    const bool belowRecommendedVram = activeGameProfile_ && snapshot_.graphicsMemoryBytes > 0 &&
        activeGameProfile_->recommendedVramGb > 0.0 &&
        static_cast<double>(snapshot_.graphicsMemoryBytes) / 1'000'000'000.0 < activeGameProfile_->recommendedVramGb;
    QString recommendation;
    if (mode == QStringLiteral("performance")) {
        recommendation = QStringLiteral("Performance first: use the Low preset, disable ray tracing if present, lower shadows/reflections, and reduce render scale if frames remain uneven.");
    } else if (mode == QStringLiteral("quality")) {
        recommendation = QStringLiteral("Visual quality first: begin at Medium, keep texture quality within available VRAM, then raise one option at a time while watching smoothness and heat.");
    } else if (mode == QStringLiteral("balanced")) {
        recommendation = QStringLiteral("Balanced: start at Medium, cap frame rate near the display refresh rate, and adjust resolution or shadows only if play is uneven.");
    } else if (belowMeasuredMinimum || cpuAverage >= 90.0 || gpuAverage >= 95.0 || memoryAverage >= 92.0 || thermalRisk) {
        recommendation = QStringLiteral("Auto sees very high current pressure%1: choose Low/performance mode, disable ray tracing, lower resolution/render scale and shadows, and cap frame rate. If Windows thermal-zone readings are near their reported limit, pause and let the laptop cool.")
            .arg(thermalRisk ? QStringLiteral(" or a thermal-zone reading near its firmware threshold") : QString{});
    } else if (gpuAverage >= 80.0 && cpuAverage < 85.0 && memoryAverage < 88.0) {
        recommendation = QStringLiteral("Auto sees graphics pressure (latest/short-window GPU %1%%): lower render scale, ray tracing, reflections, and shadows first; keep textures within the game's VRAM budget.").arg(gpuAverage, 0, 'f', 0);
    } else if (cpuAverage >= 80.0 || memoryAverage >= 82.0) {
        recommendation = QStringLiteral("Auto sees CPU/memory pressure (CPU %1%% · memory %2%%): reduce view distance/crowd density, close only background apps you choose, and cap frame rate.").arg(cpuAverage, 0, 'f', 0).arg(memoryAverage, 0, 'f', 0);
    } else if (cpuAverage >= 0.0 && memoryAverage >= 0.0) {
        recommendation = QStringLiteral("Auto sees current system headroom (CPU %1%% · memory %2%%%3). Start at Medium; raise settings gradually. Ausyn adapts this advice to current load, not FPS.")
            .arg(cpuAverage, 0, 'f', 0).arg(memoryAverage, 0, 'f', 0)
            .arg(gpuAverage >= 0.0 ? QStringLiteral(" · GPU %1%%").arg(gpuAverage, 0, 'f', 0) : QString{});
    } else {
        recommendation = QStringLiteral("Auto is waiting for fresh system readings. Recommendations are advisory: Ausyn does not edit game files, apply graphics settings, close apps, or guarantee FPS.");
    }
    if (activeGameProfile_) {
        const bool strained = belowMeasuredMinimum || belowRecommendedMemory || belowRecommendedVram ||
            cpuAverage >= 80.0 || gpuAverage >= 80.0 || memoryAverage >= 82.0 || thermalRisk;
        recommendation += QStringLiteral("\n%1 starting point: %2")
            .arg(activeGameProfile_->title, strained
                ? activeGameProfile_->performanceGuidance : activeGameProfile_->balancedGuidance);
        if (snapshot_.memoryTotalBytes > 0 && activeGameProfile_->recommendedRamGb > 0.0 &&
            static_cast<double>(snapshot_.memoryTotalBytes) / 1'000'000'000.0 < activeGameProfile_->recommendedRamGb)
            recommendation += QStringLiteral(" This PC has less RAM than the publisher's recommended amount.");
        if (snapshot_.graphicsMemoryBytes > 0 && activeGameProfile_->recommendedVramGb > 0.0 &&
            static_cast<double>(snapshot_.graphicsMemoryBytes) / 1'000'000'000.0 < activeGameProfile_->recommendedVramGb)
            recommendation += QStringLiteral(" Windows reports less dedicated graphics memory than the publisher's recommended amount.");
        if (belowMeasuredMinimum)
            recommendation += QStringLiteral(" At least one measurable publisher minimum is unmet; choose the lowest settings while recognizing CPU/GPU model compatibility is unknown.");
    }
    if (mode == QStringLiteral("auto") && snapshot_.processSamplesCapturedAt.isValid() && cpuCount < 3)
        recommendation += QStringLiteral(" Building a short live baseline (%1/3 fresh readings). Treat this first suggestion as provisional.").arg(cpuCount);
    recommendation += QStringLiteral(" Readings are system-wide. Ausyn does not read the game's FPS counter or apply settings automatically.");
    autoRecommendation_->setText(recommendation);
}

void GamingPage::appendSessionSample(const SystemSnapshot& snapshot)
{
    if (!sessionActive_ || !snapshot.capturedAt.isValid() || snapshot.capturedAt <= lastSessionSampleAt_ ||
        snapshot.capturedAt < sessionStartedAt_) return;
    lastSessionSampleAt_ = snapshot.capturedAt;
    const auto add = [](const std::optional<double>& value, double& sum, double& peak, int& count) {
        if (!value || !std::isfinite(*value)) return;
        sum += *value;
        peak = count == 0 ? *value : std::max(peak, *value);
        ++count;
    };
    add(snapshot.processorUsagePercent, cpuSum_, cpuPeak_, cpuSampleCount_);
    add(snapshot.memoryUsagePercent, memorySum_, memoryPeak_, memorySampleCount_);
    add(snapshot.graphicsUsagePercent, gpuSum_, gpuPeak_, gpuSampleCount_);
    const ThermalSensorSample* hottest = nullptr;
    for (const ThermalSensorSample& sensor : snapshot.thermalSensors) {
        if (!std::isfinite(sensor.temperatureCelsius)) continue;
        if (!hottest || sensor.temperatureCelsius > hottest->temperatureCelsius) hottest = &sensor;
    }
    if (hottest) {
        if (thermalSampleCount_ == 0 || hottest->temperatureCelsius > thermalPeakCelsius_) {
            thermalPeakCelsius_ = hottest->temperatureCelsius;
            thermalPeakSensor_ = hottest->name.isEmpty() ? QStringLiteral("Windows thermal zone") : hottest->name;
        }
        ++thermalSampleCount_;
        if (hottest->passiveTripPointCelsius && *hottest->passiveTripPointCelsius > 0.0 &&
            hottest->temperatureCelsius >= *hottest->passiveTripPointCelsius * 0.9) {
            ++thermalNearPassiveCount_;
        }
    }
    updateSessionSummary();
}

void GamingPage::updateSessionSummary()
{
    if (!sessionStartedAt_.isValid()) return;
    const QDateTime endedAt = sessionActive_ ? QDateTime::currentDateTime() : lastSessionSampleAt_;
    const qint64 seconds = std::max<qint64>(0, sessionStartedAt_.secsTo(endedAt));
    const QString duration = seconds >= 3600
        ? QStringLiteral("%1 h %2 min").arg(seconds / 3600).arg((seconds / 60) % 60)
        : QStringLiteral("%1 min %2 sec").arg(seconds / 60).arg(seconds % 60);
    const auto metric = [](const QString& name, double sum, double peak, int count) {
        return count == 0 ? QStringLiteral("%1 unavailable").arg(name)
            : QStringLiteral("%1 average %2%, peak %3% across %4 samples")
                .arg(name).arg(sum / count, 0, 'f', 1).arg(peak, 0, 'f', 1).arg(count);
    };
    const QString state = sessionActive_ ? QStringLiteral("Session in progress") : QStringLiteral("Session ended");
    const QString thermal = thermalSampleCount_ == 0
        ? QStringLiteral("Windows thermal zones unavailable during this session")
        : QStringLiteral("Windows thermal-zone peak %1 °C (%2; %3 readings)%4")
            .arg(thermalPeakCelsius_, 0, 'f', 1).arg(thermalPeakSensor_).arg(thermalSampleCount_)
            .arg(thermalNearPassiveCount_ > 0
                     ? QStringLiteral(" · %1 readings were near a reported passive trip point")
                           .arg(thermalNearPassiveCount_)
                     : QString{});
    sessionSummary_->setText(QStringLiteral("%1 · %2\n%3\n%4\n%5\n%6\n\nThermal readings are Windows ACPI zones and may not reflect CPU/GPU die temperature. Near-trip counts use a trip point only when Windows reports one. These are system-wide readings, not per-game utilization or FPS. The report is temporary and is not written to history.")
        .arg(state, duration, metric(QStringLiteral("CPU"), cpuSum_, cpuPeak_, cpuSampleCount_),
             metric(QStringLiteral("Memory"), memorySum_, memoryPeak_, memorySampleCount_),
             metric(QStringLiteral("GPU"), gpuSum_, gpuPeak_, gpuSampleCount_), thermal));
}

void GamingPage::refreshAssessment()
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(gameTitle_->text()).arg(minimumRam_->value(),0,'f',1).arg(minimumVram_->value(),0,'f',1)
        .arg(storageRequired_->value(),0,'f',1).arg(installDrive_->currentData().toString())
        .arg(snapshot_.memoryTotalBytes).arg(snapshot_.graphicsMemoryBytes);
    QString fullKey = key;
    for (const auto& volume : snapshot_.volumes)
        fullKey += QStringLiteral("|%1:%2").arg(volume.rootPath).arg(volume.freeBytes / 10'000'000ULL);
    if (fullKey == lastAssessmentKey_) return;
    lastAssessmentKey_ = std::move(fullKey);
    const GameReadiness assessment = GamingReadinessEngine::analyze(requirements(), snapshot_);
    resultTitle_->setText(assessment.title + QStringLiteral(" · readiness"));
    resultSummary_->setText(assessment.summary);
    resultConfidence_->setText(assessment.confidence.toUpper());
    auto* layout = qobject_cast<QVBoxLayout*>(checksContainer_->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    for (const RequirementCheck& check : assessment.checks) {
        auto* row = new QFrame(checksContainer_);
        row->setStyleSheet(QStringLiteral("QFrame { background:#151e2b; border:1px solid #263143; border-radius:9px; }"));
        auto* rowLayout = new QGridLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        auto* name = new QLabel(check.name, row);
        name->setObjectName(QStringLiteral("panelTitle"));
        auto* actual = new QLabel(check.actual + QStringLiteral(" · ") + check.required, row);
        actual->setObjectName(QStringLiteral("subtle"));
        auto* state = new QLabel(row);
        QString stateText;
        QString color;
        if (check.state == RequirementState::Met) { stateText = QStringLiteral("MEETS"); color = QStringLiteral("#57d6bd"); }
        else if (check.state == RequirementState::NotMet) { stateText = QStringLiteral("BELOW"); color = QStringLiteral("#f09292"); }
        else { stateText = QStringLiteral("UNKNOWN"); color = QStringLiteral("#e6b76f"); }
        state->setText(stateText);
        state->setStyleSheet(QStringLiteral("color:%1;font-size:10px;font-weight:750;letter-spacing:1px").arg(color));
        rowLayout->addWidget(name, 0, 0);
        rowLayout->addWidget(state, 0, 1, Qt::AlignRight);
        rowLayout->addWidget(actual, 1, 0, 1, 2);
        layout->addWidget(row);
    }
}

void GamingPage::runPreGameScan()
{
    QStringList lines;
    const QDateTime now = QDateTime::currentDateTime();
    if (!snapshot_.capturedAt.isValid()) {
        preGameScanSummary_->setText(QStringLiteral("No system snapshot is available yet. Wait for monitoring to collect readings, then scan again."));
        return;
    }
    const qint64 ageMilliseconds = snapshot_.capturedAt.msecsTo(now);
    if (ageMilliseconds < 0 || ageMilliseconds > 30'000) {
        const QString reason = ageMilliseconds < 0
            ? QStringLiteral("Its timestamp is ahead of this PC’s clock")
            : QStringLiteral("It is %1 seconds old").arg(ageMilliseconds / 1000);
        preGameScanSummary_->setText(QStringLiteral("The latest system snapshot is not current (%1). Wait for a fresh monitoring update and scan again; Ausyn will not treat stale readings as current.").arg(reason));
        return;
    }
    lines << QStringLiteral("Snapshot age: %1 sec · current for this quick check")
        .arg((ageMilliseconds + 500) / 1000);

    const auto loadLine = [&lines](const QString& label, const std::optional<double>& value) {
        if (!value || !std::isfinite(*value)) {
            lines << QStringLiteral("%1: unavailable").arg(label);
            return;
        }
        const QString state = *value >= 90.0 ? QStringLiteral(" · very high current load")
            : *value >= 80.0 ? QStringLiteral(" · elevated current load") : QString{};
        lines << QStringLiteral("%1: %2% current use%3").arg(label).arg(*value, 0, 'f', 1).arg(state);
    };
    loadLine(QStringLiteral("CPU"), snapshot_.processorUsagePercent);
    loadLine(QStringLiteral("Graphics"), snapshot_.graphicsUsagePercent);
    loadLine(QStringLiteral("Memory"), snapshot_.memoryUsagePercent);

    if (snapshot_.batteryOnAcPower.has_value()) {
        lines << (*snapshot_.batteryOnAcPower
            ? QStringLiteral("Power: charger/AC power reported")
            : QStringLiteral("Power: running on battery"));
    } else {
        lines << QStringLiteral("Power: charger status unavailable");
    }

    const ThermalSensorSample* hottest = nullptr;
    for (const ThermalSensorSample& sensor : snapshot_.thermalSensors) {
        if (!std::isfinite(sensor.temperatureCelsius)) continue;
        if (!hottest || sensor.temperatureCelsius > hottest->temperatureCelsius) hottest = &sensor;
    }
    if (!hottest) {
        lines << QStringLiteral("Thermal zones: unavailable from Windows");
    } else if (hottest->passiveTripPointCelsius && *hottest->passiveTripPointCelsius > 0.0) {
        const bool nearTrip = hottest->temperatureCelsius >= *hottest->passiveTripPointCelsius * 0.9;
        lines << QStringLiteral("Windows thermal zone: %1 °C · %2% of reported passive trip point%3")
            .arg(hottest->temperatureCelsius, 0, 'f', 1)
            .arg(hottest->temperatureCelsius / *hottest->passiveTripPointCelsius * 100.0, 0, 'f', 0)
            .arg(nearTrip ? QStringLiteral(" · review cooling/airflow") : QString{});
    } else {
        lines << QStringLiteral("Windows thermal zone: %1 °C · no passive trip point reported")
            .arg(hottest->temperatureCelsius, 0, 'f', 1);
    }

    if (storageRequired_->value() <= 0.0) {
        lines << QStringLiteral("Install space: enter the game's published requirement to check this drive");
    } else {
        const QString selectedRoot = installDrive_->currentData().toString();
        const auto volume = std::find_if(snapshot_.volumes.cbegin(), snapshot_.volumes.cend(),
            [&selectedRoot](const VolumeSample& item) { return item.rootPath == selectedRoot; });
        if (volume == snapshot_.volumes.cend()) {
            lines << QStringLiteral("Install space: selected drive reading unavailable");
        } else {
            const double freeGb = static_cast<double>(volume->freeBytes) / 1'000'000'000.0;
            lines << QStringLiteral("Install space on %1: %2 GB free · %3 the entered %4 GB minimum")
                .arg(selectedRoot).arg(freeGb, 0, 'f', 1)
                .arg(freeGb >= storageRequired_->value() ? QStringLiteral("meets") : QStringLiteral("below"))
                .arg(storageRequired_->value(), 0, 'f', 1);
        }
    }

    const qint64 processAgeMilliseconds = snapshot_.processSamplesCapturedAt.isValid()
        ? snapshot_.processSamplesCapturedAt.msecsTo(now) : -1;
    if (processAgeMilliseconds >= 0 && processAgeMilliseconds <= 30'000 &&
        !snapshot_.topProcesses.isEmpty()) {
        QVector<ProcessSample> processes = snapshot_.topProcesses;
        std::stable_sort(processes.begin(), processes.end(), [](const ProcessSample& left, const ProcessSample& right) {
            return left.workingSetBytes.value_or(0) > right.workingSetBytes.value_or(0);
        });
        int shown = 0;
        for (const ProcessSample& process : processes) {
            if (!process.workingSetBytes || shown >= 3) continue;
            const double memoryGb = static_cast<double>(*process.workingSetBytes) / 1'000'000'000.0;
            lines << QStringLiteral("Memory user: %1 · %2 GB working set")
                .arg(process.name.isEmpty() ? QStringLiteral("unnamed process") : process.name)
                .arg(memoryGb, 0, 'f', 2);
            ++shown;
        }
    } else {
        lines << QStringLiteral("Top memory-using processes: unavailable or stale");
    }
    preGameScanSummary_->setText(lines.join(QLatin1Char('\n')) +
        QStringLiteral("\n\nThis is a point-in-time readiness check, not a game compatibility, temperature-sensor, or FPS guarantee. High load can be normal for active work. Thermal values are Windows ACPI zones and may not represent CPU/GPU die temperature. Ausyn does not close apps or change settings."));
}

void GamingPage::launchGameWithReadinessCheck()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a game executable"),
        QString{}, QStringLiteral("Windows applications (*.exe)"));
    if (path.isEmpty()) return;

    const QFileInfo executableInfo(path);
    if (!executableInfo.isFile() || executableInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, QStringLiteral("Choose an application"),
            QStringLiteral("Select an existing Windows .exe file."));
        return;
    }
    const QString executable = executableInfo.fileName();
    if (const GameCatalogProfile* profile = GameCatalog::findExecutable(executable)) {
        const int index = catalogProfile_->findData(profile->id);
        if (index >= 0 && catalogProfile_->currentIndex() != index)
            catalogProfile_->setCurrentIndex(index);
    } else {
        const QString savedProfile = customGameExeToProfile_.value(executable.toCaseFolded());
        if (savedProfile.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("No matching game profile"),
                QStringLiteral("Save this game's requirements profile with the exact executable name “%1” first. Ausyn needs a matching profile to check readiness before launch.")
                    .arg(executable));
            return;
        }
        const int index = savedProfiles_->findData(savedProfile);
        if (index < 0) {
            QMessageBox::warning(this, QStringLiteral("Saved game profile unavailable"),
                QStringLiteral("Ausyn could not load the saved profile for “%1”. Refresh the profile list or create it again.")
                    .arg(executable));
            return;
        }
        savedProfiles_->setCurrentIndex(index);
        loadSelectedProfile();
    }

    refreshAssessment();
    const GameReadiness readiness = currentAssessment();
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 snapshotAgeMs = snapshot_.capturedAt.isValid()
        ? snapshot_.capturedAt.msecsTo(now) : -1;
    const bool freshSnapshot = snapshotAgeMs >= 0 && snapshotAgeMs <= 30'000;
    QStringList reasons;
    if (!freshSnapshot) {
        reasons << QStringLiteral("Fresh system readings are not available, so Ausyn cannot assess current pressure.");
    } else {
        for (const RequirementCheck& check : readiness.checks) {
            if (check.state == RequirementState::NotMet)
                reasons << QStringLiteral("%1 is below the entered minimum (%2; needs %3).")
                    .arg(check.name, check.actual, check.required);
            else if (check.state == RequirementState::Unknown)
                reasons << QStringLiteral("%1 could not be checked from Windows readings.").arg(check.name);
        }
        if (snapshot_.processorUsagePercent && *snapshot_.processorUsagePercent >= 85.0)
            reasons << QStringLiteral("Current whole-system CPU load is high at %1%.")
                .arg(*snapshot_.processorUsagePercent, 0, 'f', 0);
        if (snapshot_.memoryUsagePercent && *snapshot_.memoryUsagePercent >= 85.0)
            reasons << QStringLiteral("Current memory use is high at %1%.")
                .arg(*snapshot_.memoryUsagePercent, 0, 'f', 0);
        if (snapshot_.graphicsUsagePercent && *snapshot_.graphicsUsagePercent >= 85.0)
            reasons << QStringLiteral("Current graphics-engine use is high at %1%.")
                .arg(*snapshot_.graphicsUsagePercent, 0, 'f', 0);
    }
    if (reasons.isEmpty())
        reasons << QStringLiteral("The measurable RAM, dedicated VRAM, and install-drive checks meet the entered minimums, and current system load is not high.");
    const bool elevatedLoad =
        (snapshot_.processorUsagePercent && *snapshot_.processorUsagePercent >= 85.0) ||
        (snapshot_.memoryUsagePercent && *snapshot_.memoryUsagePercent >= 85.0) ||
        (snapshot_.graphicsUsagePercent && *snapshot_.graphicsUsagePercent >= 85.0);
    const bool needsOverride = !freshSnapshot || readiness.failedChecks > 0 ||
        readiness.knownChecks < readiness.checks.size() || elevatedLoad;
    reasons << QStringLiteral("This does not compare CPU/GPU model performance or guarantee compatibility, temperature, or FPS. Some games require launching through their original launcher.");

    const QString summary = QStringLiteral("%1\n\n%2\n\nLaunch the game now?")
        .arg(gameTitle_->text().trimmed().isEmpty() ? executable : gameTitle_->text().trimmed(),
             reasons.join(QLatin1Char('\n')));
    QMessageBox prompt(this);
    prompt.setIcon(needsOverride ? QMessageBox::Warning : QMessageBox::Information);
    prompt.setWindowTitle(QStringLiteral("Ausyn game readiness check"));
    prompt.setText(summary);
    auto* launchButton = prompt.addButton(
        needsOverride ? QStringLiteral("Launch anyway") : QStringLiteral("Launch game"),
        QMessageBox::AcceptRole);
    auto* cancelButton = prompt.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    prompt.setDefaultButton(qobject_cast<QPushButton*>(cancelButton));
    prompt.exec();
    if (prompt.clickedButton() != launchButton) return;

    qint64 processId = 0;
    if (!QProcess::startDetached(executableInfo.absoluteFilePath(), {}, executableInfo.absolutePath(), &processId)) {
        QMessageBox::warning(this, QStringLiteral("Could not launch game"),
            QStringLiteral("Windows could not start this executable. If the game normally uses a launcher or anti-cheat, start it there instead."));
        return;
    }
    launchStatus_->setText(QStringLiteral("Ausyn launched %1 after the readiness check. It will look for the game in the next process sample.")
        .arg(gameTitle_->text().trimmed().isEmpty() ? executable : gameTitle_->text().trimmed()));
}

} // namespace Ausyn
