#include "SeekBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

SeekBar::SeekBar(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::NoFocus);
    setMinimumHeight(16);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void SeekBar::setCacheMask(std::vector<bool> cached, std::vector<bool> missing) {
    cached_ = std::move(cached);
    missing_ = std::move(missing);
    update();
}

double SeekBar::toSeconds(int x) const {
    const double t = std::clamp(double(x - 4) / std::max(1, width() - 8), 0.0, 1.0);
    return t * dur_;
}

void SeekBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect track = rect().adjusted(4, height() / 2 - 3, -4, -(height() / 2 - 3));
    p.fillRect(track, palette().color(QPalette::Mid));
    auto band = [&](const std::vector<bool>& mask, const QColor& c, int top, int h) {
        const int n = int(mask.size());
        if (!n) return;
        for (int i = 0; i < n;) {
            if (!mask[size_t(i)]) { ++i; continue; }
            int j = i;
            while (j < n && mask[size_t(j)]) ++j;
            const int x0 = track.left() + int(double(i) / n * track.width());
            const int x1 = track.left() + int(double(j) / n * track.width());
            p.fillRect(QRect(x0, top, std::max(1, x1 - x0), h), c);
            i = j;
        }
    };
    band(cached_, QColor(140, 140, 140), track.top(), track.height());
    band(missing_, QColor(225, 225, 225), track.bottom() - 1, 3);  // greys only: no accent colours
    if (dur_ > 0) {
        if (a_ >= 0 || b_ >= 0) {
            const double a = a_ >= 0 ? a_ : 0, b = b_ >= 0 ? b_ : dur_;
            const int xa = track.left() + int(a / dur_ * track.width());
            const int xb = track.left() + int(b / dur_ * track.width());
            p.fillRect(QRect(xa, 1, std::max(1, xb - xa), 3), QColor(170, 170, 170));
        }
        const int x = track.left() + int(std::clamp(pos_ / dur_, 0.0, 1.0) * track.width());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(220, 220, 220));
        p.drawRect(QRect(x - 2, 1, 4, height() - 2));
    }
}

void SeekBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || dur_ <= 0) return;
    dragging_ = true;
    pos_ = toSeconds(int(e->position().x()));
    update();
    emit seekRequested(pos_);
}

void SeekBar::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    pos_ = toSeconds(int(e->position().x()));
    update();
    emit seekRequested(pos_);
}

void SeekBar::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }
