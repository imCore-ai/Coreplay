#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QWheelEvent>
#include <QResizeEvent>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QCheckBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>

#include "Export.h"
#include "Hotkeys.h"
#include "Subtitles.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include "GradeWindow.h"
#include "HotkeyOverlay.h"
#include "Updater.h"
#include "SettingsDialog.h"
#include "CompareBar.h"
#include "MpvEngine.h"
#include "Osd.h"
#include "Playlist.h"
#include "SeekBar.h"
#include "seq/SequenceEngine.h"

// Save As of a sequence: the same name in <dir>, <prefix>%0Nd<suffix>. The pattern is joined
// outside arg(): inside an arg() string Qt reads "%0" + digits as a place marker.
static QString gradedSequenceOutput(const QString& dir, const Sequence& s) {
    return dir + "/" + s.prefix + "%0" + QString::number(std::max(4, s.padding)) + "d" + s.suffix;
}

namespace {
// MPC-HC defaults (AppSettings.h DEFAULT_JUMPDISTANCE_1/2/3, volume step 5).
constexpr double kJumpSmall = 1.0;
constexpr double kJumpMedium = 5.0;
constexpr double kJumpLarge = 20.0;
constexpr int kVolumeStep = 5;
constexpr int kRecentMax = 20;

// Every control-bar icon is drawn in this one gray (the R / G / B letters use it too).
const QColor kIconGray(160, 160, 160);

// The style's icons in that flat gray: its own shape (alpha), no shading, no accents.
// Under the mouse the icon takes hoverInk(), the colour that reads on the style's hover
// background (QIcon::Active is the mode Qt draws for a hovered auto-raise button).
// Coreplay draws with Fusion and its own dark palette on every system (main.cpp), so the
// hover background is dark and the hovered icon is light. (It asked the style's name, and
// the style wrapped in a proxy reports none: the icons turned dark on dark.)
QColor hoverInk() { return QColor(245, 245, 245); }

// One icon in two colours: `normal`, and hoverInk() when hovered.
QIcon twoTone(const std::function<QPixmap(int px, const QColor& c)>& draw, const QColor& normal = kIconGray) {
    QIcon out;
    for (int px : {16, 24, 32}) {
        out.addPixmap(draw(px, normal), QIcon::Normal);
        out.addPixmap(draw(px, hoverInk()), QIcon::Active);
    }
    return out;
}

QIcon grayIcon(QStyle* style, QStyle::StandardPixmap sp) {
    return twoTone([style, sp](int px, const QColor& c) {
        QImage img = style->standardIcon(sp).pixmap(px, px).toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) line[x] = qRgba(c.red(), c.green(), c.blue(), qAlpha(line[x]));
        }
        return QPixmap::fromImage(img);
    });
}

// Frame-step icons as in editing apps: a bar beside a triangle (|< back, >| forward),
// in the same gray as the other control icons.
QIcon frameStepIcon(bool forward) {
    return twoTone([forward](int px, const QColor& c) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const double s = px / 16.0;
        const double barW = 2.0 * s, top = 3.0 * s, bottom = 13.0 * s;
        if (forward) {
            const QPointF tri[3] = {{3.5 * s, top}, {11.0 * s, 8.0 * s}, {3.5 * s, bottom}};
            p.drawPolygon(tri, 3);
            p.drawRect(QRectF(11.5 * s, top, barW, bottom - top));
        } else {
            p.drawRect(QRectF(2.5 * s, top, barW, bottom - top));
            const QPointF tri[3] = {{12.5 * s, top}, {5.0 * s, 8.0 * s}, {12.5 * s, bottom}};
            p.drawPolygon(tri, 3);
        }
        p.end();
        return pm;
    });
}

// Loop: a circle of two arrows chasing each other (the usual loop sign). Bright when
// loop is on, dim when it is off (the button has no pressed background of its own).
// On: the strongest ink on the bar; off: dimmer than the other icons. The bar follows the
// system theme, so the inks swap on a light bar (bright "on" on a light bar looked disabled).
QIcon loopIcon(bool on, bool lightBar) {
    return twoTone([](int px, const QColor& c) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const double s = px / 16.0, r = 5.2 * s;
        const QPointF ctr(8.0 * s, 8.0 * s);
        const QRectF box(ctr.x() - r, ctr.y() - r, 2 * r, 2 * r);
        p.setPen(QPen(c, 1.7 * s, Qt::SolidLine, Qt::FlatCap));
        p.setBrush(Qt::NoBrush);
        // Two arcs (Qt angles: degrees counter-clockwise from 3 o'clock), each ending in an
        // arrow head that points on round the circle.
        constexpr double kPi = 3.14159265358979323846;
        for (double startDeg : {25.0, 205.0}) {
            const double spanDeg = 120.0;
            p.setPen(QPen(c, 1.7 * s, Qt::SolidLine, Qt::FlatCap));
            p.setBrush(Qt::NoBrush);
            p.drawArc(box, int(startDeg * 16), int(spanDeg * 16));
            const double a = (startDeg + spanDeg) * kPi / 180.0;
            const QPointF at(ctr.x() + r * std::cos(a), ctr.y() - r * std::sin(a));
            const QPointF tangent(-std::sin(a), -std::cos(a));   // counter-clockwise, screen y down
            const QPointF radial(std::cos(a), -std::sin(a));
            const double len = 3.4 * s, half = 2.6 * s;
            const QPointF tri[3] = {at + tangent * len, at + radial * half, at - radial * half};
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPolygon(tri, 3);
        }
        p.end();
        return pm;
    }, lightBar ? (on ? QColor(20, 20, 20) : QColor(185, 185, 185)) : (on ? QColor(225, 225, 225) : QColor(95, 95, 95)));
}
// The Recycle Bin (macOS: the Trash; Linux: the freedesktop Trash). Qt's Linux trash needs
// the Trash folder to exist already; a new account may not have it, so it is made first.
bool moveToRecycleBin(const QString& path) {
#ifdef Q_OS_LINUX
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Trash";
    QDir().mkpath(trash + "/files");
    QDir().mkpath(trash + "/info");
#endif
    return QFile::moveToTrash(path);
}
}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("Coreplay");
    setAcceptDrops(true);
    buildUi();
    buildActions();
    buildMenus();
    applyHotkeys();

    QSettings st;
    if (!restoreGeometry(st.value("win/geometry").toByteArray())) resize(1280, 800);
    restoreState(st.value("win/state").toByteArray());
    volume_ = st.value("audio/volume", 100).toInt();
    muted_ = st.value("audio/muted", false).toBool();
    repeat_ = Repeat(st.value("play/repeat", int(Repeat::All)).toInt());  // loop is on unless turned off
    if (repeat_ != Repeat::None) lastLoop_ = repeat_;
    {
        const QSignalBlocker block(volSlider_);  // no "Volume" message at start-up
        volSlider_->setValue(volume_);
    }
    act("view.playlist")->setChecked(playlistDock_->isVisible());
    if (repeat_ == Repeat::One) act("play.repeat.one")->setChecked(true);
    else if (repeat_ == Repeat::All) act("play.repeat.all")->setChecked(true);
    else act("play.repeat.none")->setChecked(true);
    updateLoopButton();

    hotkeyWatcher_.addPath(Hotkeys::configPath());
    connect(&hotkeyWatcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& p) {
        applyHotkeys();
        if (!hotkeyWatcher_.files().contains(p)) hotkeyWatcher_.addPath(p);  // editors replace the file
        osd_->showMessage(tr("Hotkeys reloaded"));
    });

    statusTimer_.setInterval(250);
    connect(&statusTimer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    statusTimer_.start();

    hideCursor_.setSingleShot(true);
    hideCursor_.setInterval(2000);
    connect(&hideCursor_, &QTimer::timeout, this, [this] {
        if (!isFullScreen()) return;
        if (bottom_->underMouse()) {
            hideCursor_.start();
            return;
        }
        videoArea_->setCursor(Qt::BlankCursor);
        bottom_->hide();
    });
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow() {
    // Qt deletes the child widgets AFTER this class's members are gone. Cut every
    // path back into this object first: a dock that hides on deletion would call
    // its visibilityChanged handler, and the app-wide event filter would still run.
    qApp->removeEventFilter(this);
    playlistDock_->disconnect(this);
    infoDock_->disconnect(this);
    statusTimer_.stop();
    // Engines first: each one frees its GL resources while its view still exists.
    delete mpv_;
    delete seq_;
}

// ------------------------------------------------------------------ UI

void MainWindow::buildUi() {
    videoArea_ = new QWidget(this);
    videoArea_->setAutoFillBackground(true);
    QPalette pal = videoArea_->palette();
    pal.setColor(QPalette::Window, Qt::black);
    videoArea_->setPalette(pal);
    stack_ = new QStackedLayout(videoArea_);
    stack_->setStackingMode(QStackedLayout::StackOne);
    // The window is an OpenGL window from its first frame. The engines' OpenGL views are
    // added after the first paint (the warm-up), and a shown window that gets its first
    // OpenGL view is destroyed and made again by Qt: it vanished and came back at every
    // start. A hidden OpenGL view here makes that switch before the window is shown.
    (new QOpenGLWidget(videoArea_))->hide();
    placeholder_ = new QLabel(videoArea_);
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setTextFormat(Qt::PlainText);  // rich text costs start-up time
    // The empty player: the Coreplay logo in the centre, the hint under it. The logo
    // image loads right after the first paint (start-up time stays the same).
    {
        auto* pl = new QVBoxLayout(placeholder_);
        pl->setSpacing(22);
        pl->addStretch(1);
        logo_ = new QLabel(placeholder_);
        logo_->setAlignment(Qt::AlignCenter);
        logo_->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* hint = new QLabel(tr("Drop files here, or press Ctrl+O"), placeholder_);
        hint->setAlignment(Qt::AlignCenter);
        hint->setTextFormat(Qt::PlainText);  // rich text costs start-up time
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        QPalette ppal = hint->palette();
        ppal.setColor(QPalette::WindowText, QColor(110, 110, 110));
        hint->setPalette(ppal);
        pl->addWidget(logo_);
        pl->addWidget(hint);
        pl->addStretch(1);
    }
    stack_->addWidget(placeholder_);
    osd_ = new Osd(videoArea_);
    stats_ = new Osd(videoArea_, QColor(210, 210, 210), 12, true);
    infoOverlay_ = new Osd(videoArea_, QColor(220, 220, 220), 12, true);
    hotkeyList_ = new HotkeyOverlay(this);

    seekBar_ = new SeekBar(this);
    connect(seekBar_, &SeekBar::seekRequested, this, [this](double s) { if (cur_) cur_->seekAbsolute(s); });

    controls_ = new QWidget(this);
    auto* cl = new QHBoxLayout(controls_);
    cl->setContentsMargins(4, 0, 4, 2);
    cl->setSpacing(2);
    auto button = [&](QStyle::StandardPixmap icon, const QString& tip, std::function<void()> fn) {
        auto* b = new QToolButton(controls_);
        b->setIcon(grayIcon(style(), icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, fn);
        cl->addWidget(b);
        return b;
    };
    playBtn_ = button(QStyle::SP_MediaPlay, tr("Play/Pause"), [this] { act("play.playpause")->trigger(); });
    button(QStyle::SP_MediaStop, tr("Stop"), [this] { act("play.stop")->trigger(); });
    button(QStyle::SP_MediaSkipBackward, tr("Previous"), [this] { act("nav.prev")->trigger(); });
    button(QStyle::SP_MediaSkipForward, tr("Next"), [this] { act("nav.next")->trigger(); });
    button(QStyle::SP_ArrowLeft, tr("Frame Step Back (Left)"), [this] { act("play.framestepback")->trigger(); })
        ->setIcon(frameStepIcon(false));
    button(QStyle::SP_ArrowRight, tr("Frame Step (Right)"), [this] { act("play.framestep")->trigger(); })
        ->setIcon(frameStepIcon(true));
    // Loop on / off (Play menu). Right-click: loop this file or the playlist.
    loopBtn_ = button(QStyle::SP_BrowserReload, tr("Loop"), [this] {
        act(repeat_ == Repeat::None ? (lastLoop_ == Repeat::One ? "play.repeat.one" : "play.repeat.all") : "play.repeat.none")
            ->trigger();
    });
    loopBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(loopBtn_, &QToolButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu m(this);
        for (const char* id : {"play.repeat.none", "play.repeat.one", "play.repeat.all"}) m.addAction(act(id));
        m.exec(loopBtn_->mapToGlobal(pos));
    });
    cl->addSpacing(12);
    // R / G / B channel solo, as in BunnyLab: one at a time, click again for full colour.
    static const char* chanName[] = {"R", "G", "B"};
    static const char* chanTip[] = {QT_TRANSLATE_NOOP("MainWindow", "Red channel (R)"),
                                    QT_TRANSLATE_NOOP("MainWindow", "Green channel (G)"),
                                    QT_TRANSLATE_NOOP("MainWindow", "Blue channel (B)")};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton(controls_);
        b->setText(chanName[i]);
        b->setToolTip(tr(chanTip[i]));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        QFont f = b->font();
        f.setBold(true);
        b->setFont(f);
        b->setMinimumWidth(26);
        connect(b, &QToolButton::clicked, this, [this, i] { setChannel(i + 1, true); });
        b->installEventFilter(this);  // dark letter while hovered (eventFilter)
        cl->addWidget(b);
        chanBtn_[i] = b;
    }
    styleChannelButtons();
    cl->addStretch(1);
    timeLabel_ = new QLabel(controls_);
    // The platform's own fixed-width font: naming families that are not installed
    // (Menlo on Windows) makes Qt scan every font, ~250 ms of start-up.
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    timeLabel_->setFont(mono);
    cl->addWidget(timeLabel_);
    cl->addSpacing(10);
    muteBtn_ = button(QStyle::SP_MediaVolume, tr("Mute"), [this] { act("vol.mute")->trigger(); });
    updateMuteButton();
    volSlider_ = new QSlider(Qt::Horizontal, controls_);
    volSlider_->setRange(0, 100);
    volSlider_->setFixedWidth(90);
    volSlider_->setFocusPolicy(Qt::NoFocus);
    connect(volSlider_, &QSlider::valueChanged, this, [this](int v) { setVolume(v); });
    cl->addWidget(volSlider_);

    bottom_ = new QWidget(this);
    auto* bl = new QVBoxLayout(bottom_);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);
    compareBar_ = new CompareBar(bottom_);
    bl->addWidget(compareBar_);
    bl->addWidget(seekBar_);
    connect(compareBar_->slotB(), &CompareSlot::dropped, this, [this](const QString& p) { startCompare(p); });
    connect(compareBar_->slotB(), &CompareSlot::clicked, this, [this] { startCompare(chooseClip(tr("Open a Clip for Slot B"))); });
    connect(compareBar_->slotA(), &CompareSlot::dropped, this, [this](const QString& p) { setSlotA(p); });
    connect(compareBar_->slotA(), &CompareSlot::clicked, this, [this] { setSlotA(chooseClip(tr("Open a Clip for Slot A"))); });
    for (int i = 0; i < 2; ++i) {
        CompareSlot* slot = i == 0 ? compareBar_->slotA() : compareBar_->slotB();
        auto* menu = new QMenu(slot->lutButton());
        slot->lutButton()->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, i] {
            menu->clear();
            if (i == 0) fillLutMenu(menu, lut_, [this](const LutChoice& c) { setLut(c); });
            else fillLutMenu(menu, cmpLutB_, [this](const LutChoice& c) { setLutB(c); });
        });
    }
    bl->addWidget(controls_);

    auto* central = new QWidget(this);
    auto* vl = new QVBoxLayout(central);
    centralLayout_ = vl;
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(videoArea_, 1);
    vl->addWidget(bottom_);
    setCentralWidget(central);

    // "1:1" and "Fit" float over the top-right corner of the picture, only while zoomed.
    auto overlayButton = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(videoArea_);
        b->setText(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoFillBackground(true);
        QPalette p = b->palette();
        p.setColor(QPalette::Button, QColor(20, 20, 20, 190));
        p.setColor(QPalette::Window, QColor(20, 20, 20, 190));
        p.setColor(QPalette::ButtonText, Qt::white);
        b->setPalette(p);
        b->setMinimumSize(44, 28);
        b->hide();
        return b;
    };
    oneBtn_ = overlayButton("1:1", tr("Actual pixels: one image pixel per screen pixel"));
    fitBtn_ = overlayButton(tr("Fit"), tr("Fit the whole picture in the window"));
    connect(oneBtn_, &QToolButton::clicked, this, &MainWindow::zoom1to1);
    connect(fitBtn_, &QToolButton::clicked, this, &MainWindow::zoomFit);
    videoArea_->installEventFilter(this);

    statusLeft_ = new QLabel(this);
    statusBar()->addWidget(statusLeft_, 1);

    playlist_ = new Playlist(this);
    playlistDock_ = new QDockWidget(tr("Playlist"), this);
    playlistDock_->setObjectName("playlistDock");
    playlistDock_->setWidget(playlist_);
    addDockWidget(Qt::RightDockWidgetArea, playlistDock_);
    playlistDock_->hide();
    connect(playlist_, &Playlist::play, this, &MainWindow::playRow);
    connect(playlist_, &Playlist::compare, this, [this](int row) { startCompare(playlist_->pathAt(row)); });
    // A playlist edit can change which clip comes next.
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(playlist_->model(), sig, this, [this] { QTimer::singleShot(0, this, [this] { queueNext(); }); });
    connect(playlist_->model(), &QAbstractItemModel::rowsMoved, this, [this] { QTimer::singleShot(0, this, [this] { queueNext(); }); });
    connect(playlist_->model(), &QAbstractItemModel::layoutChanged, this, [this] { QTimer::singleShot(0, this, [this] { queueNext(); }); });
    wall_.start();
    connect(playlist_, &Playlist::dropped, this, [this](const QStringList& p, bool append) { openPaths(p, append); });
    connect(playlistDock_, &QDockWidget::visibilityChanged, this, [this](bool v) {
        if (auto* a = actions_.value("view.playlist")) a->setChecked(v);
    });

    infoText_ = new QTextEdit(this);
    infoText_->setReadOnly(true);
    infoText_->setFont(mono);
    infoDock_ = new QDockWidget(tr("Information"), this);
    infoDock_->setObjectName("infoDock");
    infoDock_->setWidget(infoText_);
    addDockWidget(Qt::RightDockWidgetArea, infoDock_);
    infoDock_->hide();
    connect(infoDock_, &QDockWidget::visibilityChanged, this, [this](bool v) {
        if (auto* a = actions_.value("view.information")) a->setChecked(v);
    });
}

