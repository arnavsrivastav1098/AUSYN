#pragma once

#include <QAbstractButton>
#include <QHash>
#include <QWidget>

class QStackedWidget;
class QTabBar;
class QVariantAnimation;

namespace Ausyn {

// A finite animation: no repaint timer runs while the switch is at rest.
class DetailSwitch final : public QAbstractButton {
    Q_OBJECT
public:
    explicit DetailSwitch(QWidget* parent = nullptr);
    void setAnimationsEnabled(bool enabled);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVariantAnimation* animation_ = nullptr;
    qreal position_ = 0.0;
    bool animationsEnabled_ = true;
};

class PageHub final : public QWidget {
    Q_OBJECT
public:
    PageHub(const QString& title, const QString& subtitle, QWidget* parent = nullptr);
    void addSection(int id, const QString& name, QWidget* content, bool detailOnly = false);
    bool selectSection(int id);
    int currentSection() const;
    void setDetailed(bool detailed);
    bool isDetailed() const;
    void setAnimationsEnabled(bool enabled);
signals:
    void detailChanged(bool detailed);
    void sectionChanged(int id);
private:
    void applyDetailMode();
    QTabBar* tabs_ = nullptr;
    QStackedWidget* content_ = nullptr;
    DetailSwitch* detail_ = nullptr;
    QHash<int, int> sectionIndices_;
};

} // namespace Ausyn
