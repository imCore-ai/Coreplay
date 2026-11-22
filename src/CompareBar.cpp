#include "CompareBar.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

CompareSlot::CompareSlot(const QString& letter, QWidget* parent) : QFrame(parent) {
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(30);
    setToolTip(tr("Slot %1: drop a clip here, or click to choose a file. The same clip may go in both slots.").arg(letter));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(8, 2, 4, 2);
    l->setSpacing(8);
    auto* tag = new QLabel(letter, this);
    QFont f = tag->font();
    f.setBold(true);
    tag->setFont(f);
    name_ = new QLabel(this);
    name_->setMinimumWidth(40);
    name_->setTextInteractionFlags(Qt::NoTextInteraction);
    QPalette pal = name_->palette();
    pal.setColor(QPalette::WindowText, QColor(190, 190, 190));
    name_->setPalette(pal);
    lut_ = new QToolButton(this);
    lut_->setPopupMode(QToolButton::InstantPopup);
    lut_->setAutoRaise(true);
    lut_->setFocusPolicy(Qt::NoFocus);
    lut_->setCursor(Qt::ArrowCursor);
    lut_->setToolTip(tr("LUT for slot %1").arg(letter));
    l->addWidget(tag);
    l->addWidget(name_, 1);
    l->addWidget(lut_);
    setFile({});
    setLutLabel(tr("None"));
}

void CompareSlot::setFile(const QString& name) {
    empty_ = name.isEmpty();
    name_->setText(empty_ ? tr("Drop a clip here, or click to open") : name);
    update();
}

QString CompareSlot::fileText() const { return empty_ ? QString() : name_->text(); }

void CompareSlot::setLutLabel(const QString& label) { lut_->setText(tr("LUT: %1").arg(label)); }

void CompareSlot::dragEnterEvent(QDragEnterEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    e->acceptProposedAction();
    hover_ = true;
    update();
}

void CompareSlot::dragLeaveEvent(QDragLeaveEvent*) {
    hover_ = false;
    update();
}

void CompareSlot::dropEvent(QDropEvent* e) {
    hover_ = false;
    update();
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        e->acceptProposedAction();
        emit dropped(u.toLocalFile());
        return;  // one clip per slot
    }
}

void CompareSlot::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit clicked();
}

// Grey only (no accent colours): a solid frame when filled, dashed when empty, lighter
// while a drag is over it.
void CompareSlot::paintEvent(QPaintEvent* e) {
    QFrame::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    p.setBrush(hover_ ? QColor(85, 85, 85) : QColor(45, 45, 45));
    QPen pen(hover_ ? QColor(200, 200, 200) : QColor(110, 110, 110), 1.2);
    if (empty_) pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.drawRoundedRect(r, 4, 4);
}

CompareBar::CompareBar(QWidget* parent) : QWidget(parent) {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(4, 3, 4, 1);
    l->setSpacing(6);
    a_ = new CompareSlot("A", this);
    b_ = new CompareSlot("B", this);
    l->addWidget(a_, 1);
    l->addWidget(b_, 1);
    hide();
}
