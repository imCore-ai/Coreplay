#include "HotkeyOverlay.h"

#include <algorithm>
#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>

#include "Hotkeys.h"

namespace {
// The group of a command, from its id.
QString groupOf(const QString& id) {
    if (id.startsWith("file.")) return "File";
    if (id.startsWith("play.") || id.startsWith("nav.")) return "Playback";
    if (id.startsWith("compare.")) return "A/B Compare";
    if (id.startsWith("pns.")) return "Pan & Scan";
    if (id == "view.grading") return "Image";  // in the Image menu
    if (id.startsWith("view.") || id.startsWith("zoom.") || id == "player.newwindow") return "View";
    if (id.startsWith("vol.") || id.startsWith("stream.") || id.startsWith("sub.")) return "Audio & Subtitles";
    if (id.startsWith("channel.") || id.startsWith("seq.") || id.startsWith("lut.")) return "Image";
    return "Other";
}
const QStringList kOrder = {"Playback", "File", "View", "A/B Compare", "Image", "Audio & Subtitles", "Pan & Scan", "Other"};

// Menu text without its accelerator marks: "Pan&&Scan Reset" -> "Pan&Scan Reset".
QString plain(const char* label) {
    QString s = QString(label).replace("&&", QChar(0x1)).remove('&').replace(QChar(0x1), '&');
    if (s.endsWith("...")) s.chop(3);
    return s;
}

struct Layout {
    int lineH, colW, keyX, top, cols, used;  // columns that fit, columns in use
    bool fits;
};

// Where the columns go at a font size. A group starts on a new column only when its
// title and first row do not fit in the current one; a long group continues in the next.
Layout layoutAt(int px, const QRect& r, const auto& groups) {
    QFont f;
    f.setPixelSize(px);
    QFont b = f;
    b.setBold(true);
    const QFontMetrics fm(f), bm(b);
    Layout l;
    l.lineH = int(px * 1.5);
    int labelW = 0, keysW = 0;
    for (const auto& g : groups) {
        labelW = std::max(labelW, bm.horizontalAdvance(g.title));
        for (const auto& row : g.rows) {
            labelW = std::max(labelW, fm.horizontalAdvance(row.label));
            keysW = std::max(keysW, fm.horizontalAdvance(row.keys));
        }
    }
    const int gap = px * 2;
    l.keyX = labelW + px;
    l.colW = l.keyX + keysW + gap;
    l.top = px * 5;                                   // the title line
    const int marginX = px * 2;
    l.cols = std::max(1, (r.width() - 2 * marginX + gap) / l.colW);
    const int linesPerCol = (r.height() - l.top - px * 3) / l.lineH;
    int col = 0, line = 0;
    for (const auto& g : groups) {
        if (line > 0 && line + 3 > linesPerCol) col++, line = 0;   // title + a row + the space
        if (line > 0) line++;                                       // a blank line between groups
        line += 1 + int(g.rows.size());
        while (line > linesPerCol) col++, line -= linesPerCol;
    }
    l.used = col + 1;
    l.fits = linesPerCol >= 3 && col < l.cols;
    return l;
}
}  // namespace

HotkeyOverlay::HotkeyOverlay(QWidget* window) : QWidget(window) {
    setFocusPolicy(Qt::StrongFocus);
    hide();
    window->installEventFilter(this);
}

void HotkeyOverlay::fill() {
    groups_.clear();
    for (const QString& t : kOrder) groups_.push_back({t, {}});
    for (const auto& d : Hotkeys::load()) {
        if (d.keys.isEmpty()) continue;           // a command with no key: nothing to learn here
        QStringList keys;
        for (const auto& k : d.keys) {
            // Keys of media keyboards (Media Play, Volume Up...) carry their name on the
            // key itself; listed, they made the columns wide and the text small.
            const int key = k[0].key();
            if (key >= Qt::Key_VolumeDown && key <= Qt::Key_MediaLast) continue;
            keys << k.toString(QKeySequence::NativeText);
        }
        if (keys.isEmpty()) continue;
        const int g = int(kOrder.indexOf(groupOf(d.id)));
        groups_[g].rows.push_back({plain(d.label), keys.join(",  ")});
    }
    std::erase_if(groups_, [](const Group& g) { return g.rows.empty(); });
}

