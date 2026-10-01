#include "battery_page.h"

#include <QFrame>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <powrprof.h>
#include <objbase.h>
#endif

#include <algorithm>
#include <cmath>
#include <utility>

namespace Ausyn {
namespace {

const QColor kChartText(148, 160, 181);
const QColor kChartGrid(42, 52, 69);
const QColor kChartLine(139, 132, 255);

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QLabel* valueLabel(QWidget* parent)
{
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setStyleSheet(QStringLiteral("color:#c3c2ff;font-size:23px;font-weight:700;"));
    return label;
}

QWidget* makeMetricWidget(const QString& title, QLabel*& value, QWidget* parent)
{
    auto* card = panel(parent);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(9);
    auto* name = new QLabel(title, card);
    name->setObjectName(QStringLiteral("metricName"));
    value = valueLabel(card);
    layout->addWidget(name);
    layout->addWidget(value);
    return card;
}

QString formatDuration(quint32 seconds)
{
    const quint32 hours = seconds / 3600;
    const quint32 minutes = (seconds % 3600) / 60;
    if (hours >= 24) return QStringLiteral("%1d %2h remaining").arg(hours / 24).arg(hours % 24);
    if (hours > 0) return QStringLiteral("%1h %2m remaining").arg(hours).arg(minutes);
    return QStringLiteral("%1m remaining").arg(minutes);
}

#ifdef Q_OS_WIN
QVector<QPair<QString, QString>> availablePowerSchemes()
{
    QVector<QPair<QString, QString>> plans;
    for (ULONG index = 0; index < 64; ++index) {
        GUID scheme{};
        DWORD guidSize = sizeof(scheme);
        const DWORD enumerateResult = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME,
            index, reinterpret_cast<UCHAR*>(&scheme), &guidSize);
        if (enumerateResult == ERROR_NO_MORE_ITEMS) break;
        if (enumerateResult != ERROR_SUCCESS || guidSize != sizeof(scheme)) continue;

        DWORD nameBytes = 0;
        PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &nameBytes);
        QString name;
        if (nameBytes >= sizeof(wchar_t) && nameBytes <= 64 * 1024) {
            QVector<wchar_t> buffer(static_cast<qsizetype>(nameBytes / sizeof(wchar_t)) + 1, L'\0');
            if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr,
                    reinterpret_cast<UCHAR*>(buffer.data()), &nameBytes) == ERROR_SUCCESS) {
                name = QString::fromWCharArray(buffer.constData());
            }
        }
        if (name.isEmpty()) name = QStringLiteral("Windows power plan %1").arg(index + 1);
        LPOLESTR text = nullptr;
        if (StringFromCLSID(scheme, &text) != S_OK || !text) continue;
        plans.append({name, QString::fromWCharArray(text)});
        CoTaskMemFree(text);
    }
    return plans;
}
#endif

} // namespace

