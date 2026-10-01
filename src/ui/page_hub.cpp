#include "page_hub.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace Ausyn {

DetailSwitch::DetailSwitch(QWidget* parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setFixedSize(168, 36);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(QStringLiteral("Show detailed information"));
    setToolTip(QStringLiteral("Switch between a simple overview and all available evidence and tools."));
    animation_ = new QVariantAnimation(this);
    animation_->setDuration(160);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        position_ = value.toReal();
        update();
    });
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        animation_->stop();
        if (animationsEnabled_ && isVisible()) {
            animation_->setStartValue(position_);
            animation_->setEndValue(checked ? 1.0 : 0.0);
            animation_->start();
        } else {
            position_ = checked ? 1.0 : 0.0;
            update();
        }
    });
}

void DetailSwitch::setAnimationsEnabled(bool enabled)
{
    animationsEnabled_ = enabled;
    if (!enabled) {
        animation_->stop();
        position_ = isChecked() ? 1.0 : 0.0;
        update();
    }
}

void DetailSwitch::paintEvent(QPaintEvent*)
{
    const bool light = property("lightTheme").toBool();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(light ? QStringLiteral("#d7dfe9") : QStringLiteral("#293548")), 1));
    p.setBrush(QColor(light ? QStringLiteral("#eef2f7") : QStringLiteral("#101722")));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(light ? QStringLiteral("#514cad") : QStringLiteral("#827cff")));
    p.drawRoundedRect(QRectF(3 + position_ * 81, 3, 81, 30), 15, 15);
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    for (int i = 0; i < 2; ++i) {
        const bool selected = isChecked() == (i == 1);
        p.setPen(QColor(selected ? QStringLiteral("#ffffff")
            : (light ? QStringLiteral("#59677b") : QStringLiteral("#9ba9bd"))));
        p.drawText(QRect(3 + i * 81, 3, 81, 30), Qt::AlignCenter,
                   i == 0 ? QStringLiteral("Simple") : QStringLiteral("Details"));
    }
    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(QStringLiteral("#a9a8ff")), 2));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18, 18);
    }
}

PageHub::PageHub(const QString& title, const QString& subtitle, QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* header = new QWidget(this);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(28, 22, 28, 18);
    auto* text = new QVBoxLayout;
    text->setSpacing(5);
    auto* heading = new QLabel(title, header);
    heading->setObjectName(QStringLiteral("hubTitle"));
    auto* description = new QLabel(subtitle, header);
    description->setObjectName(QStringLiteral("subtle"));
    description->setWordWrap(true);
    text->addWidget(heading);
    text->addWidget(description);
    row->addLayout(text, 1);
    row->addSpacing(16);
    detail_ = new DetailSwitch(header);
    detail_->setObjectName(QStringLiteral("pageDetailSwitch"));
    row->addWidget(detail_, 0, Qt::AlignTop);
    outer->addWidget(header);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("hubTabs"));
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(true);
    tabs_->setElideMode(Qt::ElideNone);
    auto* tabsRow = new QHBoxLayout;
    tabsRow->setContentsMargins(26, 0, 26, 8);
    tabsRow->addWidget(tabs_, 1);
    outer->addLayout(tabsRow);
    content_ = new QStackedWidget(this);
    outer->addWidget(content_, 1);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0 || index >= content_->count()) return;
        content_->setCurrentIndex(index);
        applyDetailMode();
        emit sectionChanged(tabs_->tabData(index).toInt());
    });
    connect(detail_, &QAbstractButton::toggled, this, [this](bool detailed) {
        applyDetailMode();
        emit detailChanged(detailed);
    });
}

void PageHub::addSection(int id, const QString& name, QWidget* widget, bool detailOnly)
{
    QWidget* page = widget;
    if (auto* scroll = qobject_cast<QScrollArea*>(widget)) {
        page = scroll->widget();
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setAutoFillBackground(false);
    }
    page->setAutoFillBackground(false);
    for (QLabel* label : page->findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (label->objectName() == QLatin1String("heroTitle") || label->objectName() == QLatin1String("eyebrow"))
            label->hide();
        if (label->objectName() == QLatin1String("heroBody")) label->setProperty("detailOnly", true);
    }
    widget->setProperty("sectionDetailOnly", detailOnly);
    const int index = content_->addWidget(widget);
    sectionIndices_.insert(id, index);
    tabs_->addTab(name);
    tabs_->setTabData(index, id);
    applyDetailMode();
}

bool PageHub::selectSection(int id)
{
    const auto it = sectionIndices_.constFind(id);
    if (it == sectionIndices_.cend()) return false;
    if (content_->widget(*it)->property("sectionDetailOnly").toBool()) setDetailed(true);
    tabs_->setCurrentIndex(*it);
    content_->setCurrentIndex(*it);
    applyDetailMode();
    return true;
}

int PageHub::currentSection() const { return tabs_->tabData(tabs_->currentIndex()).toInt(); }
bool PageHub::isDetailed() const { return detail_->isChecked(); }
void PageHub::setDetailed(bool detailed) { detail_->setChecked(detailed); applyDetailMode(); }
void PageHub::setAnimationsEnabled(bool enabled) { detail_->setAnimationsEnabled(enabled); }

void PageHub::applyDetailMode()
{
    const bool detailed = isDetailed();
    for (int i = 0; i < content_->count(); ++i)
        tabs_->setTabVisible(i, detailed || !content_->widget(i)->property("sectionDetailOnly").toBool());
    if (content_->currentWidget() && !detailed && content_->currentWidget()->property("sectionDetailOnly").toBool()) {
        tabs_->setCurrentIndex(0);
        content_->setCurrentIndex(0);
    }
    tabs_->setVisible(tabs_->count() > 1);
    if (QWidget* current = content_->currentWidget()) {
        for (QWidget* widget : current->findChildren<QWidget*>()) {
            if (widget->property("detailOnly").toBool()) widget->setVisible(detailed);
            if (widget->property("simpleOnly").toBool()) widget->setVisible(!detailed);
        }
    }
}

} // namespace Ausyn
