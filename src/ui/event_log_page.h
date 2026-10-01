#pragma once

#include "../intelligence/event_log_types.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Ausyn {

class EventLogPage final : public QWidget {
    Q_OBJECT
public:
    explicit EventLogPage(QWidget* parent = nullptr);
    void setBusy(bool busy);
    void setUpdate(const EventLogUpdate& update);
    void setIncidentHistory(const QVector<Finding>& findings, bool available, const QString& message);

signals:
    void refreshRequested();

private slots:
    void applyFilter();
    void showSelectedDetails();
    void showSelectedIncidentDetails();
    void exportVisibleEvents();
    void openReliabilityMonitor();

private:
    QVector<EventInsight> insights_;
    QVector<Finding> incidentFindings_;
    QLabel* status_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* reliabilityButton_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* severityFilter_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextBrowser* details_ = nullptr;
    QTableWidget* incidents_ = nullptr;
    QTextBrowser* incidentDetails_ = nullptr;
    QLabel* incidentStatus_ = nullptr;
    QComboBox* outcomeFilter_ = nullptr;
};

} // namespace Ausyn