class BatteryHealthChart final : public QWidget {
public:
    explicit BatteryHealthChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(112);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setObjectName(QStringLiteral("batteryHealthChart"));
        setAccessibleName(QStringLiteral("Daily battery capacity-health estimate history"));
        setToolTip(QStringLiteral("Daily averages of the Windows-reported full-charge/design-capacity estimate. Not a diagnostic or lifespan forecast."));
    }

    void setPoints(QVector<BatteryHealthTrendPoint> points)
    {
        points_ = std::move(points);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot(48.0, 12.0, qMax(1, width() - 64), qMax(1, height() - 42));
        painter.setPen(kChartText);
        if (points_.isEmpty()) {
            painter.drawText(rect(), Qt::AlignCenter,
                QStringLiteral("Daily history appears when Windows reports capacity health."));
            return;
        }

        double low = points_.first().estimatedHealthPercent;
        double high = low;
        for (const auto& point : points_) {
            low = qMin(low, point.estimatedHealthPercent);
            high = qMax(high, point.estimatedHealthPercent);
        }
        const double padding = qMax(2.0, (high - low) * 0.18);
        const double minimum = qMax(0.0, low - padding);
        const double maximum = qMin(200.0, high + padding);
        const double range = qMax(1.0, maximum - minimum);

        painter.setPen(QPen(kChartGrid, 1));
        for (int line = 0; line < 3; ++line) {
            const double ratio = static_cast<double>(line) / 2.0;
            const double y = plot.top() + ratio * plot.height();
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(kChartText);
            painter.drawText(QRectF(0, y - 8, 42, 16), Qt::AlignRight | Qt::AlignVCenter,
                QStringLiteral("%1%").arg(maximum - ratio * range, 0, 'f', 0));
            painter.setPen(QPen(kChartGrid, 1));
        }

        const qint64 firstMs = points_.first().capturedAt.toMSecsSinceEpoch();
        const qint64 lastMs = points_.last().capturedAt.toMSecsSinceEpoch();
        const qint64 spanMs = qMax<qint64>(1, lastMs - firstMs);
        const auto position = [&](const BatteryHealthTrendPoint& point) {
            const double xRatio = static_cast<double>(point.capturedAt.toMSecsSinceEpoch() - firstMs) /
                static_cast<double>(spanMs);
            const double yRatio = (maximum - point.estimatedHealthPercent) / range;
            return QPointF(plot.left() + xRatio * plot.width(), plot.top() + yRatio * plot.height());
        };

        QPainterPath path;
        QDateTime previousAt;
        bool hasSegment = false;
        for (const auto& point : points_) {
            const QPointF current = position(point);
            if (!previousAt.isValid() || previousAt.daysTo(point.capturedAt) > 2) {
                path.moveTo(current);
                hasSegment = true;
            } else if (hasSegment) {
                path.lineTo(current);
            }
            previousAt = point.capturedAt;
        }
        painter.setPen(QPen(kChartLine, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setBrush(kChartLine);
        painter.setPen(QPen(QColor(20, 25, 36), 1.5));
        for (const auto& point : points_) painter.drawEllipse(position(point), 3.2, 3.2);

        painter.setPen(kChartText);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 5, plot.width() / 2, 18),
            Qt::AlignLeft | Qt::AlignVCenter,
            points_.first().capturedAt.toLocalTime().toString(QStringLiteral("MMM d")));
        painter.drawText(QRectF(plot.center().x(), plot.bottom() + 5, plot.width() / 2, 18),
            Qt::AlignRight | Qt::AlignVCenter,
            points_.last().capturedAt.toLocalTime().toString(QStringLiteral("MMM d")));
    }

private:
    QVector<BatteryHealthTrendPoint> points_;
};