QAction* MainWindow::act(const char* id) {
    QAction* a = actions_.value(id);
    Q_ASSERT_X(a, "act", id);
    return a;
}

void MainWindow::on(const char* id, std::function<void()> fn, bool checkable) {
    QAction* a = actions_.value(id);
    if (!a) {
        a = new QAction(this);
        a->setText(id);
        actions_.insert(id, a);
    }
    a->setCheckable(checkable);
    a->setShortcutContext(Qt::WindowShortcut);
    addAction(a);  // keeps the key working while the menu bar is hidden (full screen)
    connect(a, &QAction::triggered, this, [fn] { fn(); });
}

void MainWindow::applyHotkeys() {
    mouseKeys_.clear();
    for (const auto& d : Hotkeys::load()) {
        QAction* a = actions_.value(d.id);
        if (!a) continue;
        a->setText(QCoreApplication::translate("Hotkeys", d.label));
        // Keyboard keys go to the action; mouse keys (wheel, middle click, side buttons) to the
        // table the picture's mouse events read (mouseCommand).
        QList<QKeySequence> keyboard;
        for (const auto& ks : d.keys) {
            if (!Hotkeys::isMouse(ks)) keyboard << ks;
            else if (!mouseKeys_.contains(ks[0].toCombined())) mouseKeys_.insert(ks[0].toCombined(), d.id);
        }
        a->setShortcuts(keyboard);
    }
}

// The command bound to a mouse key with these modifiers, or none.
QAction* MainWindow::mouseCommand(int key, Qt::KeyboardModifiers mods) const {
    const QString id = mouseKeys_.value(Hotkeys::mouseKey(key, mods)[0].toCombined());
    return id.isEmpty() ? nullptr : actions_.value(id);
}

void MainWindow::buildActions() {
    for (const auto& d : Hotkeys::defaults()) {
        auto* a = new QAction(QCoreApplication::translate("Hotkeys", d.label), this);
        actions_.insert(d.id, a);
    }
    auto withEngine = [this](auto fn) { return [this, fn] { if (cur_) fn(cur_); }; };

    // File
    on("file.quickopen", [this] {
        const QStringList f = QFileDialog::getOpenFileNames(this, tr("Open"));
        if (!f.isEmpty()) openPaths(f);
    });
    on("file.open", [this] { openDialog(); });
    on("file.openfolder", [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
        if (!d.isEmpty()) openPaths({d});
    });
    on("file.reopen", [this] { if (!currentPath_.isEmpty()) playRow(playlist_->currentRow()); });
    on("file.recycle", [this] { recycleCurrent(); });
    on("file.export", [this] { exportDialog(); });
    on("file.save", [this] { saveGraded(false); });
    on("file.saveas", [this] { saveGraded(true); });
    on("view.grading", [this] { toggleGradeWindow(); });
    on("file.loadsub", [this] {
        if (cur_ != mpv_ || currentPath_.isEmpty()) {
            osd_->showMessage(tr("Open a movie first: subtitles show on movies"), 2500);
            return;
        }
        const QStringList exts = Subtitles::extensions();
        QString filter = tr("Subtitles") + " (";
        for (const QString& e : exts) filter += "*." + e + " ";
        filter = filter.trimmed() + ");;" + tr("All files") + " (*)";
        const QString f = QFileDialog::getOpenFileName(this, tr("Load Subtitle"), QFileInfo(currentPath_).absolutePath(), filter);
        if (!f.isEmpty()) loadSubtitle(f);
    });
    on("sub.style", [this] { subtitleStyleDialog(); });
    on("file.saveimage", [this] { saveImage(false); });
    on("file.saveimageauto", [this] { saveImage(true); });
    on("file.close", [this] { closeMedia(); });
    on("file.properties", [this] { showProperties(); });
    on("file.openlocation", [this] { openLocation(); });
    on("file.exit", [this] { close(); });

    // Play
    on("play.playpause", withEngine([](Engine* e) { e->setPaused(!e->paused()); }));
    on("play.stop", withEngine([](Engine* e) { e->stop(); }));
    on("play.framestep", withEngine([](Engine* e) { e->frameStep(1); }));
    on("play.framestepback", withEngine([](Engine* e) { e->frameStep(-1); }));
    on("play.goto", [this] { gotoDialog(); });
    on("play.incrate", [this] { setRate(rate_ * 2.0); });
    on("play.decrate", [this] { setRate(rate_ / 2.0); });
    on("play.resetrate", [this] { setRate(1.0); });
    on("play.incauddelay", withEngine([this](Engine* e) { e->adjustAudioDelay(0.010); osd_->showMessage(tr("Audio delay +10 ms")); }));
    on("play.decauddelay", withEngine([this](Engine* e) { e->adjustAudioDelay(-0.010); osd_->showMessage(tr("Audio delay -10 ms")); }));
    on("play.seekfwdsmall", [this] { seekBy(kJumpSmall); });
    on("play.seekbacksmall", [this] { seekBy(-kJumpSmall); });
    on("play.seekfwdmed", [this] { seekBy(kJumpMedium); });
    on("play.seekbackmed", [this] { seekBy(-kJumpMedium); });
    on("play.seekfwdlarge", [this] { seekBy(kJumpLarge); });
    on("play.seekbacklarge", [this] { seekBy(-kJumpLarge); });
    on("play.seekkeyfwd", withEngine([](Engine* e) { e->seekKeyframe(1); }));
    on("play.seekkeyback", withEngine([](Engine* e) { e->seekKeyframe(-1); }));
    on("play.seekbegin", withEngine([](Engine* e) { e->seekAbsolute(0); }));
    on("play.abmarka", [this] { setAb(0); });
    on("play.abmarkb", [this] { setAb(1); });
    on("play.abclear", [this] { setAb(-1); });
    on("nav.next", [this] { if (!(cur_ && cur_->chapterStep(1))) playNext(1, repeat_ == Repeat::All); });
    on("nav.prev", [this] { if (!(cur_ && cur_->chapterStep(-1))) playNext(-1, repeat_ == Repeat::All); });
    on("nav.nextfile", [this] { nextFileInFolder(1); });
    on("nav.prevfile", [this] { nextFileInFolder(-1); });
    on("zoom.fit", [this] { zoomFit(); });
    // From the wheel: at the mouse, by the wheel's own step (trackpads send small ones);
    // from a key: at the centre, one step.
    on("zoom.in", [this] { zoomAt(wheelZoom_ ? wheelAt_ : QPointF(videoArea_->width() / 2.0, videoArea_->height() / 2.0), std::pow(1.2, wheelZoom_ ? wheelSteps_ : 1.0)); });
    on("zoom.out", [this] { zoomAt(wheelZoom_ ? wheelAt_ : QPointF(videoArea_->width() / 2.0, videoArea_->height() / 2.0), std::pow(1.2, -(wheelZoom_ ? wheelSteps_ : 1.0))); });
    on("zoom.1to1", [this] { zoom1to1(); });
    on("lut.load", [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Load a LUT"), QSettings().value("lut/lastDir").toString(),
                                                       Lut::fileFilter());
        if (f.isEmpty()) return;
        QSettings().setValue("lut/lastDir", QFileInfo(f).absolutePath());
        LutChoice c;
        c.kind = LutChoice::File;
        c.file = f;
        setLut(c);
    });
    on("lut.clear", [this] { setLut(LutChoice()); });

    auto* repeatGroup = new QActionGroup(this);
    for (auto [id, label, mode] : {std::tuple{"play.repeat.none", QT_TRANSLATE_NOOP("MainWindow", "Loop: Off"), Repeat::None},
                                   std::tuple{"play.repeat.one", QT_TRANSLATE_NOOP("MainWindow", "Loop This File"), Repeat::One},
                                   std::tuple{"play.repeat.all", QT_TRANSLATE_NOOP("MainWindow", "Loop Playlist"), Repeat::All}}) {
        on(id, [this, m = mode] {
            repeat_ = m;
            if (m != Repeat::None) lastLoop_ = m;
            QSettings().setValue("play/repeat", int(m));
            updateLoopButton();
            if (cur_) cur_->setLoopFile(m == Repeat::One);
            queueNext();
        }, true);
        actions_[id]->setText(tr(label));
        repeatGroup->addAction(actions_[id]);
    }

    // View
    on("view.caption", [this] { menuBar()->setVisible(!menuBar()->isVisible()); }, true);
    on("view.seeker", [this] { seekBar_->setVisible(act("view.seeker")->isChecked()); }, true);
    on("view.controls", [this] { controls_->setVisible(act("view.controls")->isChecked()); }, true);
    on("view.information", [this] { infoDock_->setVisible(act("view.information")->isChecked()); }, true);
    on("view.statistics", [this] { stats_->setVisible(act("view.statistics")->isChecked()); updateStatus(); }, true);
    on("view.stats", [this] { act("view.statistics")->trigger(); });
    on("view.status", [this] { statusBar()->setVisible(act("view.status")->isChecked()); }, true);
    on("view.infooverlay", [this] { setInfoOverlay(act("view.infooverlay")->isChecked()); }, true);
    on("help.hotkeys", [this] { hotkeyList_->toggle(); });
    on("help.about", [this] { showAbout(); });
    // A/B compare. Turning it on compares the playing clip (A) with the selected playlist
    // item, else the next one (B).
    on("compare.toggle", [this] {
        if (compareBar_->isVisible()) { stopCompare(); return; }
        int row = -1;
        for (QListWidgetItem* it : playlist_->selectedItems())
            if (playlist_->row(it) != playlist_->currentRow()) { row = playlist_->row(it); break; }
        if (row < 0 && playlist_->count() > 1) row = (playlist_->currentRow() + 1) % playlist_->count();
        showCompareBar(true);  // with one clip, slot B waits for a drop or a click
        if (row >= 0) startCompare(playlist_->pathAt(row));
    }, true);
    auto* cmpGroup = new QActionGroup(this);
    for (auto [id, mode] : {std::pair{"compare.flipmode", CompareMode::Flip}, std::pair{"compare.wipe", CompareMode::Wipe},
                            std::pair{"compare.sidebyside", CompareMode::SideBySide},
                            std::pair{"compare.difference", CompareMode::Difference}}) {
        on(id, [this, m = mode] { setCompareMode(m); }, true);
        cmpGroup->addAction(actions_[id]);
    }
    act("compare.wipe")->setChecked(true);
    on("compare.swap", [this] {
        if (!comparing_) return;
        cmpView_.swap = !cmpView_.swap;
        applyCompareView();
        compareMessage();
    });
    on("compare.offsetinc", [this] { if (comparing_) setCompareOffset(cmpOffset_ + 1); });
    on("compare.offsetdec", [this] { if (comparing_) setCompareOffset(cmpOffset_ - 1); });
    on("compare.offsetreset", [this] { if (comparing_) setCompareOffset(0); });
    // A file opened from Explorer while one plays: a second window (on) or this one (off).
    on("player.newwindow", [this] { QSettings().setValue("player/newWindow", act("player.newwindow")->isChecked()); }, true);
    act("player.newwindow")->setChecked(QSettings().value("player/newWindow", false).toBool());
    on("view.playlist", [this] { playlistDock_->setVisible(act("view.playlist")->isChecked()); }, true);
    act("view.seeker")->setChecked(true);
    act("view.controls")->setChecked(true);
    act("view.status")->setChecked(true);
    on("view.minimal", [this] { setViewPreset(0); });
    on("view.compact", [this] { setViewPreset(1); });
    on("view.normal", [this] { setViewPreset(2); });
    on("view.fullscreen", [this] { toggleFullscreen(); });
    on("view.exitfullscreen", [this] {  // Esc: closes the hotkey list first; nothing when windowed
        if (hotkeyList_->isVisible()) hotkeyList_->close();
        else if (isFullScreen()) toggleFullscreen();
    });
    on("view.zoom25", [this] { zoomWindow(0.25); });
    on("view.zoom50", [this] { zoomWindow(0.5); });
    on("view.zoom100", [this] { zoomWindow(1.0); });
    on("view.zoom200", [this] { zoomWindow(2.0); });
    on("view.zoomfit", [this] { autoFit(false); });
    on("view.zoomfitlarger", [this] { autoFit(true); });
    on("view.ontop", [this] {
        // On the native window itself: QWidget::setWindowFlag would destroy and
        // re-create the window under its OpenGL surface.
        if (QWindow* wh = windowHandle()) wh->setFlag(Qt::WindowStaysOnTopHint, act("view.ontop")->isChecked());
    }, true);

    // Pan & scan (MPC-HC steps)
    on("pns.reset", [this] { xf_ = ViewTransform(); applyTransform(); });
    on("pns.incsize", [this] { xf_.zoom += 0.02; applyTransform(); });
    on("pns.decsize", [this] { xf_.zoom = std::max(0.05, xf_.zoom - 0.02); applyTransform(); });
    on("pns.incwidth", [this] { xf_.scaleX += 0.02; applyTransform(); });
    on("pns.decwidth", [this] { xf_.scaleX = std::max(0.05, xf_.scaleX - 0.02); applyTransform(); });
    on("pns.incheight", [this] { xf_.scaleY += 0.02; applyTransform(); });
    on("pns.decheight", [this] { xf_.scaleY = std::max(0.05, xf_.scaleY - 0.02); applyTransform(); });
    on("pns.center", [this] { xf_.panX = xf_.panY = 0; applyTransform(); });
    on("pns.left", [this] { pan(-0.01, 0); });
    on("pns.right", [this] { pan(0.01, 0); });
    on("pns.up", [this] { pan(0, -0.01); });
    on("pns.down", [this] { pan(0, 0.01); });
    on("pns.upleft", [this] { pan(-0.01, -0.01); });
    on("pns.upright", [this] { pan(0.01, -0.01); });
    on("pns.downleft", [this] { pan(-0.01, 0.01); });
    on("pns.downright", [this] { pan(0.01, 0.01); });
    // +90 = clockwise on both engines (mpv video-rotate and the sequence shader).
    on("pns.rotatezp", [this] { xf_.rotate = (xf_.rotate + 90) % 360; applyTransform(); fitWindowToPicture(); osd_->showMessage(tr("Rotate %1").arg(xf_.rotate), 900); });
    on("pns.rotatezm", [this] { xf_.rotate = (xf_.rotate + 270) % 360; applyTransform(); fitWindowToPicture(); osd_->showMessage(tr("Rotate %1").arg(xf_.rotate), 900); });

    // Audio / subtitles
    on("vol.up", [this] { setVolume(volume_ + kVolumeStep); });
    on("vol.down", [this] { setVolume(volume_ - kVolumeStep); });
    on("vol.mute", [this] {
        muted_ = !muted_;
        QSettings().setValue("audio/muted", muted_);
        if (cur_) cur_->setMuted(muted_);
        osd_->showMessage(muted_ ? tr("Mute: On") : tr("Mute: Off"));
        updateMuteButton();
    });
    on("stream.audionext", withEngine([](Engine* e) { e->cycleAudio(1); }));
    on("stream.audioprev", withEngine([](Engine* e) { e->cycleAudio(-1); }));
    on("stream.subnext", withEngine([](Engine* e) { e->cycleSubtitle(1); }));
    on("stream.subprev", withEngine([](Engine* e) { e->cycleSubtitle(-1); }));
    on("stream.subonoff", withEngine([](Engine* e) { e->toggleSubtitles(); }));
    on("sub.prev", withEngine([](Engine* e) { e->gotoSub(-1); }));
    on("sub.next", withEngine([](Engine* e) { e->gotoSub(1); }));
    on("sub.delaydown", withEngine([this](Engine* e) { e->adjustSubDelay(-0.5); osd_->showMessage(tr("Subtitle delay -500 ms")); }));
    on("sub.delayup", withEngine([this](Engine* e) { e->adjustSubDelay(0.5); osd_->showMessage(tr("Subtitle delay +500 ms")); }));
    on("sub.posdown", withEngine([](Engine* e) { e->adjustSubPos(1); }));
    on("sub.posup", withEngine([](Engine* e) { e->adjustSubPos(-1); }));
    on("sub.fontdec", withEngine([](Engine* e) { e->adjustSubScale(-0.1); }));
    on("sub.fontinc", withEngine([](Engine* e) { e->adjustSubScale(0.1); }));

    // Misc
    on("osd.time", [this] { if (cur_) osd_->showMessage(timeText(cur_->position()) + " / " + timeText(cur_->duration())); });
    on("osd.filename", [this] { if (!currentPath_.isEmpty()) osd_->showMessage(QFileInfo(currentPath_).fileName()); });
    on("boss", [this] { if (cur_) cur_->setPaused(true); showMinimized(); });
    on("menu", [this] {
        QMenu m(this);
        fillContextMenu(&m);
        m.exec(QCursor::pos());
    });
    on("options", [this] { openSettings(0); });

    // Image sequences
    on("seq.fps", [this] { seqFpsDialog(); });
    on("seq.cache", [this] { cacheDialog(); });
    actions_["seq.cache"]->setText(tr("Sequence &Cache Size..."));
    on("channel.rgb", [this] { setChannel(0, false); });
    on("channel.r", [this] { setChannel(1, true); });
    on("channel.g", [this] { setChannel(2, true); });
    on("channel.b", [this] { setChannel(3, true); });
    on("channel.a", [this] { setChannel(4, true); });
    auto exposure = [this](double d, bool reset) {
        return [this, d, reset] {
            if (!seq_) return;
            const double v = reset ? 0.0 : seq_->seqView()->exposure() + d;
            seq_->seqView()->setExposure(v);
            osd_->showMessage(tr("Exposure: %1%2 stops").arg(v >= 0 ? "+" : "").arg(v, 0, 'f', 1));
        };
    };
    on("seq.exposureup", exposure(0.5, false));
    on("seq.exposuredown", exposure(-0.5, false));
    on("seq.exposurereset", exposure(0, true));
}

