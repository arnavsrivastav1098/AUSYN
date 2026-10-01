#include "insight_list_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace Ausyn {
namespace {

QString severityName(FindingSeverity severity)
{
    switch (severity) {
    case FindingSeverity::Critical: return QStringLiteral("Urgent");
    case FindingSeverity::Warning: return QStringLiteral("Review");
    case FindingSeverity::Information: return QStringLiteral("Info");
    }
    return QStringLiteral("Info");
}

QString severityColor(FindingSeverity severity)
{
    switch (severity) {
    case FindingSeverity::Critical: return QStringLiteral("#ff777f");
    case FindingSeverity::Warning: return QStringLiteral("#f2bb73");
    case FindingSeverity::Information: return QStringLiteral("#8fa6d9");
    }
    return QStringLiteral("#8fa6d9");
}

QString outcomeText(RecommendationOutcome outcome)
{
    switch (outcome) {
    case RecommendationOutcome::Improved: return QStringLiteral("You reported that it helped");
    case RecommendationOutcome::NoChange: return QStringLiteral("You reported no change");
    case RecommendationOutcome::Worse: return QStringLiteral("You reported that it felt worse");
    case RecommendationOutcome::Unsure: return QStringLiteral("You reported that the result was unclear");
    }
    return QStringLiteral("Outcome recorded");
}

QWidget* makeFindingCard(const Finding& finding, QWidget* parent,
                         InsightListWidget* owner, bool allowOutcomeFeedback,
                         bool showTechnicalDetails)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("panel"));
    auto* outer = new QHBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* accent = new QFrame(card);
    accent->setFixedWidth(4);
    accent->setStyleSheet(QStringLiteral("background:%1;border-top-left-radius:14px;border-bottom-left-radius:14px;")
                              .arg(severityColor(finding.severity)));
    outer->addWidget(accent);

    auto* content = new QWidget(card);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(17, 15, 18, 16);
    layout->setSpacing(8);

    auto* top = new QHBoxLayout;
    auto* title = new QLabel(finding.title, content);
    title->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;color:#e8edf6;"));
    auto* severity = new QLabel(severityName(finding.severity), content);
    severity->setStyleSheet(QStringLiteral(
        "color:%1;background:#1d2532;border:1px solid #303a4c;border-radius:8px;"
        "padding:3px 8px;font-size:10px;font-weight:700;").arg(severityColor(finding.severity)));
    top->addWidget(title);
    top->addStretch();
    top->addWidget(severity);
    layout->addLayout(top);

    auto* summary = new QLabel(finding.summary, content);
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("color:#b5c0d0;font-size:12px;"));
    layout->addWidget(summary);

    const bool expanded = owner->findingExpanded(finding.ruleId, showTechnicalDetails);
    auto* detailToggle = new QPushButton(expanded
        ? QStringLiteral("Hide evidence and confidence details")
        : QStringLiteral("Show evidence and confidence details"), content);
    detailToggle->setObjectName(QStringLiteral("secondaryButton"));
    detailToggle->setCheckable(true);
    detailToggle->setChecked(expanded);
    detailToggle->setMaximumWidth(310);
    layout->addWidget(detailToggle, 0, Qt::AlignLeft);

    auto* details = new QWidget(content);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(6);
    auto* evidence = new QLabel(QStringLiteral("Evidence  ·  %1").arg(finding.evidence), details);
    evidence->setWordWrap(true);
    evidence->setStyleSheet(QStringLiteral("color:#8f9bb0;font-size:11px;"));
    detailsLayout->addWidget(evidence);

    if (!finding.confidence.isEmpty()) {
        auto* confidence = new QLabel(QStringLiteral("Confidence in this finding · %1. %2")
            .arg(finding.confidence, finding.confidenceBasis), details);
        confidence->setWordWrap(true);
        confidence->setStyleSheet(QStringLiteral("color:#9ba9bd;font-size:11px;"));
        detailsLayout->addWidget(confidence);
    }
    if (finding.firstSeen.isValid()) {
        auto* observed = new QLabel(QStringLiteral("First seen %1")
            .arg(finding.firstSeen.toString(QStringLiteral("ddd, h:mm ap"))), details);
        observed->setStyleSheet(QStringLiteral("color:#6f7b90;font-size:10px;"));
        detailsLayout->addWidget(observed);
    }
    details->setVisible(expanded);
    layout->addWidget(details);
    QObject::connect(detailToggle, &QPushButton::toggled, owner,
        [owner, details, detailToggle, ruleId = finding.ruleId](bool visible) {
            owner->setFindingExpanded(ruleId, visible);
            details->setVisible(visible);
            detailToggle->setText(visible
                ? QStringLiteral("Hide evidence and confidence details")
                : QStringLiteral("Show evidence and confidence details"));
        });

    auto* recommendation = new QLabel(QStringLiteral("Try this  ·  %1").arg(finding.recommendation), content);
    recommendation->setWordWrap(true);
    recommendation->setStyleSheet(QStringLiteral("color:#c2c0ff;font-size:11px;"));
    layout->addWidget(recommendation);

    if (allowOutcomeFeedback && !finding.recommendation.isEmpty()) {
        const bool supportsResourceVerification =
            finding.ruleId == QStringLiteral("processor-sustained-load") ||
            finding.ruleId == QStringLiteral("memory-sustained-pressure") ||
            finding.ruleId == QStringLiteral("concurrent-cpu-memory-pressure");
        if (supportsResourceVerification && !finding.verificationStartedAt.isValid()) {
            auto* prompt = new QLabel(QStringLiteral("Optional: compare CPU and memory before and after you try this step."), content);
            prompt->setWordWrap(true);
            prompt->setStyleSheet(QStringLiteral("color:#8f9bb0;font-size:11px;"));
            layout->addWidget(prompt);
        } else if (supportsResourceVerification && !finding.verificationCheckedAt.isValid() &&
                   finding.verificationStartedAt.isValid()) {
            auto* prompt = new QLabel(QStringLiteral("Before-reading saved %1. Try the suggested step when convenient, then record a later reading.")
                .arg(finding.verificationStartedAt.toString(QStringLiteral("ddd, h:mm ap"))), content);
            prompt->setWordWrap(true);
            prompt->setStyleSheet(QStringLiteral("color:#8f9bb0;font-size:11px;"));
            layout->addWidget(prompt);
        } else if (supportsResourceVerification && finding.verificationCheckedAt.isValid()) {
            QStringList comparison;
            const auto describe = [](const QString& name, const std::optional<double>& before,
                                    const std::optional<double>& after) {
                if (!before || !after) return QString();
                const double change = *after - *before;
                return QStringLiteral("%1 %2% → %3% (%4 percentage points)")
                    .arg(name).arg(*before, 0, 'f', 1).arg(*after, 0, 'f', 1)
                    .arg((change >= 0.0 ? QStringLiteral("+") : QString()) +
                         QString::number(change, 'f', 1));
            };
            const QString cpu = describe(QStringLiteral("CPU"), finding.verificationCpuBefore,
                                         finding.verificationCpuAfter);
            const QString memory = describe(QStringLiteral("Memory"), finding.verificationMemoryBefore,
                                            finding.verificationMemoryAfter);
            if (!cpu.isEmpty()) comparison << cpu;
            if (!memory.isEmpty()) comparison << memory;
            auto* observed = new QLabel(QStringLiteral("Before-and-after readings · %1\n%2\nThis comparison is observational and does not prove the recommendation caused the change.")
                .arg(finding.verificationCheckedAt.toString(QStringLiteral("ddd, h:mm ap")),
                     comparison.join(QStringLiteral("\n"))), content);
            observed->setWordWrap(true);
            observed->setStyleSheet(QStringLiteral("color:#9ba9bd;font-size:11px;"));
            layout->addWidget(observed);
        }
        if (supportsResourceVerification) {
            auto* verificationButton = new QPushButton(
                !finding.verificationStartedAt.isValid() ? QStringLiteral("Save before-reading") :
                !finding.verificationCheckedAt.isValid() ? QStringLiteral("Record later reading") :
                    QStringLiteral("Start another comparison"), content);
            verificationButton->setObjectName(QStringLiteral("secondaryButton"));
            verificationButton->setMaximumWidth(230);
            const bool captureBaseline = !finding.verificationStartedAt.isValid() ||
                                         finding.verificationCheckedAt.isValid();
            const QString verificationRuleId = finding.ruleId;
            const QDateTime verificationFirstSeen = finding.firstSeen;
            QObject::connect(verificationButton, &QPushButton::clicked, owner,
                [owner, verificationButton, verificationRuleId, verificationFirstSeen, captureBaseline] {
                    verificationButton->setEnabled(false);
                    emit owner->recommendationVerificationRequested(
                        verificationRuleId, verificationFirstSeen, captureBaseline);
                });
            layout->addWidget(verificationButton, 0, Qt::AlignLeft);
        }

        if (finding.priorRatedOutcomeReports > 0) {
            QString priorText;
            if (finding.priorRatedOutcomeReports >= 5) {
                const double improvementPercent = 100.0 * finding.priorImprovementReports /
                                                  finding.priorRatedOutcomeReports;
                priorText = QStringLiteral("On this PC, improvement was reported in %1 of %2 earlier rated cases (%3%).")
                    .arg(finding.priorImprovementReports)
                    .arg(finding.priorRatedOutcomeReports)
                    .arg(improvementPercent, 0, 'f', 1);
            } else {
                priorText = QStringLiteral("%1 earlier rated reports for this finding type on this PC; Ausyn shows a percentage after 5 reports.")
                    .arg(finding.priorRatedOutcomeReports);
            }
            auto* prior = new QLabel(priorText + QStringLiteral(" These are local self-reports for the same finding type, not proof that a suggested step caused the change."), content);
            prior->setWordWrap(true);
            prior->setStyleSheet(QStringLiteral("color:#9ba9bd;font-size:11px;"));
            layout->addWidget(prior);
        }
        if (finding.recommendationOutcome) {
            auto* recorded = new QLabel(outcomeText(*finding.recommendationOutcome) + QStringLiteral(" · saved on this PC"), content);
            recorded->setStyleSheet(QStringLiteral("color:#87d7b0;font-size:11px;"));
            layout->addWidget(recorded);
        } else {
            auto* feedbackPrompt = new QLabel(QStringLiteral("After you try this step, record what changed:"), content);
            feedbackPrompt->setStyleSheet(QStringLiteral("color:#8f9bb0;font-size:11px;"));
            layout->addWidget(feedbackPrompt);
            auto* feedbackRow = new QHBoxLayout;
            const QList<QPair<QString, RecommendationOutcome>> choices{
                {QStringLiteral("Helped"), RecommendationOutcome::Improved},
                {QStringLiteral("No change"), RecommendationOutcome::NoChange},
                {QStringLiteral("Worse"), RecommendationOutcome::Worse},
                {QStringLiteral("Unsure"), RecommendationOutcome::Unsure},
            };
            for (const auto& choice : choices) {
                auto* button = new QPushButton(choice.first, content);
                button->setObjectName(QStringLiteral("secondaryButton"));
                button->setMaximumWidth(120);
                const QString ruleId = finding.ruleId;
                const QDateTime firstSeen = finding.firstSeen;
                const int outcome = static_cast<int>(choice.second);
                QObject::connect(button, &QPushButton::clicked, owner,
                        [owner, button, ruleId, firstSeen, outcome] {
                    button->setEnabled(false);
                    emit owner->recommendationOutcomeReported(ruleId, firstSeen, outcome);
                });
                feedbackRow->addWidget(button);
            }
            feedbackRow->addStretch();
            layout->addLayout(feedbackRow);
        }
    }

    outer->addWidget(content, 1);
    return card;
}

} // namespace