BatteryPage::BatteryPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(17);

    auto* eyebrow = new QLabel(QStringLiteral("POWER & BATTERY"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Battery & power"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("A clear view of charge, power flow, and the readings Windows makes available."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* hero = panel(this);
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(22, 20, 22, 20);
    heroLayout->setSpacing(20);
    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(8);
    charge_ = new QLabel(QStringLiteral("Waiting for Windows…"), hero);
    charge_->setStyleSheet(QStringLiteral("color:#f3f5fb;font-size:30px;font-weight:750;"));
    state_ = new QLabel(QStringLiteral("Power status will appear here."), hero);
    state_->setObjectName(QStringLiteral("subtle"));
    state_->setWordWrap(true);
    textColumn->addWidget(charge_);
    textColumn->addWidget(state_);
    textColumn->addStretch();
    gauge_ = new QProgressBar(hero);
    gauge_->setRange(0, 100);
    gauge_->setTextVisible(false);
    gauge_->setFixedSize(200, 16);
    gauge_->setStyleSheet(QStringLiteral("QProgressBar{background:#202938;border:0;border-radius:8px;} QProgressBar::chunk{background:#827cff;border-radius:8px;}"));
    heroLayout->addLayout(textColumn, 1);
    heroLayout->addWidget(gauge_, 0, Qt::AlignVCenter);
    outer->addWidget(hero);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    grid->addWidget(makeMetricWidget(QStringLiteral("Estimated time"), eta_, this), 0, 0);
    grid->addWidget(makeMetricWidget(QStringLiteral("Power rate"), rate_, this), 0, 1);
    grid->addWidget(makeMetricWidget(QStringLiteral("Reported capacity"), capacity_, this), 1, 0);
    grid->addWidget(makeMetricWidget(QStringLiteral("Capacity health estimate"), health_, this), 1, 1);
    outer->addLayout(grid);

    auto* notePanel = panel(this);
    auto* noteLayout = new QVBoxLayout(notePanel);
    noteLayout->setContentsMargins(18, 15, 18, 15);
    auto* title = new QLabel(QStringLiteral("Battery health, without guesswork"), notePanel);
    title->setObjectName(QStringLiteral("panelTitle"));
    note_ = new QLabel(QStringLiteral("Ausyn will show only readings reported by Windows. Battery wear or design health is not inferred from charge level."), notePanel);
    note_->setObjectName(QStringLiteral("subtle"));
    note_->setWordWrap(true);
    noteLayout->addWidget(title);
    noteLayout->addWidget(note_);
    trend_ = new QLabel(QStringLiteral("Battery capacity trend will appear when Windows reports enough local history."), notePanel);
    trend_->setObjectName(QStringLiteral("subtle"));
    trend_->setWordWrap(true);
    noteLayout->addWidget(trend_);
    healthChart_ = new BatteryHealthChart(notePanel);
    noteLayout->addWidget(healthChart_);
    outer->addWidget(notePanel);

    auto* controls = panel(this);
    auto* controlsLayout = new QVBoxLayout(controls);
    controlsLayout->setContentsMargins(18, 15, 18, 15);
    controlsLayout->setSpacing(9);
    auto* controlsTitle = new QLabel(QStringLiteral("Sleep & power controls"), controls);
    controlsTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* controlsInfo = new QLabel(QStringLiteral("Check Windows' current sleep blockers. Choose any plan Windows exposes; Ausyn previews the change, verifies it, and keeps the original plan for a one-click undo."), controls);
    controlsInfo->setObjectName(QStringLiteral("subtle"));
    controlsInfo->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    requestsButton_ = new QPushButton(QStringLiteral("Check sleep blockers"), controls);
    requestsButton_->setObjectName(QStringLiteral("secondaryButton"));
    powerPlanChoice_ = new QComboBox(controls);
    powerPlanChoice_->setMinimumWidth(210);
#ifdef Q_OS_WIN
    for (const auto& plan : availablePowerSchemes())
        powerPlanChoice_->addItem(plan.first, plan.second);
    if (powerPlanChoice_->count() == 0) {
        powerPlanChoice_->addItem(QStringLiteral("No power plans reported"));
        powerPlanChoice_->setEnabled(false);
    }
    GUID* activeForSelection = nullptr;
    if (PowerGetActiveScheme(nullptr, &activeForSelection) == ERROR_SUCCESS && activeForSelection) {
        LPOLESTR activeText = nullptr;
        if (StringFromCLSID(*activeForSelection, &activeText) == S_OK && activeText) {
            const int activeIndex = powerPlanChoice_->findData(QString::fromWCharArray(activeText));
            if (activeIndex >= 0) powerPlanChoice_->setCurrentIndex(activeIndex);
            CoTaskMemFree(activeText);
        }
        LocalFree(activeForSelection);
    }
#else
    powerPlanChoice_->addItem(QStringLiteral("Windows only"));
    powerPlanChoice_->setEnabled(false);
#endif
    applyPowerPlanButton_ = new QPushButton(QStringLiteral("Preview & apply"), controls);
    applyPowerPlanButton_->setObjectName(QStringLiteral("secondaryButton"));
    undoPowerButton_ = new QPushButton(QStringLiteral("Undo power change"), controls);
    undoPowerButton_->setObjectName(QStringLiteral("secondaryButton"));
    undoPowerButton_->setEnabled(false);
    buttons->addWidget(requestsButton_);
    buttons->addWidget(powerPlanChoice_);
    buttons->addWidget(applyPowerPlanButton_);
    buttons->addWidget(undoPowerButton_);
    powerActionStatus_ = new QLabel(QStringLiteral("No power setting has been changed."), controls);
    powerActionStatus_->setObjectName(QStringLiteral("subtle"));
    powerActionStatus_->setWordWrap(true);
    powerRequests_ = new QPlainTextEdit(controls);
    powerRequests_->setReadOnly(true);
    powerRequests_->setMaximumHeight(115);
    powerRequests_->setPlaceholderText(QStringLiteral("Sleep blocker results will appear here when you request a check."));
    powerRequests_->setStyleSheet(QStringLiteral("QPlainTextEdit{background:#0e131d;border:1px solid #222e3e;border-radius:8px;color:#aeb9cb;padding:8px;font-family:Consolas,monospace;}"));
    controlsLayout->addWidget(controlsTitle);
    controlsLayout->addWidget(controlsInfo);
    controlsLayout->addLayout(buttons);
    controlsLayout->addWidget(powerActionStatus_);
    controlsLayout->addWidget(powerRequests_);
    outer->addWidget(controls);
    outer->addStretch(1);

    connect(requestsButton_, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        requestsButton_->setEnabled(false);
        requestsButton_->setText(QStringLiteral("Checking…"));
        auto* process = new QProcess(this);
        wchar_t systemDirectory[MAX_PATH]{};
        const UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (systemDirectoryLength == 0 || systemDirectoryLength >= MAX_PATH) {
            powerRequests_->setPlainText(QStringLiteral("Windows could not provide the system-tools folder. Ausyn did not run a command."));
            requestsButton_->setEnabled(true);
            requestsButton_->setText(QStringLiteral("Check sleep blockers"));
            process->deleteLater();
            return;
        }
        process->setProgram(QDir(QString::fromWCharArray(systemDirectory)).filePath(QStringLiteral("powercfg.exe")));
        process->setArguments({QStringLiteral("/requests")});
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
            const QByteArray bytes = process->readAll();
            QString output = QString::fromLocal8Bit(bytes).trimmed();
            if (status != QProcess::NormalExit || exitCode != 0)
                output = QStringLiteral("Windows powercfg could not complete the read-only request check.\n%1").arg(output);
            if (output.isEmpty()) output = QStringLiteral("Windows returned no sleep request details.");
            powerRequests_->setPlainText(output);
            requestsButton_->setEnabled(true);
            requestsButton_->setText(QStringLiteral("Check sleep blockers"));
            process->deleteLater();
        });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
            powerRequests_->setPlainText(QStringLiteral("Could not start Windows powercfg. Ausyn did not change any setting."));
            requestsButton_->setEnabled(true);
            requestsButton_->setText(QStringLiteral("Check sleep blockers"));
            process->deleteLater();
        });
        process->start();