void MainWindow::buildMenus() {
    auto add = [this](QMenu* m, std::initializer_list<const char*> ids) {
        for (const char* id : ids) {
            if (!*id) m->addSeparator();
            else m->addAction(act(id));
        }
    };
    {
        // Solid, the same dark grey as the title bar (Windows 11 style draws it see-through).
        QPalette mp = menuBar()->palette();
        mp.setColor(QPalette::Window, QColor(31, 31, 31));
        menuBar()->setPalette(mp);
        menuBar()->setAutoFillBackground(true);
    }
    QToolButton* explore = nullptr;
    {
        // Top right of the menu bar. Opens the page only when clicked (nothing loads before).
        explore = new QToolButton(menuBar());
        explore->setObjectName("exploreCoreStudio");
        //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
        explore->setText(tr("Explore Core Studio"));
        explore->setToolTip("https://imcore.ai/core-studio.html");
        explore->setCursor(Qt::PointingHandCursor);
        explore->setFocusPolicy(Qt::NoFocus);
        // A filled black pill with white text (monochrome like the rest).
        explore->setStyleSheet("QToolButton { color: rgb(255,255,255); background: rgb(0,0,0); border: none;"
                               " border-radius: 8px; padding: 0px 12px; margin: 2px 8px 2px 0px; }"
                               " QToolButton:hover { background: rgb(60,60,60); }"
                               " QToolButton:pressed { background: rgb(90,90,90); }");
        connect(explore, &QToolButton::clicked, this,
                [] { QDesktopServices::openUrl(QUrl("https://imcore.ai/core-studio.html")); });
        menuBar()->setCornerWidget(explore, Qt::TopRightCorner);
    }
    QMenu* file = menuBar()->addMenu(tr("&File"));
    add(file, {"file.quickopen", "file.open", "file.openfolder", "file.reopen", ""});
    recentMenu_ = file->addMenu(tr("Recent &Files"));
    rebuildRecent();
    add(file, {"", "file.save", "file.saveas", "file.export", "file.loadsub", "", "file.saveimage", "file.saveimageauto", "", "file.properties", "file.openlocation",
               "file.recycle", "", "file.close", "file.exit"});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    add(view, {"view.caption", "view.seeker", "view.controls", "view.information", "view.statistics",
               "view.infooverlay", "view.status", "view.playlist", ""});
    QMenu* presets = view->addMenu(tr("Presets"));
    add(presets, {"view.minimal", "view.compact", "view.normal"});
    add(view, {"", "view.fullscreen", "view.exitfullscreen", ""});
    QMenu* zoom = view->addMenu(tr("&Zoom"));
    add(zoom, {"view.zoom25", "view.zoom50", "view.zoom100", "view.zoom200", "view.zoomfit", "view.zoomfitlarger"});
    QMenu* pns = view->addMenu(tr("&Pan && Scan"));
    add(pns, {"pns.reset", "", "pns.incsize", "pns.decsize", "pns.incwidth", "pns.decwidth", "pns.incheight",
              "pns.decheight", "", "pns.center", "pns.left", "pns.right", "pns.up", "pns.down", "pns.upleft",
              "pns.upright", "pns.downleft", "pns.downright", "", "pns.rotatezp", "pns.rotatezm"});
    add(view, {"zoom.in", "zoom.out", "zoom.fit", "zoom.1to1", ""});
    QMenu* lutMenu = view->addMenu(tr("&LUT"));
    connect(lutMenu, &QMenu::aboutToShow, this, [this, lutMenu] {
        lutMenu->clear();
        addLutActions(lutMenu);
    });
    add(view, {"", "view.ontop", "player.newwindow", "", "options"});

    QMenu* play = menuBar()->addMenu(tr("&Play"));
    add(play, {"play.playpause", "play.stop", "", "play.framestep", "play.framestepback", "", "play.goto", "",
               "play.incrate", "play.decrate", "play.resetrate", "", "play.seekfwdsmall", "play.seekbacksmall",
               "play.seekfwdmed", "play.seekbackmed", "play.seekfwdlarge", "play.seekbacklarge", "play.seekkeyfwd",
               "play.seekkeyback", "play.seekbegin", "", "play.abmarka", "play.abmarkb", "play.abclear", "",
               "play.repeat.none", "play.repeat.one", "play.repeat.all", "", "vol.up", "vol.down", "vol.mute",
               "play.incauddelay", "play.decauddelay", "", "stream.audionext", "stream.audioprev",
               "stream.subnext", "stream.subprev", "stream.subonoff", "sub.prev", "sub.next", "sub.delaydown",
               "sub.delayup", "sub.posup", "sub.posdown", "sub.fontinc", "sub.fontdec", "sub.style"});

    QMenu* nav = menuBar()->addMenu(tr("&Navigate"));
    add(nav, {"nav.prev", "nav.next", "nav.prevfile", "nav.nextfile"});

    QMenu* cmp = menuBar()->addMenu(tr("&Compare"));
    add(cmp, {"compare.toggle", "", "compare.flipmode", "compare.wipe", "compare.sidebyside", "compare.difference", "",
              "compare.swap", "", "compare.offsetinc", "compare.offsetdec", "compare.offsetreset"});

    QMenu* seqMenu = menuBar()->addMenu(tr("&Image"));
    add(seqMenu, {"view.grading", "", "seq.fps", "seq.cache", ""});
    QMenu* colour = seqMenu->addMenu(tr("&Colour"));
    connect(colour, &QMenu::aboutToShow, this, [this, colour] { fillColourMenu(colour); });
    add(seqMenu, {"", "channel.rgb", "channel.r", "channel.g", "channel.b", "channel.a", "",
                  "seq.exposureup", "seq.exposuredown", "seq.exposurereset"});

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(act("help.hotkeys"));
    help->addAction(tr("&Edit Hotkeys..."), this, [this] { openSettings(1); });
    // Windows, both Macs and the Linux AppImage update themselves; the .deb is a system package.
    if (Updater::supported())
        updateAction_ = help->addAction(tr("Check for &Updates..."), this, [this] { updatesDialog(); });
    help->addAction(act("help.about"));
    // The menu titles' font (bold) and exactly a menu title's height: the row stays as tall as
    // its labels. (The bar's sizeHint counts this button too, so its own margins grew the bar.)
    QFont ef = menuBar()->font();
    ef.setBold(true);
    explore->setFont(ef);
    explore->setFixedHeight(menuBar()->actionGeometry(menuBar()->actions().first()).height());
}

void MainWindow::fillColourMenu(QMenu* m) {
    m->clear();
    if (!seq_) {
        m->addAction(tr("Open an image sequence first"))->setEnabled(false);
        return;
    }
    SeqView* v = seq_->seqView();
    QMenu* in = m->addMenu(tr("&Input Colour Space"));
    auto* ig = new QActionGroup(in);
    for (const QString& cs : v->colorSpaces()) {
        QAction* a = in->addAction(cs, this, [this, v, cs] { v->setInputSpace(cs); osd_->showMessage(tr("Input: %1").arg(cs)); });
        a->setCheckable(true);
        a->setChecked(cs == v->inputSpace());
        ig->addAction(a);
    }
    for (const QString& d : v->displays()) {
        QMenu* dm = m->addMenu(tr("Display: %1").arg(d));
        auto* vg = new QActionGroup(dm);
        for (const QString& view : v->views(d)) {
            QAction* a = dm->addAction(view, this, [this, v, d, view] {
                v->setDisplayView(d, view);
                osd_->showMessage(d + " / " + view);
            });
            a->setCheckable(true);
            a->setChecked(d == v->display() && view == v->viewName());
            vg->addAction(a);
        }
    }
    m->addSeparator();
    m->addAction(tr("Config: %1").arg(v->configName()))->setEnabled(false);
}

// ------------------------------------------------------------------ engines

MpvEngine* MainWindow::mpv() {
    if (!mpv_) {
        mpv_ = new MpvEngine(videoArea_, nullptr);
        stack_->addWidget(mpv_->view());
        connectEngine(mpv_);
        connect(mpv_->mpvView(), &MpvView::wipeMoved, this, [this](float w) { cmpView_.wipe = w; });
        auto* v = mpv_->mpvView();
        v->setMouseTracking(true);
        mpv_->applySubStyle(SubStyle::load().mpvOptions());
        connect(v, &MpvView::clicked, this, [this] { if (cur_) cur_->setPaused(!cur_->paused()); });
        connect(v, &MpvView::doubleClicked, this, [this] {
            if (cur_) cur_->setPaused(!cur_->paused());  // undo the first click's play/pause
            toggleFullscreen();
        });
        connect(v, &MpvView::contextMenu, this, [this](const QPoint& p) { act("menu")->trigger(); (void)p; });
    }
    return mpv_;
}

SequenceEngine* MainWindow::seq() {
    if (!seq_) {
        seq_ = new SequenceEngine(videoArea_, nullptr);
        stack_->addWidget(seq_->view());
        connectEngine(seq_);
        connect(seq_->seqView(), &SeqView::wipeMoved, this, [this](float w) { cmpView_.wipe = w; });
        SeqView* v = seq_->seqView();
        v->setMouseTracking(true);
        connect(v, &SeqView::clicked, this, [this] { if (cur_) cur_->setPaused(!cur_->paused()); });
        connect(v, &SeqView::doubleClicked, this, [this] {
            if (cur_) cur_->setPaused(!cur_->paused());
            toggleFullscreen();
        });
        connect(v, &SeqView::contextMenu, this, [this](const QPoint&) { act("menu")->trigger(); });
        connect(v, &SeqView::colourError, this, [this](const QString& t) { osd_->showMessage(t, 6000); });
        // The seek bar's cache band: every decoded frame reports, but the band (a pass
        // over all frames under the cache lock, and a repaint) is redrawn at most 10x/s.
        cacheBandTimer_.setSingleShot(true);
        cacheBandTimer_.setInterval(100);
        connect(&cacheBandTimer_, &QTimer::timeout, this, [this] {
            if (cur_ != seq_) return;
            const Sequence& s = seq_->sequence();
            std::vector<bool> missing(s.present.size());
            for (size_t i = 0; i < missing.size(); ++i) missing[i] = !s.present[i];
            seekBar_->setCacheMask(seq_->cache()->cachedMask(), missing);
            // The info overlay was built before the first frame was decoded (a slow disk
            // or PC): it has no image header yet. Build it again now that a frame is in.
            // ("Image: " in the interface language, as SequenceEngine::info() writes it)
            if (infoOverlay_->isVisible() && seq_->seqView()->frame()
                && !infoStatic_.contains("\n" + QCoreApplication::translate("SequenceEngine", "Image: %1").section("%1", 0, 0)))
                refreshInfoOverlay(true);
        });
        connect(seq_, &Engine::cacheChanged, this, [this] { if (!cacheBandTimer_.isActive()) cacheBandTimer_.start(); });
    }
    return seq_;
}

void MainWindow::connectEngine(Engine* e) {
    connect(e, &Engine::positionChanged, this, [this, e](double s) {
        if (e != cur_) return;
        seekBar_->setPosition(s);
        const qint64 now = wall_.elapsed();
        if (switchOldEnd_ >= 0 && s > 0 && !cur_->paused()) {
            // The first frame of the new clip started at (now - s): the gap is from the old end to it.
            const double gapMs = (now - s * 1000.0 / std::max(0.01, rate_)) - switchOldEnd_;
            lastSwitch_ = QString("%1 %2").arg(switchMode_).arg(qRound(gapMs));
            switchOldEnd_ = -1;
        }
        if (s <= 0) return;  // mpv reports "no position" between two files
        lastPosWall_ = now;
        lastPos_ = s;
        lastDur_ = cur_->duration();
    });
    connect(e, &Engine::durationChanged, this, [this, e](double s) { if (e == cur_) seekBar_->setDuration(s); });
    connect(e, &Engine::pausedChanged, this, [this, e](bool p) {
        if (e != cur_) return;
        playBtn_->setIcon(grayIcon(style(), p ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
    });
    connect(e, &Engine::endReached, this, [this, e] { if (e == cur_) onEnd(); });
    connect(e, &Engine::advanced, this, [this, e](const QString& p) { if (e == cur_) onAdvanced(p); });
    connect(e, &Engine::message, this, [this](const QString& t) { osd_->showMessage(t, 4000); });
    connect(e, &Engine::mediaSizeChanged, this, [this, e] {
        if (e == cur_) QTimer::singleShot(0, this, [this] { fitWindowToPicture(); });
    });
    connect(e, &Engine::loaded, this, [this, e] {
        if (e != cur_) return;
        e->setVolume(volume_);
        e->setMuted(muted_);
        e->setSpeed(rate_);
        e->setLoopFile(repeat_ == Repeat::One);
        e->setChannel(channel_);
        applyTransform();
        updateTitle();
        if (infoDock_->isVisible()) infoText_->setPlainText(e->info());
        // Later, not now: the overlay reads mpv synchronously, and at a file change mpv's
        // core can be waiting for this thread to draw (a visible stall).
        infoStatic_.clear();
        QTimer::singleShot(300, this, [this] { refreshInfoOverlay(true); });
        queueNext();
        if (e == seq_ && !seq_->fpsFromFile())
            osd_->showMessage(tr("No frame rate in the file header: playing at %1 fps (Image > Sequence Frame Rate)")
                                  .arg(seq_->fps().value(), 0, 'f', 3), 5000);
    });
}

// ------------------------------------------------------------------ playlist / open

// One Settings dialog; OK or Apply saves, and the keys and options take effect at once.
void MainWindow::openSettings(int page) {
    if (!settings_) {
        settings_ = new SettingsDialog(this);
        settings_->setAttribute(Qt::WA_DeleteOnClose);
        connect(settings_, &QObject::destroyed, this, [this] { settings_ = nullptr; });
        connect(settings_, &SettingsDialog::applied, this, [this] {
            applyHotkeys();
            act("player.newwindow")->setChecked(newWindowPerFile());
            osd_->showMessage(tr("Settings saved"));
        });
    }
    settings_->showPage(page);
    if (qApp->organizationName() == "CoreplayTest") settings_->setAttribute(Qt::WA_ShowWithoutActivating);
    settings_->show();
    settings_->raise();
    if (qApp->organizationName() != "CoreplayTest") settings_->activateWindow();
}

QString MainWindow::testSettings(const QStringList& a) {
    const QString c = a.value(0);
    if (c == "open") { openSettings(a.value(1).toInt()); return "open"; }
    if (!settings_) return "not open";
    if (c == "select") { settings_->selectCommand(a.value(1)); return settings_->keysText(a.value(1)); }
    if (c == "set" || c == "add") return settings_->assignKey(Hotkeys::fromText(a.mid(1).join(' ')), c == "add");
    if (c == "clear") { settings_->clearKeys(); return settings_->message(); }
    if (c == "reset") { settings_->resetCommand(); return settings_->message(); }
    if (c == "resetall") { settings_->resetAll(); return settings_->message(); }
    if (c == "filter") { settings_->setFilter(a.mid(1).join(' ')); return QString::number(settings_->visibleRows()); }
    if (c == "newwindow") { settings_->setNewWindow(a.value(1) == "on"); return a.value(1); }
    if (c == "language") { settings_->setLanguage(a.value(1)); return settings_->languageNoteVisible() ? "note shown" : "no note"; }
    if (c == "software") { QSettings().setValue("gl/software", a.value(1) == "on"); return a.value(1); }
    if (c == "apply") return settings_->apply() ? "applied" : "failed";
    if (c == "close") { settings_->close(); return "closed"; }
    return "unknown";
}

bool MainWindow::newWindowPerFile() { return QSettings().value("player/newWindow", false).toBool(); }

void MainWindow::openPaths(const QStringList& paths, bool append) {
    QStringList media;
    QStringList subs;
    for (const QString& p : paths) {
        const QString suffix = QFileInfo(p).suffix().toLower();
        if (Subtitles::isSubtitleFile(p)) {
            subs << p;
            continue;
        }
        if (suffix == "m3u8" || suffix == "m3u") {
            if (!append) playlist_->load(p);
            else { Playlist tmp; tmp.load(p); for (int i = 0; i < tmp.count(); ++i) media << tmp.pathAt(i); }
            if (!append) { playRow(0); return; }
            continue;
        }
        media << p;
    }
    if (media.isEmpty()) {
        for (const QString& s : subs) loadSubtitle(s);  // onto the movie that is playing
        return;
    }
    if (!subs.isEmpty())  // with the movie: load them once it is open
        QTimer::singleShot(0, this, [this, subs] { for (const QString& s : subs) loadSubtitle(s); });
    const int first = append ? playlist_->count() : 0;
    if (!append) playlist_->clear();
    playlist_->addPaths(media);
    if (!append || !cur_) playRow(first);
}

void MainWindow::markSwitch(const char* mode) {
    switchMode_ = mode;
    // Where the old clip's last frame ended on the wall clock (only when it played to its end).
    switchOldEnd_ = lastPosWall_ >= 0 && lastDur_ > 0 && lastDur_ - lastPos_ < 0.5
                        ? lastPosWall_ + qint64((lastDur_ - lastPos_) * 1000.0 / std::max(0.01, rate_))
                        : -1;
    lastPosWall_ = -1;
}

void MainWindow::playRow(int row) {
    const QString path = playlist_->pathAt(row);
    if (path.isEmpty()) return;
    if (comparing_) {  // another clip as A: B stays, compare starts again once A is open
        pendingB_ = cmpPath_;
        stopCompare(false);
    }
    markSwitch("reopen");
    Engine* next = SequenceScanner::isImageFile(path) ? static_cast<Engine*>(seq()) : static_cast<Engine*>(mpv());
    if (cur_ && cur_ != next) cur_->close();
    cur_ = next;
    stack_->setCurrentWidget(cur_->view());
    beginRow(row);
    cur_->open(path);
    cur_->setAbLoop(-1, -1);
    if (cur_ == seq_) {
        // Sequences open on the clicked frame and play from there. A single image stays still.
        if (seq_->sequence().count() > 1)
            QMetaObject::invokeMethod(this, [this] { if (cur_ == seq_) seq_->setPaused(false); }, Qt::QueuedConnection);
    }
}

void MainWindow::startCompare(const QString& b) {
    if (b.isEmpty()) return;
    showCompareBar(true);
    if (!cur_ || currentPath_.isEmpty()) {  // nothing plays yet: B waits in its slot
        cmpPath_ = b;
        updateCompareBar();
        return;
    }
    const bool seqA = cur_ == seq_, seqB = SequenceScanner::isImageFile(b);
    if (seqA != seqB) {
        osd_->showMessage(tr("Compare needs two movies or two image sequences"), 4000);
        return;
    }
    if (comparing_) stopCompare(false);
    cmpOffset_ = 0;
    bool ok;
    if (seqA) ok = seq_->setCompare(b);
    else ok = mpv_->setCompare(b, 0);
    if (!ok) {
        osd_->showMessage(tr("Compare: cannot open %1").arg(QFileInfo(b).fileName()), 4000);
        return;
    }
    comparing_ = true;
    cmpPath_ = b;
    act("compare.toggle")->setChecked(true);
    seq()->seqView()->setLutB(cmpLutB_);
    applyCompareView();
    applyMpvLut();
    applyTransform();
    updateCompareBar();
    compareMessage();
}

void MainWindow::stopCompare(bool closeBar) {
    if (comparing_) {
        comparing_ = false;
        QTimer::singleShot(0, this, [this] { fitWindowToPicture(); });
        cmpOffset_ = 0;
        if (cur_ == seq_) {
            seq_->setCompare({});
            seq_->seqView()->setCompare(CompareView{});
        } else if (mpv_) {
            mpv_->setCompare({}, 0);
        }
        applyMpvLut();
        applyTransform();
        queueNext();
    }
    if (closeBar) {
        cmpPath_.clear();
        pendingB_.clear();
        showCompareBar(false);
        act("compare.toggle")->setChecked(false);
        osd_->showMessage(tr("Compare off"), 1500);
    }
    updateCompareBar();
}

void MainWindow::showCompareBar(bool on) {
    if (!compareBar_) return;
    compareBar_->setVisible(on);
    act("compare.toggle")->setChecked(on);
    updateCompareBar();
}

void MainWindow::updateCompareBar() {
    if (!compareBar_ || !compareBar_->isVisible()) return;
    compareBar_->slotA()->setFile(currentPath_.isEmpty() ? QString() : windowTitle().chopped(11));
    QString b;
    if (!cmpPath_.isEmpty()) {
        b = SequenceScanner::isImageFile(cmpPath_) ? SequenceScanner::scan(cmpPath_).displayName() : QFileInfo(cmpPath_).fileName();
        if (cmpOffset_) b += tr("   (offset %1%2)").arg(cmpOffset_ > 0 ? "+" : "").arg(cmpOffset_);
    }
    compareBar_->slotB()->setFile(b);
    compareBar_->slotA()->setLutLabel(lut_.kind == LutChoice::None ? tr("None") : lut_.label());
    compareBar_->slotB()->setLutLabel(cmpLutB_.kind == LutChoice::None ? tr("None") : cmpLutB_.label());
}

QString MainWindow::chooseClip(const QString& title) {
    QSettings st;
    const QString f = QFileDialog::getOpenFileName(this, title, st.value("compare/lastDir").toString());
    if (!f.isEmpty()) st.setValue("compare/lastDir", QFileInfo(f).absolutePath());
    return f;
}

// Slot A is the clip that plays: play this one (from the playlist, or added to it) and
// compare it with B again once it is open.
void MainWindow::setSlotA(const QString& path) {
    if (path.isEmpty()) return;
    const QString b = cmpPath_;
    int row = -1;
    for (int i = 0; i < playlist_->count(); ++i)
        if (QFileInfo(playlist_->pathAt(i)) == QFileInfo(path)) { row = i; break; }
    if (row < 0) {
        playlist_->addPaths({path});
        row = playlist_->count() - 1;
    }
    playRow(row);
    pendingB_ = b;  // after playRow, which keeps B the same way
    showCompareBar(true);
}

// LUT menu for one slot: load a file, presets, clear.
void MainWindow::fillLutMenu(QMenu* m, const LutChoice& current, std::function<void(const LutChoice&)> apply) {
    m->addAction(tr("Load a &LUT..."), this, [this, apply] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Load a LUT"), QSettings().value("lut/lastDir").toString(),
                                                       Lut::fileFilter());
        if (f.isEmpty()) return;
        QSettings().setValue("lut/lastDir", QFileInfo(f).absolutePath());
        LutChoice c;
        c.kind = LutChoice::File;
        c.file = f;
        apply(c);
    });
    QMenu* presets = m->addMenu(tr("LUT &Presets"));
    for (const QString& src : Lut::presetSources()) {
        QMenu* sm = presets->addMenu(src);
        for (const QString& dst : Lut::presetTargets()) {
            QAction* a = sm->addAction(tr("to %1").arg(dst), this, [apply, src, dst] {
                LutChoice c;
                c.kind = LutChoice::Preset;
                c.source = src;
                c.target = dst;
                apply(c);
            });
            a->setCheckable(true);
            a->setChecked(current.kind == LutChoice::Preset && current.source == src && current.target == dst);
        }
    }
    QAction* clear = m->addAction(tr("&No LUT"), this, [apply] { apply(LutChoice()); });
    clear->setEnabled(current.kind != LutChoice::None);
}

