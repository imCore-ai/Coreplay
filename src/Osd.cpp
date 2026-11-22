#include "Osd.h"

#include <QFontDatabase>
#include <QPainter>

Osd::Osd(QWidget* parent, QColor text, int pixelSize, bool monospace) : QLabel(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, text);
    setPalette(pal);
    QFont f = monospace ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : font();
    f.setPixelSize(pixelSize);
    setFont(f);
    setContentsMargins(10, 6, 10, 6);
    hide();
    hide_.setSingleShot(true);
    connect(&hide_, &QTimer::timeout, this, &QLabel::hide);
}

void Osd::paintEvent(QPaintEvent* e) {
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(rect(), 3, 3);
    }
    QLabel::paintEvent(e);
}

void Osd::showMessage(const QString& text, int ms) {
    setText(text);
    adjustSize();
    move(12, 12);
    raise();
    show();
    hide_.start(ms);
}
