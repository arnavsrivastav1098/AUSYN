#pragma once

#include "../monitoring/system_snapshot.h"
#include <QWidget>
#include <QVector>
#include <optional>

class QVariantAnimation;

namespace Ausyn {

class LiveActivityChart final : public QWidget {
    Q_OBJECT
public:
    explicit LiveActivityChart(QWidget* parent = nullptr);
    void addSnapshot(const SystemSnapshot& snapshot);
    void setDetailed(bool detailed);
    void setSeries(int series) { series_ = series; update(); }
    void copyHistoryFrom(const LiveActivityChart& source);
    void setAnimationsEnabled(bool enabled);
    int readingCount() const { return static_cast<int>(points_.size()); }
    bool animating() const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    struct Point { QDateTime at; std::optional<double> cpu, memory, gpu; };
    QVector<Point> points_;
    bool detailed_ = false;
    int series_ = -1;
    int hovered_ = -1;
    QVariantAnimation* arrival_ = nullptr;
    qreal arrivalPhase_ = 1;
    bool animationsEnabled_ = true;
};

class ResourceRing final : public QWidget {
public:
    explicit ResourceRing(QWidget* parent = nullptr);
    void setValue(std::optional<double> value, const QString& caption);
    void setAnimationsEnabled(bool enabled);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVariantAnimation* animation_ = nullptr;
    std::optional<double> value_;
    qreal displayed_ = 0;
    QString caption_;
    bool animationsEnabled_ = true;
};

} // namespace Ausyn