void MainWindow::setLutB(const LutChoice& c) {
    if (c.kind != LutChoice::None) {
        QString err;
        if (Lut::bakeMpvShader(c, &err).isEmpty()) {  // the same check as for A: can it be used at all
            osd_->showMessage(tr("Cannot use this LUT: %1").arg(err), 6000);
            return;
        }
    }
    cmpLutB_ = c;
    seq()->seqView()->setLutB(c);
    applyMpvLut();
    osd_->showMessage(c.kind == LutChoice::None ? tr("B: LUT off") : tr("B LUT: %1").arg(c.label()), 2500);
    updateCompareBar();
}

// Movies: one mpv shader. While comparing it holds A's LUT for the left half of the
// packed frame and B's for the right; otherwise A's LUT alone.
void MainWindow::applyMpvLut() {
    if (!mpv_) return;
    QString err, shader;
    if (comparing_ && cur_ == mpv_ && (lut_.kind != LutChoice::None || cmpLutB_.kind != LutChoice::None))
        shader = Lut::bakeMpvCompareShader(lut_, cmpLutB_, &err);
    else if (lut_.kind != LutChoice::None)
        shader = Lut::bakeMpvShader(lut_, &err);
    if (!err.isEmpty()) osd_->showMessage(tr("Cannot use this LUT: %1").arg(err), 6000);
    mpv_->setLut(shader);
}

void MainWindow::setCompareMode(CompareMode m) {
    cmpView_.mode = m;
    act(m == CompareMode::Flip ? "compare.flipmode" : m == CompareMode::Wipe ? "compare.wipe"
        : m == CompareMode::SideBySide ? "compare.sidebyside" : "compare.difference")->setChecked(true);
    if (!comparing_) {  // choosing a mode starts compare
        act("compare.toggle")->trigger();
        return;
    }
    applyCompareView();
    compareMessage();
}

void MainWindow::setCompareOffset(int frames) {
    cmpOffset_ = frames;
    if (cur_ == seq_) seq_->setCompareOffset(frames);
    else mpv_->setCompare(cmpPath_, frames);  // mpv reopens both at the same time
    compareMessage();
}

void MainWindow::applyCompareView() {
    const CompareView v = comparing_ ? cmpView_ : CompareView{};
    QTimer::singleShot(0, this, [this] { fitWindowToPicture(); });  // side by side is twice as wide
    if (cur_ == seq_) seq_->seqView()->setCompare(v);
    else if (mpv_) mpv_->setCompareView(v);
}

void MainWindow::compareMessage() {
    if (!comparing_) return;
    const QString a = QFileInfo(currentPath_).fileName(), b = QFileInfo(cmpPath_).fileName();
    QString t = tr("Compare: %1   A: %2   B: %3").arg(compareModeLabel(cmpView_.mode), a, b);
    if (cmpView_.swap) t += tr("   (swapped)");
    if (cmpOffset_) t += tr("   B offset %1%2").arg(cmpOffset_ > 0 ? "+" : "").arg(cmpOffset_);
    osd_->showMessage(t, 3000);
}

// Coreplay draws with OpenGL 4.1. A PC with no GPU, a remote session or a very old
// driver cannot give it: the picture would stay black. Checked once, after the first
// paint (start-up time stays the same); when it fails, Coreplay remembers software
// OpenGL (shipped: opengl32sw.dll) and starts again with it. Settings can undo it.
void MainWindow::checkOpenGL() {
    if (QCoreApplication::testAttribute(Qt::AA_UseSoftwareOpenGL) || qEnvironmentVariable("QT_OPENGL") == "software") {
        glState_ = "software";
        return;
    }
    QOpenGLContext ctx;
    QSurfaceFormat f = QSurfaceFormat::defaultFormat();
    ctx.setFormat(f);
    QOffscreenSurface surface;
    surface.setFormat(f);
    surface.create();
    bool ok = ctx.create() && ctx.makeCurrent(&surface);
    QString renderer;
    if (ok) {
        const QSurfaceFormat got = ctx.format();
        renderer = QString::fromLatin1(reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_RENDERER)));
        ok = std::make_pair(got.majorVersion(), got.minorVersion()) >= std::make_pair(4, 1) &&
             !renderer.contains("GDI Generic") && !renderer.contains("Basic Render");
        ctx.doneCurrent();
    }
    if (qEnvironmentVariableIsSet("COREPLAY_TEST_GL_FAIL")) ok = false;  // the tests
    glState_ = ok ? "gpu: " + renderer : "unsupported: " + renderer;
    if (ok) return;
    QSettings().setValue("gl/software", true);
    if (qApp->organizationName() == "CoreplayTest") return;  // the tests check the setting only
    // Start again on software OpenGL, with the same files.
    QStringList args = QCoreApplication::arguments().mid(1);
    if (!currentPath_.isEmpty() && !args.contains(currentPath_)) args << currentPath_;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), args);
    close();
}

// The logo at most 442 px wide, 51% of the player in a small window; sharp on HiDPI.
void MainWindow::updatePlaceholderLogo() {
    if (!logo_ || logoPm_.isNull()) return;
    const qreal dpr = devicePixelRatioF();
    const int w = std::min(442, int(placeholder_->width() * 0.51));
    if (w < 40) { logo_->clear(); return; }
    const QPixmap scaled = logoPm_.scaledToWidth(int(w * dpr), Qt::SmoothTransformation);
    // Drawn at 35 % on the black player: a quiet mark, not a bright sign.
    QPixmap pm(scaled.size());
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setOpacity(0.35);
        p.drawPixmap(0, 0, scaled);
    }
    pm.setDevicePixelRatio(dpr);
    logo_->setPixmap(pm);
}

void MainWindow::testPerfReset() {
    if (seq_) {
        seq_->cache()->resetStats();
        SeqView* v = seq_->seqView();
        v->uploads = v->uploadNs = v->paints = v->paintNs = 0;
    }
}

QString MainWindow::testCompare(const QStringList& a) {
    const QString c = a.value(0);
    if (c == "start") { startCompare(playlist_->pathAt(a.value(1).toInt())); return comparing_ ? "on" : "off"; }
    if (c == "slot") {  // slot a|b <path>: as a drop on that slot
        const QString path = a.mid(2).join(' ');
        if (a.value(1) == "a") setSlotA(path);
        else startCompare(path);
        return a.value(1);
    }
    if (c == "lutb") {  // lutb clear | lutb preset <source>|<target>
        LutChoice l;
        if (a.value(1) == "preset") {
            const QString spec = a.mid(2).join(' ');
            l.kind = LutChoice::Preset;
            l.source = spec.section('|', 0, 0);
            l.target = spec.section('|', 1);
        }
        setLutB(l);
        return l.label();
    }
    if (c == "stop") { stopCompare(); return "off"; }
    if (c == "mode") { setCompareMode(CompareMode(a.value(1).toInt())); return compareModeName(cmpView_.mode); }
    if (c == "wipe") {
        cmpView_.wipe = a.value(1).toFloat();
        applyCompareView();
        return QString::number(cmpView_.wipe);
    }
    if (c == "offset") { setCompareOffset(a.value(1).toInt()); return QString::number(cmpOffset_); }
    return "unknown";
}

// The engine went on to the queued clip by itself (gapless): the playlist follows.
void MainWindow::onAdvanced(const QString& path) {
    const int n = playlist_->count();
    int row = playlist_->currentRow() + 1 < n ? playlist_->currentRow() + 1 : 0;
    if (playlist_->pathAt(row) != path)
        for (int i = 0; i < n; ++i)
            if (playlist_->pathAt(i) == path) { row = i; break; }
    markSwitch("gapless");
    beginRow(row);
    if (abA_ < 0) cur_->setAbLoop(-1, -1);
}

// The clip after this one, for the engine to go on to with no stop: same engine only
// (a movie after a movie, a sequence after a sequence). Loop This File loops instead.
void MainWindow::queueNext() {
    if (!cur_) return;
    const int n = playlist_->count();
    const int row = playlist_->currentRow();
    QString next;
    // A clip that loops onto itself (Loop This File, or Loop Playlist with one clip) is
    // looped by its engine. Queued as its own "next", an image sequence was decoded a
    // second time into the gapless cache, and playback fell to ~9 fps.
    const bool self = repeat_ == Repeat::One || (repeat_ == Repeat::All && n == 1);
    cur_->setLoopFile(self);
    // Also while comparing: without the engine's loop, a looping compare STOPPED at the end,
    // the playlist reopened A alone and compare then reopened B, so for about a second at
    // every loop B was black (a difference view showed A itself).
    if (comparing_) return;
    if (!self && row >= 0 && n > 0) {
        const int r = row + 1 < n ? row + 1 : (repeat_ == Repeat::All ? 0 : -1);
        const QString p = r >= 0 ? playlist_->pathAt(r) : QString();
        if (!p.isEmpty() && SequenceScanner::isImageFile(p) == (cur_ == seq_)) next = p;
    }
    cur_->setNext(next);
}

void MainWindow::beginRow(int row) {
    const QString path = playlist_->pathAt(row);
    osd_->raise();
    stats_->raise();
    currentPath_ = path;
    rate_ = 1.0;
    abA_ = abB_ = -1;
    subFiles_.clear();
    seekBar_->setAb(-1, -1);
    seekBar_->setCacheMask({}, {});
    playlist_->setCurrent(row);
    addRecent(path);
    updateTitle();
    applyGrade();
}

void MainWindow::playNext(int dir, bool wrap) {
    const int n = playlist_->count();
    if (!n) return;
    int r = playlist_->currentRow() + dir;
    if (r >= n) { if (!wrap) return; r = 0; }
    if (r < 0) { if (!wrap) return; r = n - 1; }
    playRow(r);
}

void MainWindow::onEnd() {
    if (repeat_ == Repeat::One) return;  // the engine loops the file itself
    const int n = playlist_->count();
    if (playlist_->currentRow() + 1 < n) playNext(1, false);
    else if (repeat_ == Repeat::All && n > 0) playRow(0);
}

void MainWindow::nextFileInFolder(int dir) {
    if (currentPath_.isEmpty()) return;
    const QFileInfo fi(currentPath_);
    QStringList items = Playlist::expand({fi.absolutePath()});
    // Only this folder (expand also looks one level down).
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const QString& p) { return QFileInfo(p).absolutePath() != fi.absolutePath(); }),
                items.end());
    items.sort(Qt::CaseInsensitive);
    // The current item: for a sequence, the entry that belongs to the same sequence.
    int idx = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i] == fi.absoluteFilePath()) idx = i;
        else if (cur_ == seq_ && SequenceScanner::isImageFile(items[i]) &&
                 SequenceScanner::scan(items[i]).pattern() == seq_->sequence().pattern()) idx = i;
    }
    const int j = idx + dir;
    if (idx < 0 || j < 0 || j >= items.size()) return;
    openPaths({items[j]});
}

void MainWindow::closeMedia() {
    if (cur_) cur_->close();
    cur_ = nullptr;
    currentPath_.clear();
    stack_->setCurrentWidget(placeholder_);
    seekBar_->setDuration(0);
    seekBar_->setCacheMask({}, {});
    updateTitle();
}

// Counts the native windows this window gets (the tests): more than one = Qt destroyed and
// made the window again, and it vanished from the screen for a moment.
bool MainWindow::event(QEvent* e) {
    // On qApp, not a member: the first one comes while the members are still being built.
    if (e->type() == QEvent::WinIdChange && internalWinId())
        qApp->setProperty("nativeWindows", qApp->property("nativeWindows").toInt() + 1);
    return QMainWindow::event(e);
}

// ------------------------------------------------------------------ transport helpers

void MainWindow::seekBy(double s) {
    if (!cur_) return;
    cur_->seekRelative(s);
    osd_->showMessage(tr("%1%2 s").arg(s > 0 ? "+" : "").arg(s));
}

void MainWindow::setRate(double r) {
    rate_ = std::clamp(r, 1.0 / 16.0, 16.0);
    if (cur_) cur_->setSpeed(rate_);
    osd_->showMessage(tr("Speed: %1x").arg(rate_, 0, 'f', rate_ < 1 ? 3 : 2));
}

void MainWindow::setVolume(int v) {
    volume_ = std::clamp(v, 0, 100);
    if (volSlider_->value() != volume_) {
        const QSignalBlocker b(volSlider_);
        volSlider_->setValue(volume_);
    }
    if (cur_) cur_->setVolume(volume_);
    QSettings().setValue("audio/volume", volume_);
    osd_->showMessage(tr("Volume: %1%").arg(volume_));
}

void MainWindow::setAb(int which) {
    if (!cur_) return;
    if (which == 0) abA_ = cur_->position();
    else if (which == 1) abB_ = cur_->position();
    else abA_ = abB_ = -1;
    cur_->setAbLoop(abA_, abB_);
    seekBar_->setAb(abA_, abB_);
    osd_->showMessage(which < 0 ? tr("A-B loop cleared")
                                : tr("A-B mark %1: %2").arg(which == 0 ? "A" : "B").arg(timeText(cur_->position())));
}

