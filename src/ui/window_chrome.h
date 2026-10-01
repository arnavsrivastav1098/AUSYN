#pragma once
#include <QFrame>
#include <QMainWindow>

class QHBoxLayout;
namespace Ausyn {
class FramelessWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit FramelessWindow(QWidget* parent = nullptr);
protected:
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override;
};

class WindowTitleBar final : public QFrame {
    Q_OBJECT
public:
    explicit WindowTitleBar(QWidget* window, QWidget* parent = nullptr);
    QHBoxLayout* contentLayout() const { return layout_; }
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
private:
    QWidget* window_;
    QHBoxLayout* layout_;
};

class WindowControls final : public QWidget {
    Q_OBJECT
public:
    explicit WindowControls(QWidget* window, QWidget* parent = nullptr);
};
} // namespace Ausyn