#else
        powerRequests_->setPlainText(QStringLiteral("Sleep blocker inspection is available on Windows only."));
#endif
    });
    connect(applyPowerPlanButton_, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        const QString selectedPlan = powerPlanChoice_->currentData().toString();
        GUID target{};
        if (selectedPlan.isEmpty() || CLSIDFromString(reinterpret_cast<LPCOLESTR>(selectedPlan.utf16()), &target) != S_OK) {
            powerActionStatus_->setText(QStringLiteral("Choose a power plan that Windows reports as available."));
            return;
        }
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
            powerActionStatus_->setText(QStringLiteral("Windows could not report the active power plan."));
            return;
        }
        LPOLESTR previousText = nullptr;
        StringFromCLSID(*active, &previousText);
        previousPowerScheme_ = previousText ? QString::fromWCharArray(previousText) : QString{};
        if (previousText) CoTaskMemFree(previousText);
        LocalFree(active);
        if (previousPowerScheme_.isEmpty()) {
            powerActionStatus_->setText(QStringLiteral("Ausyn could not safely save the current plan, so it will not change it."));
            return;
        }
        GUID previousGuid{};
        if (CLSIDFromString(reinterpret_cast<LPCOLESTR>(previousPowerScheme_.utf16()), &previousGuid) == S_OK &&
            IsEqualGUID(previousGuid, target)) {
            powerActionStatus_->setText(QStringLiteral("That plan is already active; Ausyn left it unchanged."));
            previousPowerScheme_.clear();
            return;
        }
        const QString targetName = powerPlanChoice_->currentText();
        const auto choice = QMessageBox::question(this, QStringLiteral("Preview Windows power-plan change"),
            QStringLiteral("Ausyn will ask Windows to switch to “%1”. The selected plan may change performance and battery behaviour according to its Windows settings. The current plan will be saved so you can undo this change. Apply it now?").arg(targetName),
            QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
        if (choice != QMessageBox::Apply) return;
        if (PowerSetActiveScheme(nullptr, &target) != ERROR_SUCCESS) {
            previousPowerScheme_.clear();
            powerActionStatus_->setText(QStringLiteral("Windows could not apply the selected plan. No settings change was verified."));
            return;
        }
        undoPowerButton_->setEnabled(true);
        applyPowerPlanButton_->setEnabled(false);
        GUID* verified = nullptr;
        const bool ok = PowerGetActiveScheme(nullptr, &verified) == ERROR_SUCCESS && verified && IsEqualGUID(*verified, target);
        if (verified) LocalFree(verified);
        if (!ok) {
            powerActionStatus_->setText(QStringLiteral("Windows accepted the change, but Ausyn could not verify the selected plan. Undo is available to restore the saved plan."));
            return;
        }
        powerActionStatus_->setText(QStringLiteral("“%1” applied and verified. Your previous plan is saved for undo.").arg(targetName));
#else
        powerActionStatus_->setText(QStringLiteral("Power-plan controls are available on Windows only."));
#endif
    });
    connect(undoPowerButton_, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        GUID previous{};
        if (previousPowerScheme_.isEmpty() || CLSIDFromString(reinterpret_cast<LPCOLESTR>(previousPowerScheme_.utf16()), &previous) != S_OK ||
            PowerSetActiveScheme(nullptr, &previous) != ERROR_SUCCESS) {
            powerActionStatus_->setText(QStringLiteral("Ausyn could not restore the saved power plan. You can choose a plan in Windows Settings."));
            return;
        }
        GUID* verified = nullptr;
        const bool ok = PowerGetActiveScheme(nullptr, &verified) == ERROR_SUCCESS && verified && IsEqualGUID(*verified, previous);
        if (verified) LocalFree(verified);
        if (ok) {
            undoPowerButton_->setEnabled(false);
            applyPowerPlanButton_->setEnabled(true);
            previousPowerScheme_.clear();
            powerActionStatus_->setText(QStringLiteral("Previous power plan restored and verified."));
        } else {
            powerActionStatus_->setText(QStringLiteral("Windows did not confirm the restored plan. Check the active plan in Windows Settings."));
        }
#endif
    });
}

