#pragma once

#include "../intelligence/event_log_types.h"
#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"

#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QTableWidget;
class QVBoxLayout;

namespace Ausyn {

class TroubleshootingPage final : public QWidget {
    Q_OBJECT
public:
    explicit TroubleshootingPage(QWidget* parent = nullptr);
    void setContext(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis,
                    const EventLogUpdate& eventLogs);

signals:
    void reviewRequested(int pageIndex, const QString& pageTitle);
    void askAboutRequested(const QString& category, const QString& title,
                           const QString& summary, const QString& evidence,
                           const QString& nextStep);

private slots:
    void render();

private:
    struct EvidenceRow {
        QString signal;
        QString observation;
        QString interpretation;
    };

    [[nodiscard]] QVector<EvidenceRow> evidenceFor(int symptom) const;
    [[nodiscard]] QStringList stepsFor(int symptom) const;
    [[nodiscard]] QString titleFor(int symptom) const;
    [[nodiscard]] QString summaryFor(int symptom) const;
    [[nodiscard]] QString limitsFor(int symptom) const;
    [[nodiscard]] int relatedPageFor(int symptom) const;

    SystemSnapshot snapshot_;
    AnalysisUpdate analysis_;
    EventLogUpdate eventLogs_;
    QComboBox* symptom_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* limitations_ = nullptr;
    QLabel* progress_ = nullptr;
    QTableWidget* evidence_ = nullptr;
    QVBoxLayout* stepsLayout_ = nullptr;
    QSet<QString> completedSteps_;
    QVector<EvidenceRow> renderedEvidence_;
    int renderedStepsSymptom_ = -1;
};

} // namespace Ausyn
