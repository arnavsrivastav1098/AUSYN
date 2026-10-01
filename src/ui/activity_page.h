#pragma once
#include <QWidget>

class QLabel;
class QTableWidget;
namespace Ausyn {
class ActivityPage final : public QWidget {
    Q_OBJECT
public:
    explicit ActivityPage(QWidget* parent = nullptr);
    void record(const QString& title, const QString& detail, int destination = 0);
    void setDetailed(bool detailed);
signals:
    void reviewRequested(int destination, const QString& title);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    QTableWidget* table_ = nullptr;
    QLabel* empty_ = nullptr;
};
}
