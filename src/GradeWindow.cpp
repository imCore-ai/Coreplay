#include "GradeWindow.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// Only this window has a style sheet, and it is built the first time Alt+G is pressed
// (a style sheet at start-up costs ~300 ms, see Osd.h).
const char* kStyle = R"(
QLabel { color: #b8b8b8; font-size: 12px; }
QLabel#title { color: #e6e6e6; font-size: 13px; font-weight: 600; }
QLabel#clip { color: #8a8a8a; }
QLabel#grip { color: #6a6a6a; font-size: 14px; }
QLabel#section { color: #7c7c7c; font-size: 11px; font-weight: 600; letter-spacing: 1px; }
QLabel#value { color: #d8d8d8; font-family: Consolas, monospace; }
QLabel#valueDefault { color: #6c6c6c; font-family: Consolas, monospace; }
QToolButton { color: #9a9a9a; background: transparent; border: none; font-size: 13px; padding: 0 2px; }
QToolButton:hover { color: #ffffff; }
QToolButton:disabled { color: #444444; }
QPushButton { color: #cfcfcf; background: rgba(255,255,255,18); border: 1px solid rgba(255,255,255,40);
              border-radius: 3px; padding: 4px 10px; font-size: 12px; }
QPushButton:hover { background: rgba(255,255,255,40); color: #ffffff; }
QPushButton:disabled { color: #5a5a5a; border-color: rgba(255,255,255,18); }
QSlider::groove:horizontal { height: 3px; background: rgba(255,255,255,55); border-radius: 1px; }
QSlider::sub-page:horizontal { background: rgba(120,175,255,170); border-radius: 1px; }
QSlider::handle:horizontal { width: 11px; height: 11px; margin: -4px 0; border-radius: 5px; background: #e0e0e0; }
)";

QString fmt(double v, int decimals, const QString& suffix) { return QString::number(v, 'f', decimals) + suffix; }
}  // namespace

// ------------------------------------------------------------------ wheel

GradeWheel::GradeWheel(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::CrossCursor);
    setFixedSize(sizeHint());
    setToolTip("Drag to tint this tone range. Double-click to reset.");
}

void GradeWheel::setTint(const GradeTint& t) {
    t_ = t;
    update();
}

void GradeWheel::paintEvent(QPaintEvent*) {
    const int s = width();
    const double r = s / 2.0 - 4;
    if (disc_.size() != size() * devicePixelRatio()) {
        // hsl(angle, strength, 50%), red at 12 o'clock, clockwise; as BunnyLab's wheel.
        const qreal dpr = devicePixelRatio();
        disc_ = QImage(size() * dpr, QImage::Format_ARGB32_Premultiplied);
        disc_.setDevicePixelRatio(dpr);
        disc_.fill(Qt::transparent);
        const double c = s / 2.0;
        for (int y = 0; y < disc_.height(); ++y)
            for (int x = 0; x < disc_.width(); ++x) {
                const double dx = (x + 0.5) / dpr - c, dy = (y + 0.5) / dpr - c;
                const double d = std::hypot(dx, dy);
                if (d > r + 0.5) continue;
                double a = std::atan2(dy, dx) * 180 / kPi + 90;
                if (a < 0) a += 360;
                const double st = std::min(1.0, d / r);
                QColor col = QColor::fromHslF(float(a / 360.0), float(st), 0.5f);
                // Darker centre, as BunnyLab's.
                const double dark = std::max(0.0, 1.0 - d / (r * 0.3)) * 0.7;
                col = QColor::fromRgbF(float(col.redF() * (1 - dark) + 0.118 * dark), float(col.greenF() * (1 - dark) + 0.118 * dark),
                                       float(col.blueF() * (1 - dark) + 0.118 * dark));
                const double edge = std::clamp(r + 0.5 - d, 0.0, 1.0);  // soft rim
                disc_.setPixel(x, y, qPremultiply(QColor(col.red(), col.green(), col.blue(), int(255 * edge)).rgba()));
            }
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(0, 0, disc_);
    p.setPen(QPen(QColor(70, 70, 70), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(s / 2.0, s / 2.0), r, r);
    const double a = (t_.angle - 90) * kPi / 180, d = t_.strength * r;
    const QPointF dot(s / 2.0 + std::cos(a) * d, s / 2.0 + std::sin(a) * d);
    p.setPen(QPen(Qt::black, 1.5));
    p.setBrush(Qt::white);
    p.drawEllipse(dot, 4, 4);
}

void GradeWheel::pick(QPointF pos) {
    const double c = width() / 2.0, r = width() / 2.0 - 4;
    const double x = pos.x() - c, y = pos.y() - c;
    double a = std::atan2(y, x) * 180 / kPi + 90;
    if (a < 0) a += 360;
    GradeTint t{double(int(std::lround(a)) % 360), std::round(std::min(std::hypot(x, y) / r, 1.0) * 100) / 100};
    if (t == t_) return;
    t_ = t;
    update();
    emit tintChanged(t_);
}

void GradeWheel::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) pick(e->position());
}

void GradeWheel::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) pick(e->position());
}

void GradeWheel::mouseDoubleClickEvent(QMouseEvent*) {
    t_ = {};
    update();
    emit tintChanged(t_);
}

// ------------------------------------------------------------------ window

GradeWindow::GradeWindow(QWidget* player) : QWidget(player, Qt::Tool | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle("Color Grading");
    setStyleSheet(kStyle);
    setFocusPolicy(Qt::StrongFocus);

    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(14, 10, 14, 14);
    v->setSpacing(8);

    // Title bar: the handle that moves the window (the empty panel moves it too).
    bar_ = new QWidget;
    bar_->setCursor(Qt::SizeAllCursor);
    bar_->setToolTip("Drag to move");
    auto* top = new QHBoxLayout(bar_);
    top->setContentsMargins(0, 0, 0, 0);
    auto* grip = new QLabel(QString::fromUtf8("⠿"));  // a drag grip (six dots)
    grip->setObjectName("grip");
    top->addWidget(grip);
    auto* title = new QLabel("Color Grading");
    title->setObjectName("title");
    clip_ = new QLabel;
    clip_->setObjectName("clip");
    auto* close = new QToolButton;
    close->setText("✕");
    close->setToolTip("Close (Alt+G)");
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    top->addWidget(title);
    top->addSpacing(8);
    top->addWidget(clip_, 1);
    top->addWidget(close);
    v->addWidget(bar_);

    auto section = [&](const QString& t) {
        auto* l = new QLabel(t.toUpper());
        l->setObjectName("section");
        v->addWidget(l);
    };
    section("Primary");
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(2, 1);
    bright_ = addRow(grid, 0, "Brightness", 0, 200, 1, 100, 0, "");
    contrast_ = addRow(grid, 1, "Contrast", 0, 200, 1, 100, 0, "");
    gamma_ = addRow(grid, 2, "Gamma", 0.1, 3, 0.05, 1, 2, "");
    sat_ = addRow(grid, 3, "Saturation", 0, 200, 1, 100, 0, "");
    hue_ = addRow(grid, 4, "Hue", 0, 360, 1, 0, 0, "°");
    v->addLayout(grid);

    v->addSpacing(4);
    section("Tone & Tint");
    auto* wheels = new QHBoxLayout;
    wheels->setSpacing(10);
    const char* names[] = {"Shadows", "Midtones", "Highlights"};
    for (int i = 0; i < 3; ++i) {
        auto* col = new QVBoxLayout;
        col->setSpacing(4);
        auto* head = new QHBoxLayout;
        head->addStretch(1);
        auto* label = new QLabel(names[i]);
        head->addWidget(label);
        Tone& t = tones_[i];
        t.wheelReset = new QToolButton;
        t.wheelReset->setText("↺");
        t.wheelReset->setToolTip(QString("Reset the %1 tint").arg(QString(names[i]).toLower()));
        t.wheelReset->setFocusPolicy(Qt::NoFocus);
        head->addWidget(t.wheelReset);
        head->addStretch(1);
        col->addLayout(head);
        t.wheel = new GradeWheel;
        col->addWidget(t.wheel, 0, Qt::AlignHCenter);
        auto* g = new QGridLayout;
        g->setHorizontalSpacing(2);
        t.tone = addRow(g, 0, QString(), -100, 100, 1, 0, 0, "");
        col->addLayout(g);
        wheels->addLayout(col);
        connect(t.wheel, &GradeWheel::tintChanged, this, [this, i](const GradeTint& tint) {
            GradeTint* dst[] = {&g_.shadowsTint, &g_.midtonesTint, &g_.highlightsTint};
            *dst[i] = tint;
            emitChange();
        });
        connect(t.wheelReset, &QToolButton::clicked, this, [this, i] {
            GradeTint* dst[] = {&g_.shadowsTint, &g_.midtonesTint, &g_.highlightsTint};
            *dst[i] = {};
            emitChange();
        });
    }
    v->addLayout(wheels);

    v->addSpacing(4);
    auto* buttons = new QHBoxLayout;
    auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(b);
        return b;
    };
    QPushButton* reset = button("↺ Reset", "Remove this clip's grade");
    buttons->addStretch(1);
    QPushButton* save = button("Save", "Write the graded clip over the file (Ctrl+S).\nThe original goes to the Recycle Bin.");
    QPushButton* saveAs = button("Save As...", "Write the graded clip to a new file (Ctrl+Shift+S)");
    QPushButton* exp = button("Export...", "Export with every option, the grade baked in");
    connect(reset, &QPushButton::clicked, this, [this] {
        g_ = Grade();
        emitChange();
    });
    connect(save, &QPushButton::clicked, this, &GradeWindow::saveRequested);
    connect(saveAs, &QPushButton::clicked, this, &GradeWindow::saveAsRequested);
    connect(exp, &QPushButton::clicked, this, &GradeWindow::exportRequested);
    v->addLayout(buttons);

    setFixedWidth(460);
    showValues();
}

GradeWindow::Row GradeWindow::addRow(QGridLayout* grid, int r, const QString& label, double min, double max, double step,
                                     double def, int decimals, const QString& suffix) {
    Row row;
    row.scale = 1.0 / step;
    row.def = def;
    row.decimals = decimals;
    row.suffix = suffix;
    row.reset = new QToolButton;
    row.reset->setText("↺");
    row.reset->setToolTip("Reset");
    row.reset->setFocusPolicy(Qt::NoFocus);
    row.slider = new QSlider(Qt::Horizontal);
    row.slider->setFocusPolicy(Qt::NoFocus);
    row.slider->setRange(int(std::lround(min * row.scale)), int(std::lround(max * row.scale)));
    row.slider->setSingleStep(1);
    row.slider->setPageStep(int(std::lround(10 * row.scale * step)));
    row.value = new QLabel;
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row.value->setMinimumWidth(38);
    int c = 0;
    grid->addWidget(row.reset, r, c++);
    if (!label.isEmpty()) {
        auto* l = new QLabel(label);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        l->setMinimumWidth(72);
        grid->addWidget(l, r, c++);
    }
    grid->addWidget(row.slider, r, label.isEmpty() ? 1 : 2);
    grid->addWidget(row.value, r, label.isEmpty() ? 2 : 3);
    if (label.isEmpty()) grid->setColumnStretch(1, 1);

    // Which Grade field this row edits, by its address in the struct.
    auto field = [this, label, r]() -> double* {
        if (label == "Brightness") return &g_.brightness;
        if (label == "Contrast") return &g_.contrast;
        if (label == "Gamma") return &g_.gamma;
        if (label == "Saturation") return &g_.saturation;
        if (label == "Hue") return &g_.hue;
        return nullptr;
    };
    QSlider* s = row.slider;
    const double scale = row.scale;
    connect(s, &QSlider::valueChanged, this, [this, s, scale, field](int iv) {
        if (updating_) return;
        double* f = field();
        if (!f) {  // a tone slider: find which one
            for (int i = 0; i < 3; ++i)
                if (tones_[i].tone.slider == s) f = i == 0 ? &g_.shadows : i == 1 ? &g_.midtones : &g_.highlights;
        }
        if (!f) return;
        *f = iv / scale;
        emitChange();
    });
    connect(row.reset, &QToolButton::clicked, this, [s, scale, def] { s->setValue(int(std::lround(def * scale))); });
    Q_UNUSED(r);
    return row;
}

void GradeWindow::showValues() {
    updating_ = true;
    auto show = [](Row& row, double v) {
        row.slider->setValue(int(std::lround(v * row.scale)));
        const bool isDef = std::abs(v - row.def) < 1e-9;
        row.value->setText(fmt(v, row.decimals, row.suffix));
        row.value->setObjectName(isDef ? "valueDefault" : "value");
        row.value->style()->unpolish(row.value);
        row.value->style()->polish(row.value);
        row.reset->setEnabled(!isDef);
    };
    show(bright_, g_.brightness);
    show(contrast_, g_.contrast);
    show(gamma_, g_.gamma);
    show(sat_, g_.saturation);
    show(hue_, g_.hue);
    const double tones[] = {g_.shadows, g_.midtones, g_.highlights};
    const GradeTint tints[] = {g_.shadowsTint, g_.midtonesTint, g_.highlightsTint};
    for (int i = 0; i < 3; ++i) {
        show(tones_[i].tone, tones[i]);
        tones_[i].wheel->setTint(tints[i]);
        tones_[i].wheelReset->setEnabled(tints[i].strength > 0);
    }
    updating_ = false;
}

void GradeWindow::emitChange() {
    showValues();
    emit gradeChanged(g_);
}

void GradeWindow::setGrade(const Grade& g) {
    g_ = g;
    showValues();
}

void GradeWindow::setClipName(const QString& name, bool modified) {
    const QString text = (modified ? "* " : "") + name;
    clip_->setText(clip_->fontMetrics().elidedText(text, Qt::ElideMiddle, 260));
    clip_->setToolTip(modified ? name + "\nGraded, not saved" : name);
}

// The first time: where the user left it last time, else the right side of the player.
void GradeWindow::placeNear(QWidget* player) {
    if (placed_) return;
    placed_ = true;
    adjustSize();
    const QVariant saved = QSettings().value("grade/pos");
    if (saved.isValid() && QGuiApplication::screenAt(saved.toPoint() + QPoint(40, 20))) {
        move(keepOnScreen(saved.toPoint()));
        return;
    }
    const QRect r = player->frameGeometry();
    move(keepOnScreen(QPoint(r.right() - width() - 24, r.top() + 70)));
}

// The title bar always stays on a screen, so the window can never be lost.
QPoint GradeWindow::keepOnScreen(QPoint p) const {
    QScreen* s = QGuiApplication::screenAt(p + QPoint(width() / 2, 12));
    if (!s) s = screen();
    if (!s) return p;
    const QRect a = s->availableGeometry();
    p.setX(std::clamp(p.x(), a.left() - width() + 80, a.right() - 80));
    p.setY(std::clamp(p.y(), a.top(), a.bottom() - 40));
    return p;
}

// Tests: drag the title bar by (dx, dy) with mouse events, as a hand would.
void GradeWindow::testDrag(int dx, int dy) {
    const QPoint local(bar_->width() / 2, bar_->height() / 2);
    const QPoint g0 = bar_->mapToGlobal(local);
    auto send = [&](QEvent::Type t, QPoint g, Qt::MouseButton b, Qt::MouseButtons bs) {
        QMouseEvent ev(t, QPointF(bar_->mapFromGlobal(g)), QPointF(g), b, bs, Qt::NoModifier);
        QApplication::sendEvent(bar_, &ev);
    };
    send(QEvent::MouseButtonPress, g0, Qt::LeftButton, Qt::LeftButton);
    for (int k = 1; k <= 8; ++k) send(QEvent::MouseMove, g0 + QPoint(dx * k / 8, dy * k / 8), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, g0 + QPoint(dx, dy), Qt::LeftButton, Qt::NoButton);
}

bool GradeWindow::setControl(const QString& name, double value) {
    double* f = nullptr;
    if (name == "brightness") f = &g_.brightness;
    else if (name == "contrast") f = &g_.contrast;
    else if (name == "gamma") f = &g_.gamma;
    else if (name == "saturation") f = &g_.saturation;
    else if (name == "hue") f = &g_.hue;
    else if (name == "shadows") f = &g_.shadows;
    else if (name == "midtones") f = &g_.midtones;
    else if (name == "highlights") f = &g_.highlights;
    else if (name.endsWith("Tint.angle") || name.endsWith("Tint.strength")) {
        GradeTint* t = name.startsWith("shadows") ? &g_.shadowsTint : name.startsWith("midtones") ? &g_.midtonesTint
                     : name.startsWith("highlights")                  ? &g_.highlightsTint : nullptr;
        if (!t) return false;
        f = name.endsWith("angle") ? &t->angle : &t->strength;
    } else if (name == "reset") {
        g_ = Grade();
        emitChange();
        return true;
    }
    if (!f) return false;
    *f = value;
    emitChange();
    return true;
}

void GradeWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 38), 1));
    p.setBrush(QColor(18, 18, 18, 205));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

// Drag anywhere that is not a control to move the window.
void GradeWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    dragging_ = true;
    drag_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void GradeWindow::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) move(keepOnScreen(e->globalPosition().toPoint() - drag_));
}

void GradeWindow::mouseReleaseEvent(QMouseEvent*) {
    if (!dragging_) return;
    dragging_ = false;
    QSettings().setValue("grade/pos", pos());  // opens there next time
}

// The player's hotkeys work while this window is active: no control here takes keys.
void GradeWindow::keyPressEvent(QKeyEvent* e) { emit keyForPlayer(e); }

bool GradeWindow::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride) {  // let every key reach keyPressEvent
        e->accept();
        return true;
    }
    return QWidget::event(e);
}
