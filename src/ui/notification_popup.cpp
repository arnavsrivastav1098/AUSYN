#include "notification_popup.h"
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
namespace Ausyn {
NotificationPopup::NotificationPopup(QWidget* parent)
    : QFrame(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName(QStringLiteral("ausynNotification"));
    setAccessibleName(QStringLiteral("Ausyn proactive advice"));
    auto* column = new QVBoxLayout(this); column->setContentsMargins(20, 16, 20, 16); column->setSpacing(10);
    auto* heading = new QHBoxLayout;
    auto* logo = new QLabel(this);
    logo->setPixmap(QPixmap(QStringLiteral(":/brand/ausyn.png")).scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(28, 28);
    heading->addWidget(logo);
    level_ = new QLabel(this); level_->setObjectName(QStringLiteral("noticeLevel"));
    heading->addWidget(level_, 1);
    auto* dismiss = new QPushButton(QStringLiteral("×"), this);
    dismiss->setFixedSize(28, 28); dismiss->setAccessibleName(QStringLiteral("Dismiss this alert"));
    heading->addWidget(dismiss); column->addLayout(heading);
    title_ = new QLabel(this); title_->setObjectName(QStringLiteral("noticeTitle")); title_->setWordWrap(true);
    body_ = new QLabel(this); body_->setObjectName(QStringLiteral("noticeBody")); body_->setWordWrap(true);
    title_->setTextFormat(Qt::PlainText); body_->setTextFormat(Qt::PlainText);
    body_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    body_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
    column->addWidget(title_);
    bodyScroll_ = new QScrollArea(this);
    bodyScroll_->setFrameShape(QFrame::NoFrame); bodyScroll_->setWidgetResizable(true);
    bodyScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyScroll_->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    bodyScroll_->viewport()->setAutoFillBackground(false);
    bodyScroll_->setWidget(body_); column->addWidget(bodyScroll_);
    auto* actions = new QHBoxLayout;
    auto* review = new QPushButton(QStringLiteral("See details"), this);
    review->setObjectName(QStringLiteral("alertReview"));
    auto* quiet = new QPushButton(QStringLiteral("Quiet for 15 min"), this);
    quiet->setObjectName(QStringLiteral("alertQuiet"));
    actions->addWidget(review); actions->addWidget(quiet); column->addLayout(actions);
    workload_ = new QPushButton(QStringLiteral("Keep my current app · review options"), this);
    workload_->setObjectName(QStringLiteral("alertWorkload")); workload_->hide();
    column->addWidget(workload_);
    connect(workload_, &QPushButton::clicked, this, [this] { hide(); emit workloadRequested(); });
    connect(dismiss, &QPushButton::clicked, this, &QWidget::hide);
    connect(review, &QPushButton::clicked, this, [this] { hide(); emit reviewRequested(resource_); });
    connect(quiet, &QPushButton::clicked, this, [this] { hide(); emit snoozeRequested(); });
    dismissal_ = new QTimer(this); dismissal_->setSingleShot(true);
    connect(dismissal_, &QTimer::timeout, this, &QWidget::hide);
}
void NotificationPopup::present(const QString& title, const QString& body, int resource, bool critical, bool light, bool refreshOnly, bool workloadAction) {
    if (refreshOnly && !isVisible()) return;
    resource_ = resource;
    if (!refreshOnly) workload_->setVisible(workloadAction);
    level_->setText(critical ? QStringLiteral("AUSYN · IMPORTANT FINDING") : QStringLiteral("AUSYN · HERE’S WHAT I NOTICED"));
    title_->setText(title.left(160)); body_->setText(body.left(8000));
    // A live update changes content only. Reusing layout size hints while the
    // window already has a fixed height caused repeated growth on every sample.
    // Keep the original screen, position, lifetime and viewport unchanged.
    if (refreshOnly) {
        body_->setMaximumWidth(std::max(1, bodyScroll_->viewport()->width()));
        body_->setMinimumHeight(std::max(1, body_->heightForWidth(bodyScroll_->viewport()->width())) + 4);
        return;
    }
    setStyleSheet(QStringLiteral("QFrame#ausynNotification { background:%1; border:1px solid %2; border-radius:14px; } QLabel { background:transparent; color:%3; } QLabel#noticeLevel { color:%4; font-size:10px; font-weight:700; } QLabel#noticeTitle { font-size:18px; font-weight:700; } QLabel#noticeBody { font-size:12px; } QPushButton { background:%5; color:%3; border:1px solid %2; padding:8px; border-radius:7px; } QPushButton:hover { background:%6; }")
        .arg(light ? QStringLiteral("#f5f7fb") : QStringLiteral("#121a29"),
             critical ? QStringLiteral("#ed9292") : QStringLiteral("#6964be"),
             light ? QStringLiteral("#253247") : QStringLiteral("#e1e7f1"),
             critical ? QStringLiteral("#ef9b8d") : QStringLiteral("#a9a5ff"),
             light ? QStringLiteral("#e8ecf4") : QStringLiteral("#212c40"),
             light ? QStringLiteral("#d9deed") : QStringLiteral("#2c3850")));
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    setMinimumSize(0, 0); setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setFixedWidth(std::max(240, std::min(420, available.width() - 32)));
    ensurePolished();
    const int contentWidth = width() - 40;
    body_->setMaximumWidth(std::max(1, contentWidth - 20));
    title_->setFixedHeight(std::max(1, title_->heightForWidth(contentWidth)) + 4);
    const int bodyHeight = std::max(1, body_->heightForWidth(contentWidth - 20)) + 4;
    body_->setMinimumHeight(bodyHeight);
    // Measure wrapped text at the final width, rather than using its one-line hint.
    // On short screens the body scrolls; heading and actions always remain readable.
    const int chromeHeight = 32 + 28 + title_->height() + 40 + 4 * 10 + (workloadAction ? 50 : 0);
    const int viewportHeight = std::min(160, std::max(48, available.height() - 32 - chromeHeight));
    bodyScroll_->setFixedHeight(viewportHeight);
    bodyScroll_->verticalScrollBar()->setValue(0);
    layout()->activate();
    setFixedHeight(std::min(available.height() - 32, chromeHeight + viewportHeight));
    move(available.right() - width() - 16, available.bottom() - height() - 16);
    if (!refreshOnly) { show(); raise(); dismissal_->start(critical ? 22'000 : 14'000); }
}
}