void MainWindow::applyTransform() {
    if (cur_) cur_->setViewTransform(xf_);
    const bool zoomed = std::abs(xf_.zoom - 1.0) > 1e-6 || xf_.panX != 0 || xf_.panY != 0 ||
                        xf_.scaleX != 1.0 || xf_.scaleY != 1.0;
    fitBtn_->setVisible(zoomed);
    oneBtn_->setVisible(zoomed);
    layoutOverlays();
}

void MainWindow::pan(double dx, double dy) {
    xf_.panX += dx;
    xf_.panY += dy;
    applyTransform();
}

// Pan is in units of the view size and the picture is fitted at zoom 1, so the
// point under the cursor stays put when pan' = c - (c - pan) * f.
void MainWindow::zoomAt(QPointF pos, double factor) {
    const double cx = pos.x() / std::max(1, videoArea_->width()) - 0.5;
    const double cy = pos.y() / std::max(1, videoArea_->height()) - 0.5;
    const double z = std::clamp(xf_.zoom * factor, 0.02, 128.0);
    const double f = z / xf_.zoom;
    xf_.panX = cx - (cx - xf_.panX) * f;
    xf_.panY = cy - (cy - xf_.panY) * f;
    xf_.zoom = z;
    applyTransform();
    osd_->showMessage(tr("Zoom %1%").arg(std::lround(xf_.zoom * 100)), 800);
}

void MainWindow::zoomFit() {
    xf_.zoom = xf_.scaleX = xf_.scaleY = 1.0;
    xf_.panX = xf_.panY = 0.0;
    applyTransform();
}

void MainWindow::zoom1to1() {
    if (!cur_) return;
    const QSize m = cur_->mediaSize();
    if (m.isEmpty()) return;
    // The picture is fitted in device pixels; 1:1 undoes that fit.
    const double dpr = videoArea_->devicePixelRatioF();
    const double fit = std::min(videoArea_->width() * dpr / m.width(), videoArea_->height() * dpr / m.height());
    xf_.zoom = 1.0 / fit;
    xf_.scaleX = xf_.scaleY = 1.0;
    xf_.panX = xf_.panY = 0.0;
    applyTransform();
    osd_->showMessage(tr("Zoom 1:1"), 800);
}

void MainWindow::layoutOverlays() {
    const int m = 12;
    oneBtn_->adjustSize();
    fitBtn_->adjustSize();
    fitBtn_->move(videoArea_->width() - fitBtn_->width() - m, m);
    oneBtn_->move(fitBtn_->x() - oneBtn_->width() - 6, m);
    oneBtn_->raise();
    fitBtn_->raise();
    if (isFullScreen() && bottom_->parentWidget() == videoArea_) {
        const int h = bottom_->sizeHint().height();
        bottom_->setGeometry(0, videoArea_->height() - h, videoArea_->width(), h);
        bottom_->raise();
    }
}

// Full screen: the seek bar and controls float over the picture, half translucent,
// and hide when the mouse stops. Windowed: they sit under the picture again.
void MainWindow::setFullscreenControls(bool overlay) {
    if (overlay) {
        centralLayout_->removeWidget(bottom_);
        bottom_->setParent(videoArea_);
        bottom_->setAutoFillBackground(true);
        QPalette p = bottom_->palette();
        p.setColor(QPalette::Window, QColor(12, 12, 12, 150));
        bottom_->setPalette(p);
        bottom_->show();
        layoutOverlays();
        hideCursor_.start();
    } else {
        bottom_->setAutoFillBackground(false);
        bottom_->setPalette(QPalette());
        centralLayout_->addWidget(bottom_);
        bottom_->show();
    }
}

// ------------------------------------------------------------------ subtitles / export

void MainWindow::loadSubtitle(const QString& path) {
    if (cur_ != mpv_ || !mpv_) {
        osd_->showMessage(tr("Subtitles show on movies; open a movie first"), 2500);
        return;
    }
    mpv_->addSubtitle(path);
    if (!subFiles_.contains(path)) subFiles_ << path;
    osd_->showMessage(tr("Subtitle: %1").arg(QFileInfo(path).fileName()), 2000);
}

void MainWindow::subtitleStyleDialog() {
    SubtitleDialog d(SubStyle::load(), this);
    connect(&d, &SubtitleDialog::changed, this, [this](const SubStyle& s) { mpv()->applySubStyle(s.mpvOptions()); });
    d.exec();
}

void MainWindow::testSubStyle(const QString& key, const QString& value) {
    SubStyle s = SubStyle::load();
    if (key == "size") s.size = value.toInt();
    else if (key == "text") s.text = QColor(value);
    else if (key == "box") s.box = value == "on";
    else if (key == "boxColor") s.boxColor = QColor(value);
    else if (key == "boxOpacity") s.boxOpacity = value.toInt();
    else if (key == "font") s.font = value;
    else if (key == "reset") s = SubStyle();
    s.save();
    mpv()->applySubStyle(s.mpvOptions());
}

void MainWindow::exportDialog() {
    if (!cur_ || currentPath_.isEmpty()) {
        osd_->showMessage(tr("Open a clip first"), 2000);
        return;
    }
    if (exporter_ && exporter_->running()) {
        osd_->showMessage(tr("An export is already running"), 2000);
        return;
    }
    ExportDialog::Source s;
    s.path = currentPath_;
    s.isSequence = cur_ == seq_;
    s.fps = cur_->fps();
    s.duration = cur_->duration();
    s.abA = abA_;
    s.abB = abB_;
    s.lut = lut_;
    s.grade = currentGrade();
    if (s.isSequence) {
        s.seq = seq_->sequence();
        SeqView* v = seq_->seqView();
        s.view.kind = LutChoice::View;
        s.view.source = v->inputSpace();
        s.view.display = v->display();
        s.view.view = v->viewName();
    } else {
        s.hasAudio = !mpv_->property("audio-codec-name").isEmpty();
        const QString sid = mpv_->property("sid");
        const QString ext = mpv_->property("current-tracks/sub/external-filename");
        if (!ext.isEmpty()) s.subtitleFiles << ext;
        else if (sid != "no" && !sid.isEmpty()) s.subtitleTrack = sid;
    }
    s.subStyle = SubStyle::load();
    ExportDialog d(s, this);
    if (d.exec() != QDialog::Accepted) return;
    startExport(d.job(), true);
}

bool MainWindow::startExport(const ExportJob& job, bool showProgress) {
    QDir().mkpath(QFileInfo(job.output).absolutePath());  // a new folder typed in the dialog
    if (!exporter_) exporter_ = new Exporter(this);
    QProgressDialog* pd = nullptr;
    if (showProgress) {
        pd = new QProgressDialog(tr("Exporting %1...").arg(QFileInfo(job.output).fileName()), tr("Cancel"), 0, 1000, this);
        pd->setWindowTitle(tr("Export"));
        pd->setMinimumDuration(0);
        pd->setAttribute(Qt::WA_DeleteOnClose);
        connect(pd, &QProgressDialog::canceled, exporter_, &Exporter::cancel);
    }
    exporter_->disconnect(this);
    connect(exporter_, &Exporter::progress, this, [this, pd](double f) {
        exportProgress_ = f;
        if (pd) pd->setValue(int(f * 1000));
    });
    connect(exporter_, &Exporter::finished, this, [this, pd](bool ok, const QString& msg) {
        exportState_ = ok ? "done" : "failed";
        exportMessage_ = msg;
        if (afterExport_) {
            auto f = std::move(afterExport_);
            afterExport_ = nullptr;
            f(ok);
        }
        if (pd) pd->close();
        osd_->showMessage(msg, 5000);
        if (!ok && pd && !exporter_->wasCancelled()) QMessageBox::warning(this, tr("Export"), msg);
    });
    QString err;
    exportState_ = "running";
    exportMessage_.clear();
    exportProgress_ = 0;
    if (!exporter_->start(job, &err)) {
        exportState_ = "failed";
        exportMessage_ = err;
        if (pd) pd->close();
        if (showProgress) QMessageBox::warning(this, tr("Export"), err);
        return false;
    }
    return true;
}

bool MainWindow::testExport(const QString& presetId, const QString& output, bool colour, bool subs, double start, double end,
                            bool audio) {
    ExportDialog::Source s;
    s.path = currentPath_;
    s.isSequence = cur_ == seq_;
    s.fps = cur_ ? cur_->fps() : Rational{};
    s.abA = abA_;
    s.abB = abB_;
    ExportJob j;
    j.input = currentPath_;
    j.isSequence = s.isSequence;
    j.fps = s.fps;
    j.start = start;
    j.end = end;
    j.presetId = presetId;
    j.output = output;
    if (output.startsWith("suggest=")) {  // the name the Export window suggests, in that folder
        if (s.isSequence) s.seq = seq_->sequence();
        const ExportPreset* p = ExportPresets::find(presetId);
        if (!p) return false;
        j.output = ExportDialog::suggestedOutput(s, p, output.mid(8));
        qApp->setProperty("exportSuggested", j.output);
    }
    if (s.isSequence) {
        j.seq = seq_->sequence();
        j.startNumber = j.seq.first;
    }
    if (colour) {
        if (lut_.kind != LutChoice::None) j.colour = lut_;
        else if (s.isSequence) {
            j.colour.kind = LutChoice::View;
            j.colour.source = seq_->seqView()->inputSpace();
            j.colour.display = seq_->seqView()->display();
            j.colour.view = seq_->seqView()->viewName();
        }
        j.colourTarget = j.colour.kind == LutChoice::Preset ? j.colour.target : "sRGB";
    }
    j.includeAudio = audio;
    if (subs && mpv_) {
        j.burnSubtitles = true;
        const QString ext = mpv_->property("current-tracks/sub/external-filename");
        if (!ext.isEmpty()) j.subtitleFiles << ext;
        else j.subtitleTrack = mpv_->property("sid");
        j.subStyle = SubStyle::load();
    }
    return startExport(j, false);
}

// ------------------------------------------------------------------ updates

Updater* MainWindow::updater() {
    if (!updater_) {
        updater_ = new Updater(this);
        connect(updater_, &Updater::ready, this, [this](const QString& file) { lastUpdateFile_ = file; });
        // The last check or download error, for the test driver (update.error).
        connect(updater_, &Updater::checked, this, [](bool, const QString& err) { qApp->setProperty("updateError", err); });
        connect(updater_, &Updater::failed, this, [](const QString& err) { qApp->setProperty("updateError", err); });
    }
    return updater_;
}

// Once a day, a few seconds after start-up: quiet unless there is an update.
void MainWindow::autoCheckUpdates() {
    QSettings st;
    if (!updateAction_) return;  // no update feed for this platform (Linux: a new package)
    if (!st.value("updates/auto", true).toBool() || qApp->organizationName() == "CoreplayTest") return;
    const QDateTime last = st.value("updates/lastCheck").toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 20 * 3600) return;
    Updater* u = updater();
    connect(u, &Updater::checked, this, [this, u](bool ok) {
        if (!ok || !u->updateAvailable()) return;
        //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
        updateAction_->setText(tr("&Update to Coreplay %1...").arg(u->latest().version));
        //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
        osd_->showMessage(tr("Coreplay %1 is available: Help > Update").arg(u->latest().version), 8000);
    }, Qt::SingleShotConnection);
    u->check();
}

// Help > About: what Coreplay is, and an invitation to Core Studio (the link opens the
// browser only when clicked). Non-blocking, so playback goes on behind it.
void MainWindow::showAbout() {
    auto* box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setObjectName("aboutBox");
    //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
    box->setWindowTitle(tr("About Coreplay"));
    box->setIconPixmap(QPixmap(":/coreplay.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    box->setTextFormat(Qt::RichText);
    box->setTextInteractionFlags(Qt::TextBrowserInteraction);
    //: Keep "Core Studio", "Coreplay", "imCore Corp.", library and format names in English.
    const QString made = tr("Coreplay is made by %1 as a companion to %2, "
                            "our professional AI film studio: from the first line of the script to the final frame, "
                            "in one workspace.")
                             .arg(QString("<b>imCore Corp.</b>"), QString("<b>Core Studio</b>"));
    box->setText(QString("<h3 style='margin-bottom:2px'>%1</h3>"
                         "<p style='margin-top:0'>%2</p>"
                         "<p>%3</p>"
                         "<p>%4<br>"
                         "<a href='https://imcore.ai/core-studio.html' style='color:#d7ee87'><b>%5 &rarr;</b></a></p>"
                         "<p style='color:#8a8a8a'><small>%6</small></p>")
                     .arg(tr("Coreplay %1").arg(COREPLAY_VERSION),
                          tr("A fast, free player for movies and image sequences."), made,
                          tr("If you enjoy working with Coreplay, you will feel at home in Core Studio."),
                          tr("Explore Core Studio and go Pro"),
                          tr("Movies through libmpv (FFmpeg). Image sequences through "
                             "OpenImageIO, colour through OpenColorIO. Hotkeys follow MPC-HC.")));
    // The link's arrow points along the reading direction (left in Arabic).
    if (isRightToLeft()) box->setText(box->text().replace("&rarr;", "&larr;"));
    box->setStandardButtons(QMessageBox::Close);
    box->open();
}

void MainWindow::updatesDialog() {
    Updater* u = updater();
    QDialog d(this);
    d.setWindowTitle(tr("Check for Updates"));
    d.setMinimumWidth(460);
    auto* v = new QVBoxLayout(&d);
    auto* text = new QLabel(tr("Checking %1 ...").arg(Updater::feedUrl()), &d);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(text);
    auto* autoBox = new QCheckBox(tr("Check for updates automatically (once a day)"), &d);
    autoBox->setChecked(QSettings().value("updates/auto", true).toBool());
    connect(autoBox, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("updates/auto", on); });
    v->addWidget(autoBox);
    auto* bb = new QDialogButtonBox(&d);
    QPushButton* install = bb->addButton(Updater::installedCopy() ? tr("Download and Install") : tr("Download Installer"),
                                         QDialogButtonBox::AcceptRole);
    install->setEnabled(false);
    bb->addButton(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(install, &QPushButton::clicked, &d, &QDialog::accept);
    v->addWidget(bb);

    connect(u, &Updater::checked, &d, [u, text, install](bool ok, const QString& err) {
        const QString cur = Updater::currentVersion();
        if (!ok) {
            text->setText(QString("Coreplay %1\n\n%2").arg(cur, err));
            return;
        }
        if (!u->updateAvailable()) {
            //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
            text->setText(tr("Coreplay %1 is the newest version.").arg(cur));
            return;
        }
        //: Keep "Core Studio", "Coreplay", format and colour-space names in English.
        text->setText(tr("Coreplay %1 is available. You have %2.\n\n%3")
                          .arg(u->latest().version, cur, u->latest().notes.isEmpty() ? QString() : u->latest().notes));
        install->setEnabled(true);
    }, Qt::SingleShotConnection);
    u->check();
    if (d.exec() != QDialog::Accepted || !u->updateAvailable()) return;

    auto* pd = new QProgressDialog(tr("Downloading Coreplay %1...").arg(u->latest().version), tr("Cancel"), 0, 1000, this);
    pd->setWindowTitle(tr("Update"));
    pd->setMinimumDuration(0);
    pd->setAttribute(Qt::WA_DeleteOnClose);
    connect(pd, &QProgressDialog::canceled, u, &Updater::cancel);
    connect(u, &Updater::progress, pd, [pd](qint64 got, qint64 total) {
        if (total > 0) pd->setValue(int(got * 1000 / total));
    });
    connect(u, &Updater::failed, pd, [this, pd](const QString& msg) {
        pd->close();
        QMessageBox::warning(this, tr("Update"), msg);
    }, Qt::SingleShotConnection);
    connect(u, &Updater::ready, this, [this, pd](const QString& installer) {
        pd->close();
        if (!Updater::installedCopy()) {
            // Not a copy that can replace itself (a build folder, the .dmg, a read-only place):
            // show the verified download.
#ifdef Q_OS_WIN
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(installer)});
#elif defined(Q_OS_MACOS)
            QProcess::startDetached("open", {"-R", installer});
#else
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(installer).absolutePath()));
#endif
            return;
        }
        // The update waits for Coreplay to close, puts the new version in place and starts it.
        QString err;
        if (Updater::install(installer, &err)) close();
        else QMessageBox::warning(this, tr("Update"), err);
    }, Qt::SingleShotConnection);
    u->download();
}

void MainWindow::testUpdate(const QString& step) {
    Updater* u = updater();
    if (step == "check") u->check();
    else if (step == "download") u->download();
    // install: the real install step on the verified download (the end-to-end update test on a
    // Mac or a Linux AppImage). The release tests never use it: the Windows installer would run.
    else if (step == "install" && u->state() == "ready") {
        QString err;
        if (Updater::installedCopy() && Updater::install(lastUpdateFile_, &err)) close();
        else qApp->setProperty("updateInstallError", err.isEmpty() ? QString("not an installed copy") : err);
    }
}

void MainWindow::setChannel(int c, bool toggle) {
    if (toggle && c == channel_) c = 0;
    channel_ = c;
    seq()->setChannel(c);
    mpv()->setChannel(c);
    styleChannelButtons();
    static const char* names[] = {QT_TRANSLATE_NOOP("MainWindow", "All (RGB)"), QT_TRANSLATE_NOOP("MainWindow", "Red"),
                                  QT_TRANSLATE_NOOP("MainWindow", "Green"), QT_TRANSLATE_NOOP("MainWindow", "Blue"),
                                  QT_TRANSLATE_NOOP("MainWindow", "Alpha")};
    osd_->showMessage(tr("Channel: %1").arg(tr(names[c])), 1200);
    updateStatus();
}

// BunnyLab's behaviour in Coreplay's greys (no accent colours): grey letters; the
// active channel is filled grey with a black letter.
// The speaker with a cross while muted (mute is kept between sessions, so Coreplay may start muted).
void MainWindow::updateMuteButton() {
    if (!muteBtn_) return;
    muteBtn_->setIcon(grayIcon(style(), muted_ ? QStyle::SP_MediaVolumeMuted : QStyle::SP_MediaVolume));
    muteBtn_->setToolTip(muted_ ? tr("Mute: On") : tr("Mute"));
}

