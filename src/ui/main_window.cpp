#include "main_window.h"

#include "history_chart.h"
#include "page_hub.h"
#include "live_visuals.h"
#include "activity_page.h"
#include "resource_inspector.h"
#include "notification_popup.h"
#include "insight_list_widget.h"
#include "../assistant/assistant_engine.h"
#include "../assistant/assistant_client.h"
#include "gaming_page.h"
#include "prediction_page.h"
#include "battery_page.h"
#include "event_log_page.h"
#include "app_inventory_page.h"
#include "security_page.h"
#include "data_quality_page.h"
#include "agent_health_page.h"
#include "network_page.h"
#include "proactive_briefing_widget.h"
#include "troubleshooting_page.h"
#include "../monitoring/hardware_change_tracker.h"
#include "../monitoring/software_change_tracker.h"
#include "../monitoring/telemetry_service.h"
#include "../monitoring/system_collector.h"
#include "../monitoring/background_relief.h"
#include "../monitoring/event_log_collector.h"
#include "../monitoring/app_inventory_collector.h"
#include "../monitoring/security_status_collector.h"
#include "../settings/user_preferences.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QColor>
#include <QEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QDir>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QGridLayout>
#include <QIcon>
#include <QProcess>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPdfWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QProgressBar>
#include <QPointer>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QScreen>
#include <QShowEvent>
#include <QStatusBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QRegularExpression>
#include <QTimeEdit>
#include <QUrl>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace Ausyn {

class ProcessTrendChart final : public QWidget {
public:
    explicit ProcessTrendChart(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(250);
        setMaximumHeight(278);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(QStringLiteral("Selected process CPU and memory trend"));
    }

    void setSamples(const QVector<ProcessTrendPoint>& samples)
    {
        samples_ = samples;
        int cpuCount = 0;
        int memoryCount = 0;
        int readCount = 0;
        int writeCount = 0;
        for (const ProcessTrendPoint& point : samples_) {
            if (point.cpuPercent) ++cpuCount;
            if (point.workingSetGigabytes) ++memoryCount;
            if (point.readMegabytesPerSecond) ++readCount;
            if (point.writeMegabytesPerSecond) ++writeCount;
        }
        const QString latestCpu = !samples_.isEmpty() && samples_.last().cpuPercent
            ? QStringLiteral("%1%").arg(*samples_.last().cpuPercent, 0, 'f', 1)
            : QStringLiteral("not sampled");
        const QString latestMemory = !samples_.isEmpty() && samples_.last().workingSetGigabytes
            ? QStringLiteral("%1 GB").arg(*samples_.last().workingSetGigabytes, 0, 'f', 2)
            : QStringLiteral("not sampled");
        const QString latestRead = !samples_.isEmpty() && samples_.last().readMegabytesPerSecond
            ? QStringLiteral("%1 MB/s").arg(*samples_.last().readMegabytesPerSecond, 0, 'f', 2)
            : QStringLiteral("not sampled");
        const QString latestWrite = !samples_.isEmpty() && samples_.last().writeMegabytesPerSecond
            ? QStringLiteral("%1 MB/s").arg(*samples_.last().writeMegabytesPerSecond, 0, 'f', 2)
            : QStringLiteral("not sampled");
        setAccessibleDescription(QStringLiteral("Up to %1 recent readings held in memory. CPU: %2 samples, latest %3. Working set: %4 samples, latest %5. Read I/O: %6 samples, latest %7. Write I/O: %8 samples, latest %9. Missing readings are omitted from their line.")
            .arg(samples_.size()).arg(cpuCount).arg(latestCpu).arg(memoryCount).arg(latestMemory)
            .arg(readCount).arg(latestRead).arg(writeCount).arg(latestWrite));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#0f1622")));
        const double left = rect().left() + 42.0;
        const double width = std::max(1.0, rect().width() - 58.0);
        const double sectionHeight = std::max(1.0, (rect().height() - 70.0) / 3.0);
        const QRectF cpuPlot(left, rect().top() + 29.0, width, sectionHeight);
        const QRectF memoryPlot(left, cpuPlot.bottom() + 8.0, width, sectionHeight);
        const QRectF ioPlot(left, memoryPlot.bottom() + 8.0, width, sectionHeight);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        const QColor muted(QStringLiteral("#8592a7"));
        const QColor grid(QStringLiteral("#263244"));
        const QColor cpuColor(QStringLiteral("#a9a8ff"));
        const QColor memoryColor(QStringLiteral("#57d6bd"));
        painter.setPen(QPen(cpuColor, 2.3));
        painter.drawLine(QPointF(44, 12), QPointF(62, 12));
        painter.setPen(muted);
        painter.drawText(QRectF(68, 3, 120, 18), Qt::AlignVCenter, QStringLiteral("CPU (%)"));
        painter.setPen(QPen(memoryColor, 2.3));
        painter.drawLine(QPointF(150, 12), QPointF(168, 12));
        painter.setPen(muted);
        painter.drawText(QRectF(174, 3, 105, 18), Qt::AlignVCenter, QStringLiteral("Memory (GB)"));
        painter.setPen(QPen(QColor(QStringLiteral("#f0be73")), 2.3));
        painter.drawLine(QPointF(285, 12), QPointF(303, 12));
        painter.setPen(muted);
        painter.drawText(QRectF(309, 3, 80, 18), Qt::AlignVCenter, QStringLiteral("Read I/O"));
        painter.setPen(QPen(QColor(QStringLiteral("#69d9a8")), 2.3));
        painter.drawLine(QPointF(394, 12), QPointF(412, 12));
        painter.setPen(muted);
        painter.drawText(QRectF(418, 3, 85, 18), Qt::AlignVCenter, QStringLiteral("Write I/O"));

        double maximumMemoryGb = 1.0;
        for (const ProcessTrendPoint& point : samples_)
            if (point.workingSetGigabytes) maximumMemoryGb = std::max(maximumMemoryGb, *point.workingSetGigabytes);
        double memoryScaleGb = 1.0;
        while (memoryScaleGb < maximumMemoryGb) memoryScaleGb *= 2.0;
        double maximumIoMegabytesPerSecond = 1.0;
        for (const ProcessTrendPoint& point : samples_) {
            if (point.readMegabytesPerSecond)
                maximumIoMegabytesPerSecond = std::max(maximumIoMegabytesPerSecond, *point.readMegabytesPerSecond);
            if (point.writeMegabytesPerSecond)
                maximumIoMegabytesPerSecond = std::max(maximumIoMegabytesPerSecond, *point.writeMegabytesPerSecond);
        }
        double ioScaleMegabytesPerSecond = 1.0;
        while (ioScaleMegabytesPerSecond < maximumIoMegabytesPerSecond)
            ioScaleMegabytesPerSecond *= 2.0;

        const auto drawGrid = [&painter, &muted, &grid](const QRectF& plot, double maximum, const QString& unit) {
            for (int mark = 0; mark <= 2; ++mark) {
                const double ratio = static_cast<double>(mark) / 2.0;
                const double y = plot.bottom() - ratio * plot.height();
                painter.setPen(QPen(grid, 1));
                painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
                painter.setPen(muted);
                painter.drawText(QRectF(0.0, y - 8.0, 36.0, 16.0), Qt::AlignRight | Qt::AlignVCenter,
                    QStringLiteral("%1%2").arg(maximum * (1.0 - ratio), 0, 'f', maximum < 10.0 ? 1 : 0).arg(unit));
            }
        };
        drawGrid(cpuPlot, 100.0, QStringLiteral("%"));
        drawGrid(memoryPlot, memoryScaleGb, QStringLiteral(" GB"));

        const auto drawSeries = [this, &painter](const QRectF& plot, double maximum,
                                                  const QColor& color, const auto& valueForPoint) {
            int validSamples = 0;
            for (const ProcessTrendPoint& point : samples_)
                if (valueForPoint(point).has_value()) ++validSamples;
            if (validSamples < 2) return;
            painter.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            bool hasPrevious = false;
            QPointF previous;
            const double denominator = static_cast<double>(std::max<qsizetype>(1, samples_.size() - 1));
            for (qsizetype index = 0; index < samples_.size(); ++index) {
                const std::optional<double> metric = valueForPoint(samples_.at(index));
                if (!metric) {
                    hasPrevious = false;
                    continue;
                }
                const double x = plot.left() + static_cast<double>(index) / denominator * plot.width();
                const double y = plot.bottom() - std::clamp(*metric / maximum, 0.0, 1.0) * plot.height();
                const QPointF point(x, y);
                if (hasPrevious) painter.drawLine(previous, point);
                painter.setBrush(color.lighter(115));
                painter.drawEllipse(point, 2.5, 2.5);
                previous = point;
                hasPrevious = true;
            }
        };
        drawGrid(ioPlot, ioScaleMegabytesPerSecond, QStringLiteral(" MB/s"));
        drawSeries(cpuPlot, 100.0, cpuColor,
            [](const ProcessTrendPoint& point) { return point.cpuPercent; });
        drawSeries(memoryPlot, memoryScaleGb, memoryColor,
            [](const ProcessTrendPoint& point) { return point.workingSetGigabytes; });
        drawSeries(ioPlot, ioScaleMegabytesPerSecond, QColor(QStringLiteral("#f0be73")),
            [](const ProcessTrendPoint& point) { return point.readMegabytesPerSecond; });
        drawSeries(ioPlot, ioScaleMegabytesPerSecond, QColor(QStringLiteral("#69d9a8")),
            [](const ProcessTrendPoint& point) { return point.writeMegabytesPerSecond; });
        if (samples_.isEmpty()) {
            painter.setPen(muted);
            painter.drawText(rect().adjusted(42, 26, -12, -4), Qt::AlignCenter,
                QStringLiteral("Collecting recent process readings…"));
        }
        painter.setPen(muted);
        painter.drawText(QRectF(ioPlot.left(), ioPlot.bottom() + 4.0, ioPlot.width(), 16.0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Older samples                                      Now"));
    }

private:
    QVector<ProcessTrendPoint> samples_;
};

namespace {

void setLabelStyleIfChanged(QLabel* label, const QString& style)
{
    if (!label || label->styleSheet() == style) return;
    if (label->property("ausynThemeBaseStyle").toString() == style &&
        label->property("ausynThemeRenderedStyle").toString() == label->styleSheet()) return;
    label->setStyleSheet(style);
}

class ProcessTableItem final : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant left = data(Qt::UserRole);
        const QVariant right = other.data(Qt::UserRole);
        if (left.isValid() && right.isValid()) return left.toDouble() < right.toDouble();
        return QString::localeAwareCompare(text(), other.text()) < 0;
    }
};

void setProcessCell(QTableWidget* table, int row, int column, const QString& text,
                    const QVariant& number = QVariant())
{
    QTableWidgetItem* item = table->item(row, column);
    if (!item) {
        item = new ProcessTableItem;
        table->setItem(row, column, item);
    }
    if (item->text() != text) item->setText(text);
    if (item->data(Qt::UserRole) != number) item->setData(Qt::UserRole, number);
    if (item->background() != QBrush()) item->setBackground(QBrush());
    if (!item->toolTip().isEmpty()) item->setToolTip(QString());
}
QString assistantMessageHtml(const QString& message)
{
    QString html = message.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    static const QRegularExpression urlPattern(QStringLiteral("https://[^\\s<>]+"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = urlPattern.globalMatch(html);
    QList<QPair<int, QString>> links;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        QString shown = match.captured();
        QString url = shown;
        while (!url.isEmpty() && QStringLiteral(".,;:!?)").contains(url.back())) {
            url.chop(1);
            shown.chop(1);
        }
        QString decoded = url;
        decoded.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        const QUrl parsed(decoded);
        if (parsed.scheme() == QLatin1String("https") && !parsed.host().isEmpty() && !shown.isEmpty())
            links.append({match.capturedStart(), shown});
    }
    for (auto link = links.crbegin(); link != links.crend(); ++link) {
        const int start = link->first;
        const int length = link->second.size();
        const QString escaped = link->second;
        const QString anchor = QStringLiteral("<a href=\"%1\" style=\"color:#a9a8ff\">%1</a>").arg(escaped);
        html.replace(start, length, anchor);
    }
    return html;
}

bool isCasualConversation(const QString& message)
{
    QString normalized = message.trimmed().toCaseFolded();
    while (!normalized.isEmpty() && QStringLiteral("!?.,;:").contains(normalized.back()))
        normalized.chop(1);
    static const QSet<QString> socialPrompts{
        QStringLiteral("hi"), QStringLiteral("hello"), QStringLiteral("hey"),
        QStringLiteral("good morning"), QStringLiteral("good afternoon"), QStringLiteral("good evening"),
        QStringLiteral("how are you"), QStringLiteral("how are you doing"), QStringLiteral("thanks"),
        QStringLiteral("thank you"), QStringLiteral("bye"), QStringLiteral("goodbye"),
        QStringLiteral("who are you"), QStringLiteral("what are you"), QStringLiteral("what can you do"),
        QStringLiteral("help")};
    return socialPrompts.contains(normalized);
}

QWidget* makeScrollablePage(QWidget* page)
{
    if (qobject_cast<QScrollArea*>(page)) return page;
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pageScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(page);
    return scroll;
}

[[maybe_unused]] QString proactiveBriefing(const AnalysisUpdate& analysis,
                          const SystemSnapshot& snapshot,
                          const QStringList& securityNotices,
                          const QStringList& eventNotices,
                          const QStringList& startupNotices)
{
    QStringList notices;
    QVector<Finding> findings = analysis.findings;
    std::stable_sort(findings.begin(), findings.end(), [](const Finding& left, const Finding& right) {
        return static_cast<int>(left.severity) > static_cast<int>(right.severity);
    });
    for (qsizetype i = 0; i < std::min<qsizetype>(findings.size(), 2); ++i) {
        const Finding& finding = findings.at(i);
        QString item = QStringLiteral("%1 — %2")
            .arg(finding.title, finding.recommendation.isEmpty() ? finding.summary : finding.recommendation);
        const qint64 processSampleAgeMilliseconds = snapshot.processSamplesCapturedAt.isValid()
            ? snapshot.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        const qint64 processFreshnessLimitSeconds = std::max<qint64>(
            10, std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3);
        if (finding.ruleId == QStringLiteral("personal-baseline-resource-shift") &&
            processSampleAgeMilliseconds >= 0 &&
            processSampleAgeMilliseconds <= processFreshnessLimitSeconds * 1000) {
            const ProcessSample* highestCpu = nullptr;
            const ProcessSample* largestMemory = nullptr;
            for (const ProcessSample& process : snapshot.topProcesses) {
                if (process.cpuPercent && (!highestCpu || *process.cpuPercent > highestCpu->cpuPercent.value_or(-1.0)))
                    highestCpu = &process;
                if (process.workingSetBytes && (!largestMemory || *process.workingSetBytes > largestMemory->workingSetBytes.value_or(0)))
                    largestMemory = &process;
            }
            if (finding.evidence.contains(QStringLiteral("CPU averaged"), Qt::CaseInsensitive) && highestCpu) {
                item += QStringLiteral(" Current process sample: %1 is highest listed CPU at %2%; that’s context, not proof of cause.")
                    .arg(highestCpu->name).arg(*highestCpu->cpuPercent, 0, 'f', 1);
            }
            if (finding.evidence.contains(QStringLiteral("Memory averaged"), Qt::CaseInsensitive) && largestMemory) {
                item += QStringLiteral(" Largest listed working set: %1 (%2 GB); a process snapshot alone doesn’t establish cause.")
                    .arg(largestMemory->name)
                    .arg(static_cast<double>(*largestMemory->workingSetBytes) / 1'000'000'000.0, 0, 'f', 1);
            }
        }
        notices << item;
    }
    notices.append(securityNotices);
    notices.append(eventNotices);
    notices.append(startupNotices);

    const StorageForecast& storage = analysis.storageForecast;
    if (storage.rapidDropDetected) {
        notices << QStringLiteral("Storage changed sharply: daily system-drive samples show a %1 percentage-point decrease in free space. Check recent downloads, updates, or files you recognize; Ausyn cannot identify or remove files.")
            .arg(storage.rapidDropPercentagePoints, 0, 'f', 1);
    }
    if (storage.hasEstimate && storage.daysUntilTenPercent <= 90.0 && storage.daysUntilTenPercent >= 0.0) {
        const QString time = storage.daysUntilTenPercent < 2.0
            ? QStringLiteral("about %1 hour(s)").arg(std::max(1, static_cast<int>(std::lround(storage.daysUntilTenPercent * 24.0))))
            : QStringLiteral("about %1 day(s)").arg(static_cast<int>(std::lround(storage.daysUntilTenPercent)));
        notices << QStringLiteral("Storage trend: system-drive free space could reach 10% in %1 if the recent pattern continues. Review the Predictions page.").arg(time);
    }
    const auto volumeHeadsUp = std::find_if(analysis.volumeStorageForecasts.cbegin(),
        analysis.volumeStorageForecasts.cend(), [](const VolumeStorageForecast& volume) {
            const StorageForecast& forecast = volume.forecast;
            return forecast.currentFreePercent > 10.0 &&
                (forecast.rapidDropDetected || (forecast.hasEstimate &&
                 forecast.daysUntilTenPercent >= 0.0 && forecast.daysUntilTenPercent <= 90.0));
        });
    if (volumeHeadsUp != analysis.volumeStorageForecasts.cend()) {
        const StorageForecast& forecast = volumeHeadsUp->forecast;
        const QString drive = volumeHeadsUp->label.isEmpty() ? volumeHeadsUp->rootPath
            : QStringLiteral("%1 (%2)").arg(volumeHeadsUp->label, volumeHeadsUp->rootPath);
        if (forecast.rapidDropDetected) {
            notices << QStringLiteral("%1 changed sharply: daily samples show free space fell by %2 percentage points. Review the Predictions page; Ausyn cannot identify which files used the space.")
                .arg(drive).arg(forecast.rapidDropPercentagePoints, 0, 'f', 1);
        } else {
            notices << QStringLiteral("%1 could reach 10% free space in about %2 day(s) if its recent trend continues. Review the Predictions page.")
                .arg(drive).arg(static_cast<int>(std::lround(forecast.daysUntilTenPercent)));
        }
    }

    const MemoryPressureForecast& memory = analysis.memoryForecast;
    if (memory.hasEstimate && memory.minutesUntil90Percent >= 0.0 &&
        memory.minutesUntil90Percent <= 60.0) {
        notices << QStringLiteral("Memory pressure outlook: the recent average could reach 90% in about %1 minute(s) if it keeps rising. This is not a memory-leak diagnosis.")
            .arg(std::max(1, static_cast<int>(std::lround(memory.minutesUntil90Percent))));
    }

    const BatteryForecast& battery = analysis.batteryForecast;
    if (battery.hasEstimate && battery.minutesUntil15Percent <= 120.0 && battery.minutesUntil15Percent >= 0.0) {
        const int minutes = std::max(1, static_cast<int>(std::lround(battery.minutesUntil15Percent)));
        const QString time = minutes < 60
            ? QStringLiteral("about %1 minute(s)").arg(minutes)
            : QStringLiteral("about %1 hour(s)").arg(std::max(1, static_cast<int>(std::lround(minutes / 60.0))));
        notices << QStringLiteral("Battery trend: charge could reach 15% in %1 at the recent rate. Connect power if that timing matters; see Predictions for the estimate limits.").arg(time);
    }

    if (!notices.isEmpty())
        return QStringLiteral("Ausyn noticed:\n• %1\n\nThese are evidence-based heads-ups, not proof of a cause. Forecasts assume recent trends continue.")
            .arg(notices.mid(0, 3).join(QStringLiteral("\n• ")));
    if (!analysis.historyAvailable)
        return QStringLiteral("Nothing currently needs attention in the monitored readings. I’m still collecting local history for useful forecasts, and I’ll surface a heads-up when the evidence supports one.");
    return QStringLiteral("Nothing currently needs attention in the monitored readings. I’m keeping an eye on sustained changes and near-term storage or battery trends.");
}

constexpr auto kWindowStyle = R"(
QMainWindow, QWidget#appRoot { background: #0b0f17; color: #e8edf6; }
QWidget { color: #e8edf6; }
QFrame#sidebar { background: #0e131d; border-right: 1px solid #202938; }
QLabel#brandMark, QPushButton#brandMark { color: #a9a8ff; background: #1d2035; border: 1px solid #363957;
                   border-radius: 12px; font-size: 18px; font-weight: 800; }
QLabel#brandName { color: #f4f6fc; font-size: 18px; font-weight: 750; letter-spacing: 1px; }
QLabel#brandSub { color: #7f8aa0; font-size: 10px; letter-spacing: 1px; }
QLabel#navSection { color: #68758b; font-size: 10px; font-weight: 700; letter-spacing: 1.4px; padding: 8px 11px 3px; }
QToolButton#navButton { color: #9aa5b8; text-align: left; border: 0; border-radius: 9px;
                        padding: 10px 11px; font-size: 13px; }
QToolButton#navButton:hover { background: #171e2b; color: #e8edf6; }
QToolButton#navButton:checked { background: #1e2436; color: #c2c0ff; font-weight: 650; }
QLabel#sidebarStatus { color: #8592a7; font-size: 11px; }
QFrame#topBar { background: #0b0f17; border-bottom: 1px solid #202938; }
QLabel#breadcrumb { color: #f0f3fa; font-size: 16px; font-weight: 650; }
QLabel#subtle { color: #8995aa; font-size: 12px; }
QLabel#eyebrow { color: #9f9cff; font-size: 10px; font-weight: 750; letter-spacing: 1.5px; }
QLabel#heroTitle { color: #f3f5fb; font-size: 27px; font-weight: 700; }
QLabel#heroBody { color: #a4aec0; font-size: 13px; }
QFrame#panel { background: #111824; border: 1px solid #222e3e; border-radius: 14px; }
QTableWidget { background: #111824; alternate-background-color: #141d2a; color: #d7dfed;
                            border: 1px solid #263143; border-radius: 8px; gridline-color: #202b3a;
                            selection-background-color: #242946; selection-color: #f0f2ff; }
QHeaderView::section { background: #151f2d; color: #93a0b5; border: 0; border-bottom: 1px solid #293548;
                       padding: 9px 10px; font-size: 11px; font-weight: 650; }
QTableWidget#processTable::item { padding: 6px 8px; border: 0; }
QLabel#panelTitle { color: #e5eaf4; font-size: 14px; font-weight: 650; }
QLabel#metricValue { color: #c3c2ff; font-size: 24px; font-weight: 700; }
QLabel#metricName { color: #99a5b9; font-size: 12px; font-weight: 600; }
QLabel#metricHint { color: #748197; font-size: 10px; }
QLabel[ausynRole="metricValue"] { color: #c3c2ff; font-size: 24px; font-weight: 700; }
QLabel[ausynRole="metricHint"] { color: #748197; font-size: 10px; }
QLabel#healthBody, QLabel#liveActivity { color: #8995aa; font-size: 12px; }
QLabel#metricIcon { color: #a9a8ff; background: #20253a; border-radius: 10px; font-size: 15px; }
QPushButton#primaryButton, QPushButton#assistantSend { background: #827cff; color: #111322; border: 0; border-radius: 9px;
                            padding: 10px 15px; font-weight: 700; }
QPushButton#primaryButton:hover, QPushButton#assistantSend:hover { background: #a09cff; }
QPushButton#secondaryButton { background: #171f2c; color: #c7d0df; border: 1px solid #2a3547;
                              border-radius: 9px; padding: 9px 13px; }
QPushButton#secondaryButton:hover { background: #202a3a; }
QFrame#statusPill { background: #171f2b; border: 1px solid #2a3546; border-radius: 9px; }
QLabel#statusDot { color: #f0be73; font-size: 12px; }
QLabel#statusText { color: #b8c3d3; font-size: 11px; }
QFrame#placeholderIcon { background: #1b2032; border: 1px solid #333957; border-radius: 18px; }
QLabel#placeholderGlyph { color: #a9a8ff; font-size: 28px; }
QScrollArea { border: 0; background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #2b3548; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QLabel#hubTitle { color:#f4f6fc; font-size:27px; font-weight:700; }
QLabel#dashboardSummary { color:#f4f6fc; font-size:24px; font-weight:700; }
QLabel#dashboardNextStep { color:#b5c0d0; font-size:13px; }
QLabel#dashboardActivity { color:#87d7b0; font-size:11px; }
QLabel#inspectorValue { color:#b8b6ff; font-size:32px; font-weight:700; }
QWidget#inspectorBody { background:#0b0f17; }
QLabel#inspectorFreshness { color:#93a0b5; font-size:11px; }
QProgressBar#inspectorLevel { border:none; background:#263244; border-radius:4px; }
QProgressBar#inspectorLevel::chunk { background:#9690ff; border-radius:4px; }
QLabel#inspectorAdvice { background:#171f2c; color:#cad4e2; border:1px solid #293548; border-radius:10px; padding:14px; }
QFrame#adviceBanner { background:#252238; border-bottom:1px solid #6964be; }
QLabel#adviceBannerTitle { color:#e1dfff; font-weight:700; }
QLabel#adviceBannerBody { color:#c5cfe0; font-size:11px; }
QFrame#panel[resourceKind]:hover { border:1px solid #827cff; }
QFrame#dashboardHero { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #1d2035,stop:1 #111824); border:1px solid #333957; border-radius:18px; }
QLabel#dashboardCheckResult { background:#151e2b; border:1px solid #293548; border-radius:12px; padding:18px; color:#cad4e2; }
QPushButton#pauseMonitoring { background:#171f2c; color:#c7d0df; border:1px solid #2a3547; border-radius:9px; padding:9px 13px; }
QTabBar#hubTabs::tab { background:transparent; color:#93a0b5; padding:9px 14px; border-bottom:2px solid transparent; }
QTabBar#hubTabs::tab:selected { color:#c3c2ff; border-bottom:2px solid #827cff; }
QTabBar#hubTabs::tab:hover { background:#1b2032; border-radius:6px; }
QCheckBox { spacing:9px; min-height:28px; }
QComboBox, QLineEdit, QTimeEdit { min-height:30px; }
QComboBox, QLineEdit, QTimeEdit, QDoubleSpinBox { background:#151e2b; color:#cad4e2; border:1px solid #293548; border-radius:7px; padding:3px 9px; }
QComboBox QAbstractItemView { background:#151e2b; color:#cad4e2; selection-background-color:#242946; }
QStackedWidget { background:#0b0f17; }
)";

QFrame* makePanel(QWidget* parent = nullptr)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    return panel;
}

QWidget* makeMetricCard(const QString& key, const QString& name, const QString& glyph, const QString& status)
{
    static const QHash<QString, ResourceKind> resources{{QStringLiteral("cpu"), ResourceKind::Cpu}, {QStringLiteral("memory"), ResourceKind::Memory},
        {QStringLiteral("storage"), ResourceKind::Storage}, {QStringLiteral("gpu"), ResourceKind::Graphics},
        {QStringLiteral("battery"), ResourceKind::Battery}, {QStringLiteral("network"), ResourceKind::Network}};
    auto* card = new ResourceCard(resources.value(key));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(17, 16, 17, 15);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout;
    auto* title = new QLabel(name, card);
    title->setObjectName(QStringLiteral("metricName"));
    auto* icon = new QLabel(glyph, card);
    icon->setObjectName(QStringLiteral("metricIcon"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(34, 34);
    top->addWidget(title);
    top->addStretch();
    top->addWidget(icon);

    auto* value = new QLabel(QStringLiteral("—"), card);
    value->setProperty("ausynRole", QStringLiteral("metricValue"));
    auto* hint = new QLabel(status, card);
    hint->setProperty("ausynRole", QStringLiteral("metricHint"));
    hint->setWordWrap(true);
    hint->setProperty("detailOnly", true);
    value->setObjectName(QStringLiteral("metric.%1.value").arg(key));
    hint->setObjectName(QStringLiteral("metric.%1.hint").arg(key));
    layout->addLayout(top);
    layout->addWidget(value);
    layout->addWidget(hint);
    for (auto* label : card->findChildren<QLabel*>()) label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return card;
}

QWidget* makeDashboard(ProactiveBriefingWidget*& proactiveBriefing, QWidget*& quickStartPanel,
                       QPushButton*& quickStartCheck, QPushButton*& quickStartTroubleshoot,
                       QPushButton*& quickStartPrivacy, QPushButton*& quickStartHide)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 12, 28, 28);
    outer->setSpacing(16);
    auto* hero = makePanel(page);
    hero->setObjectName(QStringLiteral("dashboardHero"));
    auto* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(24, 22, 24, 22);
    auto* readout = new QVBoxLayout;
    auto* eyebrow = new QLabel(QStringLiteral("YOUR PC, UNDERSTOOD"), hero);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* summary = new QLabel(QStringLiteral("Getting to know your PC…"), hero);
    summary->setObjectName(QStringLiteral("dashboardSummary"));
    summary->setWordWrap(true);
    auto* next = new QLabel(QStringLiteral("I’m collecting Windows readings. Useful changes and the next step will appear here automatically."), hero);
    next->setObjectName(QStringLiteral("dashboardNextStep"));
    next->setWordWrap(true);
    auto* recent = new QLabel(QStringLiteral("Starting local monitoring"), hero);
    recent->setObjectName(QStringLiteral("dashboardActivity"));
    recent->setWordWrap(true);
    readout->addWidget(eyebrow);
    readout->addWidget(summary);
    readout->addWidget(next);
    readout->addSpacing(8);
    readout->addWidget(recent);
    heroLayout->addLayout(readout, 1);
    auto* ring = new ResourceRing(hero);
    ring->setObjectName(QStringLiteral("performanceRing"));
    ring->setProperty("detailOnly", true);
    heroLayout->addWidget(ring);
    outer->addWidget(hero);

    auto* actions = new QHBoxLayout;
    quickStartCheck = new QPushButton(QStringLiteral("Check my system"), page);
    quickStartCheck->setObjectName(QStringLiteral("primaryButton"));
    quickStartCheck->setToolTip(QStringLiteral("Show a fresh local assessment here on the dashboard."));
    quickStartTroubleshoot = new QPushButton(QStringLiteral("Guided help"), page);
    quickStartTroubleshoot->setObjectName(QStringLiteral("secondaryButton"));
    auto* pause = new QPushButton(QStringLiteral("Pause monitoring"), page);
    pause->setObjectName(QStringLiteral("pauseMonitoring"));
    actions->addWidget(quickStartCheck);
    actions->addWidget(quickStartTroubleshoot);
    actions->addWidget(pause);
    actions->addStretch();
    outer->addLayout(actions);
    auto* result = new QLabel(page);
    result->setObjectName(QStringLiteral("dashboardCheckResult"));
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result->hide();
    outer->addWidget(result);

    quickStartPanel = makePanel(page);
    auto* guide = new QHBoxLayout(quickStartPanel);
    guide->setContentsMargins(14, 10, 14, 10);
    auto* note = new QLabel(QStringLiteral("Private by default. Use Details to see evidence, findings and forecasts."), quickStartPanel);
    note->setObjectName(QStringLiteral("subtle")); note->setWordWrap(true);
    quickStartPrivacy = new QPushButton(QStringLiteral("Settings"), quickStartPanel);
    quickStartPrivacy->setObjectName(QStringLiteral("secondaryButton"));
    quickStartHide = new QPushButton(QStringLiteral("Dismiss tip"), quickStartPanel);
    quickStartHide->setObjectName(QStringLiteral("secondaryButton"));
    guide->addWidget(note, 1); guide->addWidget(quickStartPrivacy); guide->addWidget(quickStartHide);

    auto* metrics = new QGridLayout;
    metrics->setSpacing(12);
    const std::array<QString, 6> keys{QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("storage"), QStringLiteral("gpu"), QStringLiteral("battery"), QStringLiteral("network")};
    const std::array<QString, 6> names{QStringLiteral("Processor"), QStringLiteral("Memory"), QStringLiteral("Storage used"), QStringLiteral("Graphics"), QStringLiteral("Battery"), QStringLiteral("Network")};
    const std::array<QString, 6> glyphs{QStringLiteral("◉"), QStringLiteral("▤"), QStringLiteral("▱"), QStringLiteral("◇"), QStringLiteral("ϟ"), QStringLiteral("⌁")};
    for (int i = 0; i < 6; ++i) {
        QWidget* card = makeMetricCard(keys[i], names[i], glyphs[i], QStringLiteral("Waiting for a reading"));
        if (i >= 4) card->setProperty("detailOnly", true);
        metrics->addWidget(card, i / 3, i % 3);
    }
    outer->addLayout(metrics);

    auto* graph = makePanel(page);
    auto* graphLayout = new QVBoxLayout(graph);
    graphLayout->setContentsMargins(18, 15, 18, 12);
    auto* graphTitle = new QLabel(QStringLiteral("Recent activity"), graph);
    graphTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* chart = new LiveActivityChart(graph);
    chart->setObjectName(QStringLiteral("dashboardChart"));
    graphLayout->addWidget(graphTitle); graphLayout->addWidget(chart);
    outer->addWidget(graph);

    auto* briefing = makePanel(page);
    auto* briefingLayout = new QVBoxLayout(briefing);
    briefingLayout->setContentsMargins(18, 16, 18, 16);
    auto* briefingTitle = new QLabel(QStringLiteral("What needs your attention"), briefing);
    briefingTitle->setObjectName(QStringLiteral("panelTitle"));
    proactiveBriefing = new ProactiveBriefingWidget(briefing);
    proactiveBriefing->setMinimumHeight(250);
    briefingLayout->addWidget(briefingTitle); briefingLayout->addWidget(proactiveBriefing);
    outer->addWidget(briefing);

    auto* measured = makePanel(page);
    measured->setProperty("detailOnly", true);
    auto* measuredLayout = new QVBoxLayout(measured);
    measuredLayout->setContentsMargins(18, 16, 18, 16);
    auto* healthValue = new QLabel(QStringLiteral("Gathering baseline…"), measured);
    healthValue->setObjectName(QStringLiteral("healthValue"));
    auto* healthBody = new QLabel(QStringLiteral("The performance index uses supported CPU, memory and drive signals. Other sensor coverage is shown in Settings → Details."), measured);
    healthBody->setObjectName(QStringLiteral("healthBody")); healthBody->setWordWrap(true);
    auto* live = new QLabel(measured); live->setObjectName(QStringLiteral("liveActivity")); live->setWordWrap(true);
    measuredLayout->addWidget(healthValue); measuredLayout->addWidget(healthBody); measuredLayout->addWidget(live);
    outer->addWidget(measured);
    outer->addWidget(quickStartPanel);
    outer->addStretch();
    return page;
}
QString formatGigabytes(quint64 bytes)
{
    return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / 1'000'000'000.0, 0, 'f', 1);
}

QString formatRate(const std::optional<double>& bytesPerSecond)
{
    if (!bytesPerSecond) {
        return QStringLiteral("Sampling…");
    }
    const double rate = *bytesPerSecond;
    if (rate >= 1'000'000.0) {
        return QStringLiteral("%1 MB/s").arg(rate / 1'000'000.0, 0, 'f', 1);
    }
    if (rate >= 1'000.0) {
        return QStringLiteral("%1 KB/s").arg(rate / 1'000.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 B/s").arg(rate, 0, 'f', 0);
}

QString formatFileSize(qint64 bytes)
{
    if (bytes >= 1'000'000) {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / 1'000'000.0, 0, 'f', 1);
    }
    if (bytes >= 1'000) {
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1'000.0, 0, 'f', 0);
    }
    return QStringLiteral("%1 bytes").arg(bytes);
}

QWidget* makePerformancePage(QTableWidget*& processTable, QLineEdit*& processSearch,
                             QLabel*& processSnapshotAge,
                             QLabel*& foregroundWorkload,
                             QPushButton*& inspectForegroundProcess,
                             QLabel*& processDetails, QLabel*& processImpactDetails,
                             ProcessTrendChart*& processTrendChart,
                             QWidget*& processorCoreGrid)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget(scroll);
    scroll->setWidget(page);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(16);

    auto* eyebrow = new QLabel(QStringLiteral("LIVE DEVICE ACTIVITY"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Performance"), page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(
        QStringLiteral("Search and sort the running process list. Read/write values are Windows process I/O rates, not guaranteed physical-disk throughput. Process access is limited to information available to your account. "
                       "TCP and UDP ownership counts are available when Windows exposes them; per-app network byte totals are not provided by these counters."), page);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* corePanel = makePanel(page);
    auto* coreLayout = new QVBoxLayout(corePanel);
    coreLayout->setContentsMargins(17, 15, 17, 15);
    coreLayout->setSpacing(10);
    auto* coreTitle = new QLabel(QStringLiteral("Logical processor activity"), corePanel);
    coreTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* coreNote = new QLabel(QStringLiteral("Per-core Windows counters · readings may be unavailable on some systems"), corePanel);
    coreNote->setObjectName(QStringLiteral("subtle"));
    processorCoreGrid = new QWidget(corePanel);
    processorCoreGrid->setObjectName(QStringLiteral("processorCoreGrid"));
    auto* coreGrid = new QGridLayout(processorCoreGrid);
    coreGrid->setContentsMargins(0, 0, 0, 0);
    coreGrid->setHorizontalSpacing(10);
    coreGrid->setVerticalSpacing(8);
    auto* waiting = new QLabel(QStringLiteral("Waiting for Windows processor counters…"), processorCoreGrid);
    waiting->setObjectName(QStringLiteral("subtle"));
    coreGrid->addWidget(waiting, 0, 0);
    coreLayout->addWidget(coreTitle);
    coreLayout->addWidget(coreNote);
    coreLayout->addWidget(processorCoreGrid);
    corePanel->setProperty("detailOnly", true);
    outer->addWidget(corePanel);

    auto* foregroundPanel = makePanel(page);
    auto* foregroundLayout = new QVBoxLayout(foregroundPanel);
    foregroundLayout->setContentsMargins(17, 15, 17, 15);
    foregroundLayout->setSpacing(8);
    auto* foregroundTitle = new QLabel(QStringLiteral("Active foreground app"), foregroundPanel);
    foregroundTitle->setObjectName(QStringLiteral("panelTitle"));
    foregroundWorkload = new QLabel(QStringLiteral("Waiting for Windows to identify the active app…"), foregroundPanel);
    foregroundWorkload->setObjectName(QStringLiteral("subtle"));
    foregroundWorkload->setWordWrap(true);
    foregroundWorkload->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* foregroundActions = new QHBoxLayout;
    inspectForegroundProcess = new QPushButton(QStringLiteral("Inspect active app"), foregroundPanel);
    inspectForegroundProcess->setObjectName(QStringLiteral("secondaryButton"));
    inspectForegroundProcess->setEnabled(false);
    inspectForegroundProcess->setAccessibleDescription(QStringLiteral(
        "Select the foreground app in the process list and show its local resource profile."));
    foregroundActions->addWidget(inspectForegroundProcess, 0, Qt::AlignLeft);
    foregroundActions->addStretch();
    foregroundLayout->addWidget(foregroundTitle);
    foregroundLayout->addWidget(foregroundWorkload);
    foregroundLayout->addLayout(foregroundActions);
    outer->addWidget(foregroundPanel);

    auto* sectionTitle = new QLabel(QStringLiteral("Running processes"), page);
    sectionTitle->setObjectName(QStringLiteral("panelTitle"));
    outer->addWidget(sectionTitle);

    processSearch = new QLineEdit(page);
    processSearch->setObjectName(QStringLiteral("searchField"));
    processSearch->setPlaceholderText(QStringLiteral("Search process name or ID…"));
    processSearch->setClearButtonEnabled(true);
    outer->addWidget(processSearch);

    processSnapshotAge=new QLabel(QStringLiteral("Waiting for process readings…"),page);
    processSnapshotAge->setObjectName(QStringLiteral("subtle"));
    processSnapshotAge->setAccessibleName(QStringLiteral("Process snapshot freshness"));
    outer->addWidget(processSnapshotAge);

    processTable = new QTableWidget(page);
    processTable->setObjectName(QStringLiteral("processTable"));
    processTable->setColumnCount(8);
    processTable->setHorizontalHeaderLabels({QStringLiteral("Process"), QStringLiteral("ID"),
                                              QStringLiteral("CPU"), QStringLiteral("Working set"),
                                              QStringLiteral("TCP"), QStringLiteral("UDP"),
                                              QStringLiteral("Read I/O/s"), QStringLiteral("Write I/O/s")});
    processTable->setRowCount(0);
    processTable->setAlternatingRowColors(true);
    processTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    processTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    processTable->setSelectionMode(QAbstractItemView::SingleSelection);
    processTable->verticalHeader()->hide();
    processTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    processTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    processTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    processTable->setShowGrid(false);
    processTable->setSortingEnabled(true);
    processTable->sortItems(3, Qt::DescendingOrder);
    QObject::connect(processSearch, &QLineEdit::textChanged, processTable,
        [processSearch, processTable](const QString& text) {
            const QString query = text.trimmed();
            for (int row = 0; row < processTable->rowCount(); ++row) {
                bool matches = query.isEmpty();
                for (int column = 0; !matches && column < processTable->columnCount(); ++column) {
                    const auto* item = processTable->item(row, column);
                    matches = item && item->text().contains(query, Qt::CaseInsensitive);
                }
                processTable->setRowHidden(row, !matches);
            }
        });
    outer->addWidget(processTable, 1);

    auto* detailPanel = makePanel(page);
    detailPanel->setProperty("detailOnly", true);
    auto* detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(17, 15, 17, 15);
    detailLayout->setSpacing(8);
    auto* detailTitle = new QLabel(QStringLiteral("Selected process"), detailPanel);
    detailTitle->setObjectName(QStringLiteral("panelTitle"));
    processDetails = new QLabel(QStringLiteral("Select a process to inspect its current readings and executable information."), detailPanel);
    processDetails->setObjectName(QStringLiteral("subtle"));
    processDetails->setWordWrap(true);
    processDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(detailTitle);
    detailLayout->addWidget(processDetails);
    auto* trendNote = new QLabel(QStringLiteral("Recent CPU, memory, and read/write I/O trends · up to 30 samples, held in memory only"), detailPanel);
    trendNote->setObjectName(QStringLiteral("subtle"));
    processTrendChart = new ProcessTrendChart(detailPanel);
    detailLayout->addWidget(trendNote);
    detailLayout->addWidget(processTrendChart);
    processImpactDetails = new QLabel(QStringLiteral("Select a process to build a short, local resource profile."), detailPanel);
    processImpactDetails->setObjectName(QStringLiteral("subtle"));
    processImpactDetails->setWordWrap(true);
    processImpactDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(processImpactDetails);
    outer->addWidget(detailPanel);
    return scroll;
}

QWidget* makeHardwarePage(QLabel*& details, QLabel*& volumes, QLabel*& changeStatus, QLabel*& timeline)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(17);

    auto* eyebrow = new QLabel(QStringLiteral("DETECTED DEVICE PROFILE"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Hardware"), page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(
        QStringLiteral("Ausyn reports hardware information exposed by Windows. Values that are not available "
                       "through standard device APIs are called out plainly."), page);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);
    changeStatus = new QLabel(page);
    changeStatus->setWordWrap(true);
    changeStatus->setVisible(false);
    changeStatus->setStyleSheet(QStringLiteral("color:#f2ca83;background:#292315;border:1px solid #594522;border-radius:10px;padding:12px 14px;font-size:12px;"));
    outer->addWidget(changeStatus);

    auto* profile = makePanel(page);
    auto* profileLayout = new QVBoxLayout(profile);
    profileLayout->setContentsMargins(21, 19, 21, 19);
    profileLayout->setSpacing(9);
    auto* profileTitle = new QLabel(QStringLiteral("Device profile"), profile);
    profileTitle->setObjectName(QStringLiteral("panelTitle"));
    details = new QLabel(QStringLiteral("Collecting device information…"), profile);
    details->setObjectName(QStringLiteral("hardwareDetails"));
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->setStyleSheet(QStringLiteral("color:#aeb9cb;font-size:13px;line-height:1.5;"));
    details->setWordWrap(true);
    profileLayout->addWidget(profileTitle);
    profileLayout->addSpacing(5);
    profileLayout->addWidget(details);
    outer->addWidget(profile);

    auto* storage = makePanel(page);
    auto* storageLayout = new QVBoxLayout(storage);
    storageLayout->setContentsMargins(21, 19, 21, 19);
    storageLayout->setSpacing(9);
    auto* storageTitle = new QLabel(QStringLiteral("Fixed drives"), storage);
    storageTitle->setObjectName(QStringLiteral("panelTitle"));
    volumes = new QLabel(QStringLiteral("Checking mounted drives…"), storage);
    volumes->setObjectName(QStringLiteral("hardwareVolumes"));
    volumes->setWordWrap(true);
    volumes->setStyleSheet(QStringLiteral("color:#aeb9cb;font-size:13px;"));
    storageLayout->addWidget(storageTitle);
    storageLayout->addWidget(volumes);
    outer->addWidget(storage);

    auto* changes = makePanel(page);
    auto* changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(21, 19, 21, 19);
    changesLayout->setSpacing(9);
    auto* changesHeader = new QHBoxLayout;
    auto* changesTitle = new QLabel(QStringLiteral("Recent device changes"), changes);
    changesTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* clearChanges = new QPushButton(QStringLiteral("Clear timeline"), changes);
    clearChanges->setObjectName(QStringLiteral("secondaryButton"));
    clearChanges->setProperty("clearDeviceChangeHistory", true);
    changesHeader->addWidget(changesTitle);
    changesHeader->addStretch();
    changesHeader->addWidget(clearChanges);
    timeline = new QLabel(QStringLiteral("No device-profile changes have been recorded."), changes);
    timeline->setObjectName(QStringLiteral("hardwareTimeline"));
    timeline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    timeline->setWordWrap(true);
    timeline->setAccessibleDescription(QStringLiteral("Recent Windows-reported hardware profile changes, newest first. The list is stored locally and does not establish the cause of later performance changes."));
    timeline->setStyleSheet(QStringLiteral("color:#aeb9cb;font-size:12px;line-height:1.5;"));
    changesLayout->addLayout(changesHeader);
    changesLayout->addWidget(timeline);
    outer->addWidget(changes);
    outer->addStretch(1);
    return page;
}

QWidget* makeInsightPage(const QString& title,
                         const QString& eyebrowText,
                         const QString& description,
                         InsightListWidget*& findingsView)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(14);

    auto* eyebrow = new QLabel(eyebrowText, page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(description, page);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* panel = makePanel(page);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(13, 14, 5, 14);
    findingsView = new InsightListWidget(panel);
    panelLayout->addWidget(findingsView);
    outer->addWidget(panel, 1);
    return page;
}

QWidget* makeHistoryPage(HistoryChart*& chart, QLabel*& status, QLabel*& summary,
                         QComboBox*& period)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(15);

    auto* eyebrow = new QLabel(QStringLiteral("LOCAL DEVICE HISTORY"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("History & reports"), page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(
        QStringLiteral("Review locally stored processor and memory readings over a selected period. Samples stay on this "
                       "device for up to 30 days."), page);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* actions = new QHBoxLayout;
    period = new QComboBox(page);
    period->setObjectName(QStringLiteral("reportPeriod"));
    period->addItem(QStringLiteral("Last 24 hours"), 24);
    period->addItem(QStringLiteral("Last 7 days"), 24 * 7);
    period->addItem(QStringLiteral("Last 30 days"), 24 * 30);
    actions->addWidget(new QLabel(QStringLiteral("Report period"), page));
    actions->addWidget(period);
    auto* generateButton = new QPushButton(QStringLiteral("Generate summary"), page);
    generateButton->setObjectName(QStringLiteral("secondaryButton"));
    generateButton->setProperty("historyAction", QStringLiteral("generateReport"));
    auto* reportButton = new QPushButton(QStringLiteral("Export report · PDF or HTML"), page);
    reportButton->setObjectName(QStringLiteral("secondaryButton"));
    reportButton->setProperty("historyAction", QStringLiteral("exportReport"));
    auto* exportButton = new QPushButton(QStringLiteral("Export CSV"), page);
    exportButton->setObjectName(QStringLiteral("secondaryButton"));
    exportButton->setProperty("historyAction", QStringLiteral("export"));
    auto* backupButton = new QPushButton(QStringLiteral("Back up history"), page);
    backupButton->setObjectName(QStringLiteral("secondaryButton"));
    backupButton->setProperty("historyAction", QStringLiteral("backup"));
    auto* restoreButton = new QPushButton(QStringLiteral("Restore backup"), page);
    restoreButton->setObjectName(QStringLiteral("secondaryButton"));
    restoreButton->setProperty("historyAction", QStringLiteral("restore"));
    auto* clearButton = new QPushButton(QStringLiteral("Clear local data"), page);
    clearButton->setObjectName(QStringLiteral("secondaryButton"));
    clearButton->setProperty("historyAction", QStringLiteral("clear"));
    actions->addWidget(generateButton);
    actions->addWidget(reportButton);
    actions->addWidget(exportButton);
    actions->addWidget(backupButton);
    actions->addWidget(restoreButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    outer->addLayout(actions);

    auto* panel = makePanel(page);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(18, 15, 18, 14);
    status = new QLabel(QStringLiteral("Waiting for the first local sample…"), panel);
    status->setObjectName(QStringLiteral("subtle"));
    summary = new QLabel(QStringLiteral("A report summary will appear when local readings are available."), panel);
    summary->setObjectName(QStringLiteral("subtle"));
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("background:#151e2b;border:1px solid #283448;border-radius:10px;padding:12px;color:#c8d2e0;"));
    chart = new HistoryChart(panel);
    panelLayout->addWidget(status);
    panelLayout->addWidget(summary);
    panelLayout->addWidget(chart, 1);
    outer->addWidget(panel, 1);
    return page;
}

QWidget* makePlaceholderPage(const QString& title, const QString& glyph, const QString& description)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    outer->setSpacing(0);

    auto* eyebrow = new QLabel(QStringLiteral("AUSYN WORKSPACE"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    outer->addWidget(eyebrow);
    outer->addSpacing(7);
    outer->addWidget(heading);
    outer->addStretch(1);

    auto* card = makePanel(page);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setAlignment(Qt::AlignCenter);
    cardLayout->setContentsMargins(44, 46, 44, 46);
    cardLayout->setSpacing(14);

    auto* iconFrame = new QFrame(card);
    iconFrame->setObjectName(QStringLiteral("placeholderIcon"));
    iconFrame->setFixedSize(66, 66);
    auto* iconLayout = new QVBoxLayout(iconFrame);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    auto* icon = new QLabel(glyph, iconFrame);
    icon->setObjectName(QStringLiteral("placeholderGlyph"));
    icon->setAlignment(Qt::AlignCenter);
    iconLayout->addWidget(icon);
    cardLayout->addWidget(iconFrame, 0, Qt::AlignHCenter);

    auto* cardTitle = new QLabel(QStringLiteral("This space is being prepared"), card);
    cardTitle->setObjectName(QStringLiteral("panelTitle"));
    cardTitle->setAlignment(Qt::AlignCenter);
    auto* body = new QLabel(description, card);
    body->setObjectName(QStringLiteral("subtle"));
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    body->setMaximumWidth(490);
    cardLayout->addWidget(cardTitle);
    cardLayout->addWidget(body);
    outer->addWidget(card, 0, Qt::AlignCenter);
    outer->addStretch(2);
    return page;
}

QWidget* makeAssistantPage(QTextBrowser*& transcript, QLineEdit*& input,
                           const std::function<void(const QString&)>& prepareQuestion)
{
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 24);
    outer->setSpacing(13);
    auto* eyebrow = new QLabel(QStringLiteral("YOUR LOCAL PC GUIDE"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Ask Ausyn"), page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* subtitle = new QLabel(QStringLiteral("Talk naturally about your PC or a broader topic. Ausyn uses live evidence, remembers this conversation in the session, and can use your configured AI provider after review."), page);
    subtitle->setObjectName(QStringLiteral("heroBody"));
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(subtitle);
    auto* modeStatus = new QLabel(page); modeStatus->setObjectName(QStringLiteral("assistantConnectionStatus")); modeStatus->setWordWrap(true); outer->addWidget(modeStatus);
    auto* quickQuestions = new QGridLayout;
    quickQuestions->setHorizontalSpacing(10);
    quickQuestions->setVerticalSpacing(8);
    const QVector<QPair<QString, QString>> suggestions{
        {QStringLiteral("Check my system"), QStringLiteral("Check my system")},
        {QStringLiteral("What’s going on?"), QStringLiteral("What’s going on?")},
        {QStringLiteral("Why is my PC slow?"), QStringLiteral("Why is my PC slow?")},
        {QStringLiteral("What’s using my RAM?"), QStringLiteral("What’s using my RAM?")},
        {QStringLiteral("What’s running?"), QStringLiteral("What’s running?")},
        {QStringLiteral("Storage outlook"), QStringLiteral("How soon could my drive fill up?")}
    };
    int shortcutIndex = 0;
    for (const auto& suggestion : suggestions) {
        auto* button = new QPushButton(suggestion.first, page);
        button->setObjectName(QStringLiteral("secondaryButton"));
        button->setProperty("chatShortcut", true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setAccessibleName(QStringLiteral("Use suggested question: %1").arg(suggestion.first));
        QObject::connect(button, &QPushButton::clicked, page,
            [prepareQuestion, question = suggestion.second] { prepareQuestion(question); });
        quickQuestions->addWidget(button, shortcutIndex / 3, shortcutIndex % 3);
        ++shortcutIndex;
    }
    outer->addLayout(quickQuestions);
    auto* panel = makePanel(page);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(15, 12, 15, 12);
    transcript = new QTextBrowser(panel);
    transcript->setOpenExternalLinks(true);
    transcript->setObjectName(QStringLiteral("chatTranscript"));
    transcript->document()->setMaximumBlockCount(500);
    transcript->setStyleSheet(QStringLiteral("QTextBrowser { background:transparent; border:0; color:#d9e0ed; font-size:13px; }"));
    transcript->append(QStringLiteral("<b style='color:#b8b6ff'>Ausyn</b><br>Hey! Ask me what’s going on with your PC, or chat naturally. I use local readings for PC checks. For broader or current questions, set up optional Cloud AI in Settings; web search is a separate opt-in and shows cited sources."));
    panelLayout->addWidget(transcript, 1);
    outer->addWidget(panel, 1);
    auto* compose = new QHBoxLayout;
    input = new QLineEdit(page);
    input->setPlaceholderText(QStringLiteral("Ask naturally — follow up with “why?” or “what should I do?”…"));
    input->setMaxLength(2000);
    input->setMinimumHeight(44);
    input->setObjectName(QStringLiteral("chatInput"));
    auto* send = new QPushButton(QStringLiteral("Send"), page);
    send->setObjectName(QStringLiteral("assistantSend"));
    send->setMinimumHeight(42);
    compose->addWidget(input, 1);
    compose->addWidget(send);
    auto* cancel = new QPushButton(QStringLiteral("Stop waiting"), page);
    cancel->setObjectName(QStringLiteral("assistantCancel"));
    cancel->setStyleSheet(QStringLiteral("background:#171f2c;color:#c7d0df;border:1px solid #2a3547;border-radius:9px;padding:9px 13px;"));
    cancel->hide();
    compose->addWidget(cancel);
    outer->addLayout(compose);
    return page;
}

QWidget* makeSettingsPage(QCheckBox*& casual, QCheckBox*& technical, QCheckBox*& showAdvancedTools,
                          QCheckBox*& showQuickStartGuide,
                          QComboBox*& appearance,
                          QCheckBox*& saveChat, QComboBox*& retention,
                          QComboBox*& samplingInterval, QCheckBox*& adaptiveSampling,
                          QCheckBox*& desktopNotifications,
                          QCheckBox*& systemFindings, QCheckBox*& forecasts,
                          QCheckBox*& eventLogs, QCheckBox*& startupApps,
                          QCheckBox*& securityAlerts, QCheckBox*& quietHours,
                          QSlider*& resourceSensitivity, QLabel*& resourceSensitivityValue,
                          QCheckBox*& startWithWindows,
                          QTimeEdit*& quietStart, QTimeEdit*& quietEnd,
                          QCheckBox*& cloudAi, QCheckBox*& webSearch, QCheckBox*& shareNames,
                          QLineEdit*& endpoint, QLineEdit*& model, QLineEdit*& apiKey,
                          QLabel*& keyStatus)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* page = new QWidget;
    scroll->setWidget(page);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 24);
    outer->setSpacing(13);
    auto* eyebrow = new QLabel(QStringLiteral("PREFERENCES & PRIVACY"), page);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Settings"), page);
    heading->setObjectName(QStringLiteral("heroTitle"));
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    auto* style = makePanel(page);
    auto* styleLayout = new QVBoxLayout(style);
    styleLayout->setContentsMargins(20, 17, 20, 17);
    auto* styleTitle = new QLabel(QStringLiteral("How Ausyn talks"), style);
    styleTitle->setObjectName(QStringLiteral("panelTitle"));
    casual = new QCheckBox(QStringLiteral("Use a relaxed, friendly tone"), style);
    technical = new QCheckBox(QStringLiteral("Include measurement details in Ask Ausyn replies"), style);
    auto* detailNote = new QLabel(QStringLiteral("You can expand evidence on any finding even when this is off. Assistant answers can include the technical measurement notes too."), style);
    detailNote->setObjectName(QStringLiteral("subtle"));
    auto* appearanceRow = new QHBoxLayout;
    auto* appearanceLabel = new QLabel(QStringLiteral("Appearance"), style);
    appearance = new QComboBox(style);
    appearance->addItem(QStringLiteral("Dark"), false);
    appearance->addItem(QStringLiteral("Light"), true);
    appearance->setAccessibleName(QStringLiteral("Ausyn appearance theme"));
    appearanceRow->addWidget(appearanceLabel);
    appearanceRow->addWidget(appearance);
    appearanceRow->addStretch();
    auto* appearanceNote = new QLabel(QStringLiteral("Dark is the default. Your choice is saved on this PC."), style);
    appearanceNote->setObjectName(QStringLiteral("subtle"));
    detailNote->setWordWrap(true);
    styleLayout->addWidget(styleTitle);
    styleLayout->addWidget(casual);
    styleLayout->addWidget(technical);
    styleLayout->addWidget(detailNote);
    styleLayout->addLayout(appearanceRow);
    styleLayout->addWidget(appearanceNote);
    showAdvancedTools = new QCheckBox(QStringLiteral("Show advanced tools in the sidebar"), style);
    showQuickStartGuide = new QCheckBox(QStringLiteral("Show the quick-start guide on the dashboard"), style);
    auto* quickStartNote = new QLabel(QStringLiteral("You can hide the guide from the dashboard and bring it back here at any time."), style);
    quickStartNote->setObjectName(QStringLiteral("subtle"));
    quickStartNote->setWordWrap(true);
    auto* navigationNote = new QLabel(QStringLiteral("Your five pages each remember their Simple/Details choice. Details reveals additional evidence and specialist tools within the page."), style);
    navigationNote->setObjectName(QStringLiteral("subtle"));
    navigationNote->setWordWrap(true);
    showAdvancedTools->hide();
    styleLayout->addWidget(showQuickStartGuide);
    styleLayout->addWidget(quickStartNote);
    styleLayout->addWidget(navigationNote);
    outer->addWidget(style);
    auto* monitoring = makePanel(page);
    auto* monitoringLayout = new QVBoxLayout(monitoring);
    monitoringLayout->setContentsMargins(20, 17, 20, 17);
    auto* monitoringTitle = new QLabel(QStringLiteral("Monitoring"), monitoring);
    monitoringTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* monitoringNote = new QLabel(QStringLiteral("The balanced default is every 5 seconds. Choose 1–2 seconds for a more responsive view or 10 seconds to reduce sampling work further. Slower sampling can delay findings and leaves fewer history samples."), monitoring);
    monitoringNote->setObjectName(QStringLiteral("subtle"));
    monitoringNote->setWordWrap(true);
    auto* intervalRow = new QHBoxLayout;
    auto* intervalLabel = new QLabel(QStringLiteral("Sample every"), monitoring);
    samplingInterval = new QComboBox(monitoring);
    const QList<QPair<int, QString>> samplingChoices{
        {1, QStringLiteral("1 second · fastest updates")},
        {2, QStringLiteral("2 seconds · responsive")},
        {5, QStringLiteral("5 seconds · balanced")},
        {10, QStringLiteral("10 seconds · lowest sampling")},
    };
    for (const auto& choice : samplingChoices)
        samplingInterval->addItem(choice.second, choice.first);
    adaptiveSampling = new QCheckBox(QStringLiteral("Automatically reduce monitoring work if Ausyn’s collector stays busy"), monitoring);
    auto* adaptiveNote = new QLabel(QStringLiteral("After six consecutive samples use at least 20% of their interval, Ausyn temporarily samples every 10 seconds. It returns to your selected interval after 12 low-load samples. No readings or alerts are disabled; a slower interval can delay updates."), monitoring);
    adaptiveNote->setObjectName(QStringLiteral("subtle"));
    adaptiveNote->setWordWrap(true);
    intervalRow->addWidget(intervalLabel);
    intervalRow->addWidget(samplingInterval);
    intervalRow->addStretch();
    monitoringLayout->addWidget(monitoringTitle);
    monitoringLayout->addWidget(monitoringNote);
    monitoringLayout->addLayout(intervalRow);
    monitoringLayout->addWidget(adaptiveSampling);
    monitoringLayout->addWidget(adaptiveNote);
    outer->addWidget(monitoring);
    auto* startup = makePanel(page);
    auto* startupLayout = new QVBoxLayout(startup);
    startupLayout->setContentsMargins(20, 17, 20, 17);
    auto* startupTitle = new QLabel(QStringLiteral("Windows sign-in"), startup);
    startupTitle->setObjectName(QStringLiteral("panelTitle"));
    startWithWindows = new QCheckBox(QStringLiteral("Open Ausyn automatically after I sign in"), startup);
    auto* startupNote = new QLabel(QStringLiteral("This setting adds or removes Ausyn from the current Windows account’s startup list. It does not require administrator access. Ausyn opens with its normal window and monitoring settings."), startup);
    startupNote->setObjectName(QStringLiteral("subtle"));
    startupNote->setWordWrap(true);
    startupLayout->addWidget(startupTitle);
    startupLayout->addWidget(startWithWindows);
    startupLayout->addWidget(startupNote);
    outer->addWidget(startup);
    auto* alerts = makePanel(page);
    auto* alertsLayout = new QVBoxLayout(alerts);
    alertsLayout->setContentsMargins(20, 17, 20, 17);
    auto* alertsTitle = new QLabel(QStringLiteral("Desktop alerts"), alerts);
    alertsTitle->setObjectName(QStringLiteral("panelTitle"));
    desktopNotifications = new QCheckBox(QStringLiteral("Allow desktop alerts for important findings and near-term forecasts"), alerts);
    systemFindings = new QCheckBox(QStringLiteral("System findings and sustained resource pressure"), alerts);
    forecasts = new QCheckBox(QStringLiteral("Storage, battery, and memory forecasts"), alerts);
    eventLogs = new QCheckBox(QStringLiteral("Meaningful recent Windows event-log signals"), alerts);
    startupApps = new QCheckBox(QStringLiteral("Sustained resource use linked to startup items"), alerts);
    securityAlerts = new QCheckBox(QStringLiteral("Windows security status and restart notices"), alerts);
    auto* sensitivityTitle = new QLabel(QStringLiteral("Resource alert sensitivity"), alerts);
    sensitivityTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* sensitivityNote = new QLabel(QStringLiteral("Choose how elevated CPU or memory must be before Ausyn gives an early workload heads-up. This changes notifications, not what Ausyn measures."), alerts);
    sensitivityNote->setObjectName(QStringLiteral("subtle"));
    sensitivityNote->setWordWrap(true);
    auto* sensitivityRow = new QHBoxLayout;
    resourceSensitivity = new QSlider(Qt::Horizontal, alerts);
    resourceSensitivity->setObjectName(QStringLiteral("resourceAlertSensitivity"));
    resourceSensitivity->setRange(1, 5);
    resourceSensitivity->setSingleStep(1);
    resourceSensitivity->setPageStep(1);
    resourceSensitivity->setTickInterval(1);
    resourceSensitivity->setTickPosition(QSlider::TicksBelow);
    resourceSensitivity->setAccessibleName(QStringLiteral("Resource alert sensitivity"));
    resourceSensitivity->setToolTip(QStringLiteral("Higher sensitivity lowers the sustained CPU and memory thresholds for an early heads-up."));
    resourceSensitivityValue = new QLabel(QStringLiteral("Balanced"), alerts);
    resourceSensitivityValue->setMinimumWidth(110);
    resourceSensitivityValue->setObjectName(QStringLiteral("subtle"));
    resourceSensitivityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sensitivityRow->addWidget(new QLabel(QStringLiteral("Conservative"), alerts));
    sensitivityRow->addWidget(resourceSensitivity, 1);
    sensitivityRow->addWidget(new QLabel(QStringLiteral("More sensitive"), alerts));
    sensitivityRow->addWidget(resourceSensitivityValue);
    quietHours = new QCheckBox(QStringLiteral("Pause desktop alerts during quiet hours"), alerts);
    quietStart = new QTimeEdit(alerts);
    quietEnd = new QTimeEdit(alerts);
    quietStart->setDisplayFormat(QStringLiteral("h:mm AP"));
    quietEnd->setDisplayFormat(QStringLiteral("h:mm AP"));
    quietStart->setTime(QTime(22, 0));
    quietEnd->setTime(QTime(7, 0));
    auto* quietRange = new QHBoxLayout;
    quietRange->addWidget(new QLabel(QStringLiteral("From"), alerts));
    quietRange->addWidget(quietStart);
    quietRange->addWidget(new QLabel(QStringLiteral("until"), alerts));
    quietRange->addWidget(quietEnd);
    quietRange->addStretch();
    auto* alertsNote = new QLabel(QStringLiteral("Alerts are generated locally. Choose which categories can reach Windows, or pause all desktop alerts during a daily quiet-hours window (including overnight ranges). Matching start and end times keep alerts paused all day. Quiet hours pause only desktop notifications; findings remain visible in Ausyn. Findings notify when first detected or severity rises; sustained thermal warnings include the Windows-reported reading and threshold, with a 30-minute repeat cooldown. Critical escalation is shown immediately. A personal-baseline change can notify once per episode with a 30-minute cooldown. Forecasts notify once when they enter a near-term window. Ongoing alerts do not repeat every few seconds."), alerts);
    alertsNote->setObjectName(QStringLiteral("subtle"));
    alertsNote->setWordWrap(true);
    alertsLayout->addWidget(alertsTitle);
    alertsLayout->addWidget(desktopNotifications);
    alertsLayout->addWidget(systemFindings);
    alertsLayout->addWidget(forecasts);
    alertsLayout->addWidget(eventLogs);
    alertsLayout->addWidget(startupApps);
    alertsLayout->addWidget(securityAlerts);
    alertsLayout->addWidget(sensitivityTitle);
    alertsLayout->addWidget(sensitivityNote);
    alertsLayout->addLayout(sensitivityRow);
    alertsLayout->addWidget(quietHours);
    alertsLayout->addLayout(quietRange);
    alertsLayout->addWidget(alertsNote);
    outer->addWidget(alerts);
    auto* privacy = makePanel(page);
    auto* privacyLayout = new QVBoxLayout(privacy);
    privacyLayout->setContentsMargins(20, 17, 20, 17);
    auto* privacyTitle = new QLabel(QStringLiteral("Your data"), privacy);
    privacyTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* localNote = new QLabel(QStringLiteral("Monitoring and history stay on this PC. Cloud AI is optional and sends only your current question plus relevant, minimized readings after you review each request. Web search is a separate opt-in and may incur provider charges. Process names are excluded unless separately allowed below. Conversation history is never sent."), privacy);
    localNote->setObjectName(QStringLiteral("subtle"));
    localNote->setWordWrap(true);
    saveChat = new QCheckBox(QStringLiteral("Save conversations on this PC"), privacy);
    cloudAi = new QCheckBox(QStringLiteral("Allow optional cloud AI answers"), privacy);
    webSearch = new QCheckBox(QStringLiteral("Allow web search for broader or current questions (OpenAI API)"), privacy);
    webSearch->setToolTip(QStringLiteral("Uses the OpenAI gpt-5-search-api model. Search calls may incur API charges; each request still needs your approval."));
    shareNames = new QCheckBox(QStringLiteral("Include process names for questions about processes"), privacy);
    auto* retentionRow = new QHBoxLayout;
    auto* retentionLabel = new QLabel(QStringLiteral("Keep performance history for"), privacy);
    retention = new QComboBox(privacy);
    retention->addItem(QStringLiteral("7 days"), 7);
    retention->addItem(QStringLiteral("30 days"), 30);
    retention->addItem(QStringLiteral("90 days"), 90);
    retentionRow->addWidget(retentionLabel);
    retentionRow->addWidget(retention);
    retentionRow->addStretch();
    privacyLayout->addWidget(privacyTitle);
    privacyLayout->addWidget(localNote);
    privacyLayout->addWidget(saveChat);
    privacyLayout->addWidget(cloudAi);
    privacyLayout->addWidget(webSearch);
    privacyLayout->addWidget(shareNames);
    privacyLayout->addLayout(retentionRow);
    outer->addWidget(privacy);

    auto* provider = makePanel(page);
    auto* providerLayout = new QVBoxLayout(provider);
    providerLayout->setContentsMargins(20, 17, 20, 17);
    auto* providerTitle = new QLabel(QStringLiteral("AI provider"), provider);
    providerTitle->setObjectName(QStringLiteral("panelTitle"));
    auto* providerNote = new QLabel(QStringLiteral("Use an OpenAI-compatible chat-completions endpoint. For live web answers, use https://api.openai.com/v1 and a key with access to gpt-5-search-api; that search model can incur extra API charges. API keys are protected with Windows DPAPI for this Windows account."), provider);
    providerNote->setObjectName(QStringLiteral("subtle"));
    providerNote->setWordWrap(true);
    endpoint = new QLineEdit(provider);
    endpoint->setPlaceholderText(QStringLiteral("https://api.example.com/v1"));
    endpoint->setClearButtonEnabled(true);
    endpoint->setToolTip(QStringLiteral("HTTPS required, except HTTP localhost model servers."));
    model = new QLineEdit(provider);
    model->setPlaceholderText(QStringLiteral("Provider model name"));
    apiKey = new QLineEdit(provider);
    apiKey->setEchoMode(QLineEdit::Password);
    apiKey->setMaxLength(4096);
    apiKey->setPlaceholderText(QStringLiteral("Paste API key to replace saved key"));
    apiKey->setClearButtonEnabled(true);
    keyStatus = new QLabel(provider);
    keyStatus->setObjectName(QStringLiteral("subtle"));
    auto* clearKey = new QPushButton(QStringLiteral("Remove saved API key"), provider);
    clearKey->setObjectName(QStringLiteral("secondaryButton"));
    clearKey->setProperty("removeApiKey", true);
    providerLayout->addWidget(providerTitle);
    providerLayout->addWidget(providerNote);
    providerLayout->addWidget(new QLabel(QStringLiteral("Endpoint"), provider));
    providerLayout->addWidget(endpoint);
    providerLayout->addWidget(new QLabel(QStringLiteral("Model"), provider));
    providerLayout->addWidget(model);
    providerLayout->addWidget(new QLabel(QStringLiteral("API key"), provider));
    providerLayout->addWidget(apiKey);
    providerLayout->addWidget(keyStatus);
    providerLayout->addWidget(clearKey, 0, Qt::AlignLeft);
    outer->addWidget(provider);
    auto* welcomeButton = new QPushButton(QStringLiteral("Review first-launch privacy guide"), page);
    welcomeButton->setObjectName(QStringLiteral("secondaryButton"));
    welcomeButton->setProperty("showWelcomeGuide", true);
    outer->addWidget(welcomeButton, 0, Qt::AlignLeft);
    auto* tip = new QLabel(QStringLiteral("Conversation saving is off by default. You can clear saved conversations and device history from the History & reports page."), page);
    tip->setObjectName(QStringLiteral("subtle"));
    tip->setWordWrap(true);
    outer->addWidget(tip);
    outer->addStretch(1);
    return scroll;
}

int showWelcomeGuide(QWidget* parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Welcome to Ausyn"));
    dialog.setModal(true);
    dialog.setMinimumWidth(570);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background:#101722; color:#e8edf6; }"
        "QLabel { color:#e8edf6; }"
        "QPushButton { min-height:38px; padding:0 14px; border-radius:9px; }"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 25, 28, 24);
    layout->setSpacing(13);
    auto* eyebrow = new QLabel(QStringLiteral("A CLEARER VIEW OF YOUR PC"), &dialog);
    eyebrow->setStyleSheet(QStringLiteral("color:#9b9aff;font-size:10px;font-weight:700;letter-spacing:1px;"));
    auto* title = new QLabel(QStringLiteral("Welcome to Ausyn"), &dialog);
    title->setStyleSheet(QStringLiteral("font-size:25px;font-weight:700;"));
    auto* intro = new QLabel(QStringLiteral(
        "Ausyn watches supported Windows health signals, looks for sustained patterns, and explains what it measured. "
        "The first scan starts automatically; unsupported readings stay marked unavailable."), &dialog);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color:#b5c0d0;font-size:13px;"));
    auto* privacy = new QLabel(QStringLiteral(
        "<b>Private by default</b><br>Performance history stays on this PC and is kept for the period selected in Settings. "
        "Cloud AI is off until you enable it; each cloud question is reviewed before sending. "
        "Ausyn does not change system settings or close apps on its own."), &dialog);
    privacy->setWordWrap(true);
    privacy->setTextFormat(Qt::RichText);
    privacy->setStyleSheet(QStringLiteral("background:#151e2b;border:1px solid #283448;border-radius:12px;padding:14px;color:#cad4e2;"));
    layout->addWidget(eyebrow);
    layout->addWidget(title);
    layout->addWidget(intro);
    layout->addWidget(privacy);
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* review = new QPushButton(QStringLiteral("Review privacy settings"), &dialog);
    review->setObjectName(QStringLiteral("secondaryButton"));
    auto* start = new QPushButton(QStringLiteral("Continue to Ausyn"), &dialog);
    start->setObjectName(QStringLiteral("primaryButton"));
    actions->addWidget(review);
    actions->addWidget(start);
    layout->addLayout(actions);
    QObject::connect(start, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(review, &QPushButton::clicked, &dialog, [&dialog] { dialog.done(2); });
    dialog.exec();
    return dialog.result();
}

} // namespace

class ThemeController final : public QObject {
public:
    explicit ThemeController(QObject* parent) : QObject(parent)
    {
        if (QCoreApplication::instance())
            QCoreApplication::instance()->installEventFilter(this);
    }

    void apply(QWidget* root, bool light, const QString& baseWindowStyle)
    {
        root_ = root;
        light_ = light;
        baseWindowStyle_ = baseWindowStyle;
        applying_ = true;
        root->setStyleSheet(light ? recolor(baseWindowStyle) : baseWindowStyle);
        for (QWidget* widget : root->findChildren<QWidget*>()) {
            if (!widget->property("ausynThemeBaseStyle").isValid())
                widget->setProperty("ausynThemeBaseStyle", widget->styleSheet());
            const QString base = widget->property("ausynThemeBaseStyle").toString();
            const QString rendered = light ? recolor(base) : base;
            widget->setProperty("ausynThemeRenderedStyle", rendered);
            widget->setStyleSheet(rendered);
        }
        applying_ = false;
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel) {
            auto* combo = qobject_cast<QComboBox*>(watched);
            auto* spin = qobject_cast<QAbstractSpinBox*>(watched);
            auto* slider = qobject_cast<QSlider*>(watched);
            auto* wheel = static_cast<QWheelEvent*>(event);
            QWidget* control = combo ? static_cast<QWidget*>(combo) : spin ? static_cast<QWidget*>(spin) : static_cast<QWidget*>(slider);
            if (control && (!combo || !combo->view()->isVisible())) {
                QScrollArea* pageScroll = nullptr;
                for (QWidget* parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
                    if (auto* candidate = qobject_cast<QScrollArea*>(parent)) {
                        pageScroll = candidate;
                        break;
                    }
                }
                if (pageScroll && pageScroll->verticalScrollBar()->isVisible()) {
                    int delta = wheel->pixelDelta().y();
                    if (delta == 0) delta = wheel->angleDelta().y() / 3;
                    pageScroll->verticalScrollBar()->setValue(pageScroll->verticalScrollBar()->value() - delta);
                }
                wheel->accept();
                return true;
            }
        }
        if (!light_ || applying_ || event->type() != QEvent::StyleChange)
            return QObject::eventFilter(watched, event);
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || !root_ || (widget != root_ && !root_->isAncestorOf(widget)))
            return QObject::eventFilter(watched, event);
        QPointer<QWidget> guarded(widget);
        QTimer::singleShot(0, this, [this, guarded] {
            if (!guarded || applying_) return;
            const QString current = guarded->styleSheet();
            if (current == guarded->property("ausynThemeRenderedStyle").toString()) return;
            if (!guarded->property("ausynThemeBaseStyle").isValid())
                guarded->setProperty("ausynThemeBaseStyle", current);
            else
                guarded->setProperty("ausynThemeBaseStyle", current);
            const QString rendered = recolor(current);
            applying_ = true;
            guarded->setProperty("ausynThemeRenderedStyle", rendered);
            guarded->setStyleSheet(rendered);
            applying_ = false;
        });
        return QObject::eventFilter(watched, event);
    }

private:
    static QString recolor(const QString& source)
    {
        static const QHash<QString, QString> palette{
            {QStringLiteral("#0b0f17"), QStringLiteral("#f5f7fb")}, {QStringLiteral("#0e131d"), QStringLiteral("#eef2f7")}, {QStringLiteral("#101722"), QStringLiteral("#f5f7fb")},
            {QStringLiteral("#111824"), QStringLiteral("#ffffff")}, {QStringLiteral("#141d2a"), QStringLiteral("#f0f3f8")}, {QStringLiteral("#151e2b"), QStringLiteral("#f1f4f8")},
            {QStringLiteral("#151d2a"), QStringLiteral("#f1f4f8")}, {QStringLiteral("#171e2b"), QStringLiteral("#e9eef5")}, {QStringLiteral("#171f2b"), QStringLiteral("#e9eef5")},
            {QStringLiteral("#171f2c"), QStringLiteral("#e9eef5")}, {QStringLiteral("#111322"), QStringLiteral("#f5f7fb")},
            {QStringLiteral("#151f2d"), QStringLiteral("#f0f3f8")}, {QStringLiteral("#17243a"), QStringLiteral("#e8edf5")},
            {QStringLiteral("#292315"), QStringLiteral("#f4eddf")}, {QStringLiteral("#1b2032"), QStringLiteral("#e8eaf7")}, {QStringLiteral("#1d2035"), QStringLiteral("#e8eaf7")},
            {QStringLiteral("#1e2436"), QStringLiteral("#e8eaf7")}, {QStringLiteral("#20253a"), QStringLiteral("#e8eaf7")}, {QStringLiteral("#242946"), QStringLiteral("#e5e7f5")},
            {QStringLiteral("#202938"), QStringLiteral("#dfe5ed")}, {QStringLiteral("#222e3e"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#263143"), QStringLiteral("#d7dfe9")},
            {QStringLiteral("#263244"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#283448"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#293548"), QStringLiteral("#d7dfe9")},
            {QStringLiteral("#202b3a"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#202a38"), QStringLiteral("#e9eef5")},
            {QStringLiteral("#2a3546"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#2a3547"), QStringLiteral("#d7dfe9")}, {QStringLiteral("#2b3548"), QStringLiteral("#c6d0de")},
            {QStringLiteral("#333957"), QStringLiteral("#c6c9df")}, {QStringLiteral("#363957"), QStringLiteral("#c6c9df")},
            {QStringLiteral("#f4f6fc"), QStringLiteral("#202a3b")}, {QStringLiteral("#f3f5fb"), QStringLiteral("#202a3b")}, {QStringLiteral("#f0f3fa"), QStringLiteral("#202a3b")},
            {QStringLiteral("#f0f2ff"), QStringLiteral("#202a3b")}, {QStringLiteral("#e8edf6"), QStringLiteral("#202a3b")}, {QStringLiteral("#e5eaf4"), QStringLiteral("#263247")},
            {QStringLiteral("#d9e0ed"), QStringLiteral("#2a3547")}, {QStringLiteral("#d7dfed"), QStringLiteral("#2a3547")}, {QStringLiteral("#d4d8e5"), QStringLiteral("#33394f")},
            {QStringLiteral("#f9fafc"), QStringLiteral("#202a3b")}, {QStringLiteral("#d9d9ff"), QStringLiteral("#514cad")},
            {QStringLiteral("#cad4e2"), QStringLiteral("#3b4659")}, {QStringLiteral("#c7d0df"), QStringLiteral("#3b4659")}, {QStringLiteral("#c8d2e0"), QStringLiteral("#3b4659")},
            {QStringLiteral("#b5c0d0"), QStringLiteral("#59677b")}, {QStringLiteral("#b8c3d3"), QStringLiteral("#59677b")}, {QStringLiteral("#aeb9cb"), QStringLiteral("#5a687d")},
            {QStringLiteral("#9aa5b8"), QStringLiteral("#647188")}, {QStringLiteral("#9aa5b8"), QStringLiteral("#647188")}, {QStringLiteral("#9ba9bd"), QStringLiteral("#647188")},
            {QStringLiteral("#93a0b5"), QStringLiteral("#59677b")}, {QStringLiteral("#a4aec0"), QStringLiteral("#59677b")}, {QStringLiteral("#46546a"), QStringLiteral("#647188")},
            {QStringLiteral("#99a5b9"), QStringLiteral("#647188")}, {QStringLiteral("#8995aa"), QStringLiteral("#59677b")}, {QStringLiteral("#8f9bb0"), QStringLiteral("#59677b")},
            {QStringLiteral("#8592a7"), QStringLiteral("#59677b")}, {QStringLiteral("#8390a5"), QStringLiteral("#59677b")}, {QStringLiteral("#748197"), QStringLiteral("#647188")},
            {QStringLiteral("#6f7b90"), QStringLiteral("#647188")}, {QStringLiteral("#68758b"), QStringLiteral("#647188")}, {QStringLiteral("#7f8aa0"), QStringLiteral("#647188")},
            {QStringLiteral("#a9a8ff"), QStringLiteral("#514cad")}, {QStringLiteral("#b8b6ff"), QStringLiteral("#514cad")}, {QStringLiteral("#c3c2ff"), QStringLiteral("#514cad")},
            {QStringLiteral("#c2c0ff"), QStringLiteral("#514cad")}, {QStringLiteral("#9b9aff"), QStringLiteral("#514cad")}, {QStringLiteral("#9f9cff"), QStringLiteral("#514cad")},
            {QStringLiteral("#87d7b0"), QStringLiteral("#267a58")}, {QStringLiteral("#69d9a8"), QStringLiteral("#267a58")}, {QStringLiteral("#57d6bd"), QStringLiteral("#267a68")},
            {QStringLiteral("#f0be73"), QStringLiteral("#8b5e00")}, {QStringLiteral("#f2bb73"), QStringLiteral("#8b5e00")}, {QStringLiteral("#e4c27d"), QStringLiteral("#8b5e00")},
            {QStringLiteral("#f2ca83"), QStringLiteral("#8b5e00")}, {QStringLiteral("#e6b76f"), QStringLiteral("#8b5e00")}, {QStringLiteral("#ff8f88"), QStringLiteral("#b43c43")},
            {QStringLiteral("#ff777f"), QStringLiteral("#b43c43")}, {QStringLiteral("#f09292"), QStringLiteral("#b43c43")}
        };
        static const QRegularExpression colorExpression(QStringLiteral("#[0-9a-fA-F]{6}"));
        QString result;
        result.reserve(source.size());
        qsizetype copied = 0;
        auto it = colorExpression.globalMatch(source);
        while (it.hasNext()) {
            const auto match = it.next();
            result += source.mid(copied, match.capturedStart() - copied);
            const QString color = match.captured().toLower();
            result += palette.value(color, color);
            copied = match.capturedEnd();
        }
        result += source.mid(copied);
        return result;
    }

    QPointer<QWidget> root_;
    QString baseWindowStyle_;
    bool light_ = false;
    bool applying_ = false;
};

MainWindow::MainWindow(QWidget* parent, bool uiCheck)
    : FramelessWindow(parent)
    , uiCheck_(uiCheck)
{
    QApplication::setQuitOnLastWindowClosed(false);
    preferences_ = uiCheck_ ? UserPreferences{} : UserPreferencesStore::load();
    if (preferences_.saveConversationsLocally) {
        chatMessages_ = UserPreferencesStore::loadConversations();
    }
    setWindowTitle(QStringLiteral("Ausyn — Your PC, understood"));
    const QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1440, 900);
    const QSize usable(std::max(640, available.width() - 48),
                       std::max(480, available.height() - 80));
    setMinimumSize(std::min(860, usable.width()), std::min(560, usable.height()));
    resize(QSize(1440, 900).boundedTo(usable));
    setStyleSheet(QString::fromUtf8(kWindowStyle));

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    auto* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* sidebar = new QFrame(root);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(224);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout_ = sidebarLayout;
    sidebarLayout->setContentsMargins(14, 18, 14, 15);
    sidebarLayout->setSpacing(5);

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(6, 1, 6, 15);
    auto* mark = new QPushButton(sidebar);
    mark->setIcon(QIcon(QStringLiteral(":/brand/ausyn.png")));
    mark->setIconSize(QSize(38, 38));
    mark->setAccessibleName(QStringLiteral("Ausyn logo"));
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setFixedSize(38, 38);
    mark->setCursor(Qt::PointingHandCursor);
    mark->setFlat(true);
    mark->setStyleSheet(QStringLiteral("QPushButton#brandMark { padding:0; }"));
    connect(mark, &QPushButton::clicked, this, [this] {
        if (++brandClickCount_ >= 7) {
            brandClickCount_ = 0;
            QMessageBox::information(this, QStringLiteral("A little Ausyn lore"),
                QStringLiteral("Built with care by Arnav · 15 October 2006\nFirst quality-assurance lead: Ketty 🐾\n\nAUS-001: yeah, this one."));
        }
    });
    auto* brandText = new QVBoxLayout;
    brandText->setSpacing(0);
    auto* brand = new QLabel(QStringLiteral("AUSYN"), sidebar);
    brand->setObjectName(QStringLiteral("brandName"));
    auto* brandSub = new QLabel(QStringLiteral("YOUR PC, UNDERSTOOD"), sidebar);
    brandSub->setObjectName(QStringLiteral("brandSub"));
    brandText->addWidget(brand);
    brandText->addWidget(brandSub);
    brandRow->addWidget(mark);
    brandRow->addSpacing(10);
    brandRow->addLayout(brandText);
    brandRow->addStretch();
    sidebarLayout->addLayout(brandRow);

    auto* mainSection = new QLabel(QStringLiteral("YOUR SPACE"), sidebar);
    mainSection->setObjectName(QStringLiteral("navSection"));
    sidebarLayout->addWidget(mainSection);
    addNavigationItem(QStringLiteral("Main dashboard"), QStringLiteral("◫"), 0);
    addNavigationItem(QStringLiteral("Performance"), QStringLiteral("⌁"), 2);
    addNavigationItem(QStringLiteral("Ask Ausyn"), QStringLiteral("✦"), 1);
    addNavigationItem(QStringLiteral("Logs"), QStringLiteral("▤"), 18);
    sidebarLayout->addStretch(1);
    addNavigationItem(QStringLiteral("Settings"), QStringLiteral("⚙"), 9);
    auto* sidebarDivider = new QFrame(sidebar);
    sidebarDivider->setFrameShape(QFrame::HLine);
    sidebarDivider->setStyleSheet(QStringLiteral("color:#202938;"));
    sidebarLayout->addWidget(sidebarDivider);
    auto* localStatus = new QLabel(QStringLiteral("●  Local-first by design"), sidebar);
    localStatus->setObjectName(QStringLiteral("sidebarStatus"));
    localStatus->setContentsMargins(8, 6, 0, 2);
    sidebarLayout->addWidget(localStatus);

    auto* mainColumn = new QWidget(root);
    auto* mainLayout = new QVBoxLayout(mainColumn);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* topBar = new WindowTitleBar(this, mainColumn);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(62);
    auto* topLayout = topBar->contentLayout();
    topLayout->setContentsMargins(25, 0, 8, 0);
    auto* breadcrumb = new QLabel(QStringLiteral("Overview"), topBar);
    breadcrumb->setObjectName(QStringLiteral("breadcrumb"));
    pageTitle_ = breadcrumb;
    topLayout->addWidget(breadcrumb);
    topLayout->addStretch();

    auto* livePill = new QFrame(topBar);
    livePill->setObjectName(QStringLiteral("statusPill"));
    livePill->setFixedHeight(34);
    livePill->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    auto* pillLayout = new QHBoxLayout(livePill);
    pillLayout->setContentsMargins(10, 5, 10, 5);
    pillLayout->setSpacing(7);
    auto* statusDot = new QLabel(QStringLiteral("●"), livePill);
    statusDot->setObjectName(QStringLiteral("statusDot"));
    monitoringStatusDot_ = statusDot;
    auto* statusText = new QLabel(QStringLiteral("Starting monitor"), livePill);
    statusText->setObjectName(QStringLiteral("statusText"));
    monitoringStatus_ = statusText;
    pillLayout->addWidget(statusDot);
    pillLayout->addWidget(statusText);
    topLayout->addWidget(livePill, 0, Qt::AlignVCenter);
    topLayout->addWidget(new WindowControls(this, topBar));

    auto* banner = new QFrame(mainColumn); banner->setObjectName(QStringLiteral("adviceBanner"));
    auto* bannerRow = new QHBoxLayout(banner); bannerRow->setContentsMargins(25,10,15,10);
    auto* bannerText = new QVBoxLayout; bannerText->setSpacing(4);
    adviceBannerTitle_ = new QLabel(banner); adviceBannerTitle_->setObjectName(QStringLiteral("adviceBannerTitle")); adviceBannerTitle_->setWordWrap(true);
    adviceBannerBody_ = new QLabel(banner); adviceBannerBody_->setObjectName(QStringLiteral("adviceBannerBody")); adviceBannerBody_->setWordWrap(true);
    bannerText->addWidget(adviceBannerTitle_); bannerText->addWidget(adviceBannerBody_); bannerRow->addLayout(bannerText,1);
    auto* reviewAdvice = new QPushButton(QStringLiteral("See details"), banner); reviewAdvice->setObjectName(QStringLiteral("secondaryButton"));
    auto* dismissAdvice = new QPushButton(QStringLiteral("×"), banner); dismissAdvice->setAccessibleName(QStringLiteral("Dismiss current advice"));
    bannerRow->addWidget(reviewAdvice); bannerRow->addWidget(dismissAdvice);
    connect(reviewAdvice, &QPushButton::clicked, this, [this] { openResource(adviceResource_); });
    connect(dismissAdvice, &QPushButton::clicked, this, [this] { dismissedAdviceTitle_ = adviceBannerTitle_->text(); adviceBanner_->hide(); });
    adviceBanner_ = banner; banner->hide();

    pages_ = new QStackedWidget(mainColumn);
    int nextLegacyId = 0;
    const auto addPage = [this, &nextLegacyId](QWidget* page) {
        legacyPages_.insert(nextLegacyId++, makeScrollablePage(page));
    };
    QPushButton* quickStartCheck = nullptr;
    QPushButton* quickStartTroubleshoot = nullptr;
    QPushButton* quickStartPrivacy = nullptr;
    QPushButton* quickStartHide = nullptr;
    addPage(makeDashboard(proactiveBriefing_, quickStartPanel_, quickStartCheck,
                                    quickStartTroubleshoot, quickStartPrivacy, quickStartHide));
    quickStartPanel_->setVisible(preferences_.showQuickStartGuide);
    backgroundRelief_ = new BackgroundRelief(this);
    connect(backgroundRelief_, &BackgroundRelief::changed, this, [this](const QString& status) {
        if (workloadActionStatus_) workloadActionStatus_->setText(status);
        recordActivity(QStringLiteral("Background relief status"), status, 0);
        refreshWorkloadPanel();
    });
    dashboardSummary_ = legacyPage(0)->findChild<QLabel*>(QStringLiteral("dashboardSummary"));
    dashboardNextStep_ = legacyPage(0)->findChild<QLabel*>(QStringLiteral("dashboardNextStep"));
    dashboardActivity_ = legacyPage(0)->findChild<QLabel*>(QStringLiteral("dashboardActivity"));
    dashboardCheckResult_ = legacyPage(0)->findChild<QLabel*>(QStringLiteral("dashboardCheckResult"));
    pauseMonitoring_ = legacyPage(0)->findChild<QPushButton*>(QStringLiteral("pauseMonitoring"));
    dashboardChart_ = static_cast<LiveActivityChart*>(legacyPage(0)->findChild<QWidget*>(QStringLiteral("dashboardChart")));
    performanceRing_ = static_cast<ResourceRing*>(legacyPage(0)->findChild<QWidget*>(QStringLiteral("performanceRing")));
    for (ResourceCard* card : legacyPage(0)->findChildren<ResourceCard*>())
        connect(card, &ResourceCard::activated, this, &MainWindow::openResource);
    auto* runningApps = new QPushButton(QStringLiteral("Running apps"), legacyPage(0)); runningApps->setObjectName(QStringLiteral("secondaryButton"));
    if (auto* dashboardLayout = qobject_cast<QVBoxLayout*>(qobject_cast<QScrollArea*>(legacyPage(0))->widget()->layout())) {
        workloadPanel_ = makeWorkloadPanel();
        dashboardLayout->insertWidget(std::min(4, dashboardLayout->count()), workloadPanel_);
        companionPanel_ = makeCompanionPanel();
        dashboardLayout->insertWidget(std::min(4, dashboardLayout->count()), companionPanel_);
        for (int i = 0; i < dashboardLayout->count(); ++i) {
            auto* actions = qobject_cast<QHBoxLayout*>(dashboardLayout->itemAt(i)->layout());
            if (actions && actions->indexOf(quickStartCheck) >= 0) { actions->insertWidget(3, runningApps); break; }
        }
    }
    connect(runningApps, &QPushButton::clicked, this, [this] { openResource(static_cast<int>(ResourceKind::Processes)); });
    connect(quickStartCheck, &QPushButton::clicked, this, &MainWindow::checkSystemOnDashboard);
    connect(pauseMonitoring_, &QPushButton::clicked, this, [this] {
        if (monitoringEnabled_) monitoringEnabled_->setChecked(!monitoringEnabled_->isChecked());
    });
    connect(quickStartTroubleshoot, &QPushButton::clicked, this, [this] {
        showPage(17, QStringLiteral("Troubleshooting"));
    });
    connect(quickStartPrivacy, &QPushButton::clicked, this, [this] {
        showPage(9, QStringLiteral("Settings"));
    });
    connect(quickStartHide, &QPushButton::clicked, this, [this] {
        if (showQuickStartGuide_) showQuickStartGuide_->setChecked(false);
    });
    proactiveBriefing_->setPersistentState(preferences_.dismissedBriefingKeys,
                                            preferences_.snoozedBriefingKeys);
    connect(proactiveBriefing_, &ProactiveBriefingWidget::persistentStateChanged, this,
        [this](const QStringList& dismissedKeys, const QHash<QString, QDateTime>& snoozedKeys) {
            preferences_.dismissedBriefingKeys = dismissedKeys;
            preferences_.snoozedBriefingKeys = snoozedKeys;
            QString error;
            if (!UserPreferencesStore::save(preferences_, &error))
                statusBar()->showMessage(QStringLiteral("Briefing preference could not be saved: %1").arg(error), 9000);
        });
    connect(proactiveBriefing_, &ProactiveBriefingWidget::reviewRequested, this,
        [this](int pageIndex, const QString& cardTitle) {
            static const QHash<int, QString> titles{
                {2, QStringLiteral("Performance")}, {4, QStringLiteral("Battery & power")},
                {6, QStringLiteral("Diagnostics")}, {7, QStringLiteral("Recommendations")},
                {10, QStringLiteral("Predictions")}, {11, QStringLiteral("Event intelligence")},
                {12, QStringLiteral("Startup & apps")}, {13, QStringLiteral("Security & updates")}
            };
            showPage(pageIndex, titles.value(pageIndex, QStringLiteral("Ausyn insight")));
            statusBar()->showMessage(QStringLiteral("Reviewing: %1").arg(cardTitle), 5000);
        });
    connect(proactiveBriefing_, &ProactiveBriefingWidget::askAboutRequested, this,
        [this](const QString& category, const QString& title, const QString& summary,
               const QString& evidence, const QString& nextStep) {
            pendingBriefingFollowup_ = BriefingFollowup{category, title, summary, evidence, nextStep};
            chatInput_->setText(QStringLiteral("Can you explain that heads-up in plain English?"));
            showPage(1, QStringLiteral("Assistant"));
            chatInput_->setFocus();
            statusBar()->showMessage(QStringLiteral("This explanation uses the local Ausyn briefing; it won’t be sent to cloud AI."), 6500);
            sendAssistantMessage();
        });
    addPage(makeAssistantPage(chatTranscript_, chatInput_, [this](const QString& question) {
        pendingBriefingFollowup_.reset();
        chatInput_->setText(question);
        chatInput_->setFocus();
        sendAssistantMessage();
    }));
    connect(chatInput_, &QLineEdit::textEdited, this, [this](const QString&) {
        pendingBriefingFollowup_.reset();
    });
    assistantModeStatus_ = legacyPage(1)->findChild<QLabel*>(QStringLiteral("assistantConnectionStatus"));
    addPage(makePerformancePage(processTable_, processSearch_, processSnapshotAge_, foregroundWorkload_, inspectForegroundProcess_, processDetails_, processImpactDetails_,
                                           processTrendChart_, processorCoreGrid_));
    if (auto* scroll = qobject_cast<QScrollArea*>(legacyPage(2))) {
        auto* layout = qobject_cast<QVBoxLayout*>(scroll->widget()->layout());
        auto* graph = makePanel(scroll->widget());
        auto* graphLayout = new QVBoxLayout(graph);
        graphLayout->setContentsMargins(18, 14, 18, 12);
        auto* title = new QLabel(QStringLiteral("Live system load"), graph);
        title->setObjectName(QStringLiteral("panelTitle"));
        performanceChart_ = new LiveActivityChart(graph);
        graphLayout->addWidget(title);
        graphLayout->addWidget(performanceChart_);
        layout->insertWidget(3, graph);
    }
    connect(inspectForegroundProcess_, &QPushButton::clicked, this, [this] {
        if (!processTable_ || !processSearch_ || latestSnapshot_.foregroundProcessId == 0) return;
        processSearch_->clear();
        for (int row = 0; row < processTable_->rowCount(); ++row) {
            const QTableWidgetItem* idItem = processTable_->item(row, 1);
            if (idItem && idItem->text().toUInt() == latestSnapshot_.foregroundProcessId) {
                processTable_->setCurrentCell(row, 0,
                    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                processTable_->scrollToItem(processTable_->item(row, 0));
                return;
            }
        }
        statusBar()->showMessage(QStringLiteral("The active app is no longer in the latest readable process list."), 5000);
    });
    connect(processTable_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::updateSelectedProcessDetails);
    addPage(makeHardwarePage(hardwareDetails_, hardwareVolumes_, hardwareChangeStatus_, hardwareTimeline_));
    const QStringList recentHardwareChanges = HardwareChangeTracker::recentChanges();
    if (!recentHardwareChanges.isEmpty())
        hardwareTimeline_->setText(recentHardwareChanges.join(QLatin1Char('\n')));
    batteryPage_ = new BatteryPage;
    addPage(batteryPage_);
    gamingPage_ = new GamingPage;
    connect(gamingPage_, &GamingPage::gameLaunchDetected, this,
        [this](const QString& title, const QString& assessment) {
            recordActivity(QStringLiteral("Game detected: %1").arg(title), assessment, 5);
            showDesktopNotification(NotificationCategory::SystemFinding,
                QStringLiteral("Game detected: %1").arg(title), assessment.left(650), QSystemTrayIcon::Information, false, -6);
        });
    addPage(gamingPage_);
    addPage(makeInsightPage(QStringLiteral("Diagnostics"), QStringLiteral("EVIDENCE-BASED FINDINGS"),
        QStringLiteral("Ausyn watches for sustained patterns and shows the measurements behind each finding."),
        diagnosticsView_));
    addPage(makeInsightPage(QStringLiteral("Recommendations"), QStringLiteral("SAFE, USER-LED NEXT STEPS"),
        QStringLiteral("Suggestions explain why they may help. Nothing is changed automatically."),
        recommendationsView_));
    addPage(makeHistoryPage(historyChart_, historyStatus_, historySummary_, historyPeriod_));
    addPage(makeSettingsPage(casualTone_, technicalDetail_, showAdvancedTools_, showQuickStartGuide_, appearance_, saveConversations_, retentionDays_, samplingInterval_, adaptiveSampling_, desktopNotifications_,
        systemFindingAlerts_, forecastAlerts_, eventLogAlerts_, startupAlerts_, securityAlerts_, quietHoursEnabled_,
        resourceAlertSensitivity_, resourceAlertSensitivityValue_,
        startWithWindows_,
        quietHoursStart_, quietHoursEnd_,
        cloudAi_, webSearch_, shareProcessNames_, apiEndpoint_, apiModel_, apiKey_, apiKeyStatus_));
    if (auto* settingsScroll = qobject_cast<QScrollArea*>(legacyPage(9))) {
        if (auto* layout = qobject_cast<QVBoxLayout*>(settingsScroll->widget()->layout())) {
            layout->insertWidget(2, makeBehaviorSettings());
            layout->insertWidget(3, makeCompanionSettings());
        }
    }
    showAdvancedTools_->hide();
    // Specialist tools are reached through page tabs, not additional sidebar destinations.
    predictionPage_ = new PredictionPage;
    addPage(predictionPage_);
    eventLogPage_ = new EventLogPage;
    addPage(eventLogPage_);
    appInventoryPage_ = new AppInventoryPage;
    addPage(appInventoryPage_);
    appInventoryPage_->setSoftwareChangeHistory(SoftwareChangeTracker::recentChanges());
    connect(appInventoryPage_, &AppInventoryPage::clearSoftwareHistoryRequested, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("Clear software-change history and baseline?"),
                QStringLiteral("This removes Ausyn’s locally retained software inventory history and comparison baseline. The next inventory refresh will establish a new baseline without listing existing apps as new."),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        if (!SoftwareChangeTracker::clearHistory(&error)) {
            QMessageBox::warning(this, QStringLiteral("Software history could not be cleared"), error);
            return;
        }
        appInventoryPage_->setSoftwareChangeHistory({});
    });
    securityPage_ = new SecurityPage;
    addPage(securityPage_);
    dataQualityPage_ = new DataQualityPage;
    addPage(dataQualityPage_);
    agentHealthPage_ = new AgentHealthPage;
    addPage(agentHealthPage_);
    networkPage_ = new NetworkPage;
    addPage(networkPage_);
    troubleshootingPage_ = new TroubleshootingPage;
    addPage(troubleshootingPage_);
    connect(troubleshootingPage_, &TroubleshootingPage::reviewRequested, this,
        [this](int pageIndex, const QString& pageTitle) { showPage(pageIndex, pageTitle); });
    connect(troubleshootingPage_, &TroubleshootingPage::askAboutRequested, this,
        [this](const QString& category, const QString& title, const QString& summary,
               const QString& evidence, const QString& nextStep) {
            pendingBriefingFollowup_ = BriefingFollowup{category, title, summary, evidence, nextStep};
            chatInput_->setText(QStringLiteral("Can you explain this troubleshooting evidence?"));
            showPage(1, QStringLiteral("Assistant"));
            chatInput_->setFocus();
            statusBar()->showMessage(QStringLiteral("This explanation uses local evidence and stays on this PC."), 6500);
            sendAssistantMessage();
        });

    activityPage_ = new ActivityPage;
    if (auto* column = qobject_cast<QVBoxLayout*>(activityPage_->layout())) column->insertWidget(1, makeCompanionJournal());
    addPage(activityPage_);
    connect(activityPage_, &ActivityPage::reviewRequested, this, &MainWindow::showPage);
    const std::array<QString, 5> hubNames{QStringLiteral("Main dashboard"), QStringLiteral("Performance"), QStringLiteral("Ask Ausyn"), QStringLiteral("Logs"), QStringLiteral("Settings")};
    const std::array<QString, 5> hubNotes{
        QStringLiteral("What’s happening, what matters, and what to do next."),
        QStringLiteral("Understand your workload, hardware and gaming readiness."),
        QStringLiteral("A written companion with your PC’s actual evidence."),
        QStringLiteral("See Ausyn’s activity, saved history and Windows events."),
        QStringLiteral("Choose how Ausyn monitors, responds and stays in the background.")};
    for (int i = 0; i < 5; ++i) {
        auto* hub = new PageHub(hubNames[i], hubNotes[i], pages_);
        hubs_.append(hub); pages_->addWidget(hub);
    }
    const auto section = [this](int hub, int id, const QString& name, bool detailed = false) {
        legacyHub_.insert(id, hub);
        hubs_[hub]->addSection(id, name, legacyPage(id), detailed);
    };
    section(0, 0, QStringLiteral("Overview"));
    section(0, 17, QStringLiteral("Guided help"));
    section(0, 6, QStringLiteral("Findings"), true);
    section(0, 7, QStringLiteral("Next steps"), true);
    section(0, 10, QStringLiteral("Forecasts"), true);
    section(1, 2, QStringLiteral("Live activity"));
    section(1, 5, QStringLiteral("Gaming"));
    section(1, 4, QStringLiteral("Power"));
    section(1, 3, QStringLiteral("Hardware"), true);
    section(1, 16, QStringLiteral("Network"), true);
    section(2, 1, QStringLiteral("Conversation"));
    section(3, 18, QStringLiteral("Ausyn activity"));
    section(3, 8, QStringLiteral("History"));
    section(3, 11, QStringLiteral("Windows events"));
    section(3, 12, QStringLiteral("Apps and startup"), true);
    section(4, 9, QStringLiteral("Preferences"));
    section(4, 13, QStringLiteral("Security"));
    section(4, 14, QStringLiteral("Sensor coverage"), true);
    section(4, 15, QStringLiteral("Ausyn resource use"), true);
    for (int i = 0; i < hubs_.size(); ++i) {
        hubs_[i]->setDetailed(preferences_.pageDetails.value(i, false));
        connect(hubs_[i], &PageHub::detailChanged, this, [this, i](bool detailed) {
            preferences_.pageDetails.insert(i, detailed);
            updateDetailMode(i);
            if (preferencesReady_) savePreferences();
        });
        connect(hubs_[i], &PageHub::sectionChanged, this, [this, i](int id) {
            if (pages_->currentIndex() == i) showPage(id, QString());
        });
    }
    mainLayout->addWidget(topBar);
    mainLayout->addWidget(banner);
    mainLayout->addWidget(pages_, 1);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(mainColumn, 1);
    setCentralWidget(root);

    auto* trayMenu = new QMenu(this);
    auto* openAction = trayMenu->addAction(QStringLiteral("Open Ausyn"));
    connect(openAction, &QAction::triggered, this, [this] {
        trayIcon_->setToolTip(QStringLiteral("Ausyn · local device monitoring"));
        show();
        raise();
        activateWindow();
    });
    trayMenu->addSeparator();
    installCompanionTray(trayMenu);
    trayMenu->addSeparator();
    auto* quitAction = trayMenu->addAction(QStringLiteral("Quit Ausyn"));
    connect(quitAction, &QAction::triggered, this, [this] {
        quitRequested_ = true;
        close();
        QApplication::quit();
    });
    trayIcon_ = new QSystemTrayIcon(QIcon(QStringLiteral(":/brand/ausyn.ico")), this);
    trayIcon_->setToolTip(QStringLiteral("Ausyn · local device monitoring"));
    trayIcon_->setContextMenu(trayMenu);
    connect(trayIcon_, &QSystemTrayIcon::messageClicked, this, [this] {
        int destination = 6;
        QString title = QStringLiteral("Diagnostics");
        switch (lastNotificationCategory_) {
        case NotificationCategory::SystemFinding:
            destination = 6; title = QStringLiteral("Diagnostics"); break;
        case NotificationCategory::Forecast:
            destination = 10; title = QStringLiteral("Predictions"); break;
        case NotificationCategory::EventLog:
            destination = 11; title = QStringLiteral("Event intelligence"); break;
        case NotificationCategory::Startup:
            destination = 12; title = QStringLiteral("Startup & apps"); break;
        case NotificationCategory::Security:
            destination = 13; title = QStringLiteral("Security & updates"); break;
        }
        show();
        showPage(destination, title);
        raise();
        activateWindow();
        statusBar()->showMessage(QStringLiteral("Opened %1 to review the latest Ausyn alert.").arg(title), 5000);
    });
    if (!uiCheck_ && QSystemTrayIcon::isSystemTrayAvailable()) trayIcon_->show();

    eventLogWatcher_ = new QFutureWatcher<EventLogUpdate>(this);
    proactiveEventWatcher_ = new QFutureWatcher<EventLogUpdate>(this);
    connect(eventLogPage_, &EventLogPage::refreshRequested,
            this, &MainWindow::refreshEventLogs);
    connect(eventLogWatcher_, &QFutureWatcher<EventLogUpdate>::finished, this, [this] {
        if (!eventLogPage_) return;
        latestEventLogUpdate_ = eventLogWatcher_->result();
        recordActivity(QStringLiteral("Windows event check completed"), latestEventLogUpdate_.available
            ? QStringLiteral("Reviewed recent Windows events; %1 grouped signals are available for inspection.").arg(latestEventLogUpdate_.insights.size())
            : QStringLiteral("Windows event readings were unavailable. Review Windows events for the collector status."), 11);
        eventLogPage_->setUpdate(latestEventLogUpdate_);
        if (troubleshootingPage_ && activeLegacyPage_ == 17)
            troubleshootingPage_->setContext(latestSnapshot_, latestAnalysis_, latestEventLogUpdate_);
        eventLogPage_->setBusy(false);
    });
    connect(proactiveEventWatcher_, &QFutureWatcher<EventLogUpdate>::finished, this, [this] {
        const EventLogUpdate update = proactiveEventWatcher_->result();
        recordActivity(QStringLiteral("Background event check completed"), update.available
            ? QStringLiteral("Read recent Windows events and checked for repeated or meaningful signals. Review evidence in Logs.")
            : QStringLiteral("Windows did not expose a usable event result for this check."), 11);
        handleProactiveEventLogs(update);
    });
    proactiveEventTimer_ = new QTimer(this);
    proactiveEventTimer_->setInterval(5 * 60 * 1000);
    connect(proactiveEventTimer_, &QTimer::timeout, this, &MainWindow::refreshProactiveEventLogs);
    if (!uiCheck_) proactiveEventTimer_->start();
    refreshProactiveEventLogs();

    appInventoryWatcher_ = new QFutureWatcher<AppInventoryUpdate>(this);
    connect(appInventoryPage_, &AppInventoryPage::refreshRequested,
            this, &MainWindow::refreshAppInventory);
    connect(appInventoryWatcher_, &QFutureWatcher<AppInventoryUpdate>::finished, this, [this] {
        if (!appInventoryPage_) return;
        latestAppInventory_ = appInventoryWatcher_->result();
        recordActivity(QStringLiteral("App inventory check completed"), latestAppInventory_.available
            ? QStringLiteral("Read %1 startup entries and %2 installed-app records exposed by Windows.")
                .arg(latestAppInventory_.startupEntries.size()).arg(latestAppInventory_.installedApps.size())
            : QStringLiteral("Windows app inventory was unavailable for this check."), 12);
        appInventoryPage_->setUpdate(latestAppInventory_);
        const QStringList detectedChanges = SoftwareChangeTracker::observe(latestAppInventory_);
        latestAppInventory_.recentSoftwareChanges = SoftwareChangeTracker::recentChangeRecords();
        appInventoryPage_->setSoftwareChangeHistory(SoftwareChangeTracker::recentChanges());
        if (!detectedChanges.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("Ausyn observed %1 software inventory change(s). Review them under Startup & apps → Recent changes.")
                .arg(detectedChanges.size()), 9000);
        }
        appInventoryPage_->setBusy(false);
    });
    refreshAppInventory();

    securityStatusWatcher_ = new QFutureWatcher<SecurityStatusUpdate>(this);
    updateCacheWatcher_ = new QFutureWatcher<UpdateCacheResult>(this);
    connect(securityPage_, &SecurityPage::refreshSecurityRequested,
            this, &MainWindow::refreshSecurityStatus);
    connect(securityPage_, &SecurityPage::scanUpdatesRequested,
            this, &MainWindow::scanLocalUpdates);
    connect(securityStatusWatcher_, &QFutureWatcher<SecurityStatusUpdate>::finished, this, [this] {
        const SecurityStatusUpdate update = securityStatusWatcher_->result();
        recordActivity(QStringLiteral("Windows security status checked"),
            QStringLiteral("Antivirus: %1 · Firewall: %2 · Updates: %3. These are Windows-reported provider states.")
                .arg(update.antivirus.status, update.firewall.status, update.automaticUpdates.status), 13);
        if (securityPage_) securityPage_->setSecurityStatus(update);
        if (securityPage_) securityPage_->setSecurityBusy(false);
        handleProactiveSecurityStatus(update);
    });
    connect(updateCacheWatcher_, &QFutureWatcher<UpdateCacheResult>::finished, this, [this] {
        if (securityPage_) securityPage_->setUpdateCacheResult(updateCacheWatcher_->result());
        if (securityPage_) securityPage_->setUpdateBusy(false);
    });
    if (preferences_.monitoringEnabled && preferences_.proactiveSecurityScans) refreshSecurityStatus();
    securityMonitorTimer_ = new QTimer(this);
    securityMonitorTimer_->setInterval(5 * 60 * 1000);
    connect(securityMonitorTimer_, &QTimer::timeout, this, [this] {
        if (preferences_.monitoringEnabled && preferences_.proactiveSecurityScans) refreshSecurityStatus();
    });
    if (!uiCheck_) securityMonitorTimer_->start();

    casualTone_->setChecked(preferences_.casualTone);
    technicalDetail_->setChecked(preferences_.technicalDetail);
    showAdvancedTools_->setChecked(preferences_.showAdvancedTools);
    showQuickStartGuide_->setChecked(preferences_.showQuickStartGuide);
    appearance_->setCurrentIndex(appearance_->findData(preferences_.lightTheme));
    adaptiveSampling_->setChecked(preferences_.adaptiveSamplingEnabled);
    saveConversations_->setChecked(preferences_.saveConversationsLocally);
    desktopNotifications_->setChecked(preferences_.desktopNotificationsEnabled);
    systemFindingAlerts_->setChecked(preferences_.notifySystemFindings);
    forecastAlerts_->setChecked(preferences_.notifyForecasts);
    eventLogAlerts_->setChecked(preferences_.notifyEventLogs);
    startupAlerts_->setChecked(preferences_.notifyStartupApps);
    securityAlerts_->setChecked(preferences_.notifySecurity);
    quietHoursEnabled_->setChecked(preferences_.quietHoursEnabled);
    resourceAlertSensitivity_->setValue(std::clamp(preferences_.resourceAlertSensitivity, 1, 5));
    const auto updateSensitivityLabel = [this](int value) {
        static const std::array<QString, 5> labels{
            QStringLiteral("Conservative · 88% / 92%"),
            QStringLiteral("Relaxed · 82% / 88%"),
            QStringLiteral("Balanced · 75% / 85%"),
            QStringLiteral("Responsive · 70% / 82%"),
            QStringLiteral("Sensitive · 65% / 78%")};
        resourceAlertSensitivityValue_->setText(labels.at(static_cast<std::size_t>(std::clamp(value, 1, 5) - 1)));
    };
    updateSensitivityLabel(resourceAlertSensitivity_->value());
    startWithWindows_->setChecked(preferences_.startWithWindows);
    quietHoursStart_->setTime(QTime(preferences_.quietHoursStartMinute / 60, preferences_.quietHoursStartMinute % 60));
    quietHoursEnd_->setTime(QTime(preferences_.quietHoursEndMinute / 60, preferences_.quietHoursEndMinute % 60));
    const auto updateAlertControlState = [this] {
        const bool alertsEnabled = desktopNotifications_->isChecked();
        const QList<QWidget*> alertControls{systemFindingAlerts_, forecastAlerts_, eventLogAlerts_,
                                            startupAlerts_, securityAlerts_, quietHoursEnabled_};
        for (QWidget* control : alertControls)
            control->setEnabled(alertsEnabled);
        quietHoursStart_->setEnabled(alertsEnabled && quietHoursEnabled_->isChecked());
        quietHoursEnd_->setEnabled(alertsEnabled && quietHoursEnabled_->isChecked());
        resourceAlertSensitivity_->setEnabled(true);
        resourceAlertSensitivityValue_->setEnabled(true);
    };
    updateAlertControlState();
    cloudAi_->setChecked(preferences_.cloudAiEnabled);
    webSearch_->setChecked(preferences_.webSearchEnabled);
    webSearch_->setEnabled(preferences_.cloudAiEnabled);
    shareProcessNames_->setChecked(preferences_.shareProcessNames);
    apiEndpoint_->setText(preferences_.apiEndpoint);
    apiModel_->setText(preferences_.apiModel);
    apiKeyStatus_->setText(preferences_.encryptedApiKey.isEmpty()
        ? QStringLiteral("No API key saved.") : QStringLiteral("An API key is protected for this Windows account."));
    const int retentionIndex = retentionDays_->findData(preferences_.historyRetentionDays);
    retentionDays_->setCurrentIndex(retentionIndex >= 0 ? retentionIndex : 1);
    const int intervalIndex = samplingInterval_->findData(preferences_.samplingIntervalSeconds);
    const int balancedIndex = samplingInterval_->findData(5);
    samplingInterval_->setCurrentIndex(intervalIndex >= 0 ? intervalIndex : std::max(0, balancedIndex));
    if (agentHealthPage_) agentHealthPage_->setSamplingIntervalSeconds(preferences_.samplingIntervalSeconds);
    if (preferences_.saveConversationsLocally) {
        for (const ChatMessage& message : chatMessages_) {
            const QString safe = message.text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            chatTranscript_->append(QStringLiteral("<p><b style='color:%1'>%2</b><br>%3</p>")
                .arg(message.fromUser ? QStringLiteral("#9aa5b8") : QStringLiteral("#b8b6ff"),
                     message.fromUser ? QStringLiteral("You") : QStringLiteral("Ausyn"), safe));
        }
    }
    const auto sendMessage = [this] { sendAssistantMessage(); };
    connect(chatInput_, &QLineEdit::returnPressed, this, sendMessage);
    if (auto* sendButton = legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantSend"))) {
        connect(sendButton, &QPushButton::clicked, this, sendMessage);
    }
    connect(casualTone_, &QCheckBox::toggled, this, [this] { savePreferences(); });
    connect(appearance_, &QComboBox::currentIndexChanged, this, [this](int) {
        applyTheme(appearance_->currentData().toBool());
        savePreferences();
    });
    connect(showAdvancedTools_, &QCheckBox::toggled, this, [this](bool visible) {
        preferences_.showAdvancedTools = visible;
        setAdvancedToolsVisible(visible);
        savePreferences();
    });
    connect(showQuickStartGuide_, &QCheckBox::toggled, this, [this](bool visible) {
        preferences_.showQuickStartGuide = visible;
        if (quickStartPanel_) quickStartPanel_->setVisible(visible);
        savePreferences();
    });
    connect(technicalDetail_, &QCheckBox::toggled, this, [this] {
        if (hubs_.size() > 2) hubs_[2]->setDetailed(technicalDetail_->isChecked());
        savePreferences();
        if (diagnosticsView_) {
            diagnosticsView_->clearFindingExpansionOverrides();
            diagnosticsView_->setFindings(latestAnalysis_.findings,
                QStringLiteral("No sustained issues match the current evidence."), false,
                preferences_.technicalDetail);
        }
        if (recommendationsView_) {
            recommendationsView_->clearFindingExpansionOverrides();
            recommendationsView_->setFindings(latestAnalysis_.findings,
                QStringLiteral("Nothing needs your attention based on the current measurements."), true,
                preferences_.technicalDetail);
        }
    });
    connect(desktopNotifications_, &QCheckBox::toggled, this, [this] { savePreferences(); });
    const auto saveAlertSettings = [this] { savePreferences(); };
    connect(systemFindingAlerts_, &QCheckBox::toggled, this, saveAlertSettings);
    connect(forecastAlerts_, &QCheckBox::toggled, this, saveAlertSettings);
    connect(eventLogAlerts_, &QCheckBox::toggled, this, saveAlertSettings);
    connect(startupAlerts_, &QCheckBox::toggled, this, saveAlertSettings);
    connect(securityAlerts_, &QCheckBox::toggled, this, saveAlertSettings);
    connect(quietHoursEnabled_, &QCheckBox::toggled, this, [this, updateAlertControlState, saveAlertSettings] {
        updateAlertControlState();
        saveAlertSettings();
    });
    connect(desktopNotifications_, &QCheckBox::toggled, this, [updateAlertControlState] { updateAlertControlState(); });
    connect(quietHoursStart_, &QTimeEdit::timeChanged, this, saveAlertSettings);
    connect(quietHoursEnd_, &QTimeEdit::timeChanged, this, saveAlertSettings);
    connect(resourceAlertSensitivity_, &QSlider::valueChanged, this, [this, updateSensitivityLabel] (int value) {
        updateSensitivityLabel(value);
        savePreferences();
    });
    connect(systemFindingAlerts_, &QCheckBox::toggled, this, [updateAlertControlState] { updateAlertControlState(); });
    connect(startWithWindows_, &QCheckBox::toggled, this, [this](bool enabled) {
        QString error;
        if (!UserPreferencesStore::setStartWithWindows(enabled, &error)) {
            const QSignalBlocker blocker(startWithWindows_);
            startWithWindows_->setChecked(preferences_.startWithWindows);
            QMessageBox::warning(this, QStringLiteral("Startup setting could not be changed"), error);
            return;
        }
        preferences_.startWithWindows = enabled;
        savePreferences();
    });
    connect(cloudAi_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            const auto answer = QMessageBox::question(this, QStringLiteral("Enable optional cloud AI?"),
                QStringLiteral("When you ask a question, Ausyn will send that question and only relevant, minimized live readings to the configured provider. Process names are sent only when their separate setting is enabled and your question needs them. Conversation history, process IDs, file contents, and the full telemetry database are not sent. The provider processes the request under its own privacy and retention terms. Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                const QSignalBlocker blocker(cloudAi_);
                cloudAi_->setChecked(false);
                return;
            }
        }
        webSearch_->setEnabled(enabled);
        savePreferences();
    });
    connect(webSearch_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) {
            const auto answer = QMessageBox::question(this, QStringLiteral("Allow paid web-search requests?"),
                QStringLiteral("When you approve a general question, Ausyn will send the question to OpenAI’s gpt-5-search-api for live web search. Game-readiness questions may also include only the CPU, graphics, memory, and drive figures needed for comparison. Search and model usage may be billed by your API provider. Process names and conversation history are not sent. Enable web search?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                const QSignalBlocker blocker(webSearch_);
                webSearch_->setChecked(false);
                return;
            }
        }
        savePreferences();
    });
    connect(shareProcessNames_, &QCheckBox::toggled, this, [this] { savePreferences(); });
    connect(saveConversations_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!enabled) {
            QString error;
            if (!UserPreferencesStore::deleteConversations(&error)) {
                QMessageBox::warning(this, QStringLiteral("Conversation history"), error);
            }
            chatMessages_.clear();
            if (chatTranscript_) {
                chatTranscript_->clear();
                chatTranscript_->append(QStringLiteral("<b style='color:#b8b6ff'>Ausyn</b><br>Conversation history is off. Your messages are kept only in this open session."));
            }
        }
        savePreferences();
    });
    connect(retentionDays_, &QComboBox::currentIndexChanged, this, [this] { savePreferences(); });
    connect(samplingInterval_, &QComboBox::currentIndexChanged, this, [this] { savePreferences(); });
    connect(adaptiveSampling_, &QCheckBox::toggled, this, [this](bool enabled) {
        preferences_.adaptiveSamplingEnabled = enabled;
        if (telemetry_) telemetry_->setAdaptiveSamplingEnabled(enabled);
        savePreferences();
    });
    connect(apiEndpoint_, &QLineEdit::editingFinished, this, [this] { savePreferences(); });
    connect(apiModel_, &QLineEdit::editingFinished, this, [this] { savePreferences(); });
    connect(apiKey_, &QLineEdit::editingFinished, this, [this] { savePreferences(); });
    for (QPushButton* button : legacyPage(9)->findChildren<QPushButton*>()) {
        if (button->property("showWelcomeGuide").toBool()) {
            connect(button, &QPushButton::clicked, this, [this] {
                const int choice = showWelcomeGuide(this);
                if (choice == 2) showPage(9, QStringLiteral("Settings"));
            });
        }
        if (button->property("removeApiKey").toBool()) {
            connect(button, &QPushButton::clicked, this, [this] {
                preferences_.encryptedApiKey.clear();
                apiKey_->clear();
                savePreferences();
                apiKeyStatus_->setText(QStringLiteral("No API key saved."));
            });
        }
    }
    for (QPushButton* button : legacyPage(3)->findChildren<QPushButton*>()) {
        if (!button->property("clearDeviceChangeHistory").toBool()) continue;
        connect(button, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, QStringLiteral("Clear device-change timeline?"),
                    QStringLiteral("This removes Ausyn’s locally saved hardware change entries. The current device baseline remains, so future changes can still be recorded."),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            QString error;
            if (!HardwareChangeTracker::clearChangeHistory(&error)) {
                QMessageBox::warning(this, QStringLiteral("Timeline could not be cleared"), error);
                return;
            }
            hardwareTimeline_->setText(QStringLiteral("No device-profile changes have been recorded."));
        });
    }
    for (QPushButton* button : legacyPage(8)->findChildren<QPushButton*>()) {
        const QString action = button->property("historyAction").toString();
        if (action == QStringLiteral("generateReport")) {
            connect(button, &QPushButton::clicked, this, [this] {
                refreshHistorySummary();
                if (telemetry_) telemetry_->requestHistory(historyPeriodHours_);
            });
        } else if (action == QStringLiteral("exportReport")) {
            connect(button, &QPushButton::clicked, this, &MainWindow::exportSystemReport);
        } else if (action == QStringLiteral("export")) {
            connect(button, &QPushButton::clicked, this, [this] {
                const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Ausyn history"),
                    QStringLiteral("ausyn-history.csv"), QStringLiteral("CSV files (*.csv)"));
                if (!path.isEmpty() && telemetry_) telemetry_->exportHistory(path);
            });
        } else if (action == QStringLiteral("backup")) {
            connect(button, &QPushButton::clicked, this, [this] {
                const QString fileName = QStringLiteral("ausyn-history-backup-%1.sqlite3")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
                const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Back up Ausyn history"),
                    fileName, QStringLiteral("Ausyn SQLite history (*.sqlite3)"));
                if (!path.isEmpty() && telemetry_) telemetry_->backupHistory(path);
            });
        } else if (action == QStringLiteral("restore")) {
            connect(button, &QPushButton::clicked, this, [this] {
                const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Restore Ausyn history backup"),
                    QString{}, QStringLiteral("Ausyn SQLite history (*.sqlite3);;SQLite files (*.db *.sqlite)"));
                if (path.isEmpty()) return;
                const auto answer = QMessageBox::warning(this, QStringLiteral("Replace Ausyn history?"),
                    QStringLiteral("This replaces the current performance history with data from the selected backup. Chats and preferences are not included. A damaged or incompatible backup will be rejected, and Ausyn will try to preserve the current history if restore fails. Continue?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer == QMessageBox::Yes && telemetry_) telemetry_->restoreHistory(path);
            });
        } else if (action == QStringLiteral("clear")) {
            connect(button, &QPushButton::clicked, this, [this] {
                const auto answer = QMessageBox::question(this, QStringLiteral("Clear local Ausyn data?"),
                    QStringLiteral("This deletes performance samples, saved findings, recommendation feedback, and saved conversations from this PC. This cannot be undone."),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Yes) return;
                QString error;
                if (!UserPreferencesStore::deleteConversations(&error)) {
                    QMessageBox::warning(this, QStringLiteral("Could not clear all data"), error);
                    return;
                }
                chatMessages_.clear();
                companion_.forget();
                if (!uiCheck_ && !companionStatePath_.isEmpty() && QFile::exists(companionStatePath_) && !QFile::remove(companionStatePath_))
                    QMessageBox::warning(this, QStringLiteral("Companion file could not be cleared"), QStringLiteral("Windows denied removal of the saved companion file. Use Forget learned patterns in Settings to retry."));
                if (chatTranscript_) chatTranscript_->clear();
                if (telemetry_) telemetry_->clearHistory();
            });
        }
    }

    telemetry_ = new TelemetryService(this);
    assistantClient_ = new AssistantClient(this);
    connect(assistantClient_, &AssistantClient::responseReady, this, &MainWindow::completeAssistantReply);
    connect(legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantCancel")), &QPushButton::clicked,
            assistantClient_, &AssistantClient::cancel);
    telemetry_->setRetentionDays(preferences_.historyRetentionDays);
    telemetry_->setSamplingIntervalSeconds(preferences_.samplingIntervalSeconds);
    telemetry_->setAdaptiveSamplingEnabled(preferences_.adaptiveSamplingEnabled);
    monitoringFreshnessTimer_ = new QTimer(this);
    monitoringFreshnessTimer_->setInterval(1000);
    connect(monitoringFreshnessTimer_, &QTimer::timeout,
            this, &MainWindow::updateMonitoringStatus);
    monitoringFreshnessTimer_->start();
    updateMonitoringStatus();
    connect(telemetry_, &TelemetryService::snapshotReady,
            this, &MainWindow::updateSystemSnapshot, Qt::QueuedConnection);
    connect(telemetry_, &TelemetryService::analysisReady,
            this, &MainWindow::updateAnalysis, Qt::QueuedConnection);
    connect(telemetry_, &TelemetryService::historyReady,
            this, &MainWindow::updateHistory, Qt::QueuedConnection);
    connect(historyPeriod_, &QComboBox::currentIndexChanged, this, [this] {
        historyPeriodHours_ = historyPeriod_->currentData().toInt();
        if (telemetry_) telemetry_->requestHistory(historyPeriodHours_);
    });
    connect(telemetry_, &TelemetryService::historyOperationFinished, this,
            [this](const QString& operation, bool success, const QString& message) {
                Q_UNUSED(operation)
                if (success) {
                    if (message.startsWith(QStringLiteral("Local history"))) {
                        latestAnalysis_ = {};
                        if (diagnosticsView_) diagnosticsView_->setFindings({}, QStringLiteral("History cleared."));
                        if (recommendationsView_) recommendationsView_->setFindings({}, QStringLiteral("History cleared."));
                    }
                    if (historyStatus_) historyStatus_->setText(message);
                    QMessageBox::information(this, QStringLiteral("Ausyn data"), message);
                } else {
                    QMessageBox::warning(this, QStringLiteral("Ausyn data"), message);
                }
            });
    connect(recommendationsView_, &InsightListWidget::recommendationOutcomeReported, this,
            [this](const QString& ruleId, const QDateTime& firstSeen, int outcome) {
        if (!telemetry_) return;
        telemetry_->recordRecommendationOutcome(ruleId, firstSeen,
            static_cast<RecommendationOutcome>(outcome));
    });
    connect(telemetry_, &TelemetryService::recommendationOutcomeFinished, this,
            [this](const QString& ruleId, const QDateTime& firstSeen, int outcome,
                   bool success, const QString& message) {
        if (success) {
            for (Finding& finding : latestAnalysis_.findings) {
                if (finding.ruleId == ruleId && finding.firstSeen == firstSeen) {
                    finding.recommendationOutcome = static_cast<RecommendationOutcome>(outcome);
                    break;
                }
            }
            const QString empty = QStringLiteral("No sustained issues match the current evidence.");
            if (diagnosticsView_) diagnosticsView_->setFindings(latestAnalysis_.findings, empty,
                false, preferences_.technicalDetail);
            if (recommendationsView_)
                recommendationsView_->setFindings(latestAnalysis_.findings, empty, true,
                    preferences_.technicalDetail);
            statusBar()->showMessage(message, 7000);
        } else {
            QMessageBox::warning(this, QStringLiteral("Feedback not saved"), message);
            if (recommendationsView_)
                recommendationsView_->setFindings(latestAnalysis_.findings,
                    QStringLiteral("No recommendations yet."), true, preferences_.technicalDetail);
        }
    });
    connect(recommendationsView_, &InsightListWidget::recommendationVerificationRequested, this,
            [this](const QString& ruleId, const QDateTime& firstSeen, bool captureBaseline) {
        const qint64 sampleAgeMilliseconds = latestSnapshot_.capturedAt.isValid()
            ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        if (!telemetry_ || sampleAgeMilliseconds < 0 || sampleAgeMilliseconds > 30'000) {
            statusBar()->showMessage(QStringLiteral("Waiting for a fresh system sample; try the comparison again in a moment."), 7000);
            if (recommendationsView_)
                recommendationsView_->setFindings(latestAnalysis_.findings,
                    QStringLiteral("No recommendations yet."), true, preferences_.technicalDetail);
            return;
        }
        telemetry_->recordRecommendationVerification(ruleId, firstSeen, latestSnapshot_, captureBaseline);
    });
    connect(telemetry_, &TelemetryService::recommendationVerificationFinished, this,
            [this](const QString& ruleId, const QDateTime& firstSeen, bool captureBaseline,
                   const QVector<Finding>& findings, bool success, const QString& message) {
        Q_UNUSED(ruleId)
        Q_UNUSED(firstSeen)
        Q_UNUSED(captureBaseline)
        if (success) {
            latestAnalysis_.findings = findings;
            const QString diagnosticsEmpty = QStringLiteral("No sustained issues match the current evidence.");
            const QString recommendationsEmpty = QStringLiteral("Nothing needs your attention based on the current measurements.");
            if (diagnosticsView_)
                diagnosticsView_->setFindings(findings, diagnosticsEmpty, false, preferences_.technicalDetail);
            if (recommendationsView_)
                recommendationsView_->setFindings(findings, recommendationsEmpty, true, preferences_.technicalDetail);
            statusBar()->showMessage(message, 9000);
        } else {
            statusBar()->showMessage(message, 9000);
            if (recommendationsView_)
                recommendationsView_->setFindings(latestAnalysis_.findings,
                    QStringLiteral("No recommendations yet."), true, preferences_.technicalDetail);
        }
    });
    diagnosticsView_->setFindings({}, QStringLiteral("Ausyn is collecting evidence. Sustained findings will appear here with the measurements that triggered them."));
    recommendationsView_->setFindings({}, QStringLiteral("No recommendations yet. Ausyn will suggest a next step when current evidence supports one."));
    preferencesReady_ = !uiCheck_;
    applyBehaviorPreferences();
    for (int i = 0; i < hubs_.size(); ++i) updateDetailMode(i);
    if (!uiCheck_) telemetry_->start();
    recordActivity(QStringLiteral("Ausyn started"), preferences_.monitoringEnabled
        ? QStringLiteral("Local monitoring enabled. Supported Windows readings are checked in the background; system settings require your control.")
        : QStringLiteral("Monitoring is paused by your saved preference. Resume it from the dashboard or Settings."), 0);
    static const std::array<int, 5> defaultSections{0, 2, 1, 18, 9};
    showPage(defaultSections[preferences_.landingPage], QString());

    if (!navigationButtons_.isEmpty()) {
        navigationButtons_.at(preferences_.landingPage)->setChecked(true);
    }
    themeController_ = new ThemeController(this);
    applyTheme(preferences_.lightTheme);
    QTimer::singleShot(0, this, [this] {
        if (uiCheck_) return;
        QSettings settings;
        if (settings.value(QStringLiteral("onboarding/completed"), false).toBool()) return;
        const int choice = showWelcomeGuide(this);
        if (choice == QDialog::Accepted || choice == 2) {
            settings.setValue(QStringLiteral("onboarding/completed"), true);
            if (choice == 2) showPage(9, QStringLiteral("Settings"));
        }
    });
}

bool MainWindow::runUiCheck(const QString& outputDirectory)
{
    if (!uiCheck_ || !QDir().mkpath(outputDirectory)) return false;
    setProperty("usingFixture", true);
    QJsonArray checks;
    bool passed = true;
    const auto expect = [&checks, &passed](const QString& name, bool result) {
        checks.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("passed"), result}});
        passed = passed && result;
    };
    preferences_.animationsEnabled = false;
    preferences_.autonomousCompanionEnabled = false;
    preferences_.smartAttentionEnabled = false;
    preferences_.workloadAwarenessEnabled = false; // Exercise the earlier alert path before the new episode path.
    applyBehaviorPreferences();
    resize(1400, 950);
    setWindowTitle(QStringLiteral("Ausyn UI validation — sample data"));
    expect(QStringLiteral("Exactly five sidebar destinations and five main pages"), navigationButtons_.size() == 5 && pages_->count() == 5);
    expect(QStringLiteral("Main window uses Ausyn chrome with three accessible controls"), windowFlags().testFlag(Qt::FramelessWindowHint) &&
        findChild<QToolButton*>(QStringLiteral("ausynMinimize")) && findChild<QToolButton*>(QStringLiteral("ausynMaximize")) && findChild<QToolButton*>(QStringLiteral("ausynClose")));
    SystemSnapshot sample;
    sample.samplingIntervalSeconds = 5;
    sample.memoryTotalBytes = 16'000'000'000ULL;
    sample.systemVolumePath = QStringLiteral("C:\\");
    sample.systemVolumeTotalBytes = 512'000'000'000ULL;
    sample.systemVolumeFreeBytes = 230'000'000'000ULL;
    sample.batteryPercent = 82;
    sample.batteryOnAcPower = true;
    sample.processorName = QStringLiteral("Example 8-core processor (UI fixture)");
    sample.graphicsName = QStringLiteral("Example graphics adapter (UI fixture)");
    GraphicsAdapterSample adapter;
    adapter.name = sample.graphicsName; adapter.dedicatedMemoryBytes = 4'000'000'000ULL;
    adapter.localMemoryUsageBytes = 900'000'000ULL; adapter.localMemoryBudgetBytes = 3'500'000'000ULL;
    sample.graphicsAdapters.append(adapter);
    sample.operatingSystem = QStringLiteral("Windows 11 (UI fixture)");
    sample.networkReceiveBytesPerSecond = 240'000;
    sample.networkSendBytesPerSecond = 32'000;
    sample.thermalSensorStatus = QStringLiteral("No temperature readings in this UI fixture.");
    sample.logicalProcessorCount = 8;
    ProcessSample browser;
    browser.processId = 101; browser.name = QStringLiteral("Example browser.exe");
    browser.cpuPercent = 12.5; browser.workingSetBytes = 1'200'000'000ULL;
    ProcessSample editor;
    editor.processId = 102; editor.name = QStringLiteral("Example editor.exe");
    editor.cpuPercent = 3.5; editor.workingSetBytes = 450'000'000ULL;
    sample.topProcesses = {browser, editor};
    sample.foregroundProcessId = 101;
    sample.foregroundProcessName = browser.name;
    sample.volumes.append({sample.systemVolumePath, QStringLiteral("System"), sample.systemVolumeTotalBytes, sample.systemVolumeFreeBytes});
    const QDateTime now = QDateTime::currentDateTime();
    for (int i = 0; i < 36; ++i) {
        sample.capturedAt = now.addSecs((i - 35) * 5);
        sample.processSamplesCapturedAt = sample.capturedAt;
        sample.processorUsagePercent = 40 + 22 * std::sin(i * 0.4);
        sample.memoryUsagePercent = 55 + 6 * std::sin(i * 0.2);
        sample.graphicsUsagePercent = 15 + 10 * std::sin(i * 0.3);
        sample.memoryUsedBytes = static_cast<quint64>(sample.memoryTotalBytes * *sample.memoryUsagePercent / 100);
        sample.memoryAvailableBytes = sample.memoryTotalBytes - sample.memoryUsedBytes;
        sample.processorCores.clear();
        for (int core = 0; core < 8; ++core) sample.processorCores.append({QStringLiteral("Core %1").arg(core), 15.0 + core * 8});
        updateSystemSnapshot(sample);
    }
    AnalysisUpdate analysis;
    analysis.health.score = 94;
    analysis.health.coveragePercent = 100;
    analysis.health.explanation = QStringLiteral("UI fixture: CPU, memory and system drive form this limited performance index. Other health signals are not rated.");
    updateAnalysis(analysis);
    const auto capture = [this, &outputDirectory, &expect](const QString& name) {
        statusBar()->showMessage(QStringLiteral("UI VALIDATION — sample readings, not a live diagnosis"));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        expect(QStringLiteral("Render %1").arg(name), grab().save(QDir(outputDirectory).filePath(name + QStringLiteral(".png"))));
    };
    resize(1232, 592);
    showPage(0, QString());
    hubs_[0]->setDetailed(false);
    capture(QStringLiteral("dashboard-laptop"));
    auto* dashboardViewport = qobject_cast<QScrollArea*>(legacyPage(0))->viewport();
    bool readingsInView = true;
    for (const QString& key : {QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("storage")}) {
        auto* value = legacyPage(0)->findChild<QLabel*>(QStringLiteral("metric.%1.value").arg(key));
        readingsInView = readingsInView && value && value->isVisible() &&
            value->mapTo(dashboardViewport, QPoint(0, value->height())).y() <= dashboardViewport->height();
    }
    expect(QStringLiteral("Basic dashboard readings appear without scrolling at laptop height"), readingsInView);
    resize(1400, 950);
    for (auto it = legacyHub_.cbegin(); it != legacyHub_.cend(); ++it) {
        showPage(it.key(), QString());
        expect(QStringLiteral("Route section %1 into main page %2").arg(it.key()).arg(it.value()),
               pages_->currentIndex() == it.value() && hubs_[it.value()]->currentSection() == it.key());
    }
    const std::array<int, 5> first{0, 2, 1, 18, 9};
    const std::array<QString, 5> names{QStringLiteral("dashboard"), QStringLiteral("performance"), QStringLiteral("ask"), QStringLiteral("logs"), QStringLiteral("settings")};
    for (int hub = 0; hub < 5; ++hub) {
        showPage(first[hub], QString());
        hubs_[hub]->setDetailed(false);
        capture(names[hub] + QStringLiteral("-simple"));
        hubs_[hub]->setDetailed(true);
        capture(names[hub] + QStringLiteral("-details"));
    }
    showPage(2, QString());
    hubs_[1]->setDetailed(false);
    expect(QStringLiteral("Simple performance exposes CPU/memory and hides specialist columns"),
        !processTable_->isColumnHidden(2) && !processTable_->isColumnHidden(3) && processTable_->isColumnHidden(1) && processTable_->isColumnHidden(7));
    hubs_[1]->setDetailed(true);
    expect(QStringLiteral("Detailed performance restores specialist columns"), !processTable_->isColumnHidden(1) && !processTable_->isColumnHidden(7));
    checkSystemOnDashboard();
    expect(QStringLiteral("System check displays inline and remains on dashboard"), pages_->currentIndex() == 0 && dashboardCheckResult_->isVisible() && !dashboardCheckResult_->text().isEmpty());
    showPage(1, QString());
    chatInput_->setText(QStringLiteral("memory?"));
    sendAssistantMessage();
    expect(QStringLiteral("Local chat replies and re-enables the actual Send button"), chatInput_->isEnabled() &&
        legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantSend"))->isEnabled() && chatTranscript_->toPlainText().contains(QStringLiteral("Memory use")));
    const QString thermalReply = AssistantEngine::reply(QStringLiteral("why my laptop get heated"), sample, analysis, {}, preferences_);
    expect(QStringLiteral("A heating question reaches thermal guidance"), thermalReply.contains(QStringLiteral("temperature"), Qt::CaseInsensitive));
    expect(QStringLiteral("Heat questions stay local; drive temperature keeps its storage route"),
        AssistantEngine::requiresLocalThermalAssessment(QStringLiteral("why is my laptop hot")) &&
        AssistantEngine::requiresLocalThermalAssessment(QStringLiteral("CPU heated")) &&
        !AssistantEngine::requiresLocalThermalAssessment(QStringLiteral("drive temperature")) &&
        AssistantEngine::requiresLocalStorageReliability(QStringLiteral("drive temperature")));
    expect(QStringLiteral("Heating is not replaced by a generic slowdown answer"),
        !thermalReply.contains(QStringLiteral("Here’s the current picture")) &&
        AssistantEngine::reply(QStringLiteral("CPU heated"), sample, analysis, {}, preferences_)
            .contains(QStringLiteral("temperature"), Qt::CaseInsensitive));
    showPage(18, QString());
    hubs_[3]->setDetailed(false);
    auto* activityTable = activityPage_->findChild<QTableWidget*>(QStringLiteral("activityTable"));
    expect(QStringLiteral("Simple activity hides evidence details"), activityTable && activityTable->isColumnHidden(2));
    hubs_[3]->setDetailed(true);
    expect(QStringLiteral("Detailed activity restores evidence"), activityTable && !activityTable->isColumnHidden(2));
    UserPreferences simpleReply = preferences_;
    simpleReply.technicalDetail = false;
    UserPreferences detailedReply = simpleReply;
    detailedReply.technicalDetail = true;
    expect(QStringLiteral("Simple and detailed system checks have different evidence depth"),
        !AssistantEngine::reply(QStringLiteral("Check my system"), sample, analysis, {}, simpleReply).contains(QStringLiteral("Measured performance/storage index")) &&
         AssistantEngine::reply(QStringLiteral("Check my system"), sample, analysis, {}, detailedReply).contains(QStringLiteral("Measured performance/storage index")));
    preferences_.monitoringEnabled = false;
    applyBehaviorPreferences();
    expect(QStringLiteral("Pause is visible in the monitoring status"), monitoringStatus_->text().contains(QStringLiteral("paused")));
    preferences_.monitoringEnabled = true;
    applyBehaviorPreferences();
    showPage(0, QString());
    dashboardCheckResult_->hide();
    hubs_[0]->setDetailed(false);
    applyTheme(true);
    capture(QStringLiteral("dashboard-light"));
    applyTheme(false);
    resize(900, 650);
    capture(QStringLiteral("dashboard-compact"));
    expect(QStringLiteral("Compact sidebar and page stay inside the window"),
        pages_->geometry().right() <= centralWidget()->width() && navigationButtons_.first()->width() <= 224);
    expect(QStringLiteral("Compact dashboard has no horizontal overflow"),
        qobject_cast<QScrollArea*>(legacyPage(0))->horizontalScrollBar()->maximum() == 0);
    expect(QStringLiteral("Window can use the requested compact size"), width() <= 900 && height() <= 650);
    const auto statusFits = [this] { return monitoringStatus_->fontMetrics().horizontalAdvance(monitoringStatus_->text()) + 2 <= monitoringStatus_->contentsRect().width(); };
    expect(QStringLiteral("Compact live status text fits its pill"), statusFits());
    const SystemSnapshot currentSnapshot = latestSnapshot_;
    latestSnapshot_.capturedAt = QDateTime::currentDateTime().addSecs(-60);
    updateMonitoringStatus(); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    expect(QStringLiteral("Compact delayed status text fits its pill"), statusFits());
    latestSnapshot_.capturedAt = QDateTime::currentDateTime().addSecs(5);
    updateMonitoringStatus(); QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    expect(QStringLiteral("Compact clock-mismatch status text fits its pill"), statusFits());
    latestSnapshot_ = currentSnapshot; updateMonitoringStatus();
    for (int hub = 1; hub < 5; ++hub) {
        showPage(first[hub], QString());
        hubs_[hub]->setDetailed(false);
        capture(names[hub] + QStringLiteral("-compact"));
    }
    bool shortcutsFit = true;
    for (QPushButton* button : legacyPage(1)->findChildren<QPushButton*>()) {
        if (!button->property("chatShortcut").toBool()) continue;
        const QPoint right = button->mapTo(centralWidget(), QPoint(button->width(), 0));
        shortcutsFit = shortcutsFit && right.x() <= centralWidget()->width();
    }
    expect(QStringLiteral("Compact chat shortcuts fit inside the window"), shortcutsFit);
    showPage(9, QString());
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const int selected = samplingInterval_->currentIndex();
    QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, -120), Qt::NoButton,
        Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(samplingInterval_, &wheel);
    expect(QStringLiteral("Scrolling across a closed selector cannot change its setting"), samplingInterval_->currentIndex() == selected);
    recentLoadSamples_.clear();
    earlyPressureActive_ = false;
    SystemSnapshot pressure = sample;
    const QDateTime pressureNow = QDateTime::currentDateTime();
    pressure.processorUsagePercent = 90;
    pressure.memoryUsagePercent = 88;
    for (int i = 0; i < 6; ++i) {
        SystemSnapshot sparse = pressure;
        sparse.capturedAt = pressureNow.addSecs((i - 5) * 5);
        if (i != 0) { sparse.processorUsagePercent.reset(); sparse.memoryUsagePercent.reset(); }
        updateEarlyPressureHeadsUp(sparse);
    }
    expect(QStringLiteral("Missing readings cannot turn one high sample into sustained pressure"), !earlyPressureActive_);
    recentLoadSamples_.clear();
    for (int i = 0; i < 6; ++i) {
        pressure.capturedAt = pressureNow.addSecs((i - 5) * 5);
        pressure.processSamplesCapturedAt = pressure.capturedAt;
        updateEarlyPressureHeadsUp(pressure);
        if (i == 0) expect(QStringLiteral("One high reading does not create a sustained-load finding"), !earlyPressureActive_);
    }
    latestSnapshot_ = pressure;
    updateDashboardReadout();
    expect(QStringLiteral("Sustained load creates proactive dashboard advice without a question"), earlyPressureActive_ &&
        std::any_of(latestAnalysis_.findings.cbegin(), latestAnalysis_.findings.cend(), [](const Finding& finding) {
            return finding.ruleId == QLatin1String("live-sustained-resource-load");
        }));
    resize(1400, 950);
    showPage(0, QString());
    capture(QStringLiteral("dashboard-pressure"));
    // Resource inspectors are real windows, driven by the existing sample stream.
    earlyPressureActive_ = false; recentLoadSamples_.clear(); latestAnalysis_ = analysis;
    latestSnapshot_ = sample;
    const int sidebarCount = navigationButtons_.size();
    hubs_[0]->setDetailed(true);
    const auto cards = legacyPage(0)->findChildren<ResourceCard*>();
    expect(QStringLiteral("All six dashboard resources have interactive cards"), cards.size() == 6);
    for (auto* card : cards) {
        const int resource = card->property("resourceKind").toInt();
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(card, &enter);
        auto* inspector = resourceWindows_.value(resource).data();
        expect(QStringLiteral("Resource %1 opens a separate window through its card").arg(resource), inspector && inspector->isWindow() && inspector->isVisible());
        if (!inspector) continue;
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        expect(QStringLiteral("Render resource inspector %1").arg(resource), inspector->grab().save(QDir(outputDirectory).filePath(QStringLiteral("resource-%1.png").arg(resource))));
        openResource(resource);
        expect(QStringLiteral("Resource %1 reuses its window").arg(resource), resourceWindows_.value(resource).data() == inspector);
        auto* detail = inspector->findChild<DetailSwitch*>();
        auto* process = inspector->findChild<QTableWidget*>(QStringLiteral("inspectorProcesses"));
        if (resource == static_cast<int>(ResourceKind::Memory)) {
            detail->setChecked(false);
            expect(QStringLiteral("Memory simple mode hides PID"), process && process->isColumnHidden(3));
            detail->setChecked(true);
            expect(QStringLiteral("Memory detail mode restores PID and advice"), !process->isColumnHidden(3) && !inspector->summaryText().isEmpty());
            auto* filter = inspector->findChild<QLineEdit*>();
            filter->setText(QStringLiteral("editor"));
            expect(QStringLiteral("Resource process filter uses the current readable sample"), process->rowCount() == 1 && process->item(0, 0)->text() == editor.name);
            filter->clear();
        }
        inspector->findChild<QToolButton*>(QStringLiteral("ausynMaximize"))->click();
        expect(QStringLiteral("Resource %1 custom maximize works").arg(resource), inspector->isMaximized());
        inspector->findChild<QToolButton*>(QStringLiteral("ausynMaximize"))->click();
        expect(QStringLiteral("Resource %1 custom restore works").arg(resource), !inspector->isMaximized());
        inspector->findChild<QToolButton*>(QStringLiteral("ausynClose"))->click();
        expect(QStringLiteral("Resource %1 custom close hides only that window").arg(resource), !inspector->isVisible() && preferences_.monitoringEnabled);
    }
    openResource(static_cast<int>(ResourceKind::Processes));
    expect(QStringLiteral("Running apps has a separate searchable inspector; sidebar stays five"), resourceWindows_.size() == 7 && navigationButtons_.size() == sidebarCount && sidebarCount == 5);
    auto* apps = resourceWindows_.value(static_cast<int>(ResourceKind::Processes)).data();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    expect(QStringLiteral("Render running-app inspector"), apps->grab().save(QDir(outputDirectory).filePath(QStringLiteral("resource-processes.png"))));
    apps->hide();
    openResource(static_cast<int>(ResourceKind::Memory));
    auto* memoryInspector = resourceWindows_.value(static_cast<int>(ResourceKind::Memory)).data();
    SystemSnapshot beforeHidden = sample;
    beforeHidden.capturedAt = QDateTime::currentDateTime().addSecs(-3);
    memoryInspector->setContext(beforeHidden, analysis, true, true);
    hide();
    SystemSnapshot changed = sample;
    changed.capturedAt = QDateTime::currentDateTime(); changed.memoryUsagePercent = 73;
    changed.memoryUsedBytes = 11'680'000'000ULL; changed.memoryAvailableBytes = changed.memoryTotalBytes - changed.memoryUsedBytes;
    updateSystemSnapshot(changed);
    expect(QStringLiteral("Open inspector receives updates while main window is hidden"), memoryInspector->findChild<QLabel*>(QStringLiteral("inspectorValue"))->text() == QStringLiteral("73.0%"));
    memoryInspector->updateFreshness(false);
    expect(QStringLiteral("Inspector labels paused readings instead of claiming live data"), memoryInspector->findChild<QLabel*>(QStringLiteral("inspectorFreshness"))->text().contains(QStringLiteral("paused")));
    applyTheme(true); memoryInspector->setContext(changed, analysis, true, true);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    expect(QStringLiteral("Render light memory inspector"), memoryInspector->grab().save(QDir(outputDirectory).filePath(QStringLiteral("resource-memory-light.png"))));
    memoryInspector->hide(); applyTheme(false); show();

    // A visible window behind Edge has no app focus. It must not suppress cards.
    UserPreferences alertPrefs;
    const QDateTime localNight(QDate(2026, 10, 1), QTime(23, 30));
    expect(QStringLiteral("Own cards work behind another app without a Windows tray dependency"),
        AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, false, localNight).ownPopup);
    expect(QStringLiteral("Focused Ausyn suppresses desktop popups but reports why"),
        !AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, true, true, localNight).reason.isEmpty());
    alertPrefs.alertsOnlyInBackground = false;
    expect(QStringLiteral("Foreground alert preference is configurable"), AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, true, false, localNight).ownPopup);
    alertPrefs.quietHoursEnabled = true; alertPrefs.quietHoursStartMinute = 22 * 60; alertPrefs.quietHoursEndMinute = 7 * 60;
    expect(QStringLiteral("Overnight quiet hours block night but permit daytime"),
        !AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, true, localNight).ownPopup &&
        AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, true, QDateTime(QDate(2026,10,1), QTime(12,0))).ownPopup);
    alertPrefs.quietHoursEnabled = false;
    expect(QStringLiteral("Snooze affects alerts without stopping monitoring"), !AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, true, localNight, localNight.addSecs(900)).ownPopup && alertPrefs.monitoringEnabled);
    alertPrefs.notifySystemFindings = false;
    expect(QStringLiteral("Per-category alert switch is honored"), !AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, true, localNight).ownPopup);
    alertPrefs.notifySystemFindings = true; alertPrefs.ausynPopupCards = false;
    expect(QStringLiteral("Windows channel is selected explicitly and reports unavailable delivery"),
        AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, true, localNight).windowsRequest &&
        !AlertPolicy::presentation(alertPrefs, AlertCategory::SystemFinding, false, false, localNight).reason.isEmpty());

    const QString liveRule = QStringLiteral("live-sustained-resource-load");
    const QString chatBefore = chatTranscript_->toPlainText();
    recentLoadSamples_.clear(); earlyPressureActive_ = false; deliveredFindingSeverity_.clear();
    notificationCooldowns_.clear(); lastDesktopAlertAt_ = {}; lastEarlyPressureNotificationAt_ = {};
    if (notificationPopup_) notificationPopup_->hide();
    preferences_.desktopNotificationsEnabled = false;
    SystemSnapshot urgent = sample;
    urgent.memoryTotalBytes = 8'000'000'000ULL; urgent.memoryUsedBytes = 7'760'000'000ULL;
    urgent.memoryAvailableBytes = 240'000'000ULL; urgent.memoryUsagePercent = 97;
    const QDateTime urgentNow = QDateTime::currentDateTime();
    urgent.capturedAt = urgentNow.addSecs(-10); urgent.processSamplesCapturedAt = urgent.capturedAt;
    updateEarlyPressureHeadsUp(urgent);
    expect(QStringLiteral("One 97-percent reading is not called sustained pressure"), !earlyPressureActive_);
    urgent.capturedAt = urgentNow.addSecs(-5); urgent.processSamplesCapturedAt = urgent.capturedAt;
    updateEarlyPressureHeadsUp(urgent);
    expect(QStringLiteral("Suppressed urgent advice stays eligible for later presentation"), earlyPressureActive_ && !deliveredFindingSeverity_.contains(liveRule));
    preferences_.desktopNotificationsEnabled = true;
    urgent.capturedAt = urgentNow; urgent.processSamplesCapturedAt = urgent.capturedAt;
    updateEarlyPressureHeadsUp(urgent);
    expect(QStringLiteral("97-percent pressure automatically creates a card after delivery becomes allowed"),
        notificationPopup_ && notificationPopup_->isVisible() && deliveredFindingSeverity_.value(liveRule, -1) == static_cast<int>(FindingSeverity::Critical));
    expect(QStringLiteral("Urgent resource advice does not require or create a chat question"), chatTranscript_->toPlainText() == chatBefore);
    expect(QStringLiteral("Urgent popup gives actions and opens the relevant memory window"), notificationPopup_ && notificationPopup_->findChild<QPushButton*>(QStringLiteral("alertReview")) && notificationPopup_->findChild<QPushButton*>(QStringLiteral("alertQuiet")));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    expect(QStringLiteral("Render urgent desktop card"), notificationPopup_ && notificationPopup_->grab().save(QDir(outputDirectory).filePath(QStringLiteral("alert-critical-memory.png"))));
    auto* noticeBody = notificationPopup_->findChild<QLabel*>(QStringLiteral("noticeBody"));
    auto* noticeTitle = notificationPopup_->findChild<QLabel*>(QStringLiteral("noticeTitle"));
    expect(QStringLiteral("Popup allocates enough height for wrapped text and title"), noticeBody && noticeTitle &&
        noticeBody->height() >= noticeBody->heightForWidth(noticeBody->width()) && noticeTitle->height() >= noticeTitle->heightForWidth(noticeTitle->width()));
    expect(QStringLiteral("Popup stays within the available screen"), QGuiApplication::primaryScreen()->availableGeometry().contains(notificationPopup_->geometry()));
    notificationPopup_->findChild<QPushButton*>(QStringLiteral("alertReview"))->click();
    expect(QStringLiteral("Alert review opens memory directly instead of redirecting to chat"), memoryInspector->isVisible() && pages_->currentIndex() == 0);
    memoryInspector->hide();
    const QDateTime firstAlert = lastDesktopAlertAt_;
    expect(QStringLiteral("Nearby same-resource warnings are coalesced instead of repeating"),
        !showDesktopNotification(NotificationCategory::SystemFinding, QStringLiteral("Another memory finding"), QStringLiteral("Memory is busy"), QSystemTrayIcon::Warning) && lastDesktopAlertAt_ == firstAlert);
    showDesktopNotification(NotificationCategory::SystemFinding, QStringLiteral("QA snooze controls"), QStringLiteral("Sample alert for hidden validation"), QSystemTrayIcon::Information, true);
    notificationPopup_->findChild<QPushButton*>(QStringLiteral("alertQuiet"))->click();
    expect(QStringLiteral("Popup quiet button snoozes presentations and keeps monitoring enabled"), desktopSnoozedUntil_ > QDateTime::currentDateTime() && preferences_.monitoringEnabled &&
        !showDesktopNotification(NotificationCategory::SystemFinding, QStringLiteral("QA snoozed"), QStringLiteral("Memory sample"), QSystemTrayIcon::Critical, true));
    desktopSnoozedUntil_ = {};
    recentLoadSamples_.clear(); earlyPressureActive_ = false; deliveredFindingSeverity_.clear(); notificationCooldowns_.clear(); lastDesktopAlertAt_ = {};
    lastEarlyPressureNotificationAt_ = {}; urgent.samplingIntervalSeconds = 2;
    const QDateTime fastNow = QDateTime::currentDateTime();
    for (int i = 0; i < 4; ++i) {
        urgent.capturedAt = fastNow.addSecs((i - 3) * 2); urgent.processSamplesCapturedAt = urgent.capturedAt;
        updateEarlyPressureHeadsUp(urgent);
    }
    expect(QStringLiteral("Urgent memory detection also works at two-second sampling"), earlyPressureActive_ && deliveredFindingSeverity_.contains(liveRule));
    notificationPopup_->hide();
    earlyPressureActive_ = false; recentLoadSamples_.clear(); deliveredFindingSeverity_.clear(); notificationCooldowns_.clear(); lastDesktopAlertAt_ = {};
    Finding held;
    held.ruleId = QStringLiteral("qa-memory-pending"); held.title = QStringLiteral("Memory QA warning");
    held.severity = FindingSeverity::Warning; held.summary = QStringLiteral("Fixture warning; not a real diagnosis"); held.recommendation = QStringLiteral("Review the fixture readings");
    AnalysisUpdate heldAnalysis = analysis; heldAnalysis.findings.append(held);
    preferences_.desktopNotificationsEnabled = false;
    updateAnalysis(heldAnalysis);
    expect(QStringLiteral("Analysis does not mark a blocked warning as delivered"), !deliveredFindingSeverity_.contains(held.ruleId));
    preferences_.desktopNotificationsEnabled = true;
    updateAnalysis(heldAnalysis);
    expect(QStringLiteral("Analysis retries a warning after alerts are enabled"), deliveredFindingSeverity_.contains(held.ruleId));
    notificationPopup_->hide();
    QSaveFile report(QDir(outputDirectory).filePath(QStringLiteral("ui-check.json")));
    expect(QStringLiteral("Supplied brand is available to Qt and the tray"), !QIcon(QStringLiteral(":/brand/ausyn.ico")).isNull() && !trayIcon_->icon().isNull() &&
        !legacyPage(0)->window()->windowIcon().isNull());
    WorkloadCoach coach;
    SystemSnapshot workload = sample;
    workload.memoryTotalBytes = 8'000'000'000ULL; workload.memoryUsagePercent = 87; workload.memoryAvailableBytes = 1'040'000'000ULL;
    workload.processorUsagePercent = 82;
    workload.foregroundProcessId = 501; workload.foregroundProcessName = QStringLiteral("msedge.exe");
    ProcessSample edge; edge.processId = 501; edge.name = workload.foregroundProcessName; edge.cpuPercent = 35;
    edge.workingSetBytes = 2'000'000'000ULL; edge.executablePath = QStringLiteral("C:/Program Files/Example/msedge.exe");
    ProcessSample background; background.processId = 502; background.name = QStringLiteral("Example editor.exe");
    background.cpuPercent = 15; background.executablePath = QStringLiteral("C:/Program Files/Example/editor.exe");
    workload.topProcesses = {edge, background};
    const QDateTime workloadBase = QDateTime::currentDateTime().addSecs(-120);
    const auto advanceWorkload = [&workload, workloadBase](int seconds) {
        workload.capturedAt = workloadBase.addSecs(seconds); workload.processSamplesCapturedAt = workload.capturedAt;
    };
    advanceWorkload(0); coach.observe(workload, true);
    expect(QStringLiteral("Episode starts with actual readings and browser-preserving guidance"), coach.notice() && coach.notice()->body.contains(QStringLiteral("87%")) && coach.plan().contains(QStringLiteral("active tab")));
    const QString firstEpisodeKey = coach.notice() ? coach.notice()->key : QString();
    advanceWorkload(5); coach.observe(workload, true);
    expect(QStringLiteral("An unpresented episode remains eligible"), coach.notice() && coach.notice()->key == firstEpisodeKey);
    coach.markDelivered(firstEpisodeKey);
    advanceWorkload(10); coach.observe(workload, true);
    expect(QStringLiteral("Unchanged readings do not repeat a delivered warning"), !coach.notice());
    for (int second = 15; second <= 40; second += 5) { advanceWorkload(second); coach.observe(workload, true); }
    expect(QStringLiteral("Continued foreground use produces a new circumstance without treating it as consent"), coach.continuedUse() && !coach.confirmed() && coach.notice() && coach.notice()->key.endsWith(QStringLiteral("/working")));
    if (coach.notice()) coach.markDelivered(coach.notice()->key);
    expect(QStringLiteral("A user-selected running app becomes the session priority"), coach.keepApp(edge.name) && coach.confirmed());
    expect(QStringLiteral("Kept app is excluded from background relief candidates"), coach.backgroundCandidate() && coach.backgroundCandidate()->processId == background.processId);
    expect(QStringLiteral("Missing or system apps cannot become a session priority"), !coach.keepApp(QStringLiteral("missing.exe")) && !coach.keepApp(QStringLiteral("lsass.exe")));
    workload.memoryUsagePercent = 94; workload.memoryAvailableBytes = 480'000'000ULL;
    advanceWorkload(45); coach.observe(workload, true);
    expect(QStringLiteral("Worsening headroom changes the alert and keeps the chosen task"), coach.notice() && coach.notice()->key.endsWith(QStringLiteral("/tightening")) && coach.notice()->body.contains(edge.name));
    if (coach.notice()) coach.markDelivered(coach.notice()->key);
    workload.memoryUsagePercent = 97; workload.memoryAvailableBytes = 240'000'000ULL;
    advanceWorkload(50); coach.observe(workload, true);
    expect(QStringLiteral("Critical memory still interrupts a kept-app session"), coach.notice() && coach.notice()->critical && coach.notice()->title.contains(QStringLiteral("Save your work")));
    if (coach.notice()) coach.markDelivered(coach.notice()->key);
    coach.beginComparison();
    workload.processorUsagePercent = 25; workload.memoryUsagePercent = 65; workload.memoryAvailableBytes = 2'800'000'000ULL;
    for (int second = 55; second <= 85; second += 5) {
        advanceWorkload(second); coach.observe(workload, false);
        if (coach.notice() && coach.notice()->key.endsWith(QStringLiteral("/recovery"))) {
            expect(QStringLiteral("Recovery follows multiple calmer readings"), second >= 70 && coach.notice()->body.contains(QStringLiteral("repeated calmer")));
            coach.markDelivered(coach.notice()->key);
        }
    }
    expect(QStringLiteral("Comparison reports measured differences without claiming causation"), coach.comparison().contains(QStringLiteral("average")) && coach.comparison().contains(QStringLiteral("does not prove")));
    const QString beforeOld = coach.summary();
    advanceWorkload(20); coach.observe(workload, true);
    expect(QStringLiteral("Out-of-order workload samples are ignored"), coach.summary() == beforeOld);
    advanceWorkload(115); coach.observe(workload, false);
    expect(QStringLiteral("A large sampling gap clears comparison history and preserves only the explicit app choice"), coach.comparison().isEmpty() && coach.confirmed());
    expect(QStringLiteral("Activity context has an explicit reset"), [&coach] { coach.reset(); return coach.appName().isEmpty() && !coach.confirmed() && !coach.notice(); }());
    expect(QStringLiteral("A short follow-up resolves the previous memory topic"), WorkloadCoach::resolveFollowup(QStringLiteral("why?"), {QStringLiteral("memory?")}).contains(QStringLiteral("memory pressure")));
    UserPreferences contextPrefs;
    QVector<ChatMessage> contextMessages;
    for (int i = 0; i < 30; ++i) contextMessages.append({i % 2 == 0, QStringLiteral("msedge.exe ") + QString(3000, QLatin1Char('x')), workloadBase});
    expect(QStringLiteral("Conversation sharing is off by default"), AssistantClient::conversationMessages(contextMessages, workload, contextPrefs).isEmpty());
    contextPrefs.cloudConversationContext = true;
    const QJsonArray cloudHistory = AssistantClient::conversationMessages(contextMessages, workload, contextPrefs);
    expect(QStringLiteral("Cloud conversation context is bounded and hides known process names"), cloudHistory.size() == 8 && QJsonDocument(cloudHistory).toJson().size() < 13000 && !QJsonDocument(cloudHistory).toJson().contains("msedge.exe"));
    expect(QStringLiteral("Cloud preview includes the same opted-in conversation"), AssistantClient::dataPreview(QStringLiteral("why?"), workload, analysis, contextPrefs, contextMessages).contains(QStringLiteral("Recent conversation included")));
    preferences_.workloadAwarenessEnabled = true; preferences_.desktopNotificationsEnabled = true;
    preferences_.alertsOnlyInBackground = false; preferences_.quietHoursEnabled = false; desktopSnoozedUntil_ = {};
    workloadCoach_.reset(); workloadEpisodeKey_.clear(); notificationCooldowns_.clear(); lastDesktopAlertAt_ = {};
    applyBehaviorPreferences(); earlyPressureActive_ = true;
    workload.capturedAt = QDateTime::currentDateTime(); workload.processSamplesCapturedAt = workload.capturedAt;
    workload.memoryUsagePercent = 97; workload.memoryAvailableBytes = 240'000'000ULL;
    workload.memoryUsedBytes = workload.memoryTotalBytes - workload.memoryAvailableBytes;
    latestSnapshot_ = workload; updateWorkloadContext(workload); refreshWorkloadPanel();
    expect(QStringLiteral("New episode flow presents proactive critical advice without a chat question"), notificationPopup_->isVisible() && notificationPopup_->findChild<QLabel*>(QStringLiteral("noticeBody"))->text().contains(QStringLiteral("97%")));
    expect(QStringLiteral("Workload panel can keep a live sampled app"), workloadPanel_ && workloadAppChoice_->findData(edge.name) >= 0 && keepWorkloadApp_->isEnabled());
    const int edgeChoice = workloadAppChoice_->findData(edge.name);
    if (edgeChoice >= 0) { workloadAppChoice_->setCurrentIndex(edgeChoice); keepWorkloadApp_->click(); }
    expect(QStringLiteral("Keep-this-app button confirms context without changing a process"), workloadCoach_.confirmed() && !backgroundRelief_->active());
    showPage(0, QString()); capture(QStringLiteral("dashboard-work-session"));
    expect(QStringLiteral("Work-session controls render with readable wrapped advice"), workloadPanel_->grab().save(QDir(outputDirectory).filePath(QStringLiteral("workload-panel.png"))) && workloadPlan_->height() >= workloadPlan_->heightForWidth(workloadPlan_->width()));
    expect(QStringLiteral("Workload alerts provide a direct keep-app action"), notificationPopup_->findChild<QPushButton*>(QStringLiteral("alertWorkload"))->isVisible());
    notificationPopup_->grab().save(QDir(outputDirectory).filePath(QStringLiteral("alert-work-session.png")));
    notificationPopup_->hide();
    BackgroundRelief relief;
    QString reliefError;
    ProcessSample own; own.processId = static_cast<quint32>(QCoreApplication::applicationPid()); own.name = QStringLiteral("ausyn.exe"); own.executablePath = QCoreApplication::applicationFilePath();
    workload.processSamplesCapturedAt = QDateTime::currentDateTime();
    expect(QStringLiteral("Priority action refuses the Ausyn process"), !relief.apply(own, workload, QString(), &reliefError) && !relief.active());
    QProcess worker;
    worker.setProgram(QCoreApplication::applicationFilePath()); worker.setArguments({QStringLiteral("--priority-check-child")});
    worker.start(); const bool workerStarted = worker.waitForStarted(3000);
    expect(QStringLiteral("Disposable priority verification worker starts"), workerStarted);
    if (workerStarted) {
        ProcessSample target; target.processId = static_cast<quint32>(worker.processId()); target.name = QStringLiteral("Ausyn QA worker"); target.executablePath = QCoreApplication::applicationFilePath();
        workload.foregroundProcessId = edge.processId; workload.processSamplesCapturedAt = QDateTime::currentDateTime();
        expect(QStringLiteral("Priority action refuses the user-kept app"), !relief.apply(target, workload, target.name, &reliefError));
        auto wrongPath = target; wrongPath.executablePath += QStringLiteral(".wrong");
        expect(QStringLiteral("Priority action rejects mismatched executable identity"), !relief.apply(wrongPath, workload, QString(), &reliefError));
        const bool applied = relief.apply(target, workload, edge.name, &reliefError);
        expect(QStringLiteral("Temporary priority change is verified on a disposable worker only"), applied && relief.active());
        relief.restore();
        expect(QStringLiteral("Normal priority restoration is verified"), applied && !relief.active() && relief.status().contains(QStringLiteral("restored and verified")));
        workload.processSamplesCapturedAt = QDateTime::currentDateTime().addSecs(-60);
        expect(QStringLiteral("Priority action rejects stale process samples"), !relief.apply(target, workload, edge.name, &reliefError));
        worker.kill(); worker.waitForFinished(3000);
    }
    passed = runCompanionChecks(outputDirectory, checks) && passed;
    if (!report.open(QIODevice::WriteOnly)) return false;
    report.write(QJsonDocument(QJsonObject{{QStringLiteral("passed"), passed},
        {QStringLiteral("mode"), QStringLiteral("Hidden UI rendering with fixtures; no Windows collectors, cloud calls, or preference writes")},
        {QStringLiteral("checks"), checks}}).toJson(QJsonDocument::Indented));
    return report.commit() && passed;
}

void MainWindow::startCollectorCheck(const QString& outputDirectory)
{
    if (!uiCheck_ || !QDir().mkpath(outputDirectory)) { QApplication::exit(2); return; }
    struct Timing { QElapsedTimer timer; qint64 lastTick = 0; qint64 maximumGap = 0; int ticks = 0; };
    auto timing = std::make_shared<Timing>();
    timing->timer.start();
    auto* heartbeat = new QTimer(this);
    heartbeat->setInterval(50);
    connect(heartbeat, &QTimer::timeout, this, [timing] {
        const qint64 now = timing->timer.elapsed();
        timing->maximumGap = std::max(timing->maximumGap, now - timing->lastTick);
        timing->lastTick = now; ++timing->ticks;
    });
    heartbeat->start();
    auto* watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, heartbeat, timing, outputDirectory] {
        heartbeat->stop();
        QJsonObject result = watcher->result();
        result.insert(QStringLiteral("uiHeartbeatTicks"), timing->ticks);
        result.insert(QStringLiteral("maximumUiHeartbeatGapMs"), timing->maximumGap);
        result.insert(QStringLiteral("elapsedMs"), timing->timer.elapsed());
        const bool passed = result.value(QStringLiteral("validSamples")).toInt() >= 2 && timing->ticks > 0 && timing->maximumGap < 1000;
        result.insert(QStringLiteral("passed"), passed);
        result.insert(QStringLiteral("scope"), QStringLiteral("Three real Windows collections on this PC while a UI heartbeat runs. No history database, cloud request, or preference write. A smoke check, not a long workload stability benchmark."));
        showPage(0, QString());
        hubs_[0]->setDetailed(false);
        statusBar()->showMessage(QStringLiteral("LIVE COLLECTOR CHECK — actual Windows readings, history analysis disabled"));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        grab().save(QDir(outputDirectory).filePath(QStringLiteral("live-dashboard.png")));
        QSaveFile report(QDir(outputDirectory).filePath(QStringLiteral("collector-check.json")));
        const bool opened = report.open(QIODevice::WriteOnly);
        if (opened) report.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
        const bool saved = opened && report.commit();
        QApplication::exit(saved && passed ? 0 : 1);
    });
    watcher->setFuture(QtConcurrent::run([guarded = QPointer<MainWindow>(this)] {
        SystemCollector collector;
        QJsonArray samples;
        int valid = 0;
        for (int i = 0; i < 3; ++i) {
            QElapsedTimer time; time.start();
            const SystemSnapshot snapshot = collector.collect();
            const qint64 duration = time.elapsed();
            if (guarded) QMetaObject::invokeMethod(guarded, [guarded, snapshot] {
                if (guarded) guarded->updateSystemSnapshot(snapshot);
            }, Qt::QueuedConnection);
            const auto sensible = [](const std::optional<double>& value) {
                return !value || (std::isfinite(*value) && *value >= 0 && *value <= 100);
            };
            const bool ok = snapshot.capturedAt.isValid() && snapshot.memoryTotalBytes > 0 &&
                snapshot.memoryAvailableBytes <= snapshot.memoryTotalBytes && sensible(snapshot.processorUsagePercent) &&
                sensible(snapshot.memoryUsagePercent) && sensible(snapshot.graphicsUsagePercent);
            if (ok) ++valid;
            QJsonObject reading{{QStringLiteral("collectedAt"), snapshot.capturedAt.toString(Qt::ISODate)},
                {QStringLiteral("collectionMs"), duration}, {QStringLiteral("valid"), ok},
                {QStringLiteral("cpuPercent"), snapshot.processorUsagePercent ? QJsonValue(*snapshot.processorUsagePercent) : QJsonValue()},
                {QStringLiteral("memoryPercent"), snapshot.memoryUsagePercent ? QJsonValue(*snapshot.memoryUsagePercent) : QJsonValue()},
                {QStringLiteral("processCount"), snapshot.topProcesses.size()},
                {QStringLiteral("temperatureStatus"), snapshot.thermalSensorStatus}};
            for (const ProcessSample& process : snapshot.topProcesses) {
                if (process.processId == QCoreApplication::applicationPid()) {
                    if (process.cpuPercent) reading.insert(QStringLiteral("ausynCpuPercent"), *process.cpuPercent);
                    if (process.workingSetBytes) reading.insert(QStringLiteral("ausynWorkingSetMb"), static_cast<double>(*process.workingSetBytes) / 1'000'000);
                }
            }
            samples.append(reading);
            if (i < 2) QThread::msleep(5000);
        }
        return QJsonObject{{QStringLiteral("validSamples"), valid}, {QStringLiteral("samples"), samples}};
    }));
}

QWidget* MainWindow::makeBehaviorSettings()
{
    auto* panel = makePanel();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("Make Ausyn work your way"), panel);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);
    const auto checkbox = [panel, layout](const QString& text, bool value, bool detailed = false) {
        auto* control = new QCheckBox(text, panel);
        control->setChecked(value);
        if (detailed) control->setProperty("detailOnly", true);
        layout->addWidget(control);
        return control;
    };
    monitoringEnabled_ = checkbox(QStringLiteral("Monitor my system"), preferences_.monitoringEnabled);
    workloadAwareness_ = checkbox(QStringLiteral("Adapt advice to my current app"), preferences_.workloadAwarenessEnabled);
    workloadAwareness_->setToolTip(QStringLiteral("Uses foreground executable names and resource readings. Saving app patterns requires the separate Remember app patterns option. No window titles, browsing history, keystrokes or file contents. Disabling clears current task context."));
    cloudConversationContext_ = checkbox(QStringLiteral("Include recent conversation in reviewed cloud questions"), preferences_.cloudConversationContext, true);
    cloudSystemExplanations_ = checkbox(QStringLiteral("Allow cloud reasoning for local PC questions after review"), preferences_.cloudSystemExplanations, true);
    proactiveBriefingEnabled_ = checkbox(QStringLiteral("Show proactive advice on the dashboard"), preferences_.proactiveBriefingEnabled);
    closeToTray_ = checkbox(QStringLiteral("Keep monitoring in the notification area when I close the window"), preferences_.closeToTray);
    startMinimized_ = checkbox(QStringLiteral("Start in the notification area with the window hidden"), preferences_.startMinimized);
    animationsEnabled_ = checkbox(QStringLiteral("Use subtle animations"), preferences_.animationsEnabled);
    alertsOnlyInBackground_ = checkbox(QStringLiteral("Avoid desktop pop-ups while I’m actively using Ausyn"), preferences_.alertsOnlyInBackground);
    const auto choice = [panel, layout](const QString& label, const QList<QPair<int, QString>>& items,
                                       int selected, bool detailed = false) {
        auto* row = new QWidget(panel);
        if (detailed) row->setProperty("detailOnly", true);
        auto* line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
        auto* caption = new QLabel(label, row); caption->setWordWrap(true);
        auto* combo = new QComboBox(row);
        combo->setAccessibleName(label);
        for (const auto& item : items) combo->addItem(item.second, item.first);
        const int index = combo->findData(selected);
        combo->setCurrentIndex(index < 0 ? 0 : index);
        line->addWidget(caption, 1); line->addWidget(combo);
        layout->addWidget(row);
        return combo;
    };
    landingPage_ = choice(QStringLiteral("Open on"), {{0, QStringLiteral("Main dashboard")}, {1, QStringLiteral("Performance")},
        {2, QStringLiteral("Ask Ausyn")}, {3, QStringLiteral("Logs")}, {4, QStringLiteral("Settings")}}, preferences_.landingPage);
    notificationCooldown_ = choice(QStringLiteral("Wait before repeating the same alert"),
        {{1, QStringLiteral("1 minute")}, {5, QStringLiteral("5 minutes")}, {15, QStringLiteral("15 minutes")},
         {30, QStringLiteral("30 minutes")}, {60, QStringLiteral("1 hour")}}, preferences_.notificationCooldownMinutes);
    alertChannel_ = choice(QStringLiteral("Desktop alert style"), {{1, QStringLiteral("Ausyn cards (recommended)")}, {0, QStringLiteral("Windows notifications")}}, preferences_.ausynPopupCards ? 1 : 0);
    alertDeliveryStatus_ = new QLabel(panel); alertDeliveryStatus_->setObjectName(QStringLiteral("subtle")); alertDeliveryStatus_->setWordWrap(true); layout->addWidget(alertDeliveryStatus_);
    auto* testAlert = new QPushButton(QStringLiteral("Test my alert settings"), panel); testAlert->setObjectName(QStringLiteral("secondaryButton"));
    testAlert->setToolTip(QStringLiteral("Allows this test while you are using Ausyn; quiet hours, disabled alerts and category settings still apply."));
    layout->addWidget(testAlert, 0, Qt::AlignLeft);
    connect(testAlert, &QPushButton::clicked, this, [this] {
        notificationCooldowns_.remove(QString::number(static_cast<int>(NotificationCategory::SystemFinding)) + QStringLiteral("Ausyn alert check"));
        showDesktopNotification(NotificationCategory::SystemFinding, QStringLiteral("Ausyn alert check"),
            QStringLiteral("This is a test. I’m watching your Windows readings in the background and will explain important changes without waiting for a question."), QSystemTrayIcon::Information, true);
    });
    monitorGameLaunches_ = checkbox(QStringLiteral("Notice supported games when they launch"), preferences_.monitorGameLaunches, true);
    proactiveEventScans_ = checkbox(QStringLiteral("Check recent Windows warnings and errors in the background"), preferences_.proactiveEventScans, true);
    proactiveSecurityScans_ = checkbox(QStringLiteral("Check Windows security and restart status in the background"), preferences_.proactiveSecurityScans, true);
    startupResourceAnalysis_ = checkbox(QStringLiteral("Look for sustained load from configured startup apps"), preferences_.startupResourceAnalysis, true);
    backgroundScanMinutes_ = choice(QStringLiteral("Background event/security check interval"),
        {{5, QStringLiteral("5 minutes")}, {10, QStringLiteral("10 minutes")}, {15, QStringLiteral("15 minutes")},
         {30, QStringLiteral("30 minutes")}}, preferences_.backgroundScanMinutes, true);
    auto* notes = new QLabel(QStringLiteral("Monitoring and dashboard advice are independent of desktop alerts. Pause stops telemetry collection and periodic background checks; manual checks remain available. Critical alerts can repeat immediately if a condition escalates."), panel);
    notes->setObjectName(QStringLiteral("subtle")); notes->setWordWrap(true);
    notes->setProperty("detailOnly", true); layout->addWidget(notes);
    auto* presets = new QHBoxLayout;
    for (const auto& item : QList<QPair<int, QString>>{{0, QStringLiteral("Quiet & economical")}, {1, QStringLiteral("Balanced")}, {2, QStringLiteral("More responsive")}}) {
        auto* button = new QPushButton(item.second, panel);
        button->setObjectName(QStringLiteral("secondaryButton"));
        presets->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, mode = item.first] {
            const bool ready = preferencesReady_;
            preferencesReady_ = false;
            samplingInterval_->setCurrentIndex(samplingInterval_->findData(mode == 0 ? 10 : mode == 1 ? 5 : 2));
            resourceAlertSensitivity_->setValue(mode == 0 ? 2 : mode == 1 ? 3 : 4);
            backgroundScanMinutes_->setCurrentIndex(backgroundScanMinutes_->findData(mode == 0 ? 15 : 5));
            notificationCooldown_->setCurrentIndex(notificationCooldown_->findData(mode == 0 ? 30 : 15));
            adaptiveSampling_->setChecked(true);
            alertsOnlyInBackground_->setChecked(true);
            preferencesReady_ = ready;
            savePreferences();
            recordActivity(QStringLiteral("Monitoring preset changed"), mode == 0
                ? QStringLiteral("Sampling every 10 seconds; background checks every 15 minutes; repeat alert cooldown 30 minutes.")
                : mode == 1 ? QStringLiteral("Sampling every 5 seconds; background checks every 5 minutes; repeat alert cooldown 15 minutes.")
                : QStringLiteral("Sampling every 2 seconds; background checks every 5 minutes; repeat alert cooldown 15 minutes."), 9);
            statusBar()->showMessage(QStringLiteral("Preset applied. You can adjust each control below."), 5000);
        });
    }
    layout->addLayout(presets);
    for (QCheckBox* control : {monitoringEnabled_, workloadAwareness_, cloudConversationContext_, cloudSystemExplanations_, proactiveBriefingEnabled_, closeToTray_, startMinimized_,
         animationsEnabled_, alertsOnlyInBackground_, monitorGameLaunches_, proactiveEventScans_,
         proactiveSecurityScans_, startupResourceAnalysis_})
        connect(control, &QCheckBox::toggled, this, [this] { savePreferences(); });
    for (QComboBox* control : {landingPage_, notificationCooldown_, backgroundScanMinutes_, alertChannel_})
        connect(control, &QComboBox::currentIndexChanged, this, [this] { savePreferences(); });
    return panel;
}

void MainWindow::applyBehaviorPreferences()
{
    configureCompanion();
    if (assistantModeStatus_) assistantModeStatus_->setText(preferences_.cloudAiEnabled
        ? preferences_.encryptedApiKey.isEmpty() || preferences_.apiModel.trimmed().isEmpty()
            ? QStringLiteral("Local answers ready · add an API key and model in Settings for broader AI conversation.")
            : QStringLiteral("AI provider configured · each online question is reviewed · conversation context %1 · PC reasoning %2")
                .arg(preferences_.cloudConversationContext ? QStringLiteral("enabled") : QStringLiteral("off"),
                     preferences_.cloudSystemExplanations ? QStringLiteral("enabled") : QStringLiteral("local"))
        : QStringLiteral("Local intelligence · follow-ups and live PC context · optional online AI is off."));
    if (!preferences_.monitoringEnabled || !preferences_.workloadAwarenessEnabled) {
        workloadCoach_.reset();
        if (adviceBannerTitle_ && adviceBannerTitle_->text() == workloadBannerTitle_ && adviceBanner_) adviceBanner_->hide();
        workloadEpisodeKey_.clear(); workloadBannerTitle_.clear();
        for (auto it = notificationCooldowns_.begin(); it != notificationCooldowns_.end();) {
            if (it.key().startsWith(QString::number(static_cast<int>(NotificationCategory::SystemFinding)) + QStringLiteral("workload/"))) it = notificationCooldowns_.erase(it);
            else ++it;
        }
        if (lastDesktopAlertOccurrenceKey_.startsWith(QStringLiteral("workload/"))) {
            if (notificationPopup_) notificationPopup_->hide();
            lastDesktopAlertOccurrenceKey_.clear();
        }
        if (backgroundRelief_) backgroundRelief_->restore();
    }
    if (workloadPanel_) workloadPanel_->setVisible(preferences_.workloadAwarenessEnabled && !hubs_.isEmpty() && hubs_[0]->isDetailed());
    refreshWorkloadPanel();
    if (telemetry_) telemetry_->setMonitoringEnabled(preferences_.monitoringEnabled);
    const int interval = preferences_.backgroundScanMinutes * 60 * 1000;
    const auto schedule = [interval](QTimer* timer, bool enabled) {
        if (!timer) return;
        if (timer->interval() != interval) timer->setInterval(interval);
        if (enabled && !timer->isActive()) timer->start();
        if (!enabled && timer->isActive()) timer->stop();
    };
    schedule(proactiveEventTimer_, !uiCheck_ && preferences_.monitoringEnabled && preferences_.proactiveEventScans);
    schedule(securityMonitorTimer_, !uiCheck_ && preferences_.monitoringEnabled && preferences_.proactiveSecurityScans);
    for (auto* detail : findChildren<DetailSwitch*>()) detail->setAnimationsEnabled(preferences_.animationsEnabled);
    if (performanceRing_) performanceRing_->setAnimationsEnabled(preferences_.animationsEnabled);
    for (auto* chart : findChildren<LiveActivityChart*>()) chart->setAnimationsEnabled(preferences_.animationsEnabled && preferences_.monitoringEnabled && !companionEconomical_);
    if (gamingPage_) gamingPage_->setLaunchMonitoringEnabled(preferences_.monitorGameLaunches);
    if (proactiveBriefing_) proactiveBriefing_->setVisible(preferences_.proactiveBriefingEnabled);
    if (pauseMonitoring_) pauseMonitoring_->setText(preferences_.monitoringEnabled
        ? QStringLiteral("Pause monitoring") : QStringLiteral("Resume monitoring"));
    updateMonitoringStatus();
}

void MainWindow::applyTheme(bool light)
{
    preferences_.lightTheme = light;
    if (themeController_)
        themeController_->apply(this, light, QString::fromUtf8(kWindowStyle));
    for (QWidget* widget : findChildren<QWidget*>()) {
        widget->setProperty("lightTheme", light);
    }
    if (dashboardChart_) dashboardChart_->update();
    if (performanceChart_) performanceChart_->update();
    if (performanceRing_) performanceRing_->update();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (quitRequested_ || !preferences_.closeToTray) {
        event->accept();
        QApplication::quit();
        return;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        event->accept();
        QApplication::quit();
        return;
    }
    for (auto window : resourceWindows_) if (window) window->hide();
    hide();
    trayIcon_->setToolTip(preferences_.monitoringEnabled
        ? QStringLiteral("Ausyn is monitoring in the background. Right-click to open or quit.")
        : QStringLiteral("Ausyn is in the background with monitoring paused. Open to resume."));
    event->ignore();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    if (latestSnapshot_.capturedAt.isValid()) {
        QTimer::singleShot(0, this, [this] {
            processRenderedAt_ = {};
            updateSystemSnapshot(latestSnapshot_);
            updateDashboardReadout();
        });
    }
}

void MainWindow::addNavigationItem(const QString& label, const QString& glyph, int pageIndex, bool advanced)
{
    auto* button = new QToolButton;
    button->setObjectName(QStringLiteral("navButton"));
    button->setText(QStringLiteral("%1    %2").arg(glyph, label));
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setProperty("pageIndex", pageIndex);
    button->setProperty("advancedNavigation", advanced);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(38);
    navigationButtons_.append(button);
    sidebarLayout_->addWidget(button);

    connect(button, &QToolButton::clicked, this, [this, pageIndex, label] {
        showPage(pageIndex, label);
    });
}

void MainWindow::setAdvancedToolsVisible(bool visible)
{
    // Kept only to read older preferences. The five primary destinations are always available.
    preferences_.showAdvancedTools = visible;
}

QWidget* MainWindow::legacyPage(int id) const
{
    return legacyPages_.value(id, nullptr);
}

void MainWindow::showHub(int hubIndex)
{
    if (hubIndex < 0 || hubIndex >= hubs_.size()) return;
    pages_->setCurrentIndex(hubIndex);
    activeLegacyPage_ = hubs_[hubIndex]->currentSection();
    static const std::array<QString, 5> names{QStringLiteral("Main dashboard"), QStringLiteral("Performance"), QStringLiteral("Ask Ausyn"), QStringLiteral("Logs"), QStringLiteral("Settings")};
    pageTitle_->setText(names[hubIndex]);
    for (QToolButton* button : std::as_const(navigationButtons_))
        button->setChecked(legacyHub_.value(button->property("pageIndex").toInt(), -1) == hubIndex);
}

void MainWindow::showPage(int pageIndex, const QString& title)
{
    Q_UNUSED(title)
    const auto found = legacyHub_.constFind(pageIndex);
    if (found == legacyHub_.cend()) return;
    const int hub = *found;
    showHub(hub);
    hubs_[hub]->selectSection(pageIndex);
    activeLegacyPage_ = pageIndex;
    updateDetailMode(hub);
    if (pageIndex == 11 && eventLogWatcher_ && !eventLogWatcher_->isRunning()) refreshEventLogs();
    if (pageIndex == 12 && appInventoryPage_)
        appInventoryPage_->setRunningProcesses(latestSnapshot_.topProcesses, latestSnapshot_.processSamplesCapturedAt);
    if (latestSnapshot_.capturedAt.isValid()) updateSystemSnapshot(latestSnapshot_);
    if (pageIndex == 6 && diagnosticsView_)
        diagnosticsView_->setFindings(latestAnalysis_.findings, QStringLiteral("No sustained issue in the available evidence."), false, hubs_[0]->isDetailed());
    if (pageIndex == 7 && recommendationsView_)
        recommendationsView_->setFindings(latestAnalysis_.findings, QStringLiteral("No current finding needs an action."), true, hubs_[0]->isDetailed());
    if (pageIndex == 10 && predictionPage_)
        predictionPage_->setForecast(latestAnalysis_.storageForecast, latestAnalysis_.batteryForecast,
            latestAnalysis_.volumeStorageForecasts, latestAnalysis_.memoryForecast, preferences_.historyRetentionDays);
}

void MainWindow::updateDetailMode(int hubIndex)
{
    if (hubIndex < 0 || hubIndex >= hubs_.size()) return;
    const bool detailed = hubs_[hubIndex]->isDetailed();
    refreshCompanionPanel();
    if (hubIndex == 0) {
        if (workloadPanel_) workloadPanel_->setVisible(preferences_.workloadAwarenessEnabled && detailed);
        refreshWorkloadPanel();
        if (dashboardChart_) dashboardChart_->setDetailed(detailed);
        if (proactiveBriefing_) proactiveBriefing_->setDetailed(detailed);
        if (activeLegacyPage_ == 0 && dashboardCheckResult_ && dashboardCheckResult_->isVisible() && latestSnapshot_.capturedAt.isValid()) {
            UserPreferences local = preferences_;
            local.cloudAiEnabled = false;
            local.technicalDetail = detailed;
            dashboardCheckResult_->setText(AssistantEngine::reply(QStringLiteral("Check my system"), latestSnapshot_, latestAnalysis_, latestAppInventory_, local));
        }
    }
    if (hubIndex == 1 && processTable_) {
        for (int column : {1, 4, 5, 6, 7}) processTable_->setColumnHidden(column, !detailed);
        if (performanceChart_) performanceChart_->setDetailed(detailed);
        if (activeLegacyPage_ == 2 && latestSnapshot_.capturedAt.isValid()) updateSystemSnapshot(latestSnapshot_);
    }
    if (hubIndex == 2 && technicalDetail_) {
        const QSignalBlocker blocker(technicalDetail_);
        technicalDetail_->setChecked(detailed);
        preferences_.technicalDetail = detailed;
    }
    if (hubIndex == 3 && activityPage_) activityPage_->setDetailed(detailed);
}

void MainWindow::updateDashboardReadout()
{
    if (!dashboardSummary_) return;
    if (!preferences_.monitoringEnabled) {
        dashboardSummary_->setText(QStringLiteral("Monitoring is paused"));
        dashboardNextStep_->setText(QStringLiteral("Resume monitoring for a current picture. Saved history is still available in Logs."));
        dashboardActivity_->setText(QStringLiteral("Paused by your preference · no periodic telemetry collection"));
        return;
    }
    if (!latestSnapshot_.capturedAt.isValid()) return;
    const Finding* leading = nullptr;
    if (preferences_.proactiveBriefingEnabled) {
        for (const Finding& finding : latestAnalysis_.findings)
            if (!leading || static_cast<int>(finding.severity) > static_cast<int>(leading->severity)) leading = &finding;
    }
    if (preferences_.workloadAwarenessEnabled && earlyPressureActive_ && workloadCoach_.fresh(QDateTime::currentDateTime())) {
        dashboardSummary_->setText(workloadCoach_.confirmed() ? QStringLiteral("Your priority: %1").arg(workloadCoach_.appName()) : QStringLiteral("Your current task needs more headroom"));
        dashboardNextStep_->setText(workloadCoach_.plan());
    } else if (leading) {
        dashboardSummary_->setText(leading->title);
        dashboardNextStep_->setText(leading->recommendation.isEmpty() ? leading->summary : leading->recommendation);
    } else if (latestSnapshot_.memoryUsagePercent.value_or(0) >= 85) {
        dashboardSummary_->setText(QStringLiteral("Memory use is high right now"));
        dashboardNextStep_->setText(QStringLiteral("%1% in use, with %2 available. If work feels slow, review the largest apps in Performance. I’m checking whether this stays high.")
            .arg(*latestSnapshot_.memoryUsagePercent, 0, 'f', 0).arg(formatGigabytes(latestSnapshot_.memoryAvailableBytes)));
    } else if (latestSnapshot_.processorUsagePercent.value_or(0) >= 75) {
        dashboardSummary_->setText(QStringLiteral("Your PC is busy right now"));
        dashboardNextStep_->setText(QStringLiteral("CPU load is %1%. This can be normal during demanding work. I’m watching the duration; Performance shows which apps are currently using it.")
            .arg(*latestSnapshot_.processorUsagePercent, 0, 'f', 0));
    } else if (!hasSeenAnalysis_) {
        dashboardSummary_->setText(QStringLiteral("Your live readings are ready"));
        dashboardNextStep_->setText(QStringLiteral("I’m collecting history to check for sustained patterns. You can review current usage now."));
    } else {
        dashboardSummary_->setText(QStringLiteral("No sustained issue in the available readings"));
        dashboardNextStep_->setText(QStringLiteral("I’m watching workload changes and supported system signals. A useful finding and its next step will appear here automatically."));
    }
    dashboardActivity_->setText(QStringLiteral("Monitoring · updated %1 · %2 readings this session · every %3s")
        .arg(latestSnapshot_.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
        .arg(receivedSamples_).arg(latestSnapshot_.samplingIntervalSeconds));
    if (performanceRing_) performanceRing_->setValue(latestAnalysis_.health.score
        ? std::optional<double>(*latestAnalysis_.health.score) : std::nullopt, latestAnalysis_.health.explanation);
}

void MainWindow::recordActivity(const QString& title, const QString& detail, int destination)
{
    if (activityPage_) activityPage_->record(title, detail, destination);
}

MainWindow::~MainWindow() {
    if (!uiCheck_ && !companionStatePath_.isEmpty()) companion_.save(companionStatePath_, QDateTime::currentDateTime(), true);
    if (backgroundRelief_) { backgroundRelief_->disconnect(this); backgroundRelief_->restore(); }
    delete notificationPopup_.data();
}

void MainWindow::openResource(int resource) {
    // Negative destinations refer to existing tools, encoded as -(tool + 1).
    if (resource < 0) {
        const int tool = -resource - 1;
        if (!legacyHub_.contains(tool)) return;
        show(); showPage(tool, QString());
        if (!uiCheck_) { raise(); activateWindow(); }
        return;
    }
    if (resource > static_cast<int>(ResourceKind::Processes)) return;
    auto* inspector = resourceWindows_.value(resource).data();
    if (!inspector) {
        inspector = new ResourceInspector(static_cast<ResourceKind>(resource), this);
        if (uiCheck_) inspector->setAttribute(Qt::WA_DontShowOnScreen);
        inspector->setProperty("usingFixture", property("usingFixture"));
        resourceWindows_.insert(resource, inspector);
        for (auto* detail : inspector->findChildren<DetailSwitch*>()) detail->setAnimationsEnabled(preferences_.animationsEnabled);
        inspector->setProperty("lightTheme", preferences_.lightTheme);
        for (auto* child : inspector->findChildren<QWidget*>()) child->setProperty("lightTheme", preferences_.lightTheme);
        connect(inspector, &ResourceInspector::toolRequested, this, [this](int tool) { show(); showPage(tool, QString()); raise(); activateWindow(); });
    }
    if (dashboardChart_) inspector->seedChart(*dashboardChart_);
    inspector->show(); inspector->setContext(latestSnapshot_, latestAnalysis_, preferences_.monitoringEnabled, true);
    if (!uiCheck_) { inspector->raise(); inspector->activateWindow(); }
}

int MainWindow::notificationResource(const QString& title, const QString& body) const {
    const QString text = title + QLatin1Char(' ') + body;
    if (text.contains(QStringLiteral("thermal"), Qt::CaseInsensitive) || text.contains(QStringLiteral("temperature"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Cpu);
    if (text.contains(QStringLiteral("memory"), Qt::CaseInsensitive) || text.contains(QStringLiteral("RAM"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Memory);
    if (text.contains(QStringLiteral("storage"), Qt::CaseInsensitive) || text.contains(QStringLiteral("drive"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Storage);
    if (text.contains(QStringLiteral("battery"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Battery);
    if (text.contains(QStringLiteral("processor"), Qt::CaseInsensitive) || text.contains(QStringLiteral("CPU"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Cpu);
    if (text.contains(QStringLiteral("graphics"), Qt::CaseInsensitive) || text.contains(QStringLiteral("GPU"), Qt::CaseInsensitive)) return static_cast<int>(ResourceKind::Graphics);
    return static_cast<int>(ResourceKind::Processes);
}

void MainWindow::showAdviceBanner(const QString& title, const QString& body, int resource) {
    if (!adviceBanner_) return;
    if (title == dismissedAdviceTitle_) return;
    adviceResource_ = resource; adviceBannerTitle_->setText(title); adviceBannerBody_->setText(body.left(700)); adviceBanner_->show();
}

NotificationPopup* MainWindow::popupWindow() {
    if (!notificationPopup_) {
        auto* popup = new NotificationPopup;
        notificationPopup_ = popup;
        if (uiCheck_) popup->setAttribute(Qt::WA_DontShowOnScreen);
        connect(popup, &NotificationPopup::reviewRequested, this, &MainWindow::openResource);
        connect(popup, &NotificationPopup::workloadRequested, this, [this] {
            if (preferences_.monitoringEnabled && workloadCoach_.fresh(QDateTime::currentDateTime())) workloadCoach_.keepCurrentApp();
            show(); showPage(0, QString()); hubs_[0]->setDetailed(true); refreshWorkloadPanel();
            if (auto* scroll = qobject_cast<QScrollArea*>(legacyPage(0))) scroll->ensureWidgetVisible(workloadPanel_, 0, 24);
            if (!uiCheck_) { raise(); activateWindow(); }
            recordActivity(QStringLiteral("Workload options opened"), workloadCoach_.summary(), 0);
        });
        connect(popup, &NotificationPopup::snoozeRequested, this, [this] {
            desktopSnoozedUntil_ = QDateTime::currentDateTime().addSecs(15 * 60);
            recordActivity(QStringLiteral("Desktop advice snoozed"), QStringLiteral("Pop-ups paused for 15 minutes. Monitoring and dashboard advice continue."), 9);
            updateMonitoringStatus();
        });
    }
    return notificationPopup_;
}

void MainWindow::checkSystemOnDashboard()
{
    showPage(0, QStringLiteral("Main dashboard"));
    const qint64 age = latestSnapshot_.capturedAt.isValid()
        ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const bool fresh = preferences_.monitoringEnabled && age >= 0 &&
        age <= std::max(10, latestSnapshot_.samplingIntervalSeconds * 3) * 1000LL;
    if (!fresh) {
        dashboardCheckResult_->setText(preferences_.monitoringEnabled
            ? QStringLiteral("Waiting for fresh Windows readings. The dashboard will update when they arrive.")
            : QStringLiteral("Monitoring is paused. Resume it to check your current system."));
    } else {
        UserPreferences local = preferences_;
        local.cloudAiEnabled = false;
        local.technicalDetail = hubs_[0]->isDetailed();
        dashboardCheckResult_->setText(AssistantEngine::reply(QStringLiteral("Check my system"),
            latestSnapshot_, latestAnalysis_, latestAppInventory_, local,
            latestHistoryPoints_, historyQueryAvailable_, historyPeriodHours_, {}, latestEventLogUpdate_));
        recordActivity(QStringLiteral("System check completed"),
            QStringLiteral("Displayed current CPU, memory, graphics, storage, battery and available findings on the dashboard. No system settings were changed."), 0);
    }
    dashboardCheckResult_->show();
}
void MainWindow::updateSystemSnapshot(Ausyn::SystemSnapshot snapshot)
{
    const bool newReading = snapshot.capturedAt != latestSnapshot_.capturedAt;
    const bool wasAdaptivelySlowed = latestSnapshot_.monitoringAdaptationNote.contains(
        QStringLiteral("temporarily slowed"), Qt::CaseInsensitive);
    const bool isAdaptivelySlowed = snapshot.monitoringAdaptationNote.contains(
        QStringLiteral("temporarily slowed"), Qt::CaseInsensitive);
    latestSnapshot_ = snapshot;
    for (auto window : resourceWindows_) if (window && window->isVisible()) window->setContext(snapshot, latestAnalysis_, preferences_.monitoringEnabled);
    if (newReading) {
        ++receivedSamples_;
        updateEarlyPressureHeadsUp(snapshot);
        updateWorkloadContext(snapshot);
        if (dashboardChart_) dashboardChart_->addSnapshot(snapshot);
        if (performanceChart_) performanceChart_->addSnapshot(snapshot);
    }
    if (hasSeenAdaptiveSamplingState_ && wasAdaptivelySlowed != isAdaptivelySlowed) {
        statusBar()->showMessage(isAdaptivelySlowed
            ? QStringLiteral("Ausyn reduced its sampling frequency after sustained collector or app CPU load. Agent health shows the effective cadence.")
            : QStringLiteral("Ausyn’s sampling cadence has returned to your selected interval."), 9000);
    }
    hasSeenAdaptiveSamplingState_ = !snapshot.monitoringAdaptationNote.isEmpty();
    if (troubleshootingPage_ && activeLegacyPage_ == 17)
        troubleshootingPage_->setContext(latestSnapshot_, latestAnalysis_, latestEventLogUpdate_);
    updateMonitoringStatus();
    if (appInventoryPage_ && preferences_.startupResourceAnalysis && newReading)
        appInventoryPage_->setRunningProcesses(snapshot.topProcesses, snapshot.processSamplesCapturedAt);
    if (newReading && preferences_.startupResourceAnalysis) updateProactiveStartupSignals();
    if (gamingPage_ && newReading) gamingPage_->setSnapshot(snapshot);
    if (!isVisible()) return;
    if (newReading && lastUiRenderedAt_.isValid() &&
        lastUiRenderedAt_.msecsTo(snapshot.capturedAt) < 2000) return;
    lastUiRenderedAt_ = snapshot.capturedAt;
    updateDashboardReadout();
    if (networkPage_ && activeLegacyPage_ == 16) networkPage_->setSnapshot(snapshot);
    if (processorCoreGrid_ && activeLegacyPage_ == 2 && hubs_[1]->isDetailed()) {
        auto* layout = qobject_cast<QGridLayout*>(processorCoreGrid_->layout());
        if (layout && processorCoreValues_.size() != snapshot.processorCores.size()) {
            while (QLayoutItem* item = layout->takeAt(0)) {
                if (QWidget* widget = item->widget()) delete widget;
                delete item;
            }
            processorCoreValues_.clear();
            processorCoreNames_.clear();
            processorCoreBars_.clear();
            if (snapshot.processorCores.isEmpty()) {
                auto* unavailable = new QLabel(QStringLiteral("Per-core load is unavailable from Windows on this device."), processorCoreGrid_);
                unavailable->setObjectName(QStringLiteral("subtle"));
                layout->addWidget(unavailable, 0, 0);
            } else {
                for (qsizetype index = 0; index < snapshot.processorCores.size(); ++index) {
                    const ProcessorCoreSample& core = snapshot.processorCores.at(index);
                    auto* tile = makePanel(processorCoreGrid_);
                    auto* tileLayout = new QVBoxLayout(tile);
                    tileLayout->setContentsMargins(10, 8, 10, 8);
                    tileLayout->setSpacing(5);
                    auto* line = new QHBoxLayout;
                    auto* name = new QLabel(core.name, tile);
                    name->setObjectName(QStringLiteral("metricName"));
                    processorCoreNames_.append(name);
                    auto* value = new QLabel(core.utilizationPercent
                        ? QStringLiteral("%1%").arg(*core.utilizationPercent, 0, 'f', 0)
                        : QStringLiteral("—"), tile);
                    processorCoreValues_.append(value);
                    value->setStyleSheet(QStringLiteral("color:#d9d9ff;font-weight:650;"));
                    line->addWidget(name);
                    line->addStretch();
                    line->addWidget(value);
                    auto* bar = new QProgressBar(tile);
                    processorCoreBars_.append(bar);
                    bar->setRange(0, 100);
                    bar->setTextVisible(false);
                    bar->setFixedHeight(7);
                    bar->setValue(core.utilizationPercent
                        ? static_cast<int>(std::lround(*core.utilizationPercent)) : 0);
                    bar->setStyleSheet(QStringLiteral("QProgressBar{background:#202938;border:0;border-radius:3px;} QProgressBar::chunk{background:#827cff;border-radius:3px;}"));
                    tileLayout->addLayout(line);
                    tileLayout->addWidget(bar);
                    layout->addWidget(tile, static_cast<int>(index / 4), static_cast<int>(index % 4));
                }
            }
        } else if (layout) {
            for (qsizetype index = 0; index < snapshot.processorCores.size(); ++index) {
                const ProcessorCoreSample& core = snapshot.processorCores.at(index);
                processorCoreValues_[index]->setText(core.utilizationPercent
                    ? QStringLiteral("%1%").arg(*core.utilizationPercent, 0, 'f', 0)
                    : QStringLiteral("—"));
                processorCoreNames_[index]->setText(core.name);
                processorCoreBars_[index]->setValue(core.utilizationPercent
                    ? static_cast<int>(std::lround(*core.utilizationPercent)) : 0);
            }
        }
    }
    if (hardwareChangeStatus_ && !snapshot.hardwareChanges.isEmpty()) {
        hardwareChangeStatus_->setText(QStringLiteral("Hardware change detected\n%1\nAusyn refreshed the local device profile. Historical trends may reflect more than one hardware configuration.")
            .arg(snapshot.hardwareChanges.join(QLatin1Char('\n'))));
        hardwareChangeStatus_->setVisible(true);
    }
    if (hardwareTimeline_ && !snapshot.hardwareChanges.isEmpty()) {
        const QStringList recentHardwareChanges = HardwareChangeTracker::recentChanges();
        hardwareTimeline_->setText(recentHardwareChanges.isEmpty()
            ? QStringLiteral("No device-profile changes have been recorded.")
            : recentHardwareChanges.join(QLatin1Char('\n')));
        hardwareTimeline_->setAccessibleDescription(QStringLiteral("Most recent hardware profile changes, newest first. These record differences in Windows-reported device identity and do not establish the cause of performance changes."));
    }
    if (gamingPage_ && activeLegacyPage_ == 5 && !newReading) gamingPage_->setSnapshot(snapshot);
    if (batteryPage_ && activeLegacyPage_ == 4) batteryPage_->setSnapshot(snapshot);
    if (dataQualityPage_ && activeLegacyPage_ == 14) dataQualityPage_->setSnapshot(snapshot);
    if (agentHealthPage_ && activeLegacyPage_ == 15) agentHealthPage_->setSnapshot(snapshot);
    auto setMetric = [this](const QString& key, const QString& value, const QString& hint) {
        if (auto* valueLabel = pages_->findChild<QLabel*>(QStringLiteral("metric.%1.value").arg(key))) {
            valueLabel->setText(value);
        }
        if (auto* hintLabel = pages_->findChild<QLabel*>(QStringLiteral("metric.%1.hint").arg(key))) {
            hintLabel->setText(hint);
        }
    };
    const auto formatPercent = [](const std::optional<double>& value) {
        return value ? QStringLiteral("%1%").arg(*value, 0, 'f', 0) : QStringLiteral("Sampling…");
    };

    setMetric(QStringLiteral("cpu"), formatPercent(snapshot.processorUsagePercent),
              snapshot.processorUsagePercent ? QStringLiteral("Windows · total processor load")
                                             : QStringLiteral("Waiting for the first interval"));

    if (snapshot.memoryTotalBytes > 0 && snapshot.memoryUsagePercent) {
        setMetric(QStringLiteral("memory"), formatPercent(snapshot.memoryUsagePercent),
                  QStringLiteral("%1 of %2 in use · %3 available")
                      .arg(formatGigabytes(snapshot.memoryUsedBytes), formatGigabytes(snapshot.memoryTotalBytes),
                           formatGigabytes(snapshot.memoryAvailableBytes)));
    } else {
        setMetric(QStringLiteral("memory"), QStringLiteral("Unavailable"),
                  QStringLiteral("Windows did not provide memory totals"));
    }

    if (snapshot.graphicsUsagePercent) {
        QString graphicsHint = QStringLiteral("Busiest graphics engine · Windows counter");
        if (!snapshot.graphicsAdapters.isEmpty()) {
            const auto& adapter = snapshot.graphicsAdapters.first();
            if (adapter.localMemoryUsageBytes && adapter.localMemoryBudgetBytes) {
                graphicsHint += QStringLiteral(" · local memory %1 / %2")
                    .arg(formatGigabytes(*adapter.localMemoryUsageBytes),
                         formatGigabytes(*adapter.localMemoryBudgetBytes));
            }
        }
        setMetric(QStringLiteral("gpu"), formatPercent(snapshot.graphicsUsagePercent),
                  graphicsHint);
    } else {
        setMetric(QStringLiteral("gpu"), QStringLiteral("Unavailable"),
                  snapshot.graphicsName.isEmpty()
                      ? QStringLiteral("No physical graphics adapter exposed")
                      : QStringLiteral("Windows did not provide GPU engine counters"));
    }

    if (snapshot.systemVolumeTotalBytes > 0) {
        const quint64 usedBytes = snapshot.systemVolumeTotalBytes -
                                  std::min(snapshot.systemVolumeFreeBytes, snapshot.systemVolumeTotalBytes);
        const double usedPercent = 100.0 * static_cast<double>(usedBytes) /
                                  static_cast<double>(snapshot.systemVolumeTotalBytes);
        setMetric(QStringLiteral("storage"), formatPercent(usedPercent),
                  QStringLiteral("%1 free · %2 · Read %3 / Write %4")
                      .arg(formatGigabytes(snapshot.systemVolumeFreeBytes), snapshot.systemVolumePath,
                           formatRate(snapshot.diskActivity.readBytesPerSecond),
                           formatRate(snapshot.diskActivity.writeBytesPerSecond)));
    } else {
        setMetric(QStringLiteral("storage"), QStringLiteral("Unavailable"),
                  QStringLiteral("No fixed system volume was reported"));
    }

    if (snapshot.batteryPercent) {
        QString batteryHint;
        if (snapshot.batteryCharging) {
            batteryHint = QStringLiteral("Charging");
        } else if (snapshot.batteryOnAcPower && *snapshot.batteryOnAcPower) {
            batteryHint = QStringLiteral("Connected to AC power");
        } else if (snapshot.batteryOnAcPower && !*snapshot.batteryOnAcPower) {
            batteryHint = QStringLiteral("Running on battery");
        } else {
            batteryHint = QStringLiteral("Charge reported by Windows");
        }
        setMetric(QStringLiteral("battery"), QStringLiteral("%1%").arg(*snapshot.batteryPercent), batteryHint);
    } else {
        setMetric(QStringLiteral("battery"), QStringLiteral("No battery"),
                  QStringLiteral("This device does not expose a battery reading"));
    }

    if (snapshot.networkReceiveBytesPerSecond && snapshot.networkSendBytesPerSecond) {
        setMetric(QStringLiteral("network"),
                  QStringLiteral("↓ %1").arg(formatRate(snapshot.networkReceiveBytesPerSecond)),
                  QStringLiteral("↑ %1 · %2 TCP · %3 UDP endpoints")
                      .arg(formatRate(snapshot.networkSendBytesPerSecond),
                           snapshot.networkTcpEntryCount ? QString::number(*snapshot.networkTcpEntryCount) : QStringLiteral("—"),
                           snapshot.networkUdpEndpointCount ? QString::number(*snapshot.networkUdpEndpointCount) : QStringLiteral("—")));
    } else {
        setMetric(QStringLiteral("network"), QStringLiteral("Sampling…"), snapshot.networkNote);
    }

    if (auto* activity = pages_->findChild<QLabel*>(QStringLiteral("liveActivity"))) {
        QString summary = QStringLiteral("Updated %1 · CPU %2 · memory %3")
            .arg(snapshot.capturedAt.toString(QStringLiteral("h:mm:ss ap")),
                 formatPercent(snapshot.processorUsagePercent), formatPercent(snapshot.memoryUsagePercent));
        if (!snapshot.topProcesses.isEmpty()) {
            const auto& process = snapshot.topProcesses.first();
            if (process.workingSetBytes) {
                summary += QStringLiteral(" · highest memory use: %1 (%2)")
                    .arg(process.name, formatGigabytes(*process.workingSetBytes));
            }
        }
        activity->setText(summary);
    }

    if (hardwareDetails_ && activeLegacyPage_ == 3) {
        QStringList details;
        details << QStringLiteral("Device                 %1").arg(snapshot.deviceName.isEmpty()
                        ? QStringLiteral("Unavailable") : snapshot.deviceName)
                << QStringLiteral("Operating system       %1").arg(snapshot.operatingSystem.isEmpty()
                        ? QStringLiteral("Unavailable") : snapshot.operatingSystem)
                << QStringLiteral("OS version / build     %1").arg(snapshot.operatingSystemVersion.isEmpty()
                        ? QStringLiteral("Unavailable") : snapshot.operatingSystemVersion)
                << QStringLiteral("Native architecture    %1").arg(snapshot.operatingSystemArchitecture.isEmpty()
                        ? QStringLiteral("Unavailable") : snapshot.operatingSystemArchitecture)
                << QStringLiteral("System boot time       %1 · uptime %2 days %3 hours")
                    .arg(snapshot.systemBootTime.isValid()
                        ? snapshot.systemBootTime.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap"))
                        : QStringLiteral("Unavailable"))
                    .arg(snapshot.systemUptimeSeconds / 86400)
                    .arg((snapshot.systemUptimeSeconds / 3600) % 24)
                << QStringLiteral("Device type            %1").arg(snapshot.deviceFormFactor.isEmpty()
                        ? QStringLiteral("Unknown") : snapshot.deviceFormFactor)
                << QStringLiteral("Device type basis      %1").arg(snapshot.deviceFormFactorBasis.isEmpty()
                        ? QStringLiteral("Not available") : snapshot.deviceFormFactorBasis)
                << QStringLiteral("System manufacturer   %1").arg(snapshot.systemManufacturer.isEmpty()
                        ? QStringLiteral("Not reported by firmware") : snapshot.systemManufacturer)
                << QStringLiteral("System model           %1").arg(snapshot.systemModel.isEmpty()
                        ? QStringLiteral("Not reported by firmware") : snapshot.systemModel)
                << QStringLiteral("BIOS / firmware        %1 · %2 · %3")
                    .arg(snapshot.biosVendor.isEmpty() ? QStringLiteral("Vendor unavailable") : snapshot.biosVendor,
                         snapshot.biosVersion.isEmpty() ? QStringLiteral("Version unavailable") : snapshot.biosVersion,
                         snapshot.biosReleaseDate.isEmpty() ? QStringLiteral("Date unavailable") : snapshot.biosReleaseDate)
                << QStringLiteral("Processor              %1").arg(snapshot.processorName.isEmpty()
                        ? QStringLiteral("Name unavailable") : snapshot.processorName)
                << QStringLiteral("Logical processors     %1").arg(snapshot.logicalProcessorCount)
                << QStringLiteral("Installed memory       %1").arg(snapshot.memoryTotalBytes > 0
                        ? formatGigabytes(snapshot.memoryTotalBytes) : QStringLiteral("Not reported"))
                << QStringLiteral("Available memory       %1").arg(snapshot.memoryTotalBytes > 0
                        ? formatGigabytes(snapshot.memoryAvailableBytes) : QStringLiteral("Not reported"))
                << QStringLiteral("Windows system cache   %1 · reclaimable, not free memory").arg(
                        snapshot.memorySystemCacheBytes ? formatGigabytes(*snapshot.memorySystemCacheBytes)
                                                       : QStringLiteral("Not reported"))
                << QStringLiteral("Thermal sensors");
        if (snapshot.graphicsAdapters.isEmpty()) {
            details << QStringLiteral("Graphics adapters      Not exposed by DXGI");
        } else {
            details << QStringLiteral("Graphics adapters");
            for (const GraphicsAdapterSample& adapter : snapshot.graphicsAdapters) {
                details << QStringLiteral("  %1 · dedicated memory %2")
                    .arg(adapter.name, adapter.dedicatedMemoryBytes > 0
                        ? formatGigabytes(adapter.dedicatedMemoryBytes) : QStringLiteral("not reported"));
                if (adapter.localMemoryUsageBytes && adapter.localMemoryBudgetBytes) {
                    details << QStringLiteral("    local memory currently %1 / %2 (dynamic Windows budget)")
                        .arg(formatGigabytes(*adapter.localMemoryUsageBytes),
                             formatGigabytes(*adapter.localMemoryBudgetBytes));
                } else {
                    details << QStringLiteral("    current local-memory usage unavailable");
                }
            }
        }
        if (snapshot.thermalSensors.isEmpty()) {
            details << QStringLiteral("  %1").arg(snapshot.thermalSensorStatus.isEmpty()
                ? QStringLiteral("No ACPI thermal-zone readings exposed by Windows.")
                : snapshot.thermalSensorStatus);
        } else {
            for (const ThermalSensorSample& sensor : snapshot.thermalSensors) {
                QString reading = QStringLiteral("  %1  %2 °C · %3")
                    .arg(sensor.name).arg(sensor.temperatureCelsius, 0, 'f', 1).arg(sensor.source);
                if (sensor.passiveTripPointCelsius)
                    reading += QStringLiteral(" · firmware passive point %1 °C")
                        .arg(*sensor.passiveTripPointCelsius, 0, 'f', 1);
                if (sensor.secondsAbovePassiveTripPoint)
                    reading += QStringLiteral(" · above it for %1 s").arg(*sensor.secondsAbovePassiveTripPoint);
                if (sensor.criticalTripPointCelsius)
                    reading += QStringLiteral(" · firmware critical point %1 °C")
                        .arg(*sensor.criticalTripPointCelsius, 0, 'f', 1);
                details << reading;
            }
            details << QStringLiteral("  ACPI zone identity and trip points come from firmware; they may not represent CPU/GPU die sensors or reliable component limits.");
        }
        if (!snapshot.physicalDisks.isEmpty()) {
            details << QStringLiteral("Physical storage devices");
            for (const PhysicalDiskSample& disk : snapshot.physicalDisks) {
                QString model = disk.model;
                if (!disk.vendor.isEmpty() && !model.startsWith(disk.vendor, Qt::CaseInsensitive))
                    model = disk.vendor + QLatin1Char(' ') + model;
                QString line = QStringLiteral("  Disk %1  %2 · %3")
                    .arg(disk.deviceNumber).arg(model.trimmed(), disk.busType);
                if (!disk.firmware.isEmpty()) line += QStringLiteral(" · firmware %1").arg(disk.firmware);
                const QString windowsHealth = !disk.windowsHealthStatus ? QStringLiteral("not reported")
                    : *disk.windowsHealthStatus == 0 ? QStringLiteral("Healthy")
                    : *disk.windowsHealthStatus == 1 ? QStringLiteral("Warning")
                    : *disk.windowsHealthStatus == 2 ? QStringLiteral("Unhealthy")
                    : QStringLiteral("Unknown");
                const QString mediaType = !disk.mediaType ? QStringLiteral("not reported")
                    : *disk.mediaType == 3 ? QStringLiteral("HDD")
                    : *disk.mediaType == 4 ? QStringLiteral("SSD")
                    : *disk.mediaType == 5 ? QStringLiteral("storage-class memory")
                    : QStringLiteral("unspecified");
                line += QStringLiteral(" · Windows Storage health %1 · media %2").arg(windowsHealth, mediaType);
                details << line;
                const QString temperature = disk.temperatureCelsius
                    ? QStringLiteral("%1 °C").arg(*disk.temperatureCelsius) : QStringLiteral("not reported");
                const QString wear = disk.wearPercent
                    ? QStringLiteral("%1%").arg(*disk.wearPercent) : QStringLiteral("not reported");
                const QString hours = disk.powerOnHours
                    ? QStringLiteral("%1 h").arg(*disk.powerOnHours) : QStringLiteral("not reported");
                const QString readErrors = disk.uncorrectedReadErrors
                    ? QString::number(*disk.uncorrectedReadErrors) : QStringLiteral("not reported");
                const QString writeErrors = disk.uncorrectedWriteErrors
                    ? QString::number(*disk.uncorrectedWriteErrors) : QStringLiteral("not reported");
                QString reliability = QStringLiteral("    Windows drive-reported reliability · temperature %1 · wear indicator %2 · powered on %3 · uncorrected read/write errors %4 / %5")
                    .arg(temperature, wear, hours, readErrors, writeErrors);
                if (disk.maximumTemperatureCelsius)
                    reliability += QStringLiteral(" · reported normal-operation maximum %1 °C")
                        .arg(*disk.maximumTemperatureCelsius);
                details << reliability;
                if (disk.windowsHealthStatus == 1 || disk.windowsHealthStatus == 2 ||
                    (disk.uncorrectedReadErrors && *disk.uncorrectedReadErrors > 0) ||
                    (disk.uncorrectedWriteErrors && *disk.uncorrectedWriteErrors > 0) ||
                    (disk.wearPercent && *disk.wearPercent >= 100) ||
                    (disk.temperatureCelsius && disk.maximumTemperatureCelsius &&
                     *disk.temperatureCelsius > *disk.maximumTemperatureCelsius)) {
                    details << QStringLiteral("    Review the drive-reported indicator and keep a current backup. A counter alone does not confirm drive failure.");
                } else if (!disk.windowsHealthStatus && !disk.mediaType && !disk.temperatureCelsius &&
                           !disk.wearPercent && !disk.powerOnHours &&
                           !disk.uncorrectedReadErrors && !disk.uncorrectedWriteErrors) {
                    details << QStringLiteral("    Health counters are not exposed for this drive by the Windows storage provider/driver.");
                }
            }
            details << QStringLiteral("  Reliability counters are Windows/device-reported, may be incomplete, and are not a SMART certification or failure-date prediction.");
        }
        details << QStringLiteral("Cooling fans");
        if (snapshot.fans.isEmpty()) {
            details << QStringLiteral("  Windows did not expose fan telemetry on this device");
        } else {
            for (const FanSample& fan : snapshot.fans) {
                QString line = QStringLiteral("  %1 · requested speed %2 · active cooling %3 · variable speed %4")
                    .arg(fan.name,
                         fan.requestedSpeedRpm ? QStringLiteral("%1 RPM").arg(*fan.requestedSpeedRpm)
                                               : QStringLiteral("not reported"),
                         fan.activeCooling ? (*fan.activeCooling ? QStringLiteral("active") : QStringLiteral("inactive"))
                                           : QStringLiteral("not reported"),
                         fan.variableSpeed ? (*fan.variableSpeed ? QStringLiteral("yes") : QStringLiteral("no"))
                                            : QStringLiteral("not reported"));
                if (fan.previousRequestedSpeedRpm && fan.requestedSpeedRpm) {
                    line += QStringLiteral(" · target changed %1 → %2 RPM at %3")
                        .arg(*fan.previousRequestedSpeedRpm).arg(*fan.requestedSpeedRpm)
                        .arg(fan.requestedSpeedChangedAt.isValid()
                            ? fan.requestedSpeedChangedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap"))
                            : QStringLiteral("unknown time"));
                }
                if (fan.requestedSpeedRpm)
                    line += QStringLiteral(" (requested target, not measured rotor speed)");
                if (!fan.status.isEmpty()) line += QStringLiteral(" · status %1").arg(fan.status);
                details << line;
            }
        }
        details << QStringLiteral("  Current processor load: %1 · compare over time with ACPI-zone temperatures and fan-target changes.")
            .arg(snapshot.processorUsagePercent
                ? QStringLiteral("%1%").arg(*snapshot.processorUsagePercent, 0, 'f', 1)
                : QStringLiteral("not available"));
        details << QStringLiteral("  A requested fan target is not measured rotor speed; one sample cannot establish a cooling fault.");
        hardwareDetails_->setText(details.join(QLatin1Char('\n')));
    }
    if (hardwareVolumes_) {
        QStringList volumeLines;
        for (const auto& volume : snapshot.volumes) {
            const quint64 freeBytes = std::min(volume.freeBytes, volume.totalBytes);
            const QString disk = volume.physicalDiskNumbers.isEmpty()
                ? QStringLiteral("physical disk not reported")
                : QStringLiteral("physical disk(s) %1").arg(volume.physicalDiskNumbers);
            volumeLines << QStringLiteral("%1    %2 total · %3 available · %4")
                .arg(volume.rootPath, formatGigabytes(volume.totalBytes), formatGigabytes(freeBytes), disk);
        }
        hardwareVolumes_->setText(volumeLines.isEmpty()
            ? QStringLiteral("No fixed drives are currently available.")
            : volumeLines.join(QLatin1Char('\n')) + QStringLiteral("\n\nAggregate disk activity · Read %1 · Write %2")
                .arg(formatRate(snapshot.diskActivity.readBytesPerSecond),
                     formatRate(snapshot.diskActivity.writeBytesPerSecond)));
    }

    if (foregroundWorkload_) {
        const ProcessSample* foregroundProcess = nullptr;
        if (snapshot.foregroundProcessId != 0) {
            const auto it = std::find_if(snapshot.topProcesses.cbegin(), snapshot.topProcesses.cend(),
                [&snapshot](const ProcessSample& process) {
                    return process.processId == snapshot.foregroundProcessId;
                });
            if (it != snapshot.topProcesses.cend()) foregroundProcess = &*it;
        }

        if (snapshot.foregroundProcessId == 0) {
            foregroundWorkload_->setText(QStringLiteral(
                "Windows did not identify a foreground app for this sample. This reading is only the window in front; it does not account for background work."));
        } else if (!foregroundProcess) {
            foregroundWorkload_->setText(QStringLiteral(
                "Windows reports foreground process ID %1, but Ausyn could not match it to a readable process sample.")
                .arg(snapshot.foregroundProcessId));
        } else {
            const QString cpu = foregroundProcess->cpuPercent
                ? QStringLiteral("%1% CPU").arg(*foregroundProcess->cpuPercent, 0, 'f', 1)
                : QStringLiteral("CPU still sampling");
            const QString memory = foregroundProcess->workingSetBytes
                ? QStringLiteral("%1 working set").arg(formatGigabytes(*foregroundProcess->workingSetBytes))
                : QStringLiteral("working set unavailable");
            QString memoryContext = QStringLiteral("Working-set share unavailable; background work can also affect system use.");
            if (foregroundProcess->workingSetBytes && snapshot.memoryTotalBytes > 0) {
                const double sharePercent = 100.0 * static_cast<double>(*foregroundProcess->workingSetBytes) /
                    static_cast<double>(snapshot.memoryTotalBytes);
                memoryContext = QStringLiteral("About %1% of installed RAM by working set. ")
                    .arg(sharePercent, 0, 'f', 1);
                if (snapshot.memoryUsagePercent && *snapshot.memoryUsagePercent >= 85.0 && sharePercent >= 15.0) {
                    memoryContext += QStringLiteral("This coincides with high system memory use in this sample; it does not prove the app caused it.");
                } else {
                    memoryContext += QStringLiteral("This is context, not proof that the app caused any system pressure; working sets may include shared pages.");
                }
            }
            foregroundWorkload_->setText(hubs_[1]->isDetailed()
                ? QStringLiteral("%1 · PID %2 · %3 · %4\n%5")
                    .arg(foregroundProcess->name).arg(foregroundProcess->processId).arg(cpu, memory, memoryContext)
                : QStringLiteral("%1 · %2 · %3").arg(foregroundProcess->name, cpu, memory));
        }
        if (inspectForegroundProcess_)
            inspectForegroundProcess_->setEnabled(foregroundProcess != nullptr);
        foregroundWorkload_->setToolTip(snapshot.processCollectionStatus);
        foregroundWorkload_->setAccessibleDescription(foregroundWorkload_->text());
    }

    if (processTable_ && activeLegacyPage_ == 2 &&
        (processRenderedAt_ != snapshot.processSamplesCapturedAt || !newReading)) {
        processRenderedAt_ = snapshot.processSamplesCapturedAt;
        processTable_->setUpdatesEnabled(false);
        const QSignalBlocker processSelectionBlocker(processTable_);
        const int previousCurrentRow = processTable_->currentRow();
        const QString selectedProcessId = previousCurrentRow >= 0 && processTable_->item(previousCurrentRow, 1)
            ? processTable_->item(previousCurrentRow, 1)->text() : QString{};
        const int sortColumn = processTable_->horizontalHeader()->sortIndicatorSection();
        const Qt::SortOrder sortOrder = processTable_->horizontalHeader()->sortIndicatorOrder();
        processTable_->setSortingEnabled(false);
        processTable_->setRowCount(snapshot.topProcesses.size());
        for (qsizetype row = 0; row < snapshot.topProcesses.size(); ++row) {
            const auto& process = snapshot.topProcesses.at(row);
            const int tableRow = static_cast<int>(row);
            setProcessCell(processTable_, tableRow, 0, process.name);
            setProcessCell(processTable_, tableRow, 1, QString::number(process.processId), static_cast<double>(process.processId));
            setProcessCell(processTable_, tableRow, 2, process.cpuPercent
                ? QStringLiteral("%1%").arg(*process.cpuPercent, 0, 'f', 1) : QStringLiteral("Sampling"), process.cpuPercent.value_or(-1.0));
            setProcessCell(processTable_, tableRow, 3, process.workingSetBytes
                ? formatGigabytes(*process.workingSetBytes) : QStringLiteral("Access unavailable"),
                process.workingSetBytes ? static_cast<double>(*process.workingSetBytes) : -1.0);
            setProcessCell(processTable_, tableRow, 4, process.tcpConnectionCount
                ? QString::number(*process.tcpConnectionCount) : QStringLiteral("Unavailable"),
                process.tcpConnectionCount ? static_cast<double>(*process.tcpConnectionCount) : -1.0);
            setProcessCell(processTable_, tableRow, 5, process.udpEndpointCount
                ? QString::number(*process.udpEndpointCount) : QStringLiteral("Unavailable"),
                process.udpEndpointCount ? static_cast<double>(*process.udpEndpointCount) : -1.0);
            setProcessCell(processTable_, tableRow, 6, process.readIoBytesPerSecond
                ? formatRate(process.readIoBytesPerSecond) : QStringLiteral("Sampling"), process.readIoBytesPerSecond.value_or(-1.0));
            setProcessCell(processTable_, tableRow, 7, process.writeIoBytesPerSecond
                ? formatRate(process.writeIoBytesPerSecond) : QStringLiteral("Sampling"), process.writeIoBytesPerSecond.value_or(-1.0));
            if (process.processId == snapshot.foregroundProcessId) {
                for (int column = 0; column < processTable_->columnCount(); ++column) {
                    if (QTableWidgetItem* item = processTable_->item(tableRow, column)) {
                        item->setBackground(QColor(QStringLiteral("#202d40")));
                        item->setToolTip(QStringLiteral("Windows identified this process as the foreground app in the latest sample."));
                    }
                }
            }
        }
        processTable_->setSortingEnabled(true);
        if (sortColumn >= 0) processTable_->sortItems(sortColumn, sortOrder);
        const QString query = processSearch_ ? processSearch_->text().trimmed() : QString{};
        for (int row = 0; row < processTable_->rowCount(); ++row) {
            bool matches = query.isEmpty();
            for (int column = 0; !matches && column < processTable_->columnCount(); ++column) {
                const auto* item = processTable_->item(row, column);
                matches = item && item->text().contains(query, Qt::CaseInsensitive);
            }
            processTable_->setRowHidden(row, !matches);
        }
        int restoredRow = -1;
        if (!selectedProcessId.isEmpty()) {
            for (int row = 0; row < processTable_->rowCount(); ++row) {
                const auto* idItem = processTable_->item(row, 1);
                if (idItem && idItem->text() == selectedProcessId) {
                    restoredRow = row;
                    break;
                }
            }
        }
        if (restoredRow >= 0 && !processTable_->isRowHidden(restoredRow)) {
            processTable_->setCurrentCell(restoredRow, 0,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            processTable_->clearSelection();
            if (processDetails_)
                processDetails_->setText(QStringLiteral("Select a process to inspect its current readings and executable information."));
        }
        processTable_->setUpdatesEnabled(true);
    }
    if (activeLegacyPage_ == 2) {
        updateSelectedProcessDetails();
        updateProcessTrend(snapshot);
    }
}

void MainWindow::updateEarlyPressureHeadsUp(const Ausyn::SystemSnapshot& snapshot)
{
    if (!preferences_.monitoringEnabled || !snapshot.capturedAt.isValid() ||
        snapshot.capturedAt > QDateTime::currentDateTime()) return;
    const qint64 now = snapshot.capturedAt.toUTC().toMSecsSinceEpoch();
    if (!recentLoadSamples_.isEmpty()) {
        const qint64 previous = recentLoadSamples_.last().first;
        if (now <= previous) return;
        const qint64 allowedGap = std::max<qint64>(15,
            std::clamp(snapshot.samplingIntervalSeconds, 1, 10) * 3) * 1000;
        if (now - previous > allowedGap) {
            recentLoadSamples_.clear();
            if (earlyPressureActive_) {
                earlyPressureActive_ = false;
                if (adviceBannerTitle_ && adviceBannerTitle_->text() == earlyPressureFinding_.title) adviceBanner_->hide();
                if (dismissedAdviceTitle_ == earlyPressureFinding_.title) dismissedAdviceTitle_.clear();
                deliveredFindingSeverity_.remove(QStringLiteral("live-sustained-resource-load"));
                latestAnalysis_.findings.erase(std::remove_if(latestAnalysis_.findings.begin(),
                    latestAnalysis_.findings.end(), [](const Finding& finding) {
                        return finding.ruleId == QLatin1String("live-sustained-resource-load");
                    }), latestAnalysis_.findings.end());
                updateProactiveBriefing();
            }
        }
    }

    recentLoadSamples_.append({now, {
        snapshot.processorUsagePercent.value_or(-1.0),
        snapshot.memoryUsagePercent.value_or(-1.0)}});
    while (!recentLoadSamples_.isEmpty() && now - recentLoadSamples_.first().first > 30'000)
        recentLoadSamples_.removeFirst();

    if (recentLoadSamples_.size() < 2) return;

    double cpuTotal = 0.0;
    double memoryTotal = 0.0;
    int cpuCount = 0;
    int memoryCount = 0;
    qint64 firstCpuAt = 0, lastCpuAt = 0, firstMemoryAt = 0, lastMemoryAt = 0;
    for (const auto& sample : recentLoadSamples_) {
        if (std::isfinite(sample.second.first) && sample.second.first >= 0.0 && sample.second.first <= 100.0) {
            cpuTotal += sample.second.first;
            if (cpuCount++ == 0) firstCpuAt = sample.first;
            lastCpuAt = sample.first;
        }
        if (std::isfinite(sample.second.second) && sample.second.second >= 0.0 && sample.second.second <= 100.0) {
            memoryTotal += sample.second.second;
            if (memoryCount++ == 0) firstMemoryAt = sample.first;
            lastMemoryAt = sample.first;
        }
    }
    const double cpuAverage = cpuCount > 0 ? cpuTotal / cpuCount : -1.0;
    const double memoryAverage = memoryCount > 0 ? memoryTotal / memoryCount : -1.0;
    const int sensitivity = std::clamp(preferences_.resourceAlertSensitivity, 1, 5) - 1;
    static constexpr std::array<double, 5> cpuThresholds{88.0, 82.0, 75.0, 70.0, 65.0};
    static constexpr std::array<double, 5> memoryThresholds{92.0, 88.0, 85.0, 82.0, 78.0};
    static constexpr std::array<double, 5> combinedCpuThresholds{75.0, 68.0, 60.0, 55.0, 50.0};
    static constexpr std::array<double, 5> combinedMemoryThresholds{85.0, 82.0, 78.0, 74.0, 70.0};
    const bool enoughCpu = cpuCount >= 4 && lastCpuAt - firstCpuAt >= 20'000;
    const bool enoughMemory = memoryCount >= 4 && lastMemoryAt - firstMemoryAt >= 20'000;
    qint64 highMemorySince = now;
    int highMemoryReadings = 0;
    for (auto it = recentLoadSamples_.crbegin(); it != recentLoadSamples_.crend(); ++it) {
        if (!std::isfinite(it->second.second) || it->second.second < 95.0 || it->second.second > 100.0) break;
        highMemorySince = it->first;
        ++highMemoryReadings;
    }
    const bool urgentMemory = snapshot.memoryTotalBytes > 0 && snapshot.memoryAvailableBytes <= snapshot.memoryTotalBytes &&
        highMemoryReadings >= 2 && now - highMemorySince >= 5000;
    const bool criticalMemory = urgentMemory && snapshot.memoryUsagePercent.value_or(0) >= 97.0 &&
        snapshot.memoryAvailableBytes <= 256ULL * 1024 * 1024;
    const bool pressure = urgentMemory || (enoughCpu && cpuAverage >= cpuThresholds.at(static_cast<std::size_t>(sensitivity))) ||
        (enoughMemory && memoryAverage >= memoryThresholds.at(static_cast<std::size_t>(sensitivity))) ||
        (enoughCpu && enoughMemory && cpuAverage >= combinedCpuThresholds.at(static_cast<std::size_t>(sensitivity)) &&
         memoryAverage >= combinedMemoryThresholds.at(static_cast<std::size_t>(sensitivity)));

    if (!pressure) {
        if (earlyPressureActive_) {
            earlyPressureActive_ = false;
            latestAnalysis_.findings.erase(std::remove_if(latestAnalysis_.findings.begin(),
                latestAnalysis_.findings.end(), [](const Finding& finding) {
                    return finding.ruleId == QLatin1String("live-sustained-resource-load");
                }), latestAnalysis_.findings.end());
            updateProactiveBriefing();
            recordActivity(QStringLiteral("Resource pressure eased"), QStringLiteral("The recent readings no longer meet Ausyn’s workload heads-up rule. This is an observed recovery, rather than proof of a particular fix."), 2);
            if (adviceBannerTitle_ && (adviceBannerTitle_->text() == earlyPressureFinding_.title)) adviceBanner_->hide();
            if (dismissedAdviceTitle_ == earlyPressureFinding_.title) dismissedAdviceTitle_.clear();
            deliveredFindingSeverity_.remove(QStringLiteral("live-sustained-resource-load"));
        }
        return;
    }

    QStringList readings;
    if (cpuAverage >= 0.0)
        readings << QStringLiteral("CPU averaged %1% over the last %2 seconds")
            .arg(cpuAverage, 0, 'f', 0)
            .arg((now - recentLoadSamples_.first().first) / 1000);
    if (memoryAverage >= 0.0)
        readings << QStringLiteral("memory averaged %1%")
            .arg(memoryAverage, 0, 'f', 0);

    QString foreground;
    const qint64 processAge = snapshot.processSamplesCapturedAt.isValid()
        ? snapshot.processSamplesCapturedAt.msecsTo(snapshot.capturedAt) : -1;
    if (processAge >= 0 && processAge <= 30'000 && snapshot.foregroundProcessId != 0) {
        const auto process = std::find_if(snapshot.topProcesses.cbegin(), snapshot.topProcesses.cend(),
            [&snapshot](const ProcessSample& item) { return item.processId == snapshot.foregroundProcessId; });
        if (process != snapshot.topProcesses.cend() && !process->name.isEmpty())
            foreground = QStringLiteral("Foreground app: %1. This is timing context, not proof that the app caused the system load.")
                .arg(process->name);
    }

    const bool newPressureFinding = !earlyPressureActive_;
    const int previousPressureSeverity = earlyPressureActive_ ? static_cast<int>(earlyPressureFinding_.severity) : -1;
    Finding finding;
    finding.ruleId = QStringLiteral("live-sustained-resource-load");
    finding.severity = criticalMemory ? FindingSeverity::Critical : urgentMemory || memoryAverage >= 90 || cpuAverage >= 95 ? FindingSeverity::Warning : FindingSeverity::Information;
    finding.title = urgentMemory ? QStringLiteral("Memory headroom is very low") : QStringLiteral("Your PC has been under heavier load");
    finding.summary = urgentMemory
        ? QStringLiteral("Memory stayed at or above 95% across repeated fresh Windows readings for at least five seconds. Available RAM is %1. Other apps may have less room to work.").arg(formatGigabytes(snapshot.memoryAvailableBytes))
        : QStringLiteral("CPU or memory use has stayed elevated for at least 20 seconds. A demanding task can explain this; it is an early heads-up, not a fault diagnosis.");
    finding.evidence = readings.join(QStringLiteral("; ")) + QLatin1Char('.') +
        (foreground.isEmpty() ? QString{} : QStringLiteral(" ") + foreground);
    finding.recommendation = urgentMemory
        ? QStringLiteral("Save your work, then review unused tabs or apps. Reducing a demanding video or game workload may help. I won’t close anything for you.")
        : QStringLiteral("Review the busiest apps if this load was unexpected. Try reducing the current video or game workload and compare the next readings. Save work before closing anything.");
    QString processHint;
    if (processAge >= 0 && processAge <= 30'000) {
        const ProcessSample* contributor = nullptr;
        for (const auto& process : snapshot.topProcesses) {
            if (urgentMemory || memoryAverage >= 85) {
                if (process.workingSetBytes && (!contributor || process.workingSetBytes.value_or(0) > contributor->workingSetBytes.value_or(0))) contributor = &process;
            } else if (process.cpuPercent && (!contributor || *process.cpuPercent > contributor->cpuPercent.value_or(-1))) contributor = &process;
        }
        if (contributor) {
            processHint = QStringLiteral("Largest listed %1 reading: %2 at %3. This is a possible contributor, not a confirmed cause.")
                .arg(urgentMemory || memoryAverage >= 85 ? QStringLiteral("RAM") : QStringLiteral("CPU"), contributor->name,
                    urgentMemory || memoryAverage >= 85 ? formatGigabytes(contributor->workingSetBytes.value_or(0)) : QStringLiteral("%1%").arg(contributor->cpuPercent.value_or(0), 0, 'f', 1));
            finding.evidence += QLatin1Char(' ') + processHint;
        }
    }
    finding.confidence = QStringLiteral("Moderate");
    finding.confidenceBasis = urgentMemory ? QStringLiteral("Repeated fresh Windows memory readings spanning at least five seconds show very low headroom; they do not identify the cause.")
        : QStringLiteral("A short rolling window of fresh Windows samples shows elevated system-wide use; it does not identify the cause.");
    finding.lastSeen = snapshot.capturedAt;
    finding.firstSeen = earlyPressureActive_ ? earlyPressureFinding_.firstSeen : snapshot.capturedAt;
    earlyPressureFinding_ = finding;
    earlyPressureActive_ = true;

    latestAnalysis_.findings.erase(std::remove_if(latestAnalysis_.findings.begin(),
        latestAnalysis_.findings.end(), [](const Finding& current) {
            return current.ruleId == QLatin1String("live-sustained-resource-load");
        }), latestAnalysis_.findings.end());
    latestAnalysis_.findings.append(finding);
    if (newPressureFinding || static_cast<int>(finding.severity) > previousPressureSeverity) {
        dismissedAdviceTitle_.clear();
        recordActivity(finding.title, finding.evidence + QLatin1Char(' ') + finding.recommendation, 2);
        if (!preferences_.workloadAwarenessEnabled) showAdviceBanner(finding.title, finding.summary + QLatin1Char(' ') + finding.recommendation,
            static_cast<int>(urgentMemory || memoryAverage >= 85 ? ResourceKind::Memory : ResourceKind::Cpu));
    }
    const QDateTime nowLocal = snapshot.capturedAt;
    if (newPressureFinding || !lastEarlyPressureBriefingAt_.isValid() ||
        lastEarlyPressureBriefingAt_.msecsTo(nowLocal) >= 30'000) {
        updateProactiveBriefing();
        lastEarlyPressureBriefingAt_ = nowLocal;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!preferences_.workloadAwarenessEnabled && (newPressureFinding || static_cast<int>(finding.severity) > deliveredFindingSeverity_.value(finding.ruleId, -1) || !lastEarlyPressureNotificationAt_.isValid() ||
         lastEarlyPressureNotificationAt_.secsTo(nowUtc) >= preferences_.notificationCooldownMinutes * 60) &&
        showDesktopNotification(NotificationCategory::SystemFinding, finding.title,
            (finding.summary + QLatin1Char(' ') + (urgentMemory ? processHint : readings.join(QStringLiteral("; "))) + QLatin1Char(' ') + finding.recommendation).left(650),
            criticalMemory ? QSystemTrayIcon::Critical : finding.severity == FindingSeverity::Warning ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information,
            false, static_cast<int>(urgentMemory || memoryAverage >= 85 ? ResourceKind::Memory : ResourceKind::Cpu))) {
        lastEarlyPressureNotificationAt_ = nowUtc;
        deliveredFindingSeverity_.insert(finding.ruleId, static_cast<int>(finding.severity));
    }
}

void MainWindow::updateMonitoringStatus()
{
    if (automaticLease_ && backgroundRelief_) {
        backgroundRelief_->guard(workloadCoach_.confirmed() ? workloadCoach_.appName() : companion_.goal());
        const qint64 age = latestSnapshot_.capturedAt.isValid() ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
        if (!backgroundRelief_->active() || age < 0 || age > std::max(15, latestSnapshot_.samplingIntervalSeconds * 3) * 1000LL) {
            backgroundRelief_->restore(); automaticLease_ = false;
            companion_.endAutomaticAction(QStringLiteral("The background guard ended the lease, or fresh evidence stopped arriving."));
        }
    }
    refreshCompanionPanel();
    if (isVisible() && activeLegacyPage_ == 0) refreshWorkloadPanel();
    const auto fitStatus = [this] {
        if (!monitoringStatus_) return;
        monitoringStatus_->setMinimumWidth(monitoringStatus_->fontMetrics().horizontalAdvance(monitoringStatus_->text()) + 8);
    };
    for (auto window : resourceWindows_) if (window && window->isVisible()) window->updateFreshness(preferences_.monitoringEnabled);
    if (alertDeliveryStatus_) {
        const AlertPresentation delivery = AlertPolicy::presentation(preferences_, NotificationCategory::SystemFinding,
            false, trayIcon_ && trayIcon_->isVisible(), QDateTime::currentDateTime(), desktopSnoozedUntil_);
        alertDeliveryStatus_->setText(delivery.reason.isEmpty()
            ? QStringLiteral("Ready: %1. Ausyn keeps observing when another app is in front. Use the test button to check this channel.")
                .arg(preferences_.ausynPopupCards ? QStringLiteral("Ausyn popup cards") : QStringLiteral("Windows notification requests"))
            : QStringLiteral("Pop-up status: %1. Monitoring and advice in the app remain separate.").arg(delivery.reason));
    }
    if (!preferences_.monitoringEnabled) {
        if (monitoringStatus_) {
            monitoringStatus_->setText(QStringLiteral("Monitoring paused"));
            monitoringStatus_->setToolTip(QStringLiteral("Monitoring is paused. Displayed measurements retain their original capture times; resume monitoring for new readings."));
        }
        if (monitoringStatusDot_) setLabelStyleIfChanged(monitoringStatusDot_, QStringLiteral("color:#f0be73;"));
        fitStatus();
        updateDashboardReadout();
        return;
    }
    if (!isVisible()) return;
    const qint64 sampleAgeMilliseconds = latestSnapshot_.capturedAt.isValid()
        ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 staleAfterSeconds = std::max<qint64>(
        10, std::clamp(latestSnapshot_.samplingIntervalSeconds, 1, 10) * 3);
    const bool snapshotIsStale = sampleAgeMilliseconds < 0 ||
        sampleAgeMilliseconds > staleAfterSeconds * 1000;
    if (snapshotIsStale != proactiveSnapshotStale_) {
        proactiveSnapshotStale_ = snapshotIsStale;
        updateProactiveBriefing();
    }

    if (processSnapshotAge_) {
        const QDateTime capturedAt=latestSnapshot_.processSamplesCapturedAt;
        const bool collectionFailed = latestSnapshot_.processCollectionStatus.startsWith(
            QStringLiteral("Windows could not"));
        processSnapshotAge_->setToolTip(latestSnapshot_.processCollectionStatus);
        processSnapshotAge_->setAccessibleDescription(latestSnapshot_.processCollectionStatus);
        if (!capturedAt.isValid()) {
            processSnapshotAge_->setText(collectionFailed
                ? QStringLiteral("Process readings unavailable · refresh failed")
                : QStringLiteral("Waiting for process readings…"));
            setLabelStyleIfChanged(processSnapshotAge_, collectionFailed
                ? QStringLiteral("color:#f0be73;") : QString());
        } else {
            const qint64 ageMilliseconds=capturedAt.msecsTo(QDateTime::currentDateTime());
            const bool timestampAhead = ageMilliseconds < 0;
            const qint64 ageSeconds=std::max<qint64>(0,ageMilliseconds / 1000);
            const int refreshTarget=std::max(5,preferences_.samplingIntervalSeconds);
            const bool stale=ageMilliseconds>std::max(10,preferences_.samplingIntervalSeconds*3)*1000LL;
            processSnapshotAge_->setText(timestampAhead
                ? QStringLiteral("Process sample timestamp is ahead of this PC’s clock")
                : collectionFailed
                ? QStringLiteral("Process refresh failed · last good sample %1s ago").arg(ageSeconds)
                : stale ? QStringLiteral("Process readings · %1s ago · may be stale").arg(ageSeconds)
                    : QStringLiteral("Process readings · %1 · refresh target about every %2 seconds")
                        .arg(ageSeconds<3?QStringLiteral("just now"):QStringLiteral("%1s ago").arg(ageSeconds))
                        .arg(refreshTarget));
            setLabelStyleIfChanged(processSnapshotAge_, timestampAhead || collectionFailed || stale
                ? QStringLiteral("color:#f0be73;") : QString());
        }
    }
    if (!monitoringStatus_ || !monitoringStatusDot_) return;
    if (!latestSnapshot_.capturedAt.isValid()) {
        monitoringStatus_->setText(QStringLiteral("Waiting for readings"));
        monitoringStatus_->setToolTip(QStringLiteral("Waiting for the first Windows reading; no live assessment is available yet."));
        setLabelStyleIfChanged(monitoringStatus_, QStringLiteral("color:#b8c3d3;"));
        setLabelStyleIfChanged(monitoringStatusDot_, QStringLiteral("color:#f0be73;"));
        fitStatus();
        return;
    }

    const qint64 ageMilliseconds = sampleAgeMilliseconds;
    if (ageMilliseconds < 0) {
        monitoringStatus_->setText(QStringLiteral("Clock mismatch"));
        monitoringStatus_->setToolTip(QStringLiteral("The latest Windows sample timestamp is ahead of this PC’s clock. Ausyn is withholding a live interpretation."));
        setLabelStyleIfChanged(monitoringStatus_, QStringLiteral("color:#f0be73;"));
        setLabelStyleIfChanged(monitoringStatusDot_, QStringLiteral("color:#f0be73;"));
        fitStatus();
        return;
    }
    const qint64 ageSeconds = ageMilliseconds / 1000;
    if (ageMilliseconds > staleAfterSeconds * 1000LL) {
        monitoringStatus_->setText(QStringLiteral("Readings delayed"));
        monitoringStatus_->setToolTip(QStringLiteral("The latest Windows reading is %1 seconds old; the freshness limit is %2 seconds. Live interpretation is withheld until readings recover.").arg(ageSeconds).arg(staleAfterSeconds));
        setLabelStyleIfChanged(monitoringStatus_, QStringLiteral("color:#f0be73;"));
        setLabelStyleIfChanged(monitoringStatusDot_, QStringLiteral("color:#f0be73;"));
        fitStatus();
        return;
    }

    monitoringStatus_->setText(ageSeconds < 3
        ? QStringLiteral("Monitoring live")
        : QStringLiteral("Live · %1s ago").arg(ageSeconds));
    monitoringStatus_->setToolTip(QStringLiteral("Latest Windows reading: %1 · %2 seconds ago").arg(latestSnapshot_.capturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap"))).arg(ageSeconds));
    setLabelStyleIfChanged(monitoringStatus_, QStringLiteral("color:#b8c3d3;"));
    setLabelStyleIfChanged(monitoringStatusDot_, QStringLiteral("color:#69d9a8;"));
    fitStatus();
}

void MainWindow::updateSelectedProcessDetails()
{
    if (!processTable_ || !processDetails_) return;
    const int row = processTable_->currentRow();
    const auto* idItem = row >= 0 ? processTable_->item(row, 1) : nullptr;
    bool validId = false;
    const quint32 processId = idItem ? idItem->text().toUInt(&validId) : 0;
    if (!validId) {
        processTrendProcessId_ = 0;
        processTrendCapturedAt_ = {};
        processTrend_.clear();
        if (processTrendChart_) processTrendChart_->setSamples(processTrend_);
        processDetails_->setText(QStringLiteral("Select a process to inspect its current readings and executable information."));
        if (processImpactDetails_)
            processImpactDetails_->setText(QStringLiteral("Select a process to build a short, local resource profile."));
        return;
    }

    if (processId != processTrendProcessId_) {
        processTrendProcessId_ = processId;
        processTrendCapturedAt_ = latestSnapshot_.processSamplesCapturedAt;
        processTrend_.clear();
        if (processTrendChart_) processTrendChart_->setSamples(processTrend_);
        updateSelectedProcessImpact();
    }

    const auto it = std::find_if(latestSnapshot_.topProcesses.cbegin(), latestSnapshot_.topProcesses.cend(),
        [processId](const ProcessSample& process) { return process.processId == processId; });
    if (it == latestSnapshot_.topProcesses.cend()) {
        processDetails_->setText(QStringLiteral("This process is no longer running or its latest readings are unavailable."));
        return;
    }
    const QString cpu = it->cpuPercent
        ? QStringLiteral("%1%").arg(*it->cpuPercent, 0, 'f', 1) : QStringLiteral("Unavailable");
    const QString memory = it->workingSetBytes ? formatGigabytes(*it->workingSetBytes)
                                               : QStringLiteral("Unavailable");
    const QString tcp = it->tcpConnectionCount ? QString::number(*it->tcpConnectionCount)
                                               : QStringLiteral("Unavailable");
    const QString udp = it->udpEndpointCount ? QString::number(*it->udpEndpointCount)
                                             : QStringLiteral("Unavailable");
    const QString readIo = formatRate(it->readIoBytesPerSecond);
    const QString writeIo = formatRate(it->writeIoBytesPerSecond);
    const QString path = it->executablePath.isEmpty()
        ? QStringLiteral("Unavailable for this process") : it->executablePath;
    const qint64 sampleAgeMilliseconds = latestSnapshot_.processSamplesCapturedAt.isValid()
        ? latestSnapshot_.processSamplesCapturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 freshnessLimitSeconds = std::max<qint64>(
        10, std::clamp(preferences_.samplingIntervalSeconds, 1, 10) * 3);
    QString sampleStatus = QStringLiteral("Process sample timestamp unavailable");
    if (sampleAgeMilliseconds >= 0) {
        const qint64 sampleAgeSeconds = sampleAgeMilliseconds / 1000;
        sampleStatus = QStringLiteral("Process sample captured at %1 · %2 second(s) ago")
            .arg(latestSnapshot_.processSamplesCapturedAt.toLocalTime().toString(QStringLiteral("h:mm:ss ap")))
            .arg(sampleAgeSeconds);
        if (sampleAgeMilliseconds > freshnessLimitSeconds * 1000)
            sampleStatus += QStringLiteral(" · stale; readings may be outdated");
    } else if (sampleAgeMilliseconds < 0) {
        sampleStatus = QStringLiteral("Process sample timestamp is ahead of this PC’s clock");
    }
    if (latestSnapshot_.processCollectionStatus.startsWith(QStringLiteral("Windows could not")))
        sampleStatus += QStringLiteral(" · ") + latestSnapshot_.processCollectionStatus;
    processDetails_->setText(QStringLiteral("%1\n%2 · PID %3\nCPU %4 · Working set %5 · TCP %6 · UDP %7\nProcess I/O rate: read %8 · write %9\nExecutable: %10")
        .arg(sampleStatus).arg(it->name).arg(QString::number(it->processId)).arg(cpu).arg(memory)
        .arg(tcp).arg(udp).arg(readIo).arg(writeIo).arg(path));
    updateSelectedProcessImpact();
}

void MainWindow::updateProcessTrend(const SystemSnapshot& snapshot)
{
    if (processTrendProcessId_ == 0 || !processTrendChart_ ||
        !snapshot.processSamplesCapturedAt.isValid() ||
        snapshot.processSamplesCapturedAt == processTrendCapturedAt_) return;
    processTrendCapturedAt_ = snapshot.processSamplesCapturedAt;

    const auto it = std::find_if(snapshot.topProcesses.cbegin(), snapshot.topProcesses.cend(),
        [this](const ProcessSample& process) { return process.processId == processTrendProcessId_; });
    ProcessTrendPoint point;
    point.capturedAt = snapshot.processSamplesCapturedAt;
    point.systemCpuPercent = snapshot.processorUsagePercent;
    point.systemMemoryPercent = snapshot.memoryUsagePercent;
    if (it != snapshot.topProcesses.cend()) {
        point.cpuPercent = it->cpuPercent;
        if (it->workingSetBytes) {
            point.workingSetGigabytes = static_cast<double>(*it->workingSetBytes) / 1'000'000'000.0;
            if (snapshot.memoryTotalBytes > 0)
                point.workingSetSharePercent = 100.0 * static_cast<double>(*it->workingSetBytes) /
                    static_cast<double>(snapshot.memoryTotalBytes);
        }
        if (it->readIoBytesPerSecond)
            point.readMegabytesPerSecond = *it->readIoBytesPerSecond / 1'000'000.0;
        if (it->writeIoBytesPerSecond)
            point.writeMegabytesPerSecond = *it->writeIoBytesPerSecond / 1'000'000.0;
    }
    processTrend_.append(point);
    constexpr qsizetype kMaximumSamples = 30;
    if (processTrend_.size() > kMaximumSamples)
        processTrend_.remove(0, processTrend_.size() - kMaximumSamples);
    processTrendChart_->setSamples(processTrend_);
    updateSelectedProcessImpact();
}

void MainWindow::updateSelectedProcessImpact()
{
    if (!processImpactDetails_) return;
    if (processTrendProcessId_ == 0 || processTrend_.isEmpty()) {
        processImpactDetails_->setText(QStringLiteral("Select a process and leave it selected while Ausyn collects a few samples to build a short resource profile."));
        return;
    }

    double cpuSum = 0.0;
    double cpuPeak = 0.0;
    int cpuCount = 0;
    double memorySum = 0.0;
    double memoryPeak = 0.0;
    int memoryCount = 0;
    double shareSum = 0.0;
    int shareCount = 0;
    int pairedMemoryCount = 0;
    int elevatedMemoryOverlapCount = 0;
    for (const ProcessTrendPoint& point : processTrend_) {
        if (point.cpuPercent && std::isfinite(*point.cpuPercent)) {
            cpuSum += *point.cpuPercent;
            cpuPeak = cpuCount == 0 ? *point.cpuPercent : std::max(cpuPeak, *point.cpuPercent);
            ++cpuCount;
        }
        if (point.workingSetGigabytes && std::isfinite(*point.workingSetGigabytes)) {
            memorySum += *point.workingSetGigabytes;
            memoryPeak = memoryCount == 0 ? *point.workingSetGigabytes : std::max(memoryPeak, *point.workingSetGigabytes);
            ++memoryCount;
        }
        if (point.workingSetSharePercent && std::isfinite(*point.workingSetSharePercent)) {
            shareSum += *point.workingSetSharePercent;
            ++shareCount;
            if (point.systemMemoryPercent && std::isfinite(*point.systemMemoryPercent)) {
                ++pairedMemoryCount;
                if (*point.systemMemoryPercent >= 85.0 && *point.workingSetSharePercent >= 15.0)
                    ++elevatedMemoryOverlapCount;
            }
        }
    }

    const QDateTime first = processTrend_.first().capturedAt;
    const QDateTime last = processTrend_.last().capturedAt;
    const qint64 spanSeconds = first.isValid() && last.isValid() ? std::max<qint64>(0, first.secsTo(last)) : 0;
    QStringList summary;
    summary << QStringLiteral("Recent selected-app profile · %1 samples across %2 min · temporary local data")
        .arg(processTrend_.size()).arg(spanSeconds / 60);
    summary << (cpuCount == 0
        ? QStringLiteral("CPU: unavailable in these samples")
        : QStringLiteral("CPU: %1% average, %2% peak across %3 readings")
            .arg(cpuSum / cpuCount, 0, 'f', 1).arg(cpuPeak, 0, 'f', 1).arg(cpuCount));
    summary << (memoryCount == 0
        ? QStringLiteral("Working set: unavailable in these samples")
        : QStringLiteral("Working set: %1 GB average, %2 GB peak across %3 readings")
            .arg(memorySum / memoryCount, 0, 'f', 2).arg(memoryPeak, 0, 'f', 2).arg(memoryCount));
    if (shareCount > 0)
        summary << QStringLiteral("Average working set was %1% of installed RAM in %2 paired reading(s).")
            .arg(shareSum / shareCount, 0, 'f', 1).arg(shareCount);
    if (pairedMemoryCount > 0)
        summary << QStringLiteral("System memory was at least 85% while this app used at least 15% of installed RAM in %1 of %2 paired snapshots.")
            .arg(elevatedMemoryOverlapCount).arg(pairedMemoryCount);
    else
        summary << QStringLiteral("System memory pressure could not be compared for these process samples.");
    summary << QStringLiteral("This is a short, selected-process association, not proof the app caused pressure. Working sets can include shared pages. Samples are cleared when you select another process or close Ausyn.");
    processImpactDetails_->setText(summary.join(QLatin1Char('\n')));
}

void MainWindow::updateAnalysis(Ausyn::AnalysisUpdate update)
{
    if (earlyPressureActive_) {
        update.findings.erase(std::remove_if(update.findings.begin(), update.findings.end(),
            [](const Finding& finding) {
                return finding.ruleId == QLatin1String("live-sustained-resource-load");
            }), update.findings.end());
        update.findings.append(earlyPressureFinding_);
    }
    latestAnalysis_ = update;
    for (auto window : resourceWindows_) if (window && window->isVisible()) window->setContext(latestSnapshot_, update, preferences_.monitoringEnabled, true);
    updateDashboardReadout();
    if (troubleshootingPage_ && activeLegacyPage_ == 17)
        troubleshootingPage_->setContext(latestSnapshot_, latestAnalysis_, latestEventLogUpdate_);
    refreshHistorySummary();
    if (predictionPage_ && activeLegacyPage_ == 10 && isVisible()) {
        StorageForecast forecast = update.storageForecast;
        if (forecast.explanation.isEmpty() && !update.historyAvailable)
            forecast.explanation = QStringLiteral("Local history is unavailable, so Ausyn cannot build a storage forecast yet.");
        predictionPage_->setForecast(forecast, update.batteryForecast,
                                     update.volumeStorageForecasts, update.memoryForecast,
                                     preferences_.historyRetentionDays);
    }
    if (batteryPage_) batteryPage_->setHealthTrend(update.batteryHealthTrend);
    const QString emptyFindings = QStringLiteral(
        "No sustained issues match the current evidence. If Ausyn sees a persistent pattern, it will explain what it measured.");
    if (diagnosticsView_ && activeLegacyPage_ == 6 && isVisible()) {
        diagnosticsView_->setFindings(update.findings, emptyFindings, false,
            hubs_[0]->isDetailed());
    }
    QHash<QString, int> currentSeverity;
    for (const Finding& finding : update.findings) {
        const int severity = static_cast<int>(finding.severity);
        currentSeverity.insert(finding.ruleId, severity);
        const int previous = lastNotifiedSeverity_.value(finding.ruleId, -1);
        if (severity > previous)
            recordActivity(finding.title, QStringLiteral("%1 %2").arg(finding.summary, finding.recommendation), 6);
        const bool coveredByWorkloadEpisode = preferences_.workloadAwarenessEnabled && earlyPressureActive_ &&
            (finding.ruleId == QLatin1String("live-sustained-resource-load") || finding.ruleId == QLatin1String("processor-sustained-load") ||
             finding.ruleId == QLatin1String("memory-sustained-pressure") || finding.ruleId == QLatin1String("concurrent-cpu-memory-pressure"));
        if (coveredByWorkloadEpisode) continue;
        const int deliveredSeverity = deliveredFindingSeverity_.value(finding.ruleId, -1);
        const bool notificationsReady = notificationsAllowed(NotificationCategory::SystemFinding);
        const bool warningEscalation = severity >= static_cast<int>(FindingSeverity::Warning) && severity > deliveredSeverity;
        const bool personalBaselineShift = finding.ruleId == QStringLiteral("personal-baseline-resource-shift") &&
            severity > deliveredSeverity;
        const bool rapidMemoryRise = finding.ruleId == QStringLiteral("memory-rapid-rise");
        const bool thermalFinding = finding.ruleId == QStringLiteral("thermal-zone-sustained-passive-point");
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        const bool thermalCooldownElapsed = !lastThermalNotificationAt_.isValid() ||
            lastThermalNotificationAt_.secsTo(nowUtc) >= 30 * 60;
        const bool criticalThermalEscalation = thermalFinding &&
            severity >= static_cast<int>(FindingSeverity::Critical) &&
            deliveredSeverity < static_cast<int>(FindingSeverity::Critical);
        if (severity >= static_cast<int>(FindingSeverity::Warning) && severity > previous) {
            dismissedAdviceTitle_.clear();
            showAdviceBanner(finding.title, finding.recommendation.isEmpty() ? finding.summary : finding.recommendation,
                notificationResource(finding.title, finding.summary));
        }
        if (notificationsReady && warningEscalation &&
            (!thermalFinding || thermalCooldownElapsed || criticalThermalEscalation)) {
            QString body = finding.recommendation.isEmpty() ? finding.summary : finding.recommendation;
            if (thermalFinding)
                body = QStringLiteral("%1 %2").arg(finding.evidence, body);
            const auto icon = finding.severity == FindingSeverity::Critical
                ? QSystemTrayIcon::Critical : QSystemTrayIcon::Warning;
            if (showDesktopNotification(NotificationCategory::SystemFinding, finding.title, body.left(650), icon)) {
                deliveredFindingSeverity_.insert(finding.ruleId, severity);
                if (thermalFinding) lastThermalNotificationAt_ = nowUtc;
            }
        } else if (notificationsReady && (personalBaselineShift || rapidMemoryRise)) {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            const QDateTime lastNotice = lastInformationalNoticeAt_.value(finding.ruleId);
            if (!lastNotice.isValid() || lastNotice.secsTo(now) >= 30 * 60) {
                const QString body = rapidMemoryRise
                    ? QStringLiteral("%1 This indicates a quick change in memory use, not a diagnosed leak. %2")
                        .arg(finding.evidence, finding.recommendation)
                    : QStringLiteral("%1. This is a change from your own recent baseline, not proof of a fault. %2")
                        .arg(finding.evidence, finding.recommendation);
                if (showDesktopNotification(NotificationCategory::SystemFinding, finding.title, body.left(650), QSystemTrayIcon::Information)) {
                    lastInformationalNoticeAt_.insert(finding.ruleId, now);
                    deliveredFindingSeverity_.insert(finding.ruleId, severity);
                }
            }
        }
    }
    const bool hasStorageFinding = std::any_of(update.findings.cbegin(), update.findings.cend(), [](const Finding& finding) {
        return finding.ruleId == QStringLiteral("system-drive-low-space");
    });
    const bool hasBatteryFinding = std::any_of(update.findings.cbegin(), update.findings.cend(), [](const Finding& finding) {
        return finding.ruleId == QStringLiteral("battery-low-charge");
    });
    const bool storageForecastNear = update.storageForecast.hasEstimate &&
        update.storageForecast.daysUntilTenPercent >= 0.0 && update.storageForecast.daysUntilTenPercent <= 30.0 &&
        !hasStorageFinding;
    const bool rapidStorageDrop = update.storageForecast.rapidDropDetected && !hasStorageFinding;
    const bool batteryForecastNear = update.batteryForecast.hasEstimate &&
        update.batteryForecast.minutesUntil15Percent >= 0.0 && update.batteryForecast.minutesUntil15Percent <= 120.0 &&
        !hasBatteryFinding;
    const bool memoryForecastNear = update.memoryForecast.hasEstimate &&
        update.memoryForecast.minutesUntil90Percent >= 0.0 && update.memoryForecast.minutesUntil90Percent <= 30.0;
    QSet<QString> currentVolumeStorageNotices;
    QSet<QString> candidateVolumeStorageNotices;
    QStringList newVolumeStorageMessages;
    for (const VolumeStorageForecast& volume : update.volumeStorageForecasts) {
        const StorageForecast& forecast = volume.forecast;
        if (forecast.currentFreePercent <= 10.0) continue;
        const QString drive = volume.label.isEmpty() ? volume.rootPath
            : QStringLiteral("%1 (%2)").arg(volume.label, volume.rootPath);
        const QString rapidKey = volume.rootPath + QStringLiteral("|rapid-drop");
        const QString nearKey = volume.rootPath + QStringLiteral("|near-threshold");
        const bool rapid = forecast.rapidDropDetected;
        const bool near = forecast.hasEstimate && forecast.daysUntilTenPercent >= 0.0 &&
            forecast.daysUntilTenPercent <= 30.0;
        if (rapid) {
            currentVolumeStorageNotices.insert(rapidKey);
            if (!activeVolumeStorageNotices_.contains(rapidKey) && newVolumeStorageMessages.size() < 2) {
                newVolumeStorageMessages << QStringLiteral("%1: free space fell by %2 percentage points in daily samples; Ausyn cannot identify which files used the space.")
                    .arg(drive).arg(forecast.rapidDropPercentagePoints, 0, 'f', 1);
                candidateVolumeStorageNotices.insert(rapidKey);
            }
        }
        if (near) {
            currentVolumeStorageNotices.insert(nearKey);
            if (!activeVolumeStorageNotices_.contains(nearKey) && newVolumeStorageMessages.size() < 2) {
                newVolumeStorageMessages << QStringLiteral("%1 could reach 10% free space in about %2 day(s) at its recent rate.")
                    .arg(drive).arg(static_cast<int>(std::lround(forecast.daysUntilTenPercent)));
                candidateVolumeStorageNotices.insert(nearKey);
            }
        }
    }
    if (hasSeenAnalysis_ && notificationsAllowed(NotificationCategory::Forecast)) {
        if (rapidStorageDrop && !rapidStorageDropNotified_)
            rapidStorageDropNotified_ = showDesktopNotification(NotificationCategory::Forecast, QStringLiteral("System drive space changed sharply"),
                QStringLiteral("Daily samples show free space decreased by %1 percentage points. Review recent storage activity; Ausyn cannot identify which files used space.")
                    .arg(update.storageForecast.rapidDropPercentagePoints, 0, 'f', 1),
                QSystemTrayIcon::Warning);
        if (storageForecastNear && !storageForecastNotified_)
            storageForecastNotified_ = showDesktopNotification(NotificationCategory::Forecast, QStringLiteral("Storage trend needs a look"),
                QStringLiteral("At the recent rate, system-drive free space could reach 10% within about %1 day(s).")
                    .arg(static_cast<int>(std::lround(update.storageForecast.daysUntilTenPercent))),
                QSystemTrayIcon::Warning);
        if (!newVolumeStorageMessages.isEmpty() && showDesktopNotification(NotificationCategory::Forecast, QStringLiteral("Secondary-drive storage outlook"),
                newVolumeStorageMessages.join(QLatin1Char(' ')).left(650), QSystemTrayIcon::Warning))
            activeVolumeStorageNotices_.unite(candidateVolumeStorageNotices);
        if (batteryForecastNear && !batteryForecastNotified_)
            batteryForecastNotified_ = showDesktopNotification(NotificationCategory::Forecast, QStringLiteral("Battery outlook"),
                QStringLiteral("At the recent discharge rate, charge could reach 15% in about %1 minute(s).")
                    .arg(static_cast<int>(std::lround(update.batteryForecast.minutesUntil15Percent))),
                QSystemTrayIcon::Warning);
        if (memoryForecastNear && !memoryForecastNotified_)
            memoryForecastNotified_ = showDesktopNotification(NotificationCategory::Forecast, QStringLiteral("Memory pressure may increase"),
                QStringLiteral("At the recent rate, the five-minute memory average could reach 90% within about %1 minute(s). This is not a leak diagnosis.")
                    .arg(std::max(1, static_cast<int>(std::lround(update.memoryForecast.minutesUntil90Percent)))),
                QSystemTrayIcon::Information);
    }
    if (!rapidStorageDrop) rapidStorageDropNotified_ = false;
    if (!storageForecastNear) storageForecastNotified_ = false;
    activeVolumeStorageNotices_.intersect(currentVolumeStorageNotices);
    if (!batteryForecastNear) batteryForecastNotified_ = false;
    if (!memoryForecastNear) memoryForecastNotified_ = false;
    for (auto it = lastNotifiedSeverity_.cbegin(); it != lastNotifiedSeverity_.cend(); ++it) {
        if (!currentSeverity.contains(it.key()))
            recordActivity(QStringLiteral("A monitored condition cleared"),
                QStringLiteral("%1 no longer matches its sustained finding rule in the available readings. This does not prove a specific intervention caused the change.").arg(it.key()), 8);
    }
    lastNotifiedSeverity_ = std::move(currentSeverity);
    for (auto it = deliveredFindingSeverity_.begin(); it != deliveredFindingSeverity_.end();) {
        if (!lastNotifiedSeverity_.contains(it.key())) it = deliveredFindingSeverity_.erase(it); else ++it;
    }
    hasSeenAnalysis_ = true;
    if (recommendationsView_ && activeLegacyPage_ == 7 && isVisible()) {
        recommendationsView_->setFindings(update.findings,
            QStringLiteral("Nothing needs your attention based on the current measurements."), true,
            hubs_[0]->isDetailed());
    }

    if (auto* healthValue = pages_->findChild<QLabel*>(QStringLiteral("healthValue"))) {
        healthValue->setText(update.health.score
            ? QStringLiteral("%1 / 100").arg(*update.health.score)
            : QStringLiteral("Gathering baseline…"));
    }
    if (auto* healthBody = pages_->findChild<QLabel*>(QStringLiteral("healthBody"))) {
        healthBody->setText(update.health.explanation);
    }
    updateProactiveBriefing();

    if (historyStatus_) {
        if (update.historyAvailable) {
            historyStatus_->setText(QStringLiteral("Stored on this device · up to 30 days · database %1")
                .arg(formatFileSize(update.historySizeBytes)));
            historyStatus_->setToolTip(update.historyPath);
        } else {
            historyStatus_->setText(QStringLiteral("Local history is unavailable; live monitoring continues."));
            historyStatus_->setToolTip(update.historyMessage);
        }
    }

    if (auto* status = pages_->parentWidget()->findChild<QLabel*>(QStringLiteral("statusText"))) {
        status->setToolTip(update.historyAvailable
            ? QStringLiteral("System readings are shown in the live status. Local history is available on this device.")
            : QStringLiteral("System readings are shown in the live status. Local history is unavailable: %1")
                  .arg(update.historyMessage));
    }
    updateMonitoringStatus();
}

void MainWindow::updateProactiveBriefing()
{
    if (proactiveBriefing_)
        proactiveBriefing_->setBriefing(latestAnalysis_, latestSnapshot_, latestEventLogUpdate_,
                                        proactiveSecurityNotices_,
                                        proactiveEventNotices_, proactiveStartupNotices_);
}

void MainWindow::updateProactiveStartupSignals()
{
    if (!preferences_.startupResourceAnalysis) return;
    const QVector<StartupResourceSignal> resourceSignals = appInventoryPage_
        ? appInventoryPage_->proactiveReviewSignals() : QVector<StartupResourceSignal>{};
    QSet<QString> current;
    QStringList notices;
    QHash<QString, QString> newAlerts;
    for (const StartupResourceSignal& signal : resourceSignals) {
        current.insert(signal.key);
        const QString message = QStringLiteral("Startup item %1 has an exact-path process match with sustained resource use: %2. This does not prove it launched the process or caused slow boot; review it only if you don’t need it at sign-in.")
            .arg(signal.name, signal.currentUse);
        if (notices.size() < 2) notices << message;
        if (!activeStartupNotices_.contains(signal.key)) {
            const QDateTime lastNotice = startupNoticeCooldowns_.value(signal.key);
            const QDateTime now = QDateTime::currentDateTimeUtc();
            if (!lastNotice.isValid() || lastNotice.secsTo(now) >= 30 * 60)
                newAlerts.insert(signal.key, message);
        }
    }
    const bool cardChanged = proactiveStartupNotices_ != notices;
    proactiveStartupNotices_ = std::move(notices);
    activeStartupNotices_.intersect(current);
    if (!newAlerts.isEmpty() && notificationsAllowed(NotificationCategory::Startup)) {
        QStringList alertMessages;
        QStringList alertedKeys;
        for (auto it = newAlerts.cbegin(); it != newAlerts.cend() && alertMessages.size() < 2; ++it) {
            alertedKeys << it.key();
            alertMessages << it.value();
        }
        if (showDesktopNotification(NotificationCategory::Startup, QStringLiteral("Startup app resource use is sustained"),
            alertMessages.join(QStringLiteral(" ")).left(650), QSystemTrayIcon::Information)) {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            for (const QString& key : std::as_const(alertedKeys)) {
                startupNoticeCooldowns_.insert(key, now);
                activeStartupNotices_.insert(key);
            }
        }
    }
    if (cardChanged || !newAlerts.isEmpty()) updateProactiveBriefing();
}

void MainWindow::handleProactiveSecurityStatus(const SecurityStatusUpdate& update)
{
    QSet<QString> currentNotices;
    QHash<QString, QString> messageByKey;
    QStringList messages;
    const auto inspectProvider = [&currentNotices, &messageByKey, &messages](const QString& key, const ProviderHealth& provider) {
        if (!provider.available || provider.status != QStringLiteral("Needs attention")) return;
        currentNotices.insert(key);
        const QString message = QStringLiteral("Windows Security Center reports %1 needs attention. Open Security & updates to review.")
            .arg(provider.title.toLower());
        messages << message;
        messageByKey.insert(key, message);
    };
    inspectProvider(QStringLiteral("antivirus"), update.antivirus);
    inspectProvider(QStringLiteral("firewall"), update.firewall);
    inspectProvider(QStringLiteral("automatic-updates"), update.automaticUpdates);
    if (update.restartRequired) {
        currentNotices.insert(QStringLiteral("restart"));
        const QString message = QStringLiteral("Windows reports a pending restart. Save your work and restart when convenient.");
        messages << message;
        messageByKey.insert(QStringLiteral("restart"), message);
    }

    proactiveSecurityNotices_ = messages;
    activeSecurityNotices_.intersect(currentNotices);
    if (notificationsAllowed(NotificationCategory::Security)) {
        QStringList newlyDetected;
        for (const QString& key : currentNotices) {
            if (!activeSecurityNotices_.contains(key)) newlyDetected << messageByKey.value(key);
        }
        if (!newlyDetected.isEmpty()) {
            if (showDesktopNotification(NotificationCategory::Security, QStringLiteral("Windows security status needs a look"),
                newlyDetected.join(QStringLiteral(" ")).left(650), QSystemTrayIcon::Warning)) {
                activeSecurityNotices_.unite(currentNotices);
            }
        }
    }
    hasSeenSecurityStatus_ = true;
    updateProactiveBriefing();
}

void MainWindow::handleProactiveEventLogs(const EventLogUpdate& update)
{
    if (!update.available) return;

    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> currentEvents;
    QHash<QString, QString> messageByKey;
    QStringList notices;
    for (const EventInsight& event : update.insights) {
        if (!event.latestAt.isValid()) continue;
        const qint64 ageMilliseconds = event.latestAt.msecsTo(now);
        if (ageMilliseconds < 0 || ageMilliseconds > 15LL * 60 * 1000) continue;
        const bool actionableSignal = event.severity >= EventSeverity::Error ||
            (event.severity == EventSeverity::Warning && event.occurrenceCount >= 3);
        if (!actionableSignal) continue;

        const QString key = event.channel + QLatin1Char('|') + event.provider + QLatin1Char('|') +
            QString::number(event.eventId) + QLatin1Char('|') +
            (event.likelyApplicationCrash ? event.affectedApplication.trimmed().toCaseFolded() : QString());
        currentEvents.insert(key);
        const QString severity = event.severity == EventSeverity::Critical ? QStringLiteral("critical")
            : event.severity == EventSeverity::Error ? QStringLiteral("error") : QStringLiteral("warning");
        const QString message = event.windowsBugCheck
            ? QStringLiteral("Windows recorded a restart after a bugcheck/blue-screen stop error (System event 1001), %1 occurrence(s). Review the event message for any reported stop code; it does not identify the faulty component.").arg(event.occurrenceCount)
            : event.applicationHang
            ? QStringLiteral("Windows recorded an application-hang event (ID 1002), %1 occurrence(s) in the recent window. The record says the app stopped responding; it does not establish why.").arg(event.occurrenceCount)
            : event.unexpectedShutdown
            ? QStringLiteral("Windows recorded an unexpected-shutdown signal (Event ID %1), %2 occurrence(s) in the recent window. Power loss, a crash, or a forced shutdown can produce this record; it does not identify the cause. Review Event intelligence.")
                .arg(event.eventId).arg(event.occurrenceCount)
            : event.windowsHardwareError
            ? QStringLiteral("Windows recorded a WHEA hardware-error report (Event ID %1), %2 occurrence(s) in the recent window. Review the Windows message and recurrence; this log does not diagnose a component or prove hardware is failing.")
                .arg(event.eventId).arg(event.occurrenceCount)
            : event.windowsStorageEvent
            ? QStringLiteral("Windows recorded a storage-driver event (Event ID %1), %2 occurrence(s) in the recent window. Review the Windows message and recurrence; this event does not diagnose a drive or establish the cause.")
                .arg(event.eventId).arg(event.occurrenceCount)
            : event.likelyApplicationCrash
            ? (event.affectedApplication.isEmpty()
                ? QStringLiteral("Windows logged an application crash report (Application Error, Event ID 1000), %1 occurrence(s) in the recent window. Review Event intelligence; this report alone does not identify the cause.").arg(event.occurrenceCount)
                : QStringLiteral("Windows logged an application crash report for %1 (Event ID 1000), %2 occurrence(s) in the recent window. Review Event intelligence; this report alone does not identify the cause.").arg(event.affectedApplication).arg(event.occurrenceCount))
            : QStringLiteral("Windows recorded a %1 event (ID %2) from %3 in %4; %5 occurrence(s) in the recent window. Review Event intelligence. This is a log signal, not proof of cause.")
                .arg(severity).arg(event.eventId).arg(event.provider, event.channel).arg(event.occurrenceCount);
        messageByKey.insert(key, message);
        if (notices.size() < 2) notices << message;
    }
    proactiveEventNotices_ = notices;

    if (!hasSeenProactiveEvents_) {
        activeProactiveEvents_ = std::move(currentEvents);
        hasSeenProactiveEvents_ = true;
        updateProactiveBriefing();
        return;
    }

    for (const QString& key : std::as_const(currentEvents)) {
        if (!activeProactiveEvents_.contains(key)) pendingProactiveEvents_.insert(key);
    }
    for (auto it = pendingProactiveEvents_.begin(); it != pendingProactiveEvents_.end();) {
        if (!currentEvents.contains(*it)) it = pendingProactiveEvents_.erase(it);
        else ++it;
    }
    activeProactiveEvents_ = std::move(currentEvents);

    const bool globalCooldownElapsed = !lastEventAlertAt_.isValid() || lastEventAlertAt_.secsTo(now) >= 10 * 60;
    if (notificationsAllowed(NotificationCategory::EventLog) && globalCooldownElapsed) {
        QStringList alertMessages;
        QStringList alertedKeys;
        for (const QString& key : std::as_const(pendingProactiveEvents_)) {
            const QDateTime previousAlert = eventAlertCooldowns_.value(key);
            if (previousAlert.isValid() && previousAlert.secsTo(now) < 30 * 60) continue;
            const QString message = messageByKey.value(key);
            if (message.isEmpty()) continue;
            alertMessages << message;
            alertedKeys << key;
            if (alertMessages.size() >= 2) break;
        }
        if (!alertMessages.isEmpty() && showDesktopNotification(NotificationCategory::EventLog, QStringLiteral("Recent Windows errors need a look"),
                alertMessages.join(QStringLiteral(" ")).left(650), QSystemTrayIcon::Warning)) {
            lastEventAlertAt_ = now;
            for (const QString& key : std::as_const(alertedKeys)) {
                eventAlertCooldowns_.insert(key, now);
                pendingProactiveEvents_.remove(key);
            }
        }
    }
    updateProactiveBriefing();
}

void MainWindow::updateHistory(Ausyn::HistoryUpdate update)
{
    historyQueryAvailable_ = update.historyAvailable;
    latestHistoryPoints_ = update.points;
    latestRecommendationOutcomeSummaries_ = update.recommendationOutcomeSummaries;
    recommendationOutcomeHistoryAvailable_ = update.recommendationOutcomeHistoryAvailable;
    if (eventLogPage_) {
        eventLogPage_->setIncidentHistory(update.incidentFindings,
            update.incidentFindingsAvailable, update.incidentFindingsMessage);
    }
    if (historyChart_) {
        historyChart_->setHistory(update.points);
    }
    refreshHistorySummary();
    if (historyStatus_ && !update.historyAvailable && !update.historyMessage.isEmpty()) {
        historyStatus_->setText(QStringLiteral("Could not refresh local history. Live readings are still available."));
        historyStatus_->setToolTip(update.historyMessage);
    } else if (historyStatus_ && update.historyAvailable) {
        historyStatus_->setText(QStringLiteral("%1 stored samples loaded for %2 · local history")
            .arg(update.points.size())
            .arg(historyPeriod_ ? historyPeriod_->currentText() : QStringLiteral("the selected period")));
        historyStatus_->setToolTip(QStringLiteral("Samples are captured periodically and may not represent activity between captures."));
    }
}

void MainWindow::refreshHistorySummary()
{
    if (!historySummary_) return;
    if (!historyQueryAvailable_) {
        historySummary_->setText(QStringLiteral("Local history is not available for this report yet. Ausyn will keep live monitoring available and explain any history error above."));
        return;
    }
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-historyPeriodHours_ * 3600);
    int samples = 0;
    int cpuCount = 0;
    int memoryCount = 0;
    double cpuSum = 0.0;
    double memorySum = 0.0;
    double cpuPeak = 0.0;
    double memoryPeak = 0.0;
    QDateTime firstObserved;
    QDateTime lastObserved;
    for (const HistoryPoint& point : std::as_const(latestHistoryPoints_)) {
        if (!point.capturedAt.isValid() || point.capturedAt < cutoff) continue;
        if (!firstObserved.isValid() || point.capturedAt < firstObserved) firstObserved = point.capturedAt;
        if (!lastObserved.isValid() || point.capturedAt > lastObserved) lastObserved = point.capturedAt;
        samples += point.sampleCount;
        if (point.processorPercent) {
            cpuSum += *point.processorPercent * point.processorSampleCount;
            cpuPeak = std::max(cpuPeak, point.processorPeakPercent.value_or(*point.processorPercent));
            cpuCount += point.processorSampleCount;
        }
        if (point.memoryPercent) {
            memorySum += *point.memoryPercent * point.memorySampleCount;
            memoryPeak = std::max(memoryPeak, point.memoryPeakPercent.value_or(*point.memoryPercent));
            memoryCount += point.memorySampleCount;
        }
    }
    const QString cpuSummary = cpuCount > 0
        ? QStringLiteral("%1% average · %2% highest recorded sample").arg(cpuSum / cpuCount, 0, 'f', 1).arg(cpuPeak, 0, 'f', 1)
        : QStringLiteral("unavailable");
    const QString memorySummary = memoryCount > 0
        ? QStringLiteral("%1% average · %2% highest recorded sample").arg(memorySum / memoryCount, 0, 'f', 1).arg(memoryPeak, 0, 'f', 1)
        : QStringLiteral("unavailable");
    const QString period = historyPeriod_ ? historyPeriod_->currentText() : QStringLiteral("Selected period");
    QString observedWindow = QStringLiteral("No timestamped samples in the selected window");
    if (firstObserved.isValid() && lastObserved.isValid()) {
        const qint64 spanSeconds = std::max<qint64>(0, firstObserved.secsTo(lastObserved));
        QString span;
        if (spanSeconds < 60) span = QStringLiteral("less than 1 minute");
        else if (spanSeconds < 3600) span = QStringLiteral("%1 minute(s)").arg(spanSeconds / 60);
        else if (spanSeconds < 86400) span = QStringLiteral("%1 hour(s)").arg(spanSeconds / 3600);
        else span = QStringLiteral("%1 day(s), %2 hour(s)").arg(spanSeconds / 86400).arg((spanSeconds % 86400) / 3600);
        observedWindow = QStringLiteral("%1 to %2 (%3 elapsed)")
            .arg(firstObserved.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap")),
                 lastObserved.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm ap")), span);
    }
    historySummary_->setText(QStringLiteral("%1 summary  ·  %2 stored samples\nObserved sample window: %3\nProcessor: %4\nMemory: %5\nCurrent active findings: %6. Readings are limited by the history-retention setting (%7 days). Longer periods use five-minute database aggregates to keep the report lightweight; findings are not a diagnosis or complete uptime measure.")
        .arg(period).arg(samples).arg(observedWindow).arg(cpuSummary, memorySummary)
        .arg(latestAnalysis_.findings.size()).arg(preferences_.historyRetentionDays));
}

void MainWindow::exportSystemReport()
{
    if (!historyQueryAvailable_) {
        QMessageBox::information(this, QStringLiteral("Report not ready"),
            QStringLiteral("Ausyn is waiting for local history. Try again after the first readings are available."));
        return;
    }
    refreshHistorySummary();
    QMessageBox privacyReview(this);
    privacyReview.setWindowTitle(QStringLiteral("Review report privacy"));
    privacyReview.setIcon(QMessageBox::Information);
    privacyReview.setText(QStringLiteral("Choose how much device detail to include."));
    privacyReview.setInformativeText(QStringLiteral("Both reports include system readings, findings, history summaries, and nearby Windows event timing. The full report also includes processor and graphics model names, exact Windows build details, drive labels and paths, and recent hardware-change entries."));
    auto* privacyFriendlyButton = privacyReview.addButton(QStringLiteral("Privacy-friendly report"), QMessageBox::AcceptRole);
    auto* fullDetailButton = privacyReview.addButton(QStringLiteral("Include device details"), QMessageBox::ActionRole);
    privacyReview.addButton(QMessageBox::Cancel);
    privacyReview.setDefaultButton(qobject_cast<QPushButton*>(privacyFriendlyButton));
    privacyReview.exec();
    if (privacyReview.clickedButton() != privacyFriendlyButton &&
        privacyReview.clickedButton() != fullDetailButton) return;
    const bool includeDeviceDetails = privacyReview.clickedButton() == fullDetailButton;

    QString selectedFilter;
    const QString chosenPath = QFileDialog::getSaveFileName(this, QStringLiteral("Export Ausyn system report"),
        QStringLiteral("ausyn-report.html"),
        QStringLiteral("HTML report (*.html);;PDF report (*.pdf)"), &selectedFilter);
    if (chosenPath.isEmpty()) return;
    const bool pdfReport = selectedFilter.startsWith(QStringLiteral("PDF"), Qt::CaseInsensitive) ||
        QFileInfo(chosenPath).suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    const QString expectedSuffix = pdfReport ? QStringLiteral("pdf") : QStringLiteral("html");
    QString path = chosenPath;
    const QFileInfo chosenInfo(chosenPath);
    if (chosenInfo.suffix().isEmpty()) {
        path += QLatin1Char('.') + expectedSuffix;
    } else if (chosenInfo.suffix().compare(expectedSuffix, Qt::CaseInsensitive) != 0) {
        path = chosenInfo.dir().filePath(chosenInfo.completeBaseName() + QLatin1Char('.') + expectedSuffix);
    }

    QString findingsHtml;
    if (latestAnalysis_.findings.isEmpty()) {
        findingsHtml = QStringLiteral("<p>No active findings were present at export time.</p>");
    } else {
        for (const Finding& finding : latestAnalysis_.findings) {
            QString confidence = finding.confidence.isEmpty()
                ? QStringLiteral("Confidence not rated")
                : QStringLiteral("Observed-condition confidence: %1").arg(finding.confidence.toHtmlEscaped());
            if (!finding.confidenceBasis.isEmpty())
                confidence += QStringLiteral(" · %1").arg(finding.confidenceBasis.toHtmlEscaped());
            findingsHtml += QStringLiteral("<article class='finding'><h3>%1</h3><p>%2</p><p class='muted'>Evidence: %3</p><p><b>Suggested review:</b> %4</p><p class='muted'>%5</p></article>")
                .arg(finding.title.toHtmlEscaped(), finding.summary.toHtmlEscaped(),
                     finding.evidence.toHtmlEscaped(), finding.recommendation.toHtmlEscaped(), confidence);
        }
    }
    QString eventCorrelationHtml;
    const qint64 eventScanAgeMilliseconds = latestEventLogUpdate_.checkedAt.isValid()
        ? latestEventLogUpdate_.checkedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    if (!latestEventLogUpdate_.available || eventScanAgeMilliseconds < 0 ||
        eventScanAgeMilliseconds > 30LL * 60 * 1000) {
        eventCorrelationHtml = QStringLiteral("<p class='muted'>No recent on-demand Windows event-log scan was available for this report. Ausyn did not start a scan during export.</p>");
    } else {
        QStringList eventRows;
        QSet<QString> includedEvents;
        for (const Finding& finding : latestAnalysis_.findings) {
            if (finding.ruleId != QStringLiteral("processor-sustained-load") &&
                finding.ruleId != QStringLiteral("memory-sustained-pressure") &&
                finding.ruleId != QStringLiteral("concurrent-cpu-memory-pressure") &&
                finding.ruleId != QStringLiteral("memory-rapid-rise")) continue;
            if (!finding.firstSeen.isValid()) continue;
            for (const EventInsight& event : latestEventLogUpdate_.insights) {
                const auto isNear = [&finding](const QDateTime& timestamp) {
                    if (!timestamp.isValid()) return false;
                    const qint64 offsetMilliseconds = finding.firstSeen.msecsTo(timestamp);
                    return offsetMilliseconds >= -120'000 && offsetMilliseconds <= 120'000;
                };
                if (!isNear(event.firstAt) && !isNear(event.latestAt)) continue;
                const QString eventKey = QStringLiteral("%1:%2:%3")
                    .arg(event.provider).arg(event.eventId).arg(event.latestAt.toMSecsSinceEpoch());
                if (includedEvents.contains(eventKey)) continue;
                includedEvents.insert(eventKey);
                const QString severity = event.severity == EventSeverity::Critical ? QStringLiteral("Critical")
                    : event.severity == EventSeverity::Error ? QStringLiteral("Error") : QStringLiteral("Warning");
                eventRows << QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(event.channel.toHtmlEscaped(), event.provider.toHtmlEscaped(),
                         QString::number(event.eventId), severity,
                         event.latestAt.toLocalTime().toString(QStringLiteral("d MMM, h:mm ap")).toHtmlEscaped());
                if (eventRows.size() >= 8) break;
            }
            if (eventRows.size() >= 8) break;
        }
        eventCorrelationHtml = eventRows.isEmpty()
            ? QStringLiteral("<p>No Windows warning or error timing matched the current resource-pressure findings in the recent scan.</p>")
            : QStringLiteral("<p>These records occurred within two minutes of a sustained resource finding. Timing is correlation only and does not establish that an event caused the slowdown.</p><table><thead><tr><th>Channel</th><th>Provider</th><th>ID</th><th>Severity</th><th>Time</th></tr></thead><tbody>%1</tbody></table>")
                .arg(eventRows.join(QString{}));
    }
    const auto appendSnapshotRow = [this, includeDeviceDetails](QStringList& rows, const QString& label, const QString& rawValue) {
        if (!includeDeviceDetails && label == QStringLiteral("Recent hardware profile changes")) return;
        QString value = rawValue;
        if (!includeDeviceDetails && label == QStringLiteral("Operating system"))
            value = latestSnapshot_.operatingSystem.isEmpty() ? QStringLiteral("Windows version not reported") : latestSnapshot_.operatingSystem;
        else if (!includeDeviceDetails && label == QStringLiteral("Processor"))
            value = QStringLiteral("Use %1% · model details omitted")
                .arg(latestSnapshot_.processorUsagePercent ? QString::number(*latestSnapshot_.processorUsagePercent, 'f', 1) : QStringLiteral("not reported"));
        else if (!includeDeviceDetails && label == QStringLiteral("Graphics"))
            value = QStringLiteral("Activity %1 · model details omitted")
                .arg(latestSnapshot_.graphicsUsagePercent ? QStringLiteral("%1%").arg(*latestSnapshot_.graphicsUsagePercent, 0, 'f', 1) : QStringLiteral("not reported"));
        else if (!includeDeviceDetails && label == QStringLiteral("System drive"))
            value = latestSnapshot_.systemVolumeTotalBytes > 0
                ? QStringLiteral("%1 GB free of %2 GB")
                    .arg(static_cast<double>(latestSnapshot_.systemVolumeFreeBytes) / 1'000'000'000.0, 0, 'f', 1)
                    .arg(static_cast<double>(latestSnapshot_.systemVolumeTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
                : QStringLiteral("Not reported");
        rows << QStringLiteral("<tr><th>%1</th><td>%2</td></tr>")
            .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };
    QStringList snapshotRows;
    appendSnapshotRow(snapshotRows, QStringLiteral("Operating system"),
        QStringLiteral("%1 · build %2 · %3")
            .arg(latestSnapshot_.operatingSystem.isEmpty() ? QStringLiteral("Not reported") : latestSnapshot_.operatingSystem,
                 latestSnapshot_.operatingSystemVersion.isEmpty() ? QStringLiteral("Not reported") : latestSnapshot_.operatingSystemVersion,
                 latestSnapshot_.operatingSystemArchitecture.isEmpty() ? QStringLiteral("Not reported") : latestSnapshot_.operatingSystemArchitecture));
    appendSnapshotRow(snapshotRows, QStringLiteral("Processor"),
        QStringLiteral("%1 · %2% · %3 logical processors")
            .arg(latestSnapshot_.processorName.isEmpty() ? QStringLiteral("Not reported") : latestSnapshot_.processorName,
                 latestSnapshot_.processorUsagePercent ? QString::number(*latestSnapshot_.processorUsagePercent, 'f', 1) : QStringLiteral("not reported"),
                 QString::number(latestSnapshot_.logicalProcessorCount)));
    appendSnapshotRow(snapshotRows, QStringLiteral("Memory"),
        latestSnapshot_.memoryTotalBytes > 0
            ? QStringLiteral("%1% · %2 GB used of %3 GB · %4 GB available")
                .arg(latestSnapshot_.memoryUsagePercent ? QString::number(*latestSnapshot_.memoryUsagePercent, 'f', 1) : QStringLiteral("not reported"))
                .arg(static_cast<double>(latestSnapshot_.memoryUsedBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(latestSnapshot_.memoryTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(static_cast<double>(latestSnapshot_.memoryAvailableBytes) / 1'000'000'000.0, 0, 'f', 1)
            : QStringLiteral("Not reported"));
    appendSnapshotRow(snapshotRows, QStringLiteral("Graphics"),
        QStringLiteral("%1 · activity %2")
            .arg(latestSnapshot_.graphicsName.isEmpty() ? QStringLiteral("Not reported") : latestSnapshot_.graphicsName,
                 latestSnapshot_.graphicsUsagePercent ? QStringLiteral("%1%").arg(*latestSnapshot_.graphicsUsagePercent, 0, 'f', 1) : QStringLiteral("not reported")));
    appendSnapshotRow(snapshotRows, QStringLiteral("System drive"), latestSnapshot_.systemVolumeTotalBytes > 0
        ? QStringLiteral("%1 · %2 GB free of %3 GB")
            .arg(latestSnapshot_.systemVolumePath)
            .arg(static_cast<double>(latestSnapshot_.systemVolumeFreeBytes) / 1'000'000'000.0, 0, 'f', 1)
            .arg(static_cast<double>(latestSnapshot_.systemVolumeTotalBytes) / 1'000'000'000.0, 0, 'f', 1)
        : QStringLiteral("Not reported"));
    appendSnapshotRow(snapshotRows, QStringLiteral("Disk transfer rates"),
        QStringLiteral("Read %1 · write %2; rates do not measure response latency")
            .arg(formatRate(latestSnapshot_.diskActivity.readBytesPerSecond),
                 formatRate(latestSnapshot_.diskActivity.writeBytesPerSecond)));
    appendSnapshotRow(snapshotRows, QStringLiteral("Network transfer rates"),
        QStringLiteral("Receive %1 · send %2; rates do not measure connection quality")
            .arg(formatRate(latestSnapshot_.networkReceiveBytesPerSecond),
                 formatRate(latestSnapshot_.networkSendBytesPerSecond)));
    appendSnapshotRow(snapshotRows, QStringLiteral("Battery"), latestSnapshot_.batteryPercent
        ? QStringLiteral("%1% · %2")
            .arg(*latestSnapshot_.batteryPercent)
            .arg(latestSnapshot_.batteryCharging ? QStringLiteral("charging")
                : latestSnapshot_.batteryOnAcPower.value_or(false) ? QStringLiteral("connected to AC")
                : QStringLiteral("on battery"))
        : QStringLiteral("Not reported"));
    const QStringList deviceChanges = HardwareChangeTracker::recentChanges();
    appendSnapshotRow(snapshotRows, QStringLiteral("Recent hardware profile changes"),
        deviceChanges.isEmpty() ? QStringLiteral("None recorded") : deviceChanges.mid(0, 5).join(QStringLiteral("; ")));
    QString snapshotCaptureNote;
    if (latestSnapshot_.capturedAt.isValid()) {
        const qint64 sampleAgeMilliseconds = latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime());
        const qint64 freshLimitSeconds = std::max(30, preferences_.samplingIntervalSeconds * 3);
        const qint64 sampleAgeSeconds = std::max<qint64>(0, sampleAgeMilliseconds / 1000);
        const QString ageText = sampleAgeSeconds < 60
            ? QStringLiteral("%1 second(s) ago").arg(sampleAgeSeconds)
            : QStringLiteral("%1 minute(s) ago").arg((sampleAgeSeconds + 59) / 60);
        const QString freshness = sampleAgeMilliseconds < 0
            ? QStringLiteral("The timestamp is ahead of this PC’s clock; treat these readings as undated.")
            : sampleAgeMilliseconds > freshLimitSeconds * 1000LL
            ? QStringLiteral("This sample may be out of date.")
            : QStringLiteral("This sample is recent.");
        snapshotCaptureNote = QStringLiteral("<p class='muted'>System readings captured %1 (%2). %3</p>")
            .arg(latestSnapshot_.capturedAt.toLocalTime().toString(QStringLiteral("d MMM yyyy, h:mm:ss ap")).toHtmlEscaped(),
                 ageText.toHtmlEscaped(), freshness);
    } else {
        snapshotCaptureNote = QStringLiteral("<p class='muted'>Capture time is unavailable; treat these readings as undated.</p>");
    }
    const QString snapshotHtml = snapshotCaptureNote +
        QStringLiteral("<table><tbody>%1</tbody></table>").arg(snapshotRows.join(QString{}));
    const auto qualityNextStep = [](const MetricQualityRecord& record) {
        switch (record.state) {
        case MetricQualityState::WarmingUp:
            return QStringLiteral("Wait for another sample; some Windows counters need two readings to calculate a rate.");
        case MetricQualityState::Unavailable:
            if (record.metric.contains(QStringLiteral("thermal"), Qt::CaseInsensitive) ||
                record.metric.contains(QStringLiteral("trip point"), Qt::CaseInsensitive))
                return QStringLiteral("This optional Windows sensor may not be exposed by the device; check the manufacturer’s utility if needed.");
            if (record.metric.contains(QStringLiteral("fan"), Qt::CaseInsensitive))
                return QStringLiteral("Windows may not expose fan readings; check the manufacturer’s controls if needed.");
            if (record.metric.contains(QStringLiteral("network"), Qt::CaseInsensitive) ||
                record.metric.contains(QStringLiteral("adapter"), Qt::CaseInsensitive) ||
                record.metric.contains(QStringLiteral("TCP"), Qt::CaseInsensitive) ||
                record.metric.contains(QStringLiteral("UDP"), Qt::CaseInsensitive))
                return QStringLiteral("If a connection was expected, check that Windows shows an active network adapter.");
            if (record.metric.contains(QStringLiteral("process"), Qt::CaseInsensitive))
                return QStringLiteral("Wait for the next process refresh. Ausyn uses only access already available to your Windows account.");
            return QStringLiteral("Windows or this device did not provide the reading. Review the source and details; this alone does not indicate a fault.");
        case MetricQualityState::Stale:
            return QStringLiteral("Wait for the next monitor cycle. If it remains stale, check Ausyn’s monitoring status.");
        case MetricQualityState::Invalid:
            return QStringLiteral("Ausyn excluded this value from analysis. If it keeps recurring, compare it with a Windows or manufacturer reading.");
        case MetricQualityState::Error:
            return QStringLiteral("Review the source details. If the same error persists across samples, restart Ausyn and check again.");
        case MetricQualityState::Estimated:
            return QStringLiteral("Informational: use the source and details to understand what was estimated.");
        case MetricQualityState::Valid:
            return QStringLiteral("No action needed for this reading.");
        }
        return QStringLiteral("Review the source and details for this reading.");
    };
    QStringList qualityRows;
    for (const MetricQualityRecord& record : latestSnapshot_.dataQuality) {
        if (record.state == MetricQualityState::Valid || record.state == MetricQualityState::Estimated)
            continue;
        QString state;
        switch (record.state) {
        case MetricQualityState::Valid: state = QStringLiteral("Available"); break;
        case MetricQualityState::Estimated: state = QStringLiteral("Estimated"); break;
        case MetricQualityState::WarmingUp: state = QStringLiteral("Warming up"); break;
        case MetricQualityState::Unavailable: state = QStringLiteral("Unavailable"); break;
        case MetricQualityState::Stale: state = QStringLiteral("Stale"); break;
        case MetricQualityState::Invalid: state = QStringLiteral("Invalid reading"); break;
        case MetricQualityState::Error: state = QStringLiteral("Collection error"); break;
        }
        qualityRows << QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
            .arg(record.metric.toHtmlEscaped(), state.toHtmlEscaped(), record.source.toHtmlEscaped(),
                 record.detail.toHtmlEscaped(), qualityNextStep(record).toHtmlEscaped());
        if (qualityRows.size() >= 12) break;
    }
    const QString qualityHtml = qualityRows.isEmpty()
        ? QStringLiteral("<p class='muted'>No unavailable, stale, invalid, or failed metrics were listed in the current quality snapshot.</p>")
        : QStringLiteral("<h3>Data availability notes and next steps</h3><table><thead><tr><th>Metric</th><th>State</th><th>Source</th><th>Details</th><th>What you can do</th></tr></thead><tbody>%1</tbody></table>")
            .arg(qualityRows.join(QString{}));
    const QString healthSummaryHtml = latestAnalysis_.health.score
        ? QStringLiteral("<p class='score'>%1<span>/100</span></p><p>%2% signal coverage. %3</p>")
            .arg(*latestAnalysis_.health.score).arg(latestAnalysis_.health.coveragePercent)
            .arg(latestAnalysis_.health.explanation.toHtmlEscaped())
        : QStringLiteral("<p>A combined score is not available yet. %1</p>")
            .arg(latestAnalysis_.health.explanation.toHtmlEscaped());
    QStringList componentRows;
    for (const HealthComponent& component : latestAnalysis_.health.components) {
        componentRows << QStringLiteral("<tr><th>%1</th><td>%2/100</td><td>%3</td><td>%4</td></tr>")
            .arg(component.name.toHtmlEscaped())
            .arg(component.score)
            .arg(component.weight)
            .arg(component.evidence.toHtmlEscaped());
    }
    const QString healthComponentsHtml = componentRows.isEmpty()
        ? QString{}
        : QStringLiteral("<h3>Scored components</h3><table><thead><tr><th>Area</th><th>Score</th><th>Weight</th><th>Evidence</th></tr></thead><tbody>%1</tbody></table>")
            .arg(componentRows.join(QString{}));
    QString batteryTrendHtml = QStringLiteral("<p>No supported Windows battery capacity-health history is available for this device yet.</p>");
    if (!latestAnalysis_.batteryHealthTrend.isEmpty()) {
        QString rows;
        for (const BatteryHealthTrendPoint& point : latestAnalysis_.batteryHealthTrend) {
            rows += QStringLiteral("<tr><td>%1</td><td>%2%</td><td>%3</td></tr>")
                .arg(point.capturedAt.toLocalTime().toString(QStringLiteral("d MMM yyyy")).toHtmlEscaped())
                .arg(point.estimatedHealthPercent, 0, 'f', 1)
                .arg(point.sampleCount);
        }
        batteryTrendHtml = QStringLiteral("<p>Daily means of Windows-reported full-charge/design-capacity estimates. This history is not a battery diagnosis or lifespan forecast.</p><table><thead><tr><th>Day</th><th>Estimate</th><th>Samples averaged</th></tr></thead><tbody>%1</tbody></table>")
            .arg(rows);
    }
    QString batteryForecastHtml = latestAnalysis_.batteryForecast.explanation.isEmpty()
        ? QStringLiteral("<p>A battery outlook is not available from the current history.</p>")
        : QStringLiteral("<p>%1</p>").arg(latestAnalysis_.batteryForecast.explanation.toHtmlEscaped());
    if (latestAnalysis_.batteryForecast.hasEstimate)
        batteryForecastHtml += QStringLiteral("<p>Estimated time to 15% charge: about %1 minutes across %2 minutes of observed history. This is conditional on the current discharge trend.</p>")
            .arg(static_cast<int>(std::lround(latestAnalysis_.batteryForecast.minutesUntil15Percent)))
            .arg(latestAnalysis_.batteryForecast.observedMinutes);
    QString storageOutlookHtml;
    if (latestSnapshot_.volumes.isEmpty()) {
        storageOutlookHtml = QStringLiteral("<p>Windows has not reported any fixed-drive capacity readings yet.</p>");
    } else {
        QString rows;
        int secondaryDriveNumber = 0;
        for (const VolumeSample& volume : latestSnapshot_.volumes) {
            const quint64 freeBytes = std::min(volume.freeBytes, volume.totalBytes);
            const double freePercent = volume.totalBytes == 0 ? 0.0 :
                100.0 * static_cast<double>(freeBytes) / static_cast<double>(volume.totalBytes);
            const bool systemDrive = volume.rootPath.compare(latestSnapshot_.systemVolumePath,
                Qt::CaseInsensitive) == 0;
            const StorageForecast* forecast = systemDrive ? &latestAnalysis_.storageForecast : nullptr;
            if (!systemDrive) {
                const auto found = std::find_if(latestAnalysis_.volumeStorageForecasts.cbegin(),
                    latestAnalysis_.volumeStorageForecasts.cend(), [&volume](const VolumeStorageForecast& item) {
                        return item.rootPath.compare(volume.rootPath, Qt::CaseInsensitive) == 0;
                    });
                if (found != latestAnalysis_.volumeStorageForecasts.cend()) forecast = &found->forecast;
            }
            const QString name = includeDeviceDetails
                ? (volume.label.isEmpty() ? volume.rootPath : QStringLiteral("%1 (%2)").arg(volume.label, volume.rootPath))
                : systemDrive ? QStringLiteral("System drive")
                              : QStringLiteral("Other fixed drive %1").arg(++secondaryDriveNumber);
            const QString trend = forecast && !forecast->explanation.isEmpty()
                ? forecast->explanation : QStringLiteral("Collecting local daily history.");
            const QString eta = forecast && forecast->hasEstimate
                ? QStringLiteral("About %1 day(s)").arg(static_cast<int>(std::lround(forecast->daysUntilTenPercent)))
                : QStringLiteral("Not available yet");
            const QString observed = forecast && forecast->observedDays > 0
                ? QString::number(forecast->observedDays) : QStringLiteral("—");
            rows += QStringLiteral("<tr><td>%1</td><td>%2 GB free (%3%)</td><td>%4</td><td>%5</td><td>%6</td></tr>")
                .arg(name.toHtmlEscaped())
                .arg(static_cast<double>(freeBytes) / 1'000'000'000.0, 0, 'f', 1)
                .arg(freePercent, 0, 'f', 1)
                .arg(trend.toHtmlEscaped())
                .arg(eta.toHtmlEscaped())
                .arg(observed.toHtmlEscaped());
        }
        storageOutlookHtml = QStringLiteral("<p>Free-space forecasts project measured daily trends to the 10% attention threshold. They are conditional estimates, not promises, and do not identify which files or apps used space.</p><table><thead><tr><th>Drive</th><th>Current free space</th><th>Observed trend</th><th>Estimated time to 10%</th><th>History days</th></tr></thead><tbody>%1</tbody></table>")
            .arg(rows);
    }
    const QString html = QStringLiteral(
        "<!doctype html><html><head><meta charset='utf-8'><title>Ausyn system report</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font:15px 'Segoe UI',sans-serif;max-width:920px;margin:40px auto;padding:0 20px;color:#202a38;line-height:1.6;background:#f9fafc}"
        "h1{color:#343078;margin-bottom:4px}h2{color:#343078;margin-top:0}h3{margin-bottom:6px}header{border-bottom:1px solid #dce2ed;padding-bottom:16px}section{padding:20px 24px;background:white;border:1px solid #e1e6ef;border-radius:14px;margin:18px 0;box-shadow:0 3px 12px #17243a0a}"
        ".muted{color:#59677a}.score{font-size:34px;font-weight:700;color:#343078;margin:0}.score span{font-size:15px;color:#59677a}table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:9px;border-bottom:1px solid #e5e9f0;vertical-align:top}th{width:28%;color:#46546a}.finding{border-top:1px solid #e5e9f0;padding:12px 0}.finding:first-child{border-top:0}@media print{body{margin:0;background:white;padding:0}section{box-shadow:none;break-inside:avoid}}</style></head><body><header><h1>Ausyn system report</h1>"
        "<p class='muted'>Generated %1 · History period and device detail choice: %2. Review before sharing.</p></header>"
        "<section><h2>1. Health overview</h2>%3<p>%4</p>%5</section>"
        "<section><h2>2. Current system snapshot</h2>%6%7</section>"
        "<section><h2>3. Findings and suggested review</h2>%8<h3>Nearby Windows event timings</h3>%9</section>"
        "<section><h2>4. Storage outlooks</h2>%10</section>"
        "<section><h2>5. Battery outlook and capacity trend</h2>%11%12</section>"
        "<footer class='muted'><p>Ausyn stores performance history locally. This report excludes chat history and per-process names/readings. Sample averages can miss activity between captures; findings describe observed conditions and do not prove causes.</p></footer>"
        "</body></html>")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("d MMM yyyy, h:mm ap")).toHtmlEscaped(),
             QStringLiteral("%1 · device model/build/path details %2")
                .arg(historyPeriod_ ? historyPeriod_->currentText().toHtmlEscaped() : QStringLiteral("selected period"),
                     includeDeviceDetails ? QStringLiteral("included") : QStringLiteral("omitted")),
             healthSummaryHtml,
             historySummary_->text().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")),
             healthComponentsHtml, snapshotHtml, qualityHtml, findingsHtml, eventCorrelationHtml,
             storageOutlookHtml, batteryForecastHtml, batteryTrendHtml);
    QString error;
    if (pdfReport) {
        QByteArray pdfBytes;
        QBuffer pdfBuffer(&pdfBytes);
        if (!pdfBuffer.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, QStringLiteral("Report export failed"),
                QStringLiteral("Ausyn could not prepare the PDF report."));
            return;
        }
        {
            QPdfWriter writer(&pdfBuffer);
            writer.setTitle(QStringLiteral("Ausyn system report"));
            writer.setCreator(QStringLiteral("Ausyn"));
            writer.setResolution(96);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins(QMarginsF(17.0, 17.0, 17.0, 17.0), QPageLayout::Millimeter);
            const QRect pageArea = writer.pageLayout().paintRectPixels(writer.resolution());
            const qreal footerHeight = 24.0;
            QTextDocument document;
            document.setDefaultFont(QFont(QStringLiteral("Segoe UI"), 10));
            document.setHtml(html);
            const QSizeF documentPageSize(pageArea.width(), std::max(1, pageArea.height() - static_cast<int>(footerHeight)));
            document.setPageSize(documentPageSize);
            QPainter painter(&writer);
            if (!painter.isActive()) {
                error = QStringLiteral("Ausyn could not start the PDF renderer.");
            } else {
                const int pageCount = std::max(1, document.pageCount());
                for (int page = 0; page < pageCount; ++page) {
                    if (page > 0 && !writer.newPage()) {
                        error = QStringLiteral("Ausyn could not finish a page in the PDF report.");
                        break;
                    }
                    painter.save();
                    painter.translate(pageArea.topLeft());
                    const QRectF source(0.0, page * documentPageSize.height(),
                                        documentPageSize.width(), documentPageSize.height());
                    document.drawContents(&painter, source);
                    painter.setPen(QColor(QStringLiteral("#dce2ed")));
                    painter.drawLine(QPointF(0.0, documentPageSize.height() + 4.0),
                                     QPointF(documentPageSize.width(), documentPageSize.height() + 4.0));
                    painter.setPen(QColor(QStringLiteral("#59677a")));
                    painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
                    painter.drawText(QRectF(0.0, documentPageSize.height() + 7.0,
                                             documentPageSize.width(), 13.0),
                                     Qt::AlignRight | Qt::AlignVCenter,
                                     QStringLiteral("Ausyn system report · Page %1 of %2").arg(page + 1).arg(pageCount));
                    painter.restore();
                }
                painter.end();
            }
        }
        pdfBuffer.close();
        if (error.isEmpty() && pdfBytes.isEmpty())
            error = QStringLiteral("The PDF renderer returned an empty report.");
        if (error.isEmpty()) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(pdfBytes) != pdfBytes.size() || !file.commit())
                error = file.errorString();
        }
    } else {
        QSaveFile file(path);
        const QByteArray reportBytes = html.toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
            file.write(reportBytes) != reportBytes.size() || !file.commit())
            error = file.errorString();
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Report export failed"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("System report saved to %1").arg(path), 8000);
}

void MainWindow::sendAssistantMessage()
{
    if (assistantClient_ && assistantClient_->isBusy()) {
        statusBar()->showMessage(QStringLiteral("Ausyn is finishing your current reply. Please wait a moment."), 4000);
        return;
    }
    const QString question = chatInput_ ? chatInput_->text().trimmed() : QString();
    if (question.isEmpty()) return;
    const QString localQuestion = question.toLower().trimmed();
    const bool companionQuestion = QStringList{QStringLiteral("brief me"), QStringLiteral("what are you doing"), QStringLiteral("what are you doing?"),
        QStringLiteral("what did you do"), QStringLiteral("what did you do?"), QStringLiteral("what have you learned"), QStringLiteral("what have you learned?"), QStringLiteral("your background decisions")}.contains(localQuestion);
    if (companionQuestion) {
        chatTranscript_->append(QStringLiteral("<p><b style='color:#9aa5b8'>You</b><br>%1</p>").arg(question.toHtmlEscaped()));
        chatInput_->clear();
        const qint64 age = latestSnapshot_.capturedAt.isValid() ? latestSnapshot_.capturedAt.secsTo(QDateTime::currentDateTime()) : -1;
        const QString freshness = !preferences_.monitoringEnabled ? QStringLiteral("Monitoring is paused. This is recorded context.\n\n") : age < 0 || age > std::max(15, latestSnapshot_.samplingIntervalSeconds * 3) ? QStringLiteral("Waiting for fresh readings. This is recorded context.\n\n") : QString{};
        pendingBriefingFollowup_.reset(); chatMessages_.append({true, question, QDateTime::currentDateTime()});
        completeAssistantReply(question, freshness + (localQuestion.contains(QStringLiteral("learned")) ? companion_.profileSummary() : companion_.digest()), false, QString());
        return;
    }
    QVector<QString> previousQuestions;
    for (const auto& message : chatMessages_) if (message.fromUser) previousQuestions.append(message.text);
    const QString contextQuestion = WorkloadCoach::resolveFollowup(question, previousQuestions);
    const QVector<ChatMessage> requestConversation = chatMessages_;
    const bool workQuestion = preferences_.workloadAwarenessEnabled &&
        (question.contains(QStringLiteral("keep using"), Qt::CaseInsensitive) || question.contains(QStringLiteral("keep this app"), Qt::CaseInsensitive) ||
         question.contains(QStringLiteral("current task"), Qt::CaseInsensitive) || question.contains(QStringLiteral("without closing"), Qt::CaseInsensitive) ||
         question.contains(QStringLiteral("work session"), Qt::CaseInsensitive));
    const QString workloadContext = !preferences_.workloadAwarenessEnabled || !preferences_.monitoringEnabled || !workloadCoach_.fresh(QDateTime::currentDateTime()) ? QString{}
        : preferences_.shareProcessNames ? workloadCoach_.summary() + QLatin1Char(' ') + workloadCoach_.plan()
        : workloadCoach_.confirmed() ? QStringLiteral("The user has chosen an app to keep running this session. Suggest relief around the task; its name is withheld.")
        : QString{};
    const std::optional<BriefingFollowup> briefingFollowup = pendingBriefingFollowup_;
    pendingBriefingFollowup_.reset();
    const qint64 sampleAgeMilliseconds = latestSnapshot_.capturedAt.isValid()
        ? latestSnapshot_.capturedAt.msecsTo(QDateTime::currentDateTime()) : -1;
    const qint64 sampleAgeSeconds = sampleAgeMilliseconds >= 0
        ? (sampleAgeMilliseconds + 999) / 1000 : -1;
    const qint64 staleAfterSeconds = std::max(10, preferences_.samplingIntervalSeconds * 3);
    const bool telemetryFresh = sampleAgeMilliseconds >= 0 &&
        sampleAgeMilliseconds <= staleAfterSeconds * 1000LL;
    const bool historyQuestion = AssistantEngine::requiresLocalHistory(contextQuestion);
    const bool eventLogQuestion = AssistantEngine::requiresLocalEventLogs(contextQuestion);
    const bool baselineQuestion = AssistantEngine::requiresLocalBaselineComparison(contextQuestion);
    const bool monitoringStatusQuestion = AssistantEngine::requiresLocalMonitoringStatus(contextQuestion);
    const bool asksGameReadiness = AssistantEngine::requiresLocalGamingReadiness(contextQuestion);
    const bool gameCatalogMatched = gamingPage_ && asksGameReadiness &&
        gamingPage_->selectCatalogProfileForQuestion(question);
    const bool searchForUnknownGame = asksGameReadiness && !gameCatalogMatched &&
        preferences_.webSearchEnabled;
    const bool localEvidenceQuestion = workQuestion || briefingFollowup.has_value() || historyQuestion ||
        eventLogQuestion ||
        AssistantEngine::requiresLocalSlowdownAssessment(contextQuestion) ||
        AssistantEngine::requiresLocalThermalAssessment(contextQuestion) ||
        AssistantEngine::requiresLocalEventCorrelation(contextQuestion, latestAnalysis_, latestEventLogUpdate_) ||
        baselineQuestion ||
        monitoringStatusQuestion ||
        AssistantEngine::requiresLocalBatteryHealthHistory(contextQuestion) ||
        AssistantEngine::requiresLocalForecast(contextQuestion) ||
        AssistantEngine::requiresLocalProcessQuestion(contextQuestion, latestSnapshot_) ||
        AssistantEngine::requiresLocalProcessBreakdown(contextQuestion) ||
        AssistantEngine::requiresLocalStorageReliability(contextQuestion) ||
        AssistantEngine::requiresLocalPastRecommendationOutcomes(contextQuestion) ||
        AssistantEngine::requiresLocalSystemCheck(contextQuestion) ||
        QRegularExpression(QStringLiteral("\\b(memory|ram|cpu|gpu|battery|network|storage|graphics|processor)\\b"), QRegularExpression::CaseInsensitiveOption).match(contextQuestion).hasMatch() ||
        (asksGameReadiness && !searchForUnknownGame) || gameCatalogMatched;
    UserPreferences requestPreferences = preferences_;
    const bool casualConversation = isCasualConversation(question);
    const bool localOnly = briefingFollowup.has_value() || historyQuestion || eventLogQuestion || baselineQuestion || monitoringStatusQuestion || gameCatalogMatched ||
        AssistantEngine::requiresLocalForecast(contextQuestion) || AssistantEngine::requiresLocalPastRecommendationOutcomes(contextQuestion) ||
        AssistantEngine::requiresLocalBatteryHealthHistory(contextQuestion) || AssistantEngine::requiresLocalStorageReliability(contextQuestion);
    if (localOnly || (localEvidenceQuestion && !preferences_.cloudSystemExplanations)) requestPreferences.cloudAiEnabled = false;
    if (uiCheck_) requestPreferences.cloudAiEnabled = false;
    bool useWebSearch = preferences_.webSearchEnabled && requestPreferences.cloudAiEnabled &&
        !localEvidenceQuestion && !casualConversation;
    if (requestPreferences.cloudAiEnabled && !preferences_.encryptedApiKey.isEmpty() && (useWebSearch || !preferences_.apiModel.trimmed().isEmpty()) && (!localEvidenceQuestion || telemetryFresh)) {
        QMessageBox preview(this);
        preview.setWindowTitle(QStringLiteral("Review cloud AI request"));
        preview.setIcon(QMessageBox::Information);
        preview.setText(useWebSearch
            ? QStringLiteral("Search the web and send this question plus the listed relevant device facts to OpenAI using gpt-5-search-api?")
            : QStringLiteral("Send this question and the listed device facts to %1 using model %2?")
                .arg(preferences_.apiEndpoint, preferences_.apiModel.isEmpty() ? QStringLiteral("(not configured)") : preferences_.apiModel));
        preview.setInformativeText(useWebSearch
            ? QStringLiteral("API model and search usage may be charged. The detailed preview lists the question, selected facts and any recent conversation you opted to include. Search sources are shown with citations.")
            : QStringLiteral("Review the exact question, device facts, session context and optional recent conversation below. Startup inventory and file contents are excluded. The provider's data handling terms apply."));
        preview.setDetailedText(AssistantClient::dataPreview(contextQuestion, latestSnapshot_, latestAnalysis_, preferences_, requestConversation, workloadContext));
        preview.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        preview.setDefaultButton(QMessageBox::No);
        if (preview.exec() != QMessageBox::Yes) {
            requestPreferences.cloudAiEnabled = false;
            useWebSearch = false;
        }
    }
    const QString safeQuestion = question.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    chatTranscript_->append(QStringLiteral("<p><b style='color:#9aa5b8'>You</b><br>%1</p>").arg(safeQuestion));
    chatInput_->clear();
    if (localEvidenceQuestion && (!telemetryFresh || !preferences_.monitoringEnabled) && !historyQuestion && !eventLogQuestion && !monitoringStatusQuestion && !briefingFollowup) {
        const QString response = !preferences_.monitoringEnabled ? QStringLiteral("Monitoring is paused. Resume it on the dashboard or in Settings before I assess live system conditions.")
            : sampleAgeMilliseconds < 0 && latestSnapshot_.capturedAt.isValid()
            ? QStringLiteral("My latest sample timestamp is ahead of this PC’s clock, so I can’t safely treat it as live. Check the Windows clock and wait for the next valid sample.")
            : sampleAgeMilliseconds < 0
            ? QStringLiteral("I haven’t received a system reading yet, so I can’t give you a live diagnosis. Give Ausyn a moment to connect to its local monitor, then ask me again.")
            : QStringLiteral("My latest system reading is %1 seconds old, so I can’t safely describe it as live or send it to cloud AI. The status at the top will return to Monitoring live when fresh readings resume. You can still review saved history while the monitor catches up.").arg(sampleAgeSeconds);
        chatMessages_.append(ChatMessage{true, question, QDateTime::currentDateTime()});
        if (preferences_.saveConversationsLocally) {
            QString error;
            if (!UserPreferencesStore::saveConversations(chatMessages_, &error))
                QMessageBox::warning(this, QStringLiteral("Conversation history"), error);
        }
        completeAssistantReply(question, response, false, QString());
        return;
    }
    QString response = briefingFollowup
        ? AssistantEngine::explainBriefingItem(briefingFollowup->category, briefingFollowup->title,
            briefingFollowup->summary, briefingFollowup->evidence, briefingFollowup->nextStep,
            latestSnapshot_, preferences_)
        : workQuestion ? workloadCoach_.summary() + QStringLiteral("\n\n") + workloadCoach_.plan() + QStringLiteral("\n\n") + companion_.status() + QStringLiteral("\nBackground guidance follows stable app use automatically. Details offers an explicit Keep override and a comparison check. Automatic OS actions require the standing executable rules in Settings.")
        : AssistantEngine::reply(contextQuestion, latestSnapshot_, latestAnalysis_, latestAppInventory_,
            preferences_, latestHistoryPoints_, historyQueryAvailable_, historyPeriodHours_,
            gamingPage_ ? gamingPage_->currentAssessment() : GameReadiness{}, latestEventLogUpdate_,
            latestRecommendationOutcomeSummaries_, recommendationOutcomeHistoryAvailable_);
    if (!briefingFollowup && !workQuestion && localEvidenceQuestion && preferences_.workloadAwarenessEnabled && (workloadCoach_.confirmed() || contextQuestion != question))
        response += QStringLiteral("\n\nYour current task: ") + workloadCoach_.summary() + QLatin1Char(' ') + workloadCoach_.plan();
    chatMessages_.append(ChatMessage{true, question, QDateTime::currentDateTime()});
    if (preferences_.saveConversationsLocally) {
        QString error;
        if (!UserPreferencesStore::saveConversations(chatMessages_, &error))
            QMessageBox::warning(this, QStringLiteral("Conversation history"), error);
    }
    if (assistantClient_ && assistantClient_->isBusy()) {
        chatTranscript_->append(QStringLiteral("<p><i style='color:#8995aa'>One answer is still on its way. I’ll keep this question in this session.</i></p>"));
        return;
    }
    chatInput_->setEnabled(false);
    if (auto* send = legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantSend"))) {
        send->setEnabled(false);
        send->setText(QStringLiteral("Thinking…"));
    }
    assistantClient_->ask(contextQuestion, response, latestSnapshot_, latestAnalysis_, requestPreferences, useWebSearch, requestConversation, workloadContext);
    if (auto* cancel = legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantCancel")))
        cancel->setVisible(assistantClient_->isBusy());
}

void MainWindow::completeAssistantReply(QString question, QString response, bool usedCloud, QString note)
{
    Q_UNUSED(question)
    const QString safeResponse = assistantMessageHtml(response);
    const QString label = usedCloud ? QStringLiteral("Ausyn · cloud") : QStringLiteral("Ausyn");
    chatTranscript_->append(QStringLiteral("<p><b style='color:#b8b6ff'>%1</b><br>%2</p>").arg(label, safeResponse));
    if (!note.isEmpty()) {
        chatTranscript_->append(QStringLiteral("<p style='color:#8995aa;font-size:11px'>%1</p>").arg(note.toHtmlEscaped()));
    }
    chatMessages_.append(ChatMessage{false, response, QDateTime::currentDateTime()});
    if (chatMessages_.size() > 100) chatMessages_.erase(chatMessages_.begin(), chatMessages_.end() - 100);
    if (preferences_.saveConversationsLocally) {
        QString error;
        if (!UserPreferencesStore::saveConversations(chatMessages_, &error))
            QMessageBox::warning(this, QStringLiteral("Conversation history"), error);
    }
    chatInput_->setEnabled(true);
    if (auto* cancel = legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantCancel"))) cancel->hide();
    if (auto* send = legacyPage(1)->findChild<QPushButton*>(QStringLiteral("assistantSend"))) {
        send->setEnabled(true);
        send->setText(QStringLiteral("Send"));
    }
}

void MainWindow::savePreferences()
{
    if (!preferencesReady_) return;
    preferences_.autonomousCompanionEnabled = companionEnabled_->isChecked();
    preferences_.rememberActivity = rememberActivity_->isChecked();
    preferences_.automaticReliefEnabled = automaticReliefEnabled_->isChecked();
    preferences_.smartAttentionEnabled = smartAttention_->isChecked();
    preferences_.attentionBudget = attentionBudget_->currentData().toInt();
    preferences_.workSessionMinutes = workSessionMinutes_->currentData().toInt();
    preferences_.workloadAwarenessEnabled = workloadAwareness_->isChecked();
    preferences_.cloudConversationContext = cloudConversationContext_->isChecked();
    preferences_.cloudSystemExplanations = cloudSystemExplanations_->isChecked();
    if (!casualTone_ || !technicalDetail_ || !appearance_ || !showQuickStartGuide_ || !adaptiveSampling_ || !saveConversations_ || !retentionDays_ || !desktopNotifications_ || !startWithWindows_ || !resourceAlertSensitivity_ || !webSearch_ ||
        !systemFindingAlerts_ || !forecastAlerts_ || !eventLogAlerts_ || !startupAlerts_ || !securityAlerts_ ||
        !quietHoursEnabled_ || !quietHoursStart_ || !quietHoursEnd_) return;
    const bool wasMonitoring = preferences_.monitoringEnabled;
    preferences_.monitoringEnabled = monitoringEnabled_->isChecked();
    preferences_.animationsEnabled = animationsEnabled_->isChecked();
    preferences_.closeToTray = closeToTray_->isChecked();
    preferences_.startMinimized = startMinimized_->isChecked();
    preferences_.proactiveBriefingEnabled = proactiveBriefingEnabled_->isChecked();
    preferences_.monitorGameLaunches = monitorGameLaunches_->isChecked();
    preferences_.proactiveEventScans = proactiveEventScans_->isChecked();
    preferences_.proactiveSecurityScans = proactiveSecurityScans_->isChecked();
    preferences_.startupResourceAnalysis = startupResourceAnalysis_->isChecked();
    preferences_.alertsOnlyInBackground = alertsOnlyInBackground_->isChecked();
    preferences_.ausynPopupCards = alertChannel_->currentData().toBool();
    preferences_.backgroundScanMinutes = backgroundScanMinutes_->currentData().toInt();
    preferences_.notificationCooldownMinutes = notificationCooldown_->currentData().toInt();
    preferences_.landingPage = landingPage_->currentData().toInt();
    preferences_.casualTone = casualTone_->isChecked();
    preferences_.technicalDetail = technicalDetail_->isChecked();
    preferences_.showQuickStartGuide = showQuickStartGuide_->isChecked();
    preferences_.lightTheme = appearance_->currentData().toBool();
    preferences_.saveConversationsLocally = saveConversations_->isChecked();
    preferences_.desktopNotificationsEnabled = desktopNotifications_->isChecked();
    preferences_.startWithWindows = startWithWindows_->isChecked();
    preferences_.notifySystemFindings = systemFindingAlerts_->isChecked();
    preferences_.notifyForecasts = forecastAlerts_->isChecked();
    preferences_.notifyEventLogs = eventLogAlerts_->isChecked();
    preferences_.notifyStartupApps = startupAlerts_->isChecked();
    preferences_.notifySecurity = securityAlerts_->isChecked();
    preferences_.resourceAlertSensitivity = std::clamp(resourceAlertSensitivity_->value(), 1, 5);
    preferences_.quietHoursEnabled = quietHoursEnabled_->isChecked();
    preferences_.quietHoursStartMinute = quietHoursStart_->time().hour() * 60 + quietHoursStart_->time().minute();
    preferences_.quietHoursEndMinute = quietHoursEnd_->time().hour() * 60 + quietHoursEnd_->time().minute();
    preferences_.cloudAiEnabled = cloudAi_->isChecked();
    preferences_.webSearchEnabled = webSearch_->isChecked();
    preferences_.shareProcessNames = shareProcessNames_->isChecked();
    preferences_.historyRetentionDays = retentionDays_->currentData().toInt();
    preferences_.samplingIntervalSeconds = samplingInterval_->currentData().toInt();
    preferences_.adaptiveSamplingEnabled = adaptiveSampling_->isChecked();
    if (proactiveBriefing_) {
        preferences_.dismissedBriefingKeys = proactiveBriefing_->dismissedKeys();
        preferences_.snoozedBriefingKeys = proactiveBriefing_->snoozedKeys();
    }
    preferences_.apiEndpoint = apiEndpoint_->text().trimmed();
    preferences_.apiModel = apiModel_->text().trimmed();
    if (!apiKey_->text().isEmpty()) {
        QString keyError;
        const QByteArray protectedKey = UserPreferencesStore::protectApiKey(apiKey_->text(), &keyError);
        if (protectedKey.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("API key was not saved"), keyError);
            return;
        }
        preferences_.encryptedApiKey = protectedKey;
    }
    if (telemetry_) telemetry_->setRetentionDays(preferences_.historyRetentionDays);
    if (telemetry_) telemetry_->setSamplingIntervalSeconds(preferences_.samplingIntervalSeconds);
    if (telemetry_) telemetry_->setAdaptiveSamplingEnabled(preferences_.adaptiveSamplingEnabled);
    if (agentHealthPage_) agentHealthPage_->setSamplingIntervalSeconds(preferences_.samplingIntervalSeconds);
    QString error;
    const bool settingsSaved = UserPreferencesStore::save(preferences_, &error);
    if (!settingsSaved) {
        QMessageBox::warning(this, QStringLiteral("Settings could not be saved"), error);
    } else if (preferences_.saveConversationsLocally && !chatMessages_.isEmpty()) {
        const bool saved = UserPreferencesStore::saveConversations(chatMessages_, &error);
        if (!saved) QMessageBox::warning(this, QStringLiteral("Conversation history"), error);
    }
    if (settingsSaved && apiKey_ && !apiKey_->text().isEmpty()) {
        apiKey_->clear();
        apiKeyStatus_->setText(QStringLiteral("API key protected for this Windows account."));
    }
    applyBehaviorPreferences();
    if (wasMonitoring != preferences_.monitoringEnabled) {
        recentLoadSamples_.clear();
        earlyPressureActive_ = false;
        recordActivity(preferences_.monitoringEnabled ? QStringLiteral("Monitoring resumed") : QStringLiteral("Monitoring paused"),
            preferences_.monitoringEnabled ? QStringLiteral("Windows telemetry collection and enabled background checks resumed.")
                : QStringLiteral("Periodic telemetry, event and security checks paused. Earlier readings remain visible with their timestamps."), 9);
    }
    updateDashboardReadout();
}

bool MainWindow::notificationsAllowed(NotificationCategory category) const
{
    if (!preferences_.monitoringEnabled) return false;
    if (!preferences_.desktopNotificationsEnabled) return false;
    switch (category) {
    case NotificationCategory::SystemFinding:
        if (!preferences_.notifySystemFindings) return false;
        break;
    case NotificationCategory::Forecast:
        if (!preferences_.notifyForecasts) return false;
        break;
    case NotificationCategory::EventLog:
        if (!preferences_.notifyEventLogs) return false;
        break;
    case NotificationCategory::Startup:
        if (!preferences_.notifyStartupApps) return false;
        break;
    case NotificationCategory::Security:
        if (!preferences_.notifySecurity) return false;
        break;
    }
    return !AlertPolicy::quietNow(preferences_, QDateTime::currentDateTime());
}

bool MainWindow::showDesktopNotification(NotificationCategory category, const QString& title,
                                         const QString& body, int icon, bool test, int resourceOverride, const QString& occurrenceKey)
{
    const QString key = QString::number(static_cast<int>(category)) + (occurrenceKey.isEmpty() ? title : occurrenceKey);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime previous = notificationCooldowns_.value(key);
    if (!test && icon != QSystemTrayIcon::Critical && previous.isValid() &&
        previous.secsTo(now) < preferences_.notificationCooldownMinutes * 60) return false;
    const int resource = resourceOverride != -100 ? resourceOverride : category == NotificationCategory::Security ? -14
        : category == NotificationCategory::EventLog ? -12 : category == NotificationCategory::Startup ? -13 : notificationResource(title, body);
    showAdviceBanner(title, body, resource);
    const QWidget* active = QApplication::activeWindow();
    const bool appHasFocus = active && (active == this || isAncestorOf(active));
    const AlertPresentation delivery = AlertPolicy::presentation(preferences_, category, !test && appHasFocus,
        trayIcon_ && trayIcon_->isVisible() && QSystemTrayIcon::supportsMessages(), now, desktopSnoozedUntil_);
    if (!delivery.ownPopup && !delivery.windowsRequest) {
        if (suppressedAlertReasons_.value(key) != delivery.reason) {
            suppressedAlertReasons_.insert(key, delivery.reason);
            if (suppressedAlertReasons_.size() > 64) suppressedAlertReasons_.clear();
            recordActivity(QStringLiteral("Advice available: %1").arg(title), QStringLiteral("%1. %2").arg(delivery.reason, body), 9);
        }
        if (alertDeliveryStatus_) alertDeliveryStatus_->setText(QStringLiteral("Latest alert status: %1").arg(delivery.reason));
        return false;
    }
    // Coalesce nearby reports about the same resource. An escalation can still interrupt.
    // Rejected presentations stay eligible in the caller, so this does not lose warnings.
    if (!test && lastDesktopAlertAt_.isValid() && lastDesktopAlertAt_.secsTo(now) < 30 &&
        icon <= lastDesktopAlertIcon_ && (resource == lastDesktopAlertResource_ ||
            (notificationPopup_ && notificationPopup_->isVisible() && icon < lastDesktopAlertIcon_))) return false;
    suppressedAlertReasons_.remove(key);
    const bool urgent = icon == QSystemTrayIcon::Critical ||
        (category == NotificationCategory::Security && icon >= QSystemTrayIcon::Warning) ||
        (icon == QSystemTrayIcon::Warning && latestSnapshot_.memoryUsagePercent.value_or(0) >= 95);
    if (!test && preferences_.smartAttentionEnabled && !companion_.canInterrupt(now, urgent)) {
        companion_.defer(key, title);
        if (alertDeliveryStatus_) alertDeliveryStatus_->setText(QStringLiteral("Ordinary alert grouped in your background briefing; attention budget reached."));
        return false;
    }
    if (delivery.ownPopup) popupWindow()->present(title, body, resource, icon == QSystemTrayIcon::Critical, preferences_.lightTheme, false, occurrenceKey.startsWith(QStringLiteral("workload/")));
    else if (!uiCheck_) trayIcon_->showMessage(title, body.left(240), static_cast<QSystemTrayIcon::MessageIcon>(icon), 8000);
    notificationCooldowns_.insert(key, now);
    lastDesktopAlertAt_ = now; lastDesktopAlertIcon_ = icon; lastDesktopAlertResource_ = resource;
    lastDesktopAlertOccurrenceKey_ = occurrenceKey;
    if (!test) companion_.interrupted(now, urgent);
    if (notificationCooldowns_.size() > 200) {
        for (auto it = notificationCooldowns_.begin(); it != notificationCooldowns_.end();) {
            if (it.value().secsTo(now) > 24 * 3600) it = notificationCooldowns_.erase(it);
            else ++it;
        }
    }
    lastNotificationCategory_ = category;
    recordActivity(delivery.ownPopup ? QStringLiteral("Ausyn card shown: %1").arg(title) : QStringLiteral("Windows alert requested: %1").arg(title), body,
        category == NotificationCategory::Forecast ? 10 : category == NotificationCategory::EventLog ? 11 :
        category == NotificationCategory::Startup ? 12 : category == NotificationCategory::Security ? 13 : 6);
    if (alertDeliveryStatus_) alertDeliveryStatus_->setText(delivery.ownPopup ? QStringLiteral("Latest alert: Ausyn card shown") : QStringLiteral("Latest alert: requested from Windows; Windows controls final delivery"));
    return true;
}

void MainWindow::refreshEventLogs()
{
    if (uiCheck_) return;
    if (!eventLogWatcher_ || eventLogWatcher_->isRunning()) return;
    if (eventLogPage_) eventLogPage_->setBusy(true);
    eventLogWatcher_->setFuture(QtConcurrent::run(&EventLogCollector::collect));
}

void MainWindow::refreshProactiveEventLogs()
{
    if (uiCheck_) return;
    if (!preferences_.monitoringEnabled || !preferences_.proactiveEventScans) return;
    if (!proactiveEventWatcher_ || proactiveEventWatcher_->isRunning()) return;
    proactiveEventWatcher_->setFuture(QtConcurrent::run([] {
        return EventLogCollector::collectRecent(35);
    }));
}

void MainWindow::refreshAppInventory()
{
    if (uiCheck_) return;
    if (!appInventoryWatcher_ || appInventoryWatcher_->isRunning()) return;
    if (appInventoryPage_) appInventoryPage_->setBusy(true);
    appInventoryWatcher_->setFuture(QtConcurrent::run(&AppInventoryCollector::collect));
}

void MainWindow::refreshSecurityStatus()
{
    if (uiCheck_) return;
    if (!securityStatusWatcher_ || securityStatusWatcher_->isRunning()) return;
    if (securityPage_) securityPage_->setSecurityBusy(true);
    securityStatusWatcher_->setFuture(QtConcurrent::run(&SecurityStatusCollector::readSecurityStatus));
}

void MainWindow::scanLocalUpdates()
{
    if (!updateCacheWatcher_ || updateCacheWatcher_->isRunning()) return;
    if (securityPage_) securityPage_->setUpdateBusy(true);
    updateCacheWatcher_->setFuture(QtConcurrent::run(&SecurityStatusCollector::scanLocalUpdateCache));
}

} // namespace Ausyn