void BatteryPage::setSnapshot(const SystemSnapshot& snapshot)
{
    if (!snapshot.batteryPercent) {
        charge_->setText(QStringLiteral("No battery detected"));
        state_->setText(QStringLiteral("This device does not expose a battery charge reading."));
        gauge_->setValue(0);
        eta_->setText(QStringLiteral("Unavailable"));
        rate_->setText(QStringLiteral("Unavailable"));
        capacity_->setText(QStringLiteral("Unavailable"));
        health_->setText(QStringLiteral("Unavailable"));
        note_->setText(QStringLiteral("Windows reports no battery on this device. Desktop alerts remain available for other device findings."));
        return;
    }

    const unsigned int percent = std::min(*snapshot.batteryPercent, 100u);
    charge_->setText(QStringLiteral("%1% charge").arg(percent));
    gauge_->setValue(static_cast<int>(percent));
    QString powerState = QStringLiteral("Charge level reported by Windows");
    if (snapshot.batteryCharging) powerState = QStringLiteral("Charging · connected to power");
    else if (snapshot.batteryOnAcPower && *snapshot.batteryOnAcPower) powerState = QStringLiteral("Connected to AC power");
    else if (snapshot.batteryOnAcPower && !*snapshot.batteryOnAcPower) powerState = QStringLiteral("Running on battery");
    state_->setText(powerState);

    eta_->setText(snapshot.batteryEstimatedSeconds
        ? formatDuration(*snapshot.batteryEstimatedSeconds)
        : QStringLiteral("Not reported"));
    rate_->setText(snapshot.batteryRateMilliwatts
        ? QStringLiteral("%1 W").arg(static_cast<double>(*snapshot.batteryRateMilliwatts) / 1000.0, 0, 'f', 1)
        : QStringLiteral("Not reported"));
    if (snapshot.batteryRemainingCapacityMwh && snapshot.batteryMaximumCapacityMwh) {
        capacity_->setText(QStringLiteral("%1 / %2 Wh")
            .arg(static_cast<double>(*snapshot.batteryRemainingCapacityMwh) / 1000.0, 0, 'f', 1)
            .arg(static_cast<double>(*snapshot.batteryMaximumCapacityMwh) / 1000.0, 0, 'f', 1));
        note_->setText(QStringLiteral("Windows reports remaining and maximum operating capacity. %1")
            .arg(snapshot.batteryHealthPercent
                ? QStringLiteral("The separate health estimate compares full-charge capacity with design capacity; see its limits below.")
                : QStringLiteral("Design/full-charge capacity was unavailable, so Ausyn does not infer battery wear from charge.")));
    } else {
        capacity_->setText(QStringLiteral("Not reported"));
        note_->setText(snapshot.batteryHealthPercent
            ? QStringLiteral("Current remaining/max capacity was unavailable. The separate design/full-charge ratio is an estimate, not a battery diagnostic.")
            : QStringLiteral("Battery wear and design health are not exposed by the standard reading on this device. Charge percentage is not used as a health estimate."));
    }
    health_->setText(snapshot.batteryHealthPercent
        ? QStringLiteral("%1% estimated").arg(*snapshot.batteryHealthPercent, 0, 'f', 1)
        : QStringLiteral("Not reported"));
    if (snapshot.batteryHealthPercent) {
        note_->setText(note_->text() + QStringLiteral(" Estimated from Windows-reported full-charge/design capacity; reporting depends on firmware and the battery driver. Values can be above 100% and are not a diagnostic."));
    }
}