void MainWindow::updateLoopButton() {
    if (!loopBtn_) return;
    loopBtn_->setIcon(loopIcon(repeat_ != Repeat::None, controls_->palette().color(QPalette::Window).lightness() > 128));
    loopBtn_->setToolTip(repeat_ == Repeat::One   ? tr("Loop: on, this file (click: off; right-click: options)")
                         : repeat_ == Repeat::All ? tr("Loop: on, the playlist (click: off; right-click: options)")
                                                  : tr("Loop: off (click: on; right-click: options)"));
}

void MainWindow::styleChannelButtons() {
    for (int i = 0; i < 3; ++i) {
        QToolButton* b = chanBtn_[i];
        const bool on = channel_ == i + 1;
        QPalette p = b->palette();
        // Their own button colour (150) is the hover background in every style: black reads on it.
        p.setColor(QPalette::ButtonText, on || b->underMouse() ? QColor(0, 0, 0) : kIconGray);
        p.setColor(QPalette::Window, QColor(150, 150, 150));
        p.setColor(QPalette::Button, QColor(150, 150, 150));
        b->setPalette(p);
        b->setAutoFillBackground(on);
        //: Tooltip of the R, G, B buttons. %1 = the letter R, G or B (also the key);
        //: %2 = " (active)" when that channel is shown, else empty.
        b->setToolTip(tr("%1 channel%2 (%1)").arg(b->text(), on ? tr(" (active)") : QString()));
    }
}

// Right-click menu: export, LUTs, rotation and subtitles first, then the main menus.
void MainWindow::fillContextMenu(QMenu* m) {
    m->addAction(act("file.export"));
    m->addSeparator();
    QMenu* lut = m->addMenu(tr("&LUT"));
    addLutActions(lut);
    m->addAction(act("pns.rotatezp"));
    m->addAction(act("pns.rotatezm"));
    m->addSeparator();
    m->addAction(act("file.loadsub"));
    m->addAction(act("sub.style"));
    m->addSeparator();
    for (QAction* a : menuBar()->actions()) m->addAction(a);
}

void MainWindow::addLutActions(QMenu* m) {
    m->addAction(act("lut.load"));
    QMenu* presets = m->addMenu(tr("LUT &Presets"));
    for (const QString& src : Lut::presetSources()) {
        QMenu* sm = presets->addMenu(src);
        for (const QString& dst : Lut::presetTargets()) {
            QAction* a = sm->addAction(tr("to %1").arg(dst), this, [this, src, dst] {
                LutChoice c;
                c.kind = LutChoice::Preset;
                c.source = src;
                c.target = dst;
                setLut(c);
            });
            a->setCheckable(true);
            a->setChecked(lut_.kind == LutChoice::Preset && lut_.source == src && lut_.target == dst);
        }
    }
    QAction* clear = m->addAction(act("lut.clear")->text(), this, [this] { setLut(LutChoice()); });
    clear->setEnabled(lut_.kind != LutChoice::None);
    if (lut_.kind != LutChoice::None) m->addAction(tr("Active: %1").arg(lut_.label()))->setEnabled(false);
}

// One LUT for both engines, both on the GPU: sequences in the OCIO shader, movies as
// a baked 3D texture in an mpv shader.
void MainWindow::setLut(const LutChoice& c) {
    QString shader;
    if (c.kind != LutChoice::None) {
        QString err;
        shader = Lut::bakeMpvShader(c, &err);
        if (shader.isEmpty()) {
            osd_->showMessage(tr("Cannot use this LUT: %1").arg(err), 6000);
            return;
        }
    }
    lut_ = c;
    seq()->seqView()->setLut(c);
    mpv();
    applyMpvLut();
    updateCompareBar();
    osd_->showMessage(c.kind == LutChoice::None ? tr("LUT off") : tr("LUT: %1").arg(c.label()), 2500);
    updateStatus();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        if (wasMaximized_) showMaximized();
        else showNormal();
        menuBar()->setVisible(!act("view.caption")->isChecked());
        statusBar()->setVisible(act("view.status")->isChecked());
        setFullscreenControls(false);
        videoArea_->unsetCursor();
    } else {
        wasMaximized_ = isMaximized();
        menuBar()->hide();
        statusBar()->hide();
        showFullScreen();
        setFullscreenControls(true);
    }
}

// The picture as the window shows it: turned by 90 or 270 degrees it is upright, and side
// by side compare shows A and B next to each other (twice as wide).
QSizeF MainWindow::shownSize() const {
    if (!cur_) return {};
    QSizeF m = cur_->mediaSize();
    if (m.isEmpty()) return {};
    if (xf_.rotate % 180 != 0) m.transpose();
    if (comparing_ && cmpView_.mode == CompareMode::SideBySide) m.setWidth(m.width() * 2);
    return m;
}

// The window takes the picture's shape: its width stays, its height follows (smaller when the
// screen is too short), so there are no black bars at the sides or at the top and bottom.
// Not in full screen or maximized: there the screen decides the shape.
void MainWindow::fitWindowToPicture() {
    if (!cur_ || !isVisible() || isFullScreen() || isMaximized() || isMinimized()) return;
    const QSizeF p = shownSize();
    if (p.isEmpty() || videoArea_->width() <= 0) return;
    const double a = p.width() / p.height();
    const QSize chrome = size() - videoArea_->size();                 // menu, bars, docked playlist
    const QSize frame = frameGeometry().size() - size();              // title bar and borders
    const QRect avail = screen()->availableGeometry();
    int vw = videoArea_->width();
    int vh = int(std::lround(vw / a));
    const int maxH = avail.height() - frame.height() - chrome.height();
    if (vh > maxH) {
        vh = maxH;
        vw = int(std::lround(vh * a));
    }
    const QSize want = QSize(vw, vh) + chrome;
    if (want == size()) return;
    resize(want);
    QRect g = frameGeometry();  // still on the screen
    if (g.bottom() > avail.bottom()) g.moveBottom(avail.bottom());
    if (g.top() < avail.top()) g.moveTop(avail.top());
    if (g.topLeft() != frameGeometry().topLeft()) move(g.topLeft());
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
#ifndef Q_OS_WIN
    // macOS, Linux: the shape is set again when the user stops resizing (Windows: while dragging).
    if (!refitTimer_) {
        refitTimer_ = new QTimer(this);
        refitTimer_->setSingleShot(true);
        refitTimer_->setInterval(250);
        connect(refitTimer_, &QTimer::timeout, this, [this] { fitWindowToPicture(); });
    }
    refitTimer_->start();
#endif
}

// Windows: while the user drags an edge or a corner, the window keeps the picture's shape
// (as in mpv). The dragged side decides, the other side follows.
bool MainWindow::nativeEvent(const QByteArray& type, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SIZING && cur_ && !isFullScreen() && !isMaximized()) {
        const QSizeF p = shownSize();
        RECT wr;
        if (!p.isEmpty() && videoArea_->width() > 0 && GetWindowRect(msg->hwnd, &wr)) {
            auto* r = reinterpret_cast<RECT*>(msg->lParam);
            const double dpr = devicePixelRatioF(), a = p.width() / p.height();
            // Everything around the picture, in device pixels: frame, title, menu, bars.
            const int chromeW = (wr.right - wr.left) - int(std::lround(videoArea_->width() * dpr));
            const int chromeH = (wr.bottom - wr.top) - int(std::lround(videoArea_->height() * dpr));
            const int w = r->right - r->left, h = r->bottom - r->top;
            switch (msg->wParam) {
                case WMSZ_TOP:
                case WMSZ_BOTTOM:  // height drives width
                    r->right = r->left + chromeW + int(std::lround((h - chromeH) * a));
                    break;
                case WMSZ_TOPLEFT:
                case WMSZ_TOPRIGHT:  // width drives height, the top edge moves
                    r->top = r->bottom - (chromeH + int(std::lround((w - chromeW) / a)));
                    break;
                default:  // left, right, bottom corners: width drives height
                    r->bottom = r->top + chromeH + int(std::lround((w - chromeW) / a));
                    break;
            }
            *result = TRUE;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(type, message, result);
}

void MainWindow::zoomWindow(double factor) {
    if (!cur_ || isFullScreen()) return;
    const QSize m = cur_->mediaSize();
    if (m.isEmpty()) return;
    if (isMaximized()) showNormal();
    const QSize extra = size() - videoArea_->size();
    resize(QSize(int(m.width() * factor), int(m.height() * factor)) + extra);
}

void MainWindow::autoFit(bool largerOnly) {
    if (!cur_ || isFullScreen()) return;
    const QSize m = cur_->mediaSize();
    if (m.isEmpty()) return;
    const QRect avail = screen()->availableGeometry();
    const QSize extra = frameGeometry().size() - videoArea_->size();
    const QSize room = avail.size() - extra;
    double f = std::min(double(room.width()) / m.width(), double(room.height()) / m.height());
    if (largerOnly) f = std::min(f, 1.0);
    if (isMaximized()) showNormal();
    resize(QSize(int(m.width() * f), int(m.height() * f)) + (size() - videoArea_->size()));
    move(avail.center() - QPoint(width() / 2, height() / 2));
}

void MainWindow::setViewPreset(int p) {
    const bool menu = p == 2, bars = p >= 1, status = p == 2;
    menuBar()->setVisible(menu);
    act("view.caption")->setChecked(!menu);
    seekBar_->setVisible(bars);
    act("view.seeker")->setChecked(bars);
    controls_->setVisible(bars);
    act("view.controls")->setChecked(bars);
    statusBar()->setVisible(status);
    act("view.status")->setChecked(status);
}

// ------------------------------------------------------------------ dialogs

void MainWindow::openDialog() {
    QDialog d(this);
    d.setWindowTitle(tr("Open"));
    auto* l = new QVBoxLayout(&d);
    l->addWidget(new QLabel(tr("File, folder or URL:")));
    auto* row = new QHBoxLayout;
    auto* edit = new QLineEdit(&d);
    edit->setMinimumWidth(460);
    edit->setText(QSettings().value("open/last").toString());
    edit->selectAll();
    auto* browse = new QPushButton(tr("Browse..."), &d);
    row->addWidget(edit, 1);
    row->addWidget(browse);
    l->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    l->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(browse, &QPushButton::clicked, &d, [&] {
        const QString f = QFileDialog::getOpenFileName(&d, tr("Open"));
        if (!f.isEmpty()) edit->setText(QDir::toNativeSeparators(f));
    });
    if (d.exec() != QDialog::Accepted || edit->text().trimmed().isEmpty()) return;
    const QString t = edit->text().trimmed();
    QSettings().setValue("open/last", t);
    openPaths({QFileInfo::exists(t) ? QDir::fromNativeSeparators(t) : t});
}

void MainWindow::gotoDialog() {
    if (!cur_) return;
    const Rational r = cur_->fps();
    bool ok = false;
    const QString t = QInputDialog::getText(this, tr("Go To"),
                                            tr("Time (hh:mm:ss.ms) or frame number:"), QLineEdit::Normal,
                                            timeText(cur_->position()), &ok).trimmed();
    if (!ok || t.isEmpty()) return;
    if (t.contains(':')) {
        double s = 0;
        for (const QString& part : t.split(':')) s = s * 60 + part.toDouble();
        cur_->seekAbsolute(s);
    } else if (r.valid()) {
        long long frame = t.toLongLong(&ok);
        if (!ok) return;
        if (cur_ == seq_) seq_->seekFrameIndex(int(frame - seq_->sequence().first));
        else cur_->seekAbsolute(frame / r.value());
    }
}

void MainWindow::seqFpsDialog() {
    if (!seq_) return;
    const QStringList items = {"23.976 (24000/1001)", "24", "25", "29.97 (30000/1001)", "30", "47.952 (48000/1001)",
                               "48", "50", "59.94 (60000/1001)", "60", "120"};
    const Rational cur = seq_->fps();
    bool ok = false;
    QString t = QInputDialog::getItem(this, tr("Sequence Frame Rate"),
                                      tr("Frame rate for this sequence (now %1/%2%3).\n"
                                              "Type a fraction such as 30000/1001 for an exact rate.")
                                          .arg(cur.num).arg(cur.den).arg(seq_->fpsFromFile() ? tr(", from the file") : QString()),
                                      items, 1, true, &ok);
    if (!ok) return;
    Rational r;
    static const QRegularExpression frac(R"((\d+)\s*/\s*(\d+))");
    if (auto m = frac.match(t); m.hasMatch()) r = {m.captured(1).toInt(), m.captured(2).toInt()};
    else if (double v = t.section(' ', 0, 0).toDouble(); v > 0) {
        r = std::abs(v - std::round(v)) < 1e-9 ? Rational{int(std::round(v)), 1} : Rational{int(std::round(v * 1000)), 1000};
    }
    if (!r.valid()) return;
    seq_->setFps(r, true);
    QSettings st;
    st.setValue("seq/defaultFpsNum", r.num);
    st.setValue("seq/defaultFpsDen", r.den);
    osd_->showMessage(tr("Sequence frame rate: %1/%2").arg(r.num).arg(r.den));
}

void MainWindow::cacheDialog() {
    QSettings st;
    bool ok = false;
    const double gb = QInputDialog::getDouble(this, tr("Sequence Cache Size"), tr("RAM for decoded frames (GB):"),
                                              st.value("seq/cacheGB", 8.0).toDouble(), 0.5, 1024, 1, &ok);
    if (!ok) return;
    st.setValue("seq/cacheGB", gb);
    if (seq_) seq_->setCacheBudget(size_t(gb * double(size_t(1) << 30)));
}

void MainWindow::saveImage(bool autoName) {
    if (!cur_) return;
    const QFileInfo fi(currentPath_);
    const qint64 frame = cur_->frameNumber();
    const QString base = (cur_ == seq_ ? seq_->sequence().prefix : fi.completeBaseName()) +
                         (frame >= 0 ? QString("_%1").arg(frame) : QString()) + ".png";
    QString path;
    if (autoName) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Coreplay";
        QDir().mkpath(dir);
        path = dir + "/" + base;
    } else {
        path = QFileDialog::getSaveFileName(this, tr("Save Image"), fi.absolutePath() + "/" + base,
                                            "PNG (*.png);;JPEG (*.jpg);;TIFF (*.tif)");
        if (path.isEmpty()) return;
    }
    osd_->showMessage(saveImageTo(path) ? tr("Saved %1").arg(QDir::toNativeSeparators(path)) : tr("Could not save the image"));
}

bool MainWindow::saveImageTo(const QString& path) {
    if (!cur_) return false;
    bool ok = cur_->saveImage(path);
    // A movie's still comes from the decoder, before the GPU grade: bake the grade into it
    // (the same Grade::apply as Save and Export). A sequence's still is the graded view.
    const Grade g = currentGrade();
    if (ok && cur_ == mpv_ && !g.isIdentity()) {
        QImage img(path);
        ok = !img.isNull();
        if (ok) {
            img = img.convertToFormat(QImage::Format_RGB32);
            for (int y = 0; y < img.height(); ++y) {
                auto* row = reinterpret_cast<QRgb*>(img.scanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    float c[3] = {qRed(row[x]) / 255.f, qGreen(row[x]) / 255.f, qBlue(row[x]) / 255.f};
                    g.apply(c);
                    row[x] = qRgb(int(c[0] * 255 + 0.5f), int(c[1] * 255 + 0.5f), int(c[2] * 255 + 0.5f));
                }
            }
            ok = img.save(path);
        }
    }
    return ok;
}

void MainWindow::recycleCurrent() {
    if (!cur_ || currentPath_.isEmpty()) return;
    QStringList files;
    QString what;
    if (cur_ == seq_) {
        const Sequence& s = seq_->sequence();
        for (int f = s.first; f <= s.last; ++f)
            if (s.present[size_t(f - s.first)]) files << s.pathFor(f);
        what = tr("all %1 frames of\n%2").arg(files.size()).arg(QDir::toNativeSeparators(s.pattern()));
    } else {
        files << currentPath_;
        what = QDir::toNativeSeparators(currentPath_);
    }
    if (QMessageBox::question(this, tr("Move to Recycle Bin"), tr("Move %1\nto the Recycle Bin?").arg(what)) != QMessageBox::Yes)
        return;
    const int row = playlist_->currentRow();
    cur_->close();
    int failed = 0;
    for (const QString& f : files)
        if (!moveToRecycleBin(f)) ++failed;
    delete playlist_->takeItem(row);
    playlist_->setCurrent(-1);
    if (failed) osd_->showMessage(tr("%1 file(s) could not be moved").arg(failed), 4000);
    if (row < playlist_->count()) playRow(row);
    else closeMedia();
}

void MainWindow::showProperties() {
    if (!cur_) return;
    QMessageBox box(this);
    box.setWindowTitle(tr("Properties"));
    box.setText(cur_->info());
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.exec();
}

void MainWindow::openLocation() {
    if (currentPath_.isEmpty()) return;
#if defined(Q_OS_WIN)
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(currentPath_)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", currentPath_});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(currentPath_).absolutePath()));
#endif
}

void MainWindow::addRecent(const QString& path) {
    QSettings st;
    QStringList r = st.value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > kRecentMax) r.removeLast();
    st.setValue("recent", r);
    rebuildRecent();
}

void MainWindow::rebuildRecent() {
    recentMenu_->clear();
    const QStringList r = QSettings().value("recent").toStringList();
    for (const QString& p : r)
        recentMenu_->addAction(QDir::toNativeSeparators(p), this, [this, p] { openPaths({p}); });
    recentMenu_->addSeparator();
    recentMenu_->addAction(tr("Clear"), this, [this] { QSettings().remove("recent"); rebuildRecent(); });
}

// ------------------------------------------------------------------ status

QString MainWindow::timeText(double s) const {
    if (s < 0) s = 0;
    const int h = int(s / 3600), m = int(std::fmod(s, 3600) / 60);
    const double sec = std::fmod(s, 60);
    return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(sec, 6, 'f', 3, QChar('0'));
}

void MainWindow::updateTitle() {
    if (currentPath_.isEmpty()) { setWindowTitle("Coreplay"); return; }
    const QString name = cur_ == seq_ && seq_ ? seq_->sequence().displayName() : QFileInfo(currentPath_).fileName();
    const bool modified = unsavedGrades_.contains(currentPath_);
    setWindowTitle((modified ? "* " : "") + name + " - Coreplay");
    if (gradeWin_) gradeWin_->setClipName(name, modified);
    updateCompareBar();
}