void HotkeyOverlay::fit() {
    fontPx_ = 8;
    for (int px = 17; px >= 8; --px) {
        if (layoutAt(px, rect(), groups_).fits) {
            fontPx_ = px;
            break;
        }
    }
}

void HotkeyOverlay::toggle() { isVisible() ? close() : open(); }

void HotkeyOverlay::open() {
    fill();
    setGeometry(parentWidget()->rect());
    fit();
    focusBefore_ = QApplication::focusWidget();
    raise();
    show();
    setFocus();
    update();
}

void HotkeyOverlay::close() {
    if (!isVisible()) return;
    hide();
    if (focusBefore_) focusBefore_->setFocus();
    focusBefore_ = nullptr;
}

int HotkeyOverlay::rows() const {
    int n = 0;
    for (const auto& g : groups_) n += int(g.rows.size());
    return isVisible() ? n : 0;
}

QString HotkeyOverlay::text() const {
    if (!isVisible()) return {};
    QStringList l;
    for (const auto& g : groups_)
        for (const auto& r : g.rows) l << r.label + " = " + r.keys;
    return l.join(" | ");
}

void HotkeyOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 200));
    const int px = fontPx_;
    const Layout l = layoutAt(px, rect(), groups_);
    const int marginX = px * 2;
    const int usedW = l.used * l.colW - px * 2;  // centred on the columns in use
    const int x0 = std::max(marginX, (width() - usedW) / 2);
    const int linesPerCol = (height() - l.top - px * 3) / l.lineH;

    QFont f;
    f.setPixelSize(px);
    QFont b = f;
    b.setBold(true);
    QFont h = f;
    h.setPixelSize(px * 2);
    h.setBold(true);

    p.setFont(h);
    p.setPen(QColor(235, 235, 235));
    p.drawText(QRect(0, px, width(), px * 3), Qt::AlignHCenter | Qt::AlignVCenter, "Hotkeys");
    p.setFont(f);
    p.setPen(QColor(140, 140, 140));
    p.drawText(QRect(0, height() - px * 3, width(), px * 2), Qt::AlignHCenter | Qt::AlignVCenter,
               "F1, Esc or a click closes this.  Change keys in Help > Edit Hotkeys.");

    int col = 0, line = 0;
    auto at = [&](int ln) { return QPoint(x0 + col * l.colW, l.top + ln * l.lineH); };
    for (const auto& g : groups_) {
        if (line > 0 && line + 3 > linesPerCol) col++, line = 0;
        if (line > 0) line++;
        p.setFont(b);
        p.setPen(QColor(120, 175, 255));
        QPoint o = at(line);
        p.drawText(QRect(o.x(), o.y(), l.colW, l.lineH), Qt::AlignLeft | Qt::AlignVCenter, g.title);
        line++;
        p.setFont(f);
        for (const auto& r : g.rows) {
            if (line >= linesPerCol) col++, line = 0;
            o = at(line);
            p.setPen(QColor(215, 215, 215));
            p.drawText(QRect(o.x(), o.y(), l.keyX, l.lineH), Qt::AlignLeft | Qt::AlignVCenter, r.label);
            p.setPen(QColor(255, 210, 120));
            p.drawText(QRect(o.x() + l.keyX, o.y(), l.colW - l.keyX, l.lineH), Qt::AlignLeft | Qt::AlignVCenter, r.keys);
            line++;
        }
    }
}

void HotkeyOverlay::mousePressEvent(QMouseEvent*) { close(); }

void HotkeyOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

// Esc closes this list, not full screen (it is also the Exit Full Screen key).
bool HotkeyOverlay::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape &&
        static_cast<QKeyEvent*>(e)->modifiers() == Qt::NoModifier) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

bool HotkeyOverlay::eventFilter(QObject* o, QEvent* e) {
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
        fit();
        raise();
    }
    return QWidget::eventFilter(o, e);
}