void BatteryPage::setHealthTrend(const QVector<BatteryHealthTrendPoint>& trend)
{
    QVector<BatteryHealthTrendPoint> valid;
    valid.reserve(trend.size());
    for (const BatteryHealthTrendPoint& point : trend) {
        if (point.capturedAt.isValid() && std::isfinite(point.estimatedHealthPercent) &&
            point.estimatedHealthPercent > 0.0 && point.estimatedHealthPercent <= 200.0)
            valid.append(point);
    }
    healthChart_->setPoints(valid);
    if (valid.size() < 3) {
        trend_->setText(QStringLiteral("Capacity trend: collecting daily Windows estimates (%1 valid day(s); at least 3 days spanning a week are needed).")
            .arg(valid.size()));
        return;
    }
    const auto& first = valid.first();
    const auto& latest = valid.last();
    const qint64 spanDays = first.capturedAt.daysTo(latest.capturedAt);
    if (spanDays < 7) {
        trend_->setText(QStringLiteral("Capacity trend: %1 daily readings across %2 day(s); Ausyn waits for at least a week of history before comparing changes.")
            .arg(valid.size()).arg(spanDays));
        return;
    }
    const double difference = latest.estimatedHealthPercent - first.estimatedHealthPercent;
    const QString direction = difference < -0.5 ? QStringLiteral("lower")
        : difference > 0.5 ? QStringLiteral("higher") : QStringLiteral("about the same");
    trend_->setText(QStringLiteral("Capacity-health estimate is %1 across the %2-day observed history (%3% to %4%). Daily averages smooth short-term reporting variation; this is not a battery diagnosis or lifespan forecast.")
        .arg(direction).arg(spanDays)
        .arg(first.estimatedHealthPercent, 0, 'f', 1)
        .arg(latest.estimatedHealthPercent, 0, 'f', 1));
}

} // namespace Ausyn