void MainWindow::updateStatus() {
    if (!cur_) {
        timeLabel_->clear();
        statusLeft_->setText(tr("Ready"));
        return;
    }
    const Rational r = cur_->fps();
    const qint64 frame = cur_->frameNumber();
    QString t = timeText(cur_->position()) + " / " + timeText(cur_->duration());
    if (frame >= 0) {
        //: Keep the three leading spaces: they separate the fields (the statistics overlay splits on them).
        if (cur_ == seq_) t += tr("   frame %1 [%2-%3]").arg(frame).arg(seq_->sequence().first).arg(seq_->sequence().last);
        else t += tr("   frame %1").arg(frame);
    }
    timeLabel_->setText(t);
    QString s = cur_->paused() ? tr("Paused") : tr("Playing");
    if (r.valid()) s += tr("   %1 fps").arg(r.value(), 0, 'f', 3);
    if (cur_ == seq_) {
        if (!seq_->fpsFromFile()) s += tr(" (set, not from the file)");
        if (!seq_->paused()) s += tr("   playing at %1 fps").arg(seq_->measuredFps(), 0, 'f', 2);
        s += tr("   cache %1 / %2 GB").arg(seq_->cache()->usedBytes() / double(1 << 30), 0, 'f', 1)
                 .arg(seq_->cache()->budget() / double(1 << 30), 0, 'f', 0);
        if (int miss = seq_->sequence().missingCount()) s += tr("   %1 missing frames").arg(miss);
    }
    if (rate_ != 1.0) s += tr("   speed %1x").arg(rate_);
    // B waiting for a new A: start as soon as A shows a picture.
    if (!pendingB_.isEmpty() && cur_ && !cur_->mediaSize().isEmpty() && !comparing_) {
        const QString b = pendingB_;
        pendingB_.clear();
        startCompare(b);
    }
    if (lut_.kind != LutChoice::None) s += tr("   LUT: %1").arg(lut_.label());
    if (channel_) s += tr("   channel %1").arg(QString("RGBA").at(channel_ - 1));
    if (comparing_) s += tr("   compare %1 with %2").arg(compareModeLabel(cmpView_.mode), QFileInfo(cmpPath_).fileName());
    statusLeft_->setText(s);
    if (stats_->isVisible()) {
        stats_->setText(s.replace("   ", "\n") + "\n" + t.replace("   ", "\n"));
        stats_->adjustSize();
        stats_->move(videoArea_->width() - stats_->width() - 12, 12);
        stats_->raise();
    }
    if (infoDock_->isVisible() && cur_ == seq_) infoText_->setPlainText(cur_->info());
    refreshInfoOverlay(cur_ == seq_);  // a sequence's header can change from frame to frame
}

void MainWindow::setInfoOverlay(bool on) {
    act("view.infooverlay")->setChecked(on);
    infoOverlay_->setVisible(on);
    refreshInfoOverlay(true);
}

// Everything known about the clip on a translucent block (top left): the file, the
// engine's properties, the live position, then the file's own metadata.
void MainWindow::refreshInfoOverlay(bool clipChanged) {
    if (!infoOverlay_ || !infoOverlay_->isVisible()) return;
    if (!cur_ || currentPath_.isEmpty()) {
        infoOverlay_->setText(tr("No file"));
    } else {
        if (clipChanged || infoStatic_.isEmpty()) {
            QStringList l;
            const QFileInfo fi(currentPath_);
            if (cur_ == seq_) {
                l << tr("Folder: %1").arg(QDir::toNativeSeparators(fi.absolutePath()));
            } else {
                l << tr("Size: %1 MB   modified %2").arg(fi.size() / 1048576.0, 0, 'f', 1)
                         .arg(fi.lastModified().toString("yyyy-MM-dd HH:mm"));
            }
            l << cur_->info();
            const QStringList meta = cur_->metadata();
            l << "" << (meta.isEmpty() ? tr("Metadata: none") : tr("Metadata (%1)").arg(meta.size()));
            const int maxLines = std::max(4, (videoArea_->height() - 160) / 15);
            for (int i = 0; i < meta.size() && i < maxLines; ++i) l << "  " + meta[i];
            if (meta.size() > maxLines) l << tr("  ... %1 more (File > Properties)").arg(meta.size() - maxLines);
            infoStatic_ = l.join('\n');
        }
        QString live = timeLabel_->text();
        if (rate_ != 1.0) live += tr("   speed %1x").arg(rate_);
        infoOverlay_->setText(QString("%1\n%2\n%3").arg(windowTitle().chopped(11), live, infoStatic_));
    }
    infoOverlay_->adjustSize();
    infoOverlay_->move(12, 12);
    infoOverlay_->raise();
}

// ------------------------------------------------------------------ test hooks

QVariantMap MainWindow::testState() const {
    QVariantMap m;
    m["engine"] = !cur_ ? "none" : cur_ == seq_ ? "seq" : "mpv";
    m["title"] = windowTitle();
    m["playlist.count"] = playlist_->count();
    m["playlist.current"] = playlist_->currentRow();
    m["playlist.visible"] = playlistDock_->isVisible();
    m["menu.visible"] = menuBar()->isVisible();
    m["seekbar.visible"] = seekBar_->isVisible();
    m["controls.visible"] = controls_->isVisible();
    m["status.visible"] = statusBar()->isVisible();
    m["fullscreen"] = isFullScreen();
    m["ontop"] = windowHandle() && (windowHandle()->flags() & Qt::WindowStaysOnTopHint);
    m["winId"] = qulonglong(winId());
    m["window.w"] = width();
    m["window.h"] = height();
    m["menubar.h"] = menuBar()->isVisible() ? menuBar()->height() : 0;
    m["menubar.itemh"] = menuBar()->actions().isEmpty() ? 0 : menuBar()->actionGeometry(menuBar()->actions().first()).height();
#ifdef Q_OS_WIN
    {   // the title bar as Windows has it (DWM): dark mode on, and its caption colour
        BOOL dark = FALSE;
        COLORREF caption = 0;
        const HWND h = reinterpret_cast<HWND>(winId());
        DwmGetWindowAttribute(h, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);
        DwmGetWindowAttribute(h, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
        m["titlebar.dark"] = bool(dark);
        m["titlebar.caption"] = QString("%1,%2,%3").arg(GetRValue(caption)).arg(GetGValue(caption)).arg(GetBValue(caption));
    }
#endif
    m["picture.area"] = QString("%1x%2").arg(videoArea_->width()).arg(videoArea_->height());
    m["picture.areaAspect"] = videoArea_->height() > 0 ? double(videoArea_->width()) / videoArea_->height() : 0.0;
    {   const QSizeF s = shownSize();
        m["picture.aspect"] = s.isEmpty() ? 0.0 : s.width() / s.height(); }
    m["video.w"] = videoArea_->width();
    m["video.h"] = videoArea_->height();
    m["volume"] = volume_;
    m["muted"] = muted_;
    m["rate"] = rate_;
    m["abA"] = abA_;
    m["abB"] = abB_;
    m["pan.zoom"] = xf_.zoom;
    m["pan.x"] = xf_.panX;
    m["pan.rotate"] = xf_.rotate;
    m["pan.y"] = xf_.panY;
    m["zoomButtons.visible"] = fitBtn_->isVisible();
    m["controls.overlay"] = bottom_->parentWidget() == videoArea_;
    m["controls.shown"] = bottom_->isVisible();
    m["lut"] = lut_.label();
    m["channel"] = channel_;
    m["update.state"] = updater_ ? updater_->state() : QString("idle");
    m["update.latest"] = updater_ ? updater_->latest().version : QString();
    m["update.available"] = updater_ && updater_->updateAvailable();
    m["update.feed"] = Updater::feedUrl();
    m["update.supported"] = Updater::supported();
    m["update.installedCopy"] = Updater::installedCopy();
    m["update.installError"] = qApp->property("updateInstallError").toString();
    m["update.error"] = qApp->property("updateError").toString();
    m["version"] = Updater::currentVersion();
    m["newWindow"] = newWindowPerFile();
    m["info.overlay"] = infoOverlay_->isVisible();
    m["hotkeys.shown"] = hotkeyList_->isVisible();
    m["hotkeys.rows"] = hotkeyList_->rows();
    m["hotkeys.text"] = hotkeyList_->text();
    m["hotkeys.fontPx"] = hotkeyList_->fontPx();
    m["grade.summary"] = currentGrade().summary();
    m["grade.modified"] = unsavedGrades_.contains(currentPath_);
    m["grade.unsaved"] = int(unsavedGrades_.size());
    m["grade.window"] = gradeWin_ && gradeWin_->isVisible();
    m["grade.x"] = gradeWin_ ? gradeWin_->x() : 0;
    m["grade.y"] = gradeWin_ ? gradeWin_->y() : 0;
    m["grade.mpvShader"] = mpv_ ? mpv_->property("glsl-shaders").contains("/grade_") : false;
    m["title"] = windowTitle();
    m["info.text"] = infoOverlay_->isVisible() ? infoOverlay_->text().replace('\n', " | ") : QString();
    m["info.metadata"] = cur_ ? cur_->metadata().size() : 0;
    m["switch.last"] = lastSwitch_;
    // Speed probes (perf reset clears them).
    if (seq_) {
        const auto [n, ns] = seq_->cache()->decodeStats();
        m["perf.decodeMs"] = n ? ns / 1e6 / n : 0.0;
        m["perf.decoded"] = n;
        const SeqView* v = seq_->seqView();
        m["perf.uploadMs"] = v->uploads ? v->uploadNs / 1e6 / v->uploads : 0.0;
        m["perf.paintMs"] = v->paints ? v->paintNs / 1e6 / v->paints : 0.0;
        m["perf.paints"] = v->paints;
    }
    if (mpv_) {
        m["perf.mpvDropped"] = mpv_->property("frame-drop-count");
        m["perf.mpvDecoderDropped"] = mpv_->property("decoder-frame-drop-count");
        m["perf.mpvDelayed"] = mpv_->property("vo-delayed-frame-count");
        m["perf.mpvHwdec"] = mpv_->property("hwdec-current");
        m["perf.mpvVfFps"] = mpv_->property("estimated-vf-fps");
        m["perf.mpvDisplayFps"] = mpv_->property("estimated-display-fps");
    }
    m["window.nativeWindows"] = qApp->property("nativeWindows").toInt();
    m["gl.state"] = glState_;
    m["gl.softwareSetting"] = QSettings().value("gl/software", false).toBool();
    m["mpv.fastScaling"] = mpv_ && mpv_->mpvView()->software();
    m["mpv.scale"] = mpv_ ? mpv_->property("scale") : QString();
    m["repeat"] = repeat_ == Repeat::One ? "one" : repeat_ == Repeat::All ? "all" : "none";
    m["loopButton"] = loopBtn_ ? loopBtn_->toolTip() : QString();
    m["muteButton"] = muteBtn_ ? muteBtn_->toolTip() : QString();
    m["placeholder.shown"] = stack_->currentWidget() == placeholder_;
    m["placeholder.logo"] = logo_ && !logo_->pixmap().isNull() ? logo_->pixmap().width() / logo_->pixmap().devicePixelRatio() : 0;
    m["compare.on"] = comparing_;
    m["compare.bar"] = compareBar_ && compareBar_->isVisible();
    m["compare.slotA"] = compareBar_ ? compareBar_->slotA()->fileText() : QString();
    m["compare.slotB"] = compareBar_ ? compareBar_->slotB()->fileText() : QString();
    m["compare.lutB"] = cmpLutB_.kind == LutChoice::None ? QString("None") : cmpLutB_.label();
    m["mpv.glsl-shaders.cmp"] = mpv_ ? mpv_->property("glsl-shaders").contains("cmp_") : false;
    // The compare picture is on screen (a movie needs ~0.5 s to reopen with B; a sequence
    // shows B once B's frame has decoded, which a slow disk or CPU can delay).
    m["compare.ready"] = comparing_ && ((cur_ == seq_ && seq_->seqView()->frameB())
                                        || (mpv_ && mpv_->mpvView()->compare().mode != CompareMode::Off));
    m["compare.mode"] = compareModeName(comparing_ ? cmpView_.mode : CompareMode::Off);
    m["compare.swap"] = cmpView_.swap;
    m["compare.wipe"] = cmpView_.wipe;
    m["compare.offset"] = cmpOffset_;
    m["compare.b"] = QFileInfo(cmpPath_).fileName();
    if (comparing_ && cur_ == seq_) m["compare.bFrame"] = seq_->compareSequence().first + std::clamp(seq_->index() + cmpOffset_, 0, std::max(0, seq_->compareSequence().count() - 1));
    if (auto* about = findChild<QMessageBox*>("aboutBox")) {
        m["about.visible"] = about->isVisible();
        m["about.text"] = about->text();
    } else {
        m["about.visible"] = false;
    }
    m["settings.visible"] = settings_ && settings_->isVisible();
    m["settings.rows"] = settings_ ? settings_->visibleRows() : 0;
    m["settings.message"] = settings_ ? settings_->message() : QString();   // "gapless <ms>" or "reopen <ms>": gap at the last clip change
    m["pid"] = qApp->applicationPid();
    m["listen"] = qApp->property("listenName");
    {
        QMenu cm;
        const_cast<MainWindow*>(this)->fillContextMenu(&cm);
        QStringList items;
        for (QAction* a : cm.actions())
            if (!a->isSeparator()) items << a->text().remove('&');
        m["contextMenu"] = items.join(" | ");
        QStringList lutItems;
        for (QAction* a : cm.actions())
            if (a->menu() && a->text().remove('&') == tr("&LUT").remove('&'))
                for (QAction* b : a->menu()->actions()) if (!b->isSeparator()) lutItems << b->text().remove('&');
        m["contextMenu.lut"] = lutItems.join(" | ");
    }
    m["export.state"] = exportState_;
    m["export.suggested"] = qApp->property("exportSuggested").toString();
    m["export.message"] = exportMessage_;
    m["export.progress"] = exportProgress_;
    m["export.options"] = exporter_ ? exporter_->lastOptions() : QString();
    m["export.subtitleId"] = exporter_ ? exporter_->burnedSubtitleId() : QString();
    m["subtitles.loaded"] = subFiles_.size();
    m["channelButton.active"] = channel_ >= 1 && channel_ <= 3 ? chanBtn_[channel_ - 1]->autoFillBackground() : false;
    if (mpv_) {
        m["mpv.vf"] = mpv_->property("vf");
        m["mpv.lastLog"] = mpv_->lastLog();
        m["mpv.glsl-shaders"] = mpv_->property("glsl-shaders");
        m["mpv.sid"] = mpv_->property("sid");
        m["mpv.sub-text"] = mpv_->property("sub-text");
        m["mpv.sub-font-size"] = mpv_->property("sub-font-size");
        m["mpv.sub-border-style"] = mpv_->property("sub-border-style");
    }
    m["osd"] = osd_->isVisible() ? osd_->text() : QString();
    m["startupMs"] = qApp->property("startupMs");
    m["handovers"] = qApp->property("handovers");
    m["firstPaintMs"] = qApp->property("firstPaintMs");
    m["mainEnterMs"] = qApp->property("mainEnterMs");
    m["windowShownMs"] = qApp->property("windowShownMs");
    if (cur_) {
        m["frame"] = cur_->frameNumber();
        m["position"] = cur_->position();
        m["duration"] = cur_->duration();
        m["paused"] = cur_->paused();
        const Rational r = cur_->fps();
        m["fps"] = r.value();
        m["fps.rational"] = QString("%1/%2").arg(r.num).arg(r.den);
        m["media.w"] = cur_->mediaSize().width();
        m["media.h"] = cur_->mediaSize().height();
    }
    if (cur_ && cur_ == seq_) {
        const Sequence& s = seq_->sequence();
        m["seq.count"] = s.count();
        m["seq.first"] = s.first;
        m["seq.last"] = s.last;
        m["seq.missing"] = s.missingCount();
        m["seq.pattern"] = s.pattern();
        m["seq.fpsFromFile"] = seq_->fpsFromFile();
        m["seq.measuredFps"] = seq_->measuredFps();
        m["seq.inputSpace"] = seq_->seqView()->inputSpace();
        m["seq.display"] = seq_->seqView()->display();
        m["seq.view"] = seq_->seqView()->viewName();
        m["seq.exposure"] = seq_->seqView()->exposure();
        m["seq.channel"] = seq_->seqView()->channel();
        int cached = 0;
        for (bool b : seq_->cache()->cachedMask()) cached += b;
        m["seq.cachedFrames"] = cached;
        if (FramePtr f = seq_->seqView()->frame()) {
            static const char* types[] = {"u8", "u16", "f16", "f32"};
            m["seq.pixelType"] = types[int(f->sourceType)];  // the file's type
            m["seq.memType"] = types[int(f->type)];          // as kept in RAM
            m["seq.memChannels"] = f->channels;
            m["seq.channels"] = f->sourceChannels;
            m["seq.frameMissing"] = f->missing;
            m["seq.frameError"] = f->error;
        }
    }
    if (cur_ && cur_ == mpv_) {
        for (const char* p : {"volume", "speed", "pause", "mute", "ao", "hwdec-current", "video-codec-name", "aid",
                              "ab-loop-a", "ab-loop-b", "video-zoom", "video-rotate", "loop-file", "path"})
            m[QString("mpv.") + p] = mpv_->property(p);
    }
    return m;
}

QWidget* MainWindow::testView() const { return stack_->currentWidget(); }

void MainWindow::testMpvSet(const QString& name, const QString& value) {
    mpv()->setPropertyString(name.toUtf8().constData(), value);
}

QImage MainWindow::testGrabImage() { return videoArea_->grab().toImage(); }

bool MainWindow::testGrab(const QString& path) {
    // The whole video area, labels and on-screen text included: what the user sees.
    const QImage img = videoArea_->grab().toImage();
    return !img.isNull() && img.save(path);
}

// ------------------------------------------------------------------ colour grading

void MainWindow::toggleGradeWindow() {
    if (!gradeWin_) {
        gradeWin_ = new GradeWindow(this);
        connect(gradeWin_, &GradeWindow::gradeChanged, this, [this](const Grade& g) { setCurrentGrade(g); });
        connect(gradeWin_, &GradeWindow::saveRequested, this, [this] { saveGraded(false); });
        connect(gradeWin_, &GradeWindow::saveAsRequested, this, [this] { saveGraded(true); });
        connect(gradeWin_, &GradeWindow::exportRequested, this, [this] { exportDialog(); });
        connect(gradeWin_, &GradeWindow::keyForPlayer, this, [this](QKeyEvent* e) { forwardKey(e); });
    }
    if (gradeWin_->isVisible()) {
        gradeWin_->hide();
        return;
    }
    gradeWin_->setGrade(currentGrade());
    updateTitle();
    gradeWin_->placeNear(this);
    gradeWin_->show();
    gradeWin_->raise();
}

void MainWindow::setCurrentGrade(const Grade& g) {
    if (currentPath_.isEmpty()) {
        osd_->showMessage(tr("Open a clip first"), 2000);
        if (gradeWin_) gradeWin_->setGrade(Grade());
        return;
    }
    // Modified = the clip has a grade that is not in a file yet.
    if (g.isIdentity()) {
        grades_.remove(currentPath_);
        unsavedGrades_.remove(currentPath_);
    } else {
        grades_.insert(currentPath_, g);
        unsavedGrades_.insert(currentPath_);
    }
    applyGrade();
    updateTitle();
}

void MainWindow::applyGrade() {
    const Grade g = currentGrade();
    if (mpv_) mpv_->setGrade(cur_ == mpv_ ? g : Grade());
    if (seq_) seq_->seqView()->setGrade(cur_ == seq_ ? g : Grade());
    if (gradeWin_ && gradeWin_->grade() != g) gradeWin_->setGrade(g);
}

// The grading window keeps no keys: a hotkey typed there runs the player's command.
void MainWindow::forwardKey(QKeyEvent* e) {
    const QKeySequence ks(e->keyCombination());
    for (QAction* a : std::as_const(actions_))
        if (a->isEnabled() && a->shortcuts().contains(ks)) {
            a->trigger();
            return;
        }
}

// The preset that keeps the file's kind: the same codec for movies, the same image
// format (and depth) for sequences.
QString MainWindow::savePresetFor(const QString& ext) const {
    if (cur_ == seq_) {
        FramePtr f = seq_->seqView()->frame();
        const bool deep = f && f->sourceType != PixelType::U8;
        if (ext == "exr") return f && f->sourceType == PixelType::F32 ? "exr_float" : "exr_half";
        if (ext == "png") return deep ? "png16" : "png8";
        if (ext == "tif" || ext == "tiff") return deep ? "tiff16" : "tiff8";
        if (ext == "dpx") return deep ? "dpx16" : "dpx10";
        if (ext == "jpg" || ext == "jpeg") return "jpeg";
        if (ext == "webp") return "webp";
        if (ext == "jxl") return "jxl";
        return deep ? "tiff16" : "png8";
    }
    const QString codec = mpv_ ? mpv_->property("video-format") : QString();
    const QString pix = mpv_ ? mpv_->property("video-params/pixelformat") : QString();
    const bool tenBit = pix.contains("10") || pix.contains("12") || pix.contains("16");
    if (codec == "h264") return "h264";
    if (codec == "hevc") return "h265";
    if (codec == "av1") return "av1";
    if (codec == "vp9") return "vp9";
    if (codec == "prores") return pix.startsWith("yuva") || pix.contains("444") ? "prores4444" : "prores422hq";
    if (codec == "dnxhd") return pix.contains("444") ? "dnxhr_444" : tenBit ? "dnxhr_hqx" : "dnxhr_hq";
    if (codec == "cfhd") return "cineform";
    if (codec == "ffv1") return "ffv1";
    if (codec == "utvideo") return "utvideo";
    if (codec == "qtrle") return "qtrle";
    if (codec == "hap") return "hap";
    if (codec == "mjpeg") return "mjpeg";
    if (codec == "gif") return "gif";
    if (ext == "mov") return "prores422hq";
    if (ext == "webm") return "vp9";
    if (ext == "avi") return "mjpeg";
    return "h264";
}

void MainWindow::saveGraded(bool asNew) {
    if (!cur_ || currentPath_.isEmpty()) {
        osd_->showMessage(tr("Open a clip first"), 2000);
        return;
    }
    if (exporter_ && exporter_->running()) {
        osd_->showMessage(tr("A save or export is already running"), 2500);
        return;
    }
    const bool sequence = cur_ == seq_;
    if (!asNew && currentGrade().isIdentity()) {
        osd_->showMessage(tr("Nothing to save: this clip has no grade (Alt+G)"), 3000);
        return;
    }
    const QFileInfo fi(currentPath_);
    if (!asNew && sequence) {
        // Writing every frame of a sequence over itself would replace the scene-linear
        // originals with display values: a sequence is always saved as new frames.
        osd_->showMessage(tr("Image sequences are saved as new frames: choose a folder"), 3500);
        asNew = true;
    }
    if (!asNew) {
        if (qApp->organizationName() != "CoreplayTest" &&
            QMessageBox::question(this, tr("Save"),
                                  tr("Write the grade into\n%1?\n\nThe clip is encoded again with its own codec. The original goes to the Recycle Bin.")
                                      .arg(QDir::toNativeSeparators(currentPath_))) !=
                QMessageBox::Yes)
            return;
        saveGradedTo(QString(), true, true);
        return;
    }
    QString output;
    if (sequence) {
        const Sequence& s = seq_->sequence();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for the graded frames"), s.dir);
        if (dir.isEmpty()) return;
        if (QDir(dir) == QDir(s.dir)) {
            QMessageBox::warning(this, tr("Save As"), tr("Choose another folder: the graded frames would replace the originals."));
            return;
        }
        output = gradedSequenceOutput(dir, s);
    } else {
        const QString ext = fi.suffix().toLower();
        output = QFileDialog::getSaveFileName(this, tr("Save As"), fi.absolutePath() + "/" + fi.completeBaseName() + "_graded." + ext,
                                              QString("%1 (*.%2);;%3 (*)").arg(tr("Same format"), ext, tr("All files")));
        if (output.isEmpty()) return;
        if (QFileInfo(output).absoluteFilePath() == fi.absoluteFilePath()) {
            saveGraded(false);  // the same file: that is Save
            return;
        }
    }
    saveGradedTo(output, false, true);
}

// Writes the graded clip (what the player shows: the LUT or colour view, then the grade)
// with the same codec / image format. inPlace: to a temporary file next to it, then the
// original goes to the Recycle Bin and the new file takes its name.
bool MainWindow::saveGradedTo(const QString& outputIn, bool inPlace, bool showProgress) {
    if (!cur_ || currentPath_.isEmpty()) return false;
    const bool sequence = cur_ == seq_;
    const QString path = currentPath_;
    const QFileInfo fi(path);
    QString output = outputIn;
    if (inPlace) output = fi.absolutePath() + "/" + fi.completeBaseName() + ".coreplay-save." + fi.suffix();
    const QString ext = sequence ? QFileInfo(seq_->sequence().suffix).suffix().toLower() : QFileInfo(output).suffix().toLower();

    ExportJob j;
    j.input = path;
    j.isSequence = sequence;
    j.fps = cur_->fps();
    j.presetId = savePresetFor(ext);
    j.output = output;
    if (sequence) {
        j.seq = seq_->sequence();
        j.startNumber = j.seq.first;
        if (lut_.kind != LutChoice::None) {
            j.colour = lut_;
        } else {
            SeqView* v = seq_->seqView();
            j.colour.kind = LutChoice::View;
            j.colour.source = v->inputSpace();
            j.colour.display = v->display();
            j.colour.view = v->viewName();
        }
    } else {
        j.colour = lut_;
        static const QHash<QString, QString> muxers = {{"mp4", "mp4"}, {"m4v", "mp4"},     {"mov", "mov"}, {"mkv", "matroska"},
                                                      {"webm", "webm"}, {"avi", "avi"}, {"gif", "gif"}};
        j.muxer = muxers.value(ext);
        j.includeAudio = !mpv_->property("audio-codec-name").isEmpty();
    }
    if (j.colour.kind != LutChoice::None)
        j.colourTarget = j.colour.kind == LutChoice::Preset ? j.colour.target
                         : j.colour.display.contains("Rec.709") ? "Rec.709"
                         : j.colour.display.contains("2020")    ? "Rec.2020"
                                                                : "sRGB";
    j.grade = currentGrade();
    const bool lutBaked = !sequence && lut_.kind != LutChoice::None;

    afterExport_ = [this, path, output, inPlace, lutBaked](bool ok) {
        if (!ok) {
            if (inPlace) QFile::remove(output);
            return;
        }
        if (!inPlace) {
            unsavedGrades_.remove(path);  // the graded picture is in a file now
            updateTitle();
            osd_->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(output)), 4000);
            return;
        }
        // Release the file, keep the original in the Recycle Bin, put the new one in its place.
        const int row = playlist_->currentRow();
        const double pos = cur_ ? cur_->position() : 0.0;
        if (cur_) cur_->close();
        if (!moveToRecycleBin(path)) {
            exportState_ = "failed";
            exportMessage_ = tr("Could not move the original to the Recycle Bin. The graded clip is %1").arg(QDir::toNativeSeparators(output));
            QMessageBox::warning(this, tr("Save"), exportMessage_);
            if (row >= 0) playRow(row);
            return;
        }
        QFile::rename(output, path);
        grades_.remove(path);  // the grade is in the file now: showing it again would double it
        unsavedGrades_.remove(path);
        if (lutBaked) setLut(LutChoice());  // the same for the LUT
        if (row >= 0) {
            playRow(row);
            if (cur_ && pos > 0) {
                Engine* e = cur_;
                connect(e, &Engine::loaded, this, [e, pos] { e->seekAbsolute(pos); }, Qt::SingleShotConnection);
            }
        }
        exportMessage_ = tr("Saved %1").arg(QDir::toNativeSeparators(path));
        osd_->showMessage(exportMessage_ + tr(" (the original is in the Recycle Bin)"), 5000);
        updateTitle();
    };
    if (!startExport(j, showProgress)) {
        afterExport_ = nullptr;
        return false;
    }
    return true;
}