InsightListWidget::InsightListWidget(QWidget* parent)
    : QScrollArea(parent)
    , content_(new QWidget(this))
    , layout_(new QVBoxLayout(content_))
{
    setObjectName(QStringLiteral("insightList"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content_->setObjectName(QStringLiteral("insightListContent"));
    content_->setStyleSheet(QStringLiteral("background:transparent;"));
    layout_->setContentsMargins(0, 0, 8, 0);
    layout_->setSpacing(11);
    layout_->addStretch();
    setWidget(content_);
}

void InsightListWidget::setFindings(QVector<Finding> findings, const QString& emptyMessage,
                                    bool allowOutcomeFeedback, bool showTechnicalDetails)
{
    while (QLayoutItem* item = layout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (findings.isEmpty()) {
        auto* empty = new QLabel(emptyMessage, content_);
        empty->setObjectName(QStringLiteral("subtle"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->setMinimumHeight(170);
        layout_->addWidget(empty);
    } else {
        for (const Finding& finding : findings) {
            layout_->addWidget(makeFindingCard(finding, content_, this, allowOutcomeFeedback,
                                               showTechnicalDetails));
        }
    }
    layout_->addStretch();
}

bool InsightListWidget::findingExpanded(const QString& ruleId, bool defaultExpanded) const
{
    const auto it = expandedOverrides_.constFind(ruleId);
    return it == expandedOverrides_.cend() ? defaultExpanded : it.value();
}

void InsightListWidget::setFindingExpanded(const QString& ruleId, bool expanded)
{
    expandedOverrides_.insert(ruleId, expanded);
}

void InsightListWidget::clearFindingExpansionOverrides()
{
    expandedOverrides_.clear();
}

} // namespace Ausyn
