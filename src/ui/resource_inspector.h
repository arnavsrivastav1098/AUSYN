#pragma once
#include "window_chrome.h"
#include "../intelligence/insight_types.h"
#include "../monitoring/system_snapshot.h"
class QLabel;
class QLineEdit;
class QProgressBar;
class QTableWidget;
namespace Ausyn {
class DetailSwitch;
class LiveActivityChart;
enum class ResourceKind : int { Cpu, Memory, Graphics, Storage, Battery, Network, Processes };
QString resourceTitle(ResourceKind kind);
class ResourceCard final : public QFrame {
    Q_OBJECT
public:
    explicit ResourceCard(ResourceKind kind, QWidget* parent = nullptr);
signals:
    void activated(int resource);
protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    ResourceKind kind_;
};

class ResourceInspector final : public FramelessWindow {
    Q_OBJECT
public:
    explicit ResourceInspector(ResourceKind kind, QWidget* parent = nullptr);
    ResourceKind resource() const { return kind_; }
    void setContext(const SystemSnapshot& snapshot, const AnalysisUpdate& analysis, bool monitoring, bool force = false);
    void seedChart(const LiveActivityChart& source);
    void updateFreshness(bool monitoring);
    QString summaryText() const;
signals:
    void toolRequested(int tool);
private:
    void render();
    ResourceKind kind_;
    SystemSnapshot snapshot_;
    AnalysisUpdate analysis_;
    bool monitoring_ = true;
    QDateTime renderedAt_;
    QLabel* value_; QLabel* summary_; QLabel* advice_; QLabel* source_;
    QProgressBar* level_; QTableWidget* facts_; QTableWidget* processes_;
    QLineEdit* filter_; DetailSwitch* details_; LiveActivityChart* chart_;
};
}
