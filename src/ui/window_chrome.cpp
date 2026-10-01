#include "window_chrome.h"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace Ausyn {
namespace {
class ControlButton final : public QToolButton {
public:
    enum Role { Minimize, Maximize, Close };
    ControlButton(Role role, QWidget* window, QWidget* parent)
        : QToolButton(parent), role_(role), window_(window) {
        setFixedSize(38, 34);
        setCursor(Qt::PointingHandCursor);
        setAutoRaise(true);
        const QString name = role == Minimize ? QStringLiteral("Minimize Ausyn")
            : role == Maximize ? QStringLiteral("Maximize or restore Ausyn") : QStringLiteral("Close this window");
        setToolTip(name); setAccessibleName(name);
        setObjectName(role == Minimize ? QStringLiteral("ausynMinimize")
            : role == Maximize ? QStringLiteral("ausynMaximize") : QStringLiteral("ausynClose"));
        connect(this, &QToolButton::clicked, this, [this] {
            if (role_ == Minimize) window_->showMinimized();
            else if (role_ == Maximize) {
                if (window_->isMaximized()) window_->showNormal(); else window_->showMaximized();
            } else window_->close();
        });
    }
protected:
    void paintEvent(QPaintEvent*) override {
        const bool light = property("lightTheme").toBool();
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(role_ == Close ? QStringLiteral("#b43d50")
                : light ? QStringLiteral("#e1e7ef") : QStringLiteral("#242c3d")));
            p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
        }
        p.setPen(QPen(QColor(role_ == Close && underMouse() ? QStringLiteral("#ffffff")
            : light ? QStringLiteral("#34445b") : QStringLiteral("#c8d3e3")), 1.5));
        const QPointF c = QRectF(rect()).center();
        if (role_ == Minimize) p.drawLine(c + QPointF(-6, 3), c + QPointF(6, 3));
        else if (role_ == Close) {
            p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
            p.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5));
        } else if (window_->isMaximized()) {
            p.drawRect(QRectF(c.x() - 3, c.y() - 6, 9, 9));
            p.fillRect(QRectF(c.x() - 6, c.y() - 3, 9, 9), QColor(light ? QStringLiteral("#f5f7fb") : QStringLiteral("#101722")));
            p.drawRect(QRectF(c.x() - 6, c.y() - 3, 9, 9));
        } else p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
        if (hasFocus()) { p.setPen(QPen(QColor(QStringLiteral("#9690ff")), 1)); p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 6, 6); }
    }
private:
    Role role_; QWidget* window_;
};
}

FramelessWindow::FramelessWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint);
}

bool FramelessWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* event = static_cast<MSG*>(message);
    if (event && event->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
        RECT bounds{};
        if (GetWindowRect(event->hwnd, &bounds)) {
            const int x = static_cast<short>(LOWORD(event->lParam));
            const int y = static_cast<short>(HIWORD(event->lParam));
            const int border = std::max(5, static_cast<int>(6 * devicePixelRatioF()));
            const bool left = x < bounds.left + border, right = x >= bounds.right - border;
            const bool top = y < bounds.top + border, bottom = y >= bounds.bottom - border;
            const int hit = top && left ? HTTOPLEFT : top && right ? HTTOPRIGHT : bottom && left ? HTBOTTOMLEFT
                : bottom && right ? HTBOTTOMRIGHT : left ? HTLEFT : right ? HTRIGHT : top ? HTTOP : bottom ? HTBOTTOM : HTCLIENT;
            if (hit != HTCLIENT) { *result = hit; return true; }
        }
    }
    if (event && event->message == WM_GETMINMAXINFO) {
        QMainWindow::nativeEvent(type, message, result);
        MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(event->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(event->lParam);
            limits->ptMinTrackSize.x = std::max<LONG>(limits->ptMinTrackSize.x, qRound(minimumWidth() * devicePixelRatioF()));
            limits->ptMinTrackSize.y = std::max<LONG>(limits->ptMinTrackSize.y, qRound(minimumHeight() * devicePixelRatioF()));
            limits->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
            limits->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
            *result = 0;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(type, message, result);
}

WindowTitleBar::WindowTitleBar(QWidget* window, QWidget* parent) : QFrame(parent), window_(window) {
    setObjectName(QStringLiteral("ausynTitleBar"));
    setFixedHeight(54);
    layout_ = new QHBoxLayout(this); layout_->setContentsMargins(18, 0, 8, 0); layout_->setSpacing(10);
}
void WindowTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window_->windowHandle() && window_->windowHandle()->startSystemMove()) event->accept();
    else QFrame::mousePressEvent(event);
}
void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (window_->isMaximized()) window_->showNormal(); else window_->showMaximized();
        event->accept();
    } else QFrame::mouseDoubleClickEvent(event);
}
WindowControls::WindowControls(QWidget* window, QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(3);
    row->addWidget(new ControlButton(ControlButton::Minimize, window, this));
    row->addWidget(new ControlButton(ControlButton::Maximize, window, this));
    row->addWidget(new ControlButton(ControlButton::Close, window, this));
}
} // namespace Ausyn
