#pragma once

#include "../intelligence/insight_types.h"

#include <QWidget>

namespace Ausyn {

class HistoryChart final : public QWidget {
public:
    explicit HistoryChart(QWidget* parent = nullptr);

    void setHistory(QVector<HistoryPoint> points);
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<HistoryPoint> points_;
};

} // namespace Ausyn
