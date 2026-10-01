#pragma once

#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"

#include <QWidget>

class QLabel;
class QComboBox;
class QProgressBar;
class QPushButton;
class QPlainTextEdit;

namespace Ausyn {

class BatteryHealthChart;

class BatteryPage final : public QWidget {
    Q_OBJECT
public:
    explicit BatteryPage(QWidget* parent = nullptr);
    void setSnapshot(const SystemSnapshot& snapshot);
    void setHealthTrend(const QVector<BatteryHealthTrendPoint>& trend);

private:
    QLabel* charge_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* eta_ = nullptr;
    QLabel* rate_ = nullptr;
    QLabel* capacity_ = nullptr;
    QLabel* health_ = nullptr;
    QLabel* trend_ = nullptr;
    QLabel* note_ = nullptr;
    BatteryHealthChart* healthChart_ = nullptr;
    QProgressBar* gauge_ = nullptr;
    QPushButton* requestsButton_ = nullptr;
    QComboBox* powerPlanChoice_ = nullptr;
    QPushButton* applyPowerPlanButton_ = nullptr;
    QPushButton* undoPowerButton_ = nullptr;
    QPlainTextEdit* powerRequests_ = nullptr;
    QLabel* powerActionStatus_ = nullptr;
    QString previousPowerScheme_;
};

} // namespace Ausyn
