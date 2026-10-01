#include "activity_page.h"
#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace Ausyn {
ActivityPage::ActivityPage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 16, 28, 24);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("What Ausyn has been doing"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    auto* note = new QLabel(QStringLiteral("See checks, new findings, recoveries and changes to monitoring. This session log stays in memory and keeps the latest 100 entries."), this);
    note->setObjectName(QStringLiteral("subtle")); note->setWordWrap(true);
    note->setProperty("detailOnly", true);
    layout->addWidget(title); layout->addWidget(note);
    empty_ = new QLabel(QStringLiteral("Activity appears here as Ausyn checks your PC."), this);
    empty_->setObjectName(QStringLiteral("subtle")); layout->addWidget(empty_);
    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("activityTable"));
    table_->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Activity"), QStringLiteral("Evidence / next step")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->setColumnWidth(1, 225);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(true);
    layout->addWidget(table_, 1);
    auto* review = new QPushButton(QStringLiteral("Review selected activity"), this);
    review->setObjectName(QStringLiteral("secondaryButton")); layout->addWidget(review, 0, Qt::AlignLeft);
    const auto open = [this] {
        const int row = table_->currentRow();
        if (row >= 0 && table_->item(row, 1))
            emit reviewRequested(table_->item(row, 1)->data(Qt::UserRole).toInt(), table_->item(row, 1)->text());
    };
    connect(review, &QPushButton::clicked, this, open);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [open](int, int) { open(); });
    setDetailed(false);
}

void ActivityPage::setDetailed(bool detailed)
{
    table_->setColumnHidden(2, !detailed);
    table_->horizontalHeader()->setSectionResizeMode(1, detailed ? QHeaderView::Interactive : QHeaderView::Stretch);
    if (detailed) table_->setColumnWidth(1, 225);
    table_->resizeRowsToContents();
}

void ActivityPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (table_) table_->resizeRowsToContents();
}

void ActivityPage::record(const QString& title, const QString& detail, int destination)
{
    empty_->hide();
    table_->setUpdatesEnabled(false);
    table_->insertRow(0);
    auto* time = new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("h:mm:ss ap")));
    time->setToolTip(QDateTime::currentDateTime().toString(Qt::ISODate));
    auto* event = new QTableWidgetItem(title); event->setData(Qt::UserRole, destination);
    event->setToolTip(detail);
    auto* evidence = new QTableWidgetItem(detail); evidence->setToolTip(detail);
    table_->setItem(0, 0, time); table_->setItem(0, 1, event); table_->setItem(0, 2, evidence);
    table_->resizeRowToContents(0);
    table_->setRowHeight(0, std::max(48, table_->rowHeight(0)));
    while (table_->rowCount() > 100) table_->removeRow(table_->rowCount() - 1);
    table_->setUpdatesEnabled(true);
}
}
