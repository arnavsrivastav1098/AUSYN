#include "data_quality_page.h"

#include <QColor>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace Ausyn {
namespace {

QFrame* panel(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QString stateName(MetricQualityState state)
{
    switch (state) {
    case MetricQualityState::Valid: return QStringLiteral("Valid");
    case MetricQualityState::Estimated: return QStringLiteral("Estimated");
    case MetricQualityState::WarmingUp: return QStringLiteral("Warming up");
    case MetricQualityState::Unavailable: return QStringLiteral("Unavailable");
    case MetricQualityState::Stale: return QStringLiteral("Stale");
    case MetricQualityState::Invalid: return QStringLiteral("Invalid");
    case MetricQualityState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QColor stateColor(MetricQualityState state)
{
    switch (state) {
    case MetricQualityState::Valid: return QColor(QStringLiteral("#87d7b0"));
    case MetricQualityState::Estimated: return QColor(QStringLiteral("#c3c2ff"));
    case MetricQualityState::WarmingUp: return QColor(QStringLiteral("#e4c27d"));
    case MetricQualityState::Unavailable: return QColor(QStringLiteral("#99a5b9"));
    case MetricQualityState::Stale:
    case MetricQualityState::Invalid:
    case MetricQualityState::Error: return QColor(QStringLiteral("#ff8f88"));
    }
    return QColor(QStringLiteral("#99a5b9"));
}

QString nextStep(const MetricQualityRecord& record)
{
    switch (record.state) {
    case MetricQualityState::Valid:
        return QStringLiteral("No action needed for this reading.");
    case MetricQualityState::Estimated:
        return QStringLiteral("Informational: use the source and details to understand what was estimated.");
    case MetricQualityState::WarmingUp:
        return QStringLiteral("Wait for another sample. Some Windows counters need two readings before they can calculate a rate.");
    case MetricQualityState::Unavailable:
        if (record.metric.contains(QStringLiteral("thermal"), Qt::CaseInsensitive) ||
            record.metric.contains(QStringLiteral("trip point"), Qt::CaseInsensitive))
            return QStringLiteral("This optional Windows sensor may not be exposed by your device. Check the manufacturer’s utility if you need that reading.");
        if (record.metric.contains(QStringLiteral("fan"), Qt::CaseInsensitive))
            return QStringLiteral("Windows may not expose fan telemetry on this device. Check the manufacturer’s controls for fan information.");
        if (record.metric.contains(QStringLiteral("network"), Qt::CaseInsensitive) ||
            record.metric.contains(QStringLiteral("adapter"), Qt::CaseInsensitive) ||
            record.metric.contains(QStringLiteral("TCP"), Qt::CaseInsensitive) ||
            record.metric.contains(QStringLiteral("UDP"), Qt::CaseInsensitive))
            return QStringLiteral("If you expected a connection, check that Windows shows an active network adapter.");
        if (record.metric.contains(QStringLiteral("process"), Qt::CaseInsensitive))
            return QStringLiteral("Wait for the next process refresh. Ausyn uses only access already available to your Windows account.");
        return QStringLiteral("Windows or this device did not provide the reading. Review the listed source and details; this alone does not indicate a fault.");
    case MetricQualityState::Stale:
        return QStringLiteral("Wait for the next monitor cycle. If it remains stale, check the monitoring status at the top of Ausyn.");
    case MetricQualityState::Invalid:
        return QStringLiteral("Ausyn excluded this value from analysis. If it keeps recurring, compare it with a Windows or device-manufacturer reading.");
    case MetricQualityState::Error:
        return QStringLiteral("Review the source details. If the same error persists across samples, restart Ausyn and check again.");
    }
    return QStringLiteral("Review the source and details for this reading.");
}

} // namespace

DataQualityPage::DataQualityPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(30, 26, 30, 28);
    outer->setSpacing(15);

    auto* eyebrow = new QLabel(QStringLiteral("ADVANCED · TRANSPARENT MEASUREMENTS"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto* heading = new QLabel(QStringLiteral("Data quality"), this);
    heading->setObjectName(QStringLiteral("heroTitle"));
    auto* intro = new QLabel(QStringLiteral("See where each reading came from and whether Ausyn accepted it, estimated it, or marked it unavailable. Invalid measurements are excluded from analysis."), this);
    intro->setObjectName(QStringLiteral("heroBody"));
    intro->setWordWrap(true);
    outer->addWidget(eyebrow);
    outer->addWidget(heading);
    outer->addWidget(intro);

    auto* content = panel(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(10);
    summary_ = new QLabel(QStringLiteral("Waiting for the first validated sample…"), content);
    summary_->setObjectName(QStringLiteral("subtle"));
    summary_->setWordWrap(true);
    table_ = new QTableWidget(content);
    table_->setObjectName(QStringLiteral("processTable"));
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("Reading"), QStringLiteral("Quality"),
        QStringLiteral("Value"), QStringLiteral("Source / unit"), QStringLiteral("Details"),
        QStringLiteral("What you can do")});
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_->setWordWrap(true);
    layout->addWidget(summary_);
    layout->addWidget(table_, 1);
    outer->addWidget(content, 1);
}

void DataQualityPage::setSnapshot(const SystemSnapshot& snapshot)
{
    table_->setRowCount(0);
    int valid = 0;
    int unavailable = 0;
    int estimated = 0;
    int flagged = 0;
    for (const MetricQualityRecord& record : snapshot.dataQuality) {
        switch (record.state) {
        case MetricQualityState::Valid: ++valid; break;
        case MetricQualityState::Unavailable:
        case MetricQualityState::WarmingUp: ++unavailable; break;
        case MetricQualityState::Estimated: ++estimated; break;
        case MetricQualityState::Stale:
        case MetricQualityState::Invalid:
        case MetricQualityState::Error: ++flagged; break;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QStringList columns{record.metric, stateName(record.state), record.observedValue,
            QStringLiteral("%1 · %2").arg(record.source, record.unit), record.detail, nextStep(record)};
        for (int column = 0; column < columns.size(); ++column) {
            auto* item = new QTableWidgetItem(columns.at(column));
            item->setToolTip(columns.at(column));
            if (column == 1) item->setForeground(stateColor(record.state));
            table_->setItem(row, column, item);
        }
    }
    summary_->setText(QStringLiteral("Latest sample %1 · %2 valid · %3 estimated · %4 unavailable or warming up · %5 flagged")
        .arg(snapshot.capturedAt.toString(QStringLiteral("h:mm:ss ap")))
        .arg(valid).arg(estimated).arg(unavailable).arg(flagged));
}

} // namespace Ausyn
