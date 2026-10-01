#include "security_page.h"

#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace Ausyn {
namespace {

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QWidget* providerCard(const QString& title, QLabel*& value, QLabel*& detail, QWidget* parent)
{
    auto* card = panel(parent);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(17, 16, 17, 16);
    layout->setSpacing(8);
    auto* heading = new QLabel(title, card);
    heading->setObjectName(QStringLiteral("metricName"));
    value = new QLabel(QStringLiteral("Checking…"), card);
    value->setStyleSheet(QStringLiteral("color:#c3c2ff;font-size:20px;font-weight:700;"));
    detail = new QLabel(QStringLiteral("Waiting for Windows Security Center."), card);
    detail->setObjectName(QStringLiteral("subtle"));
    detail->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(value);
    layout->addWidget(detail, 1);
    return card;
}

} // namespace

SecurityPage::SecurityPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(16);

    auto* eyebrow = new QLabel(QStringLiteral("DEVICE PROTECTION & UPDATES"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Security & updates"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("A transparent view of the health Windows reports. Ausyn does not change protection settings or install updates."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    grid->addWidget(providerCard(QStringLiteral("Antivirus protection"), antivirusValue_, antivirusDetail_, this), 0, 0);
    grid->addWidget(providerCard(QStringLiteral("Firewall"), firewallValue_, firewallDetail_, this), 0, 1);
    grid->addWidget(providerCard(QStringLiteral("Automatic update settings"), autoUpdateValue_, autoUpdateDetail_, this), 1, 0);
    auto* restart = panel(this);
    auto* restartLayout = new QVBoxLayout(restart);
    restartLayout->setContentsMargins(17, 16, 17, 16);
    auto* restartHeading = new QLabel(QStringLiteral("Restart status"), restart);
    restartHeading->setObjectName(QStringLiteral("metricName"));
    restartValue_ = new QLabel(QStringLiteral("Checking…"), restart);
    restartValue_->setStyleSheet(QStringLiteral("color:#c3c2ff;font-size:20px;font-weight:700;"));
    restartLayout->addWidget(restartHeading);
    restartLayout->addWidget(restartValue_);
    grid->addWidget(restart, 1, 1);
    outer->addLayout(grid);

    auto* updatePanel = panel(this);
    auto* updateLayout = new QVBoxLayout(updatePanel);
    updateLayout->setContentsMargins(19, 17, 19, 17);
    updateLayout->setSpacing(10);
    auto* updateHeading = new QLabel(QStringLiteral("Windows Update"), updatePanel);
    updateHeading->setObjectName(QStringLiteral("panelTitle"));
    updateStatus_ = new QLabel(QStringLiteral("Ausyn has not scanned the local update cache yet."), updatePanel);
    updateStatus_->setObjectName(QStringLiteral("subtle"));
    updateStatus_->setWordWrap(true);
    updateTitles_ = new QTextBrowser(updatePanel);
    updateTitles_->setMaximumHeight(125);
    updateTitles_->setStyleSheet(QStringLiteral("QTextBrowser{background:#0e131d;border:1px solid #222e3e;border-radius:8px;color:#aeb9cb;padding:8px;}"));
    updateTitles_->setText(QStringLiteral("The local cache scan is offline and does not install anything."));
    auto* actions = new QHBoxLayout;
    refreshSecurityButton_ = new QPushButton(QStringLiteral("Refresh protection status"), updatePanel);
    refreshSecurityButton_->setObjectName(QStringLiteral("secondaryButton"));
    scanUpdatesButton_ = new QPushButton(QStringLiteral("Scan local update cache"), updatePanel);
    scanUpdatesButton_->setObjectName(QStringLiteral("primaryButton"));
    auto* openSettings = new QPushButton(QStringLiteral("Open Windows Update settings"), updatePanel);
    openSettings->setObjectName(QStringLiteral("secondaryButton"));
    actions->addWidget(refreshSecurityButton_);
    actions->addWidget(scanUpdatesButton_);
    actions->addWidget(openSettings);
    actions->addStretch();
    updateLayout->addWidget(updateHeading);
    updateLayout->addWidget(updateStatus_);
    updateLayout->addWidget(updateTitles_);
    updateLayout->addLayout(actions);
    outer->addWidget(updatePanel, 1);
    connect(refreshSecurityButton_, &QPushButton::clicked, this, &SecurityPage::refreshSecurityRequested);
    connect(scanUpdatesButton_, &QPushButton::clicked, this, &SecurityPage::scanUpdatesRequested);
    connect(openSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:windowsupdate")));
    });
}

void SecurityPage::setSecurityStatus(const SecurityStatusUpdate& update)
{
    antivirusValue_->setText(update.antivirus.status);
    antivirusDetail_->setText(update.antivirus.detail);
    firewallValue_->setText(update.firewall.status);
    firewallDetail_->setText(update.firewall.detail);
    autoUpdateValue_->setText(update.automaticUpdates.status);
    autoUpdateDetail_->setText(update.automaticUpdates.detail);
    restartValue_->setText(update.restartRequired
        ? QStringLiteral("Restart needed") : QStringLiteral("No restart flag found"));
    restartValue_->setToolTip(QStringLiteral("Windows servicing registry indicators. This does not list every reason an application may request a restart."));
    antivirusDetail_->setToolTip(QStringLiteral("Last checked %1").arg(update.checkedAt.toString(Qt::ISODate)));
}

void SecurityPage::setUpdateCacheResult(const UpdateCacheResult& result)
{
    updateStatus_->setText(QStringLiteral("Checked %1 · %2")
        .arg(result.checkedAt.toString(QStringLiteral("h:mm:ss ap")), result.status));
    updateTitles_->setPlainText(result.updateTitles.isEmpty()
        ? QStringLiteral("No update titles returned. A successful empty result means no pending software update was found in the local cache, not that Windows is fully up to date.")
        : result.updateTitles.join(QStringLiteral("\n")));
    if (result.updateCount > result.updateTitles.size()) {
        updateTitles_->append(QStringLiteral("\n… and %1 more in the local cache.")
            .arg(result.updateCount - result.updateTitles.size()));
    }
}

void SecurityPage::setSecurityBusy(bool busy)
{
    refreshSecurityButton_->setEnabled(!busy);
    refreshSecurityButton_->setText(busy ? QStringLiteral("Checking…") : QStringLiteral("Refresh protection status"));
}

void SecurityPage::setUpdateBusy(bool busy)
{
    scanUpdatesButton_->setEnabled(!busy);
    scanUpdatesButton_->setText(busy ? QStringLiteral("Scanning local cache…") : QStringLiteral("Scan local update cache"));
    if (busy) updateStatus_->setText(QStringLiteral("Searching the local Windows Update cache. No online search or installation is started."));
}

} // namespace Ausyn