// Tests: grade show | hide | set <control> <value> | reset | saveas <path> | save
QString MainWindow::testGrade(const QStringList& a) {
    const QString cmd = a.value(0);
    if (cmd == "show" || cmd == "hide") {
        if ((cmd == "show") != (gradeWin_ && gradeWin_->isVisible())) toggleGradeWindow();
        return {};
    }
    if (cmd == "set" || cmd == "reset") {
        if (!gradeWin_) {
            toggleGradeWindow();
            gradeWin_->hide();
        }
        if (cmd == "reset") return gradeWin_->setControl("reset", 0) ? QString() : "reset failed";
        return gradeWin_->setControl(a.value(1), a.value(2).toDouble()) ? QString() : "unknown control " + a.value(1);
    }
    if (cmd == "grab") {  // the player with the grading window over it, as on the screen
        QPixmap shot = grab();
        if (gradeWin_ && gradeWin_->isVisible()) {
            QPainter p(&shot);
            p.drawPixmap(gradeWin_->geometry().topLeft() - mapToGlobal(QPoint(0, 0)), gradeWin_->grab());
        }
        return shot.save(a.value(1)) ? QString() : "cannot write " + a.value(1);
    }
    if (cmd == "drag") {
        if (!gradeWin_ || !gradeWin_->isVisible()) return "the grading window is not open";
        const QPoint before = gradeWin_->pos(), want(a.value(1).toInt(), a.value(2).toInt());
        gradeWin_->testDrag(want.x(), want.y());
        const QPoint moved = gradeWin_->pos() - before;
        if (moved != want) return QString("moved by %1,%2").arg(moved.x()).arg(moved.y());
        if (QSettings().value("grade/pos").toPoint() != gradeWin_->pos()) return "the position was not kept";
        return {};
    }
    if (cmd == "still") return saveImageTo(a.value(1)) ? QString() : "the still was not saved";
    if (cmd == "saveas") return saveGradedTo(a.value(1), false, false) ? QString() : "the save did not start";
    if (cmd == "saveasdir") {  // Save As of a sequence into a folder, named as the menu names it
        if (cur_ != seq_) return "not a sequence";
        return saveGradedTo(gradedSequenceOutput(a.value(1), seq_->sequence()), false, false) ? QString() : "the save did not start";
    }
    if (cmd == "save") {
        if (currentGrade().isIdentity()) return "no grade";
        return saveGradedTo(QString(), true, false) ? QString() : "the save did not start";
    }
    return "unknown grade command " + cmd;
}

// ------------------------------------------------------------------ events

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!unsavedGrades_.isEmpty() && qApp->organizationName() != "CoreplayTest") {
        QStringList names;
        for (const QString& p : unsavedGrades_) names << QFileInfo(p).fileName();
        names.sort();
        QMessageBox box(QMessageBox::Warning, "Coreplay",
                        tr("%1 graded clip(s) are not saved:\n\n%2\n\nClose and lose the grades?")
                            .arg(names.size()).arg(names.mid(0, 8).join('\n') + (names.size() > 8 ? "\n..." : "")),
                        QMessageBox::NoButton, this);
        QPushButton* discard = box.addButton(tr("Close Without Saving"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != discard) {
            e->ignore();
            return;
        }
    }
    QSettings st;
    if (isFullScreen()) toggleFullscreen();
    st.setValue("win/geometry", saveGeometry());
    st.setValue("win/state", saveState());
    QMainWindow::closeEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls()) paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    // MPC-HC: a drop opens; with Shift it appends to the playlist.
    openPaths(paths, QApplication::keyboardModifiers() & Qt::ShiftModifier);
}

void MainWindow::mouseReleaseEvent(QMouseEvent* e) {
    // The mouse's side buttons, bound like keys (default: previous / next in the playlist).
    if (e->button() == Qt::BackButton || e->button() == Qt::ForwardButton) {
        if (QAction* a = mouseCommand(e->button() == Qt::BackButton ? Hotkeys::MouseBack : Hotkeys::MouseForward, e->modifiers()))
            a->trigger();
        return;
    }
    QMainWindow::mouseReleaseEvent(e);
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if ((e->type() == QEvent::Enter || e->type() == QEvent::Leave) &&
        (o == chanBtn_[0] || o == chanBtn_[1] || o == chanBtn_[2]))
        QTimer::singleShot(0, this, [this] { styleChannelButtons(); });  // after underMouse() changed
    if (!warmedUp_ && e->type() == QEvent::Paint && o == placeholder_) {
        // The window is on screen. Now warm up both engines, so the first file opens
        // without the ~0.4 s switch to GL rendering and the library loading.
        warmedUp_ = true;
        const qint64 start = qApp->property("processStartEpochMs").toLongLong();
        if (start) qApp->setProperty("firstPaintMs", QDateTime::currentMSecsSinceEpoch() - start);
        QTimer::singleShot(50, this, [this] {
            mpv();
            seq();
        });
        QTimer::singleShot(4000, this, [this] { autoCheckUpdates(); });
        QTimer::singleShot(150, this, [this] { checkOpenGL(); });
        QTimer::singleShot(0, this, [this] {
            logoPm_.load(":/coreplay_wordmark.png");
            updatePlaceholderLogo();
        });
    }
    if (o == placeholder_ && e->type() == QEvent::Resize) updatePlaceholderLogo();
    if (o == videoArea_ && e->type() == QEvent::Resize) layoutOverlays();
    if (o == placeholder_ && e->type() == QEvent::MouseButtonDblClick) {
        toggleFullscreen();
        return true;
    }
    const bool onView = cur_ && o == cur_->view();
    if (onView && e->type() == QEvent::Wheel) {
        auto* w = static_cast<QWheelEvent*>(e);
        // Bound like a key (Settings > Hotkeys): by default Wheel = zoom at the mouse and
        // Ctrl+Wheel = jump 20 s (MPC-HC). macOS turns Shift+wheel into a sideways scroll.
        const int d = w->angleDelta().y() ? w->angleDelta().y() : w->angleDelta().x();
        if (d == 0) return true;
        QAction* a = mouseCommand(d > 0 ? Hotkeys::WheelUp : Hotkeys::WheelDown, w->modifiers());
        if (!a) return true;
        const QString id = actions_.key(a);
        if (id == "zoom.in" || id == "zoom.out") {  // smooth: every event, by its own size
            wheelZoom_ = true;
            wheelAt_ = w->position();
            wheelSteps_ = std::abs(d) / 120.0;
            a->trigger();
            wheelZoom_ = false;
            return true;
        }
        // Any other command: once per wheel notch (120), so a trackpad does not repeat it.
        if ((wheelAcc_ > 0) != (d > 0)) wheelAcc_ = 0;
        wheelAcc_ += d;
        while (std::abs(wheelAcc_) >= 120) {
            a->trigger();
            wheelAcc_ -= wheelAcc_ > 0 ? 120 : -120;
        }
        return true;
    }
    if (onView && e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::MiddleButton) {
        dragging_ = true;
        dragLast_ = static_cast<QMouseEvent*>(e)->position();
        middleFrom_ = dragLast_;
        cur_->view()->setCursor(Qt::ClosedHandCursor);
        return true;
    }
    if (dragging_ && e->type() == QEvent::MouseMove && onView) {
        const QPointF p = static_cast<QMouseEvent*>(e)->position();
        pan((p.x() - dragLast_.x()) / std::max(1, videoArea_->width()),
            (p.y() - dragLast_.y()) / std::max(1, videoArea_->height()));
        dragLast_ = p;
        return true;
    }
    if (dragging_ && e->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent*>(e)->button() == Qt::MiddleButton) {
        dragging_ = false;
        if (cur_) cur_->view()->unsetCursor();
        // Held and moved: a pan. Pressed and let go in place: a Middle Click (bindable).
        auto* me = static_cast<QMouseEvent*>(e);
        if ((me->position() - middleFrom_).manhattanLength() < 5)
            if (QAction* a = mouseCommand(Hotkeys::MiddleClick, me->modifiers())) a->trigger();
        return true;
    }
    if (e->type() == QEvent::MouseMove && isFullScreen()) {
        videoArea_->unsetCursor();
        if (!bottom_->isVisible()) {
            bottom_->show();
            layoutOverlays();
        }
        hideCursor_.start();
    }
    return QMainWindow::eventFilter(o, e);
}
