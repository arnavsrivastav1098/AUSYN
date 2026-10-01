#pragma once
#include <QFrame>
class QLabel;
class QTimer;
class QScrollArea;
class QPushButton;
namespace Ausyn {
class NotificationPopup final : public QFrame {
    Q_OBJECT
public:
    explicit NotificationPopup(QWidget* parent = nullptr);
    void present(const QString& title, const QString& body, int resource, bool critical, bool light, bool refreshOnly = false, bool workloadAction = false);
signals:
    void reviewRequested(int resource);
    void snoozeRequested();
    void workloadRequested();
private:
    QLabel* title_; QLabel* body_; QLabel* level_; QTimer* dismissal_;
    QScrollArea* bodyScroll_;
    QPushButton* workload_;
    int resource_ = 0;
};
}
