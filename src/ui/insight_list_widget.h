#pragma once

#include "../intelligence/insight_types.h"

#include <QScrollArea>
#include <QHash>

class QVBoxLayout;
class QWidget;

namespace Ausyn {

class InsightListWidget final : public QScrollArea {
    Q_OBJECT
public:
    explicit InsightListWidget(QWidget* parent = nullptr);

    void setFindings(QVector<Finding> findings, const QString& emptyMessage,
                     bool allowOutcomeFeedback = false,
                     bool showTechnicalDetails = false);
    [[nodiscard]] bool findingExpanded(const QString& ruleId, bool defaultExpanded) const;
    void setFindingExpanded(const QString& ruleId, bool expanded);
    void clearFindingExpansionOverrides();

signals:
    void recommendationOutcomeReported(QString ruleId, QDateTime firstSeen, int outcome);
    void recommendationVerificationRequested(QString ruleId, QDateTime firstSeen, bool captureBaseline);

private:
    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QHash<QString, bool> expandedOverrides_;
};

} // namespace Ausyn
