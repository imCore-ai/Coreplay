#include "TestDriver.h"

#include <QAbstractButton>
#include <QAction>
#include <QMimeData>
#include <QUrl>
#include "CompareBar.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QToolButton>
#include <QHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QKeySequence>
#include <QTest>
#include <QTextStream>
#include <QTimer>

#include "MainWindow.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

TestDriver::TestDriver(MainWindow* w, const QString& script, const QString& log)
    : QObject(w), w_(w), logPath_(log) {
    QFile f(script);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) lines_ = QString::fromUtf8(f.readAll()).split('\n');
    QFile::remove(logPath_);
}

void TestDriver::start() { QTimer::singleShot(0, this, &TestDriver::step); }

void TestDriver::log(const QString& line) {
    QFile f(logPath_);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&f) << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << "  " << line << "\n";
    }
}

bool TestDriver::check(const QString& field, const QString& op, const QString& value, QString* got) {
    const QVariantMap st = w_->testState();
    if (!st.contains(field)) { *got = "<no such field>"; return false; }
    const QVariant v = st.value(field);
    *got = v.toString();
    bool numA = false, numB = false;
    const double a = v.toString().toDouble(&numA);
    const double b = value.toDouble(&numB);
    if (numA && numB) {
        if (op == "==") return a == b;
        if (op == "!=") return a != b;
        if (op == ">=") return a >= b;
        if (op == "<=") return a <= b;
        if (op == ">") return a > b;
        if (op == "<") return a < b;
        if (op == "~") return std::abs(a - b) <= std::abs(b) * 0.03 + 1e-9;
    }
    if (op == "==") return v.toString() == value;
    if (op == "has") return v.toString().contains(value);  // text contains
    if (op == "!=") return v.toString() != value;
    return false;
}

void TestDriver::step() {
    while (pc_ < lines_.size()) {
        QString line = lines_[pc_].trimmed();
        line.replace("$MEDIA", qEnvironmentVariable("COREPLAY_TEST_MEDIA"));
        line.replace("$OUT", qEnvironmentVariable("COREPLAY_TEST_OUT"));
        const QStringList t = line.split(' ', Qt::SkipEmptyParts);
        if (t.isEmpty() || line.startsWith('#')) { ++pc_; continue; }
        const QString cmd = t[0];
        const QString rest = line.mid(cmd.size()).trimmed();

        if (cmd == "open" || cmd == "append") {
            log(cmd + " " + rest);
            w_->openPaths(rest.split('|', Qt::SkipEmptyParts), cmd == "append");
        } else if (cmd == "key") {
            const QKeySequence ks = QKeySequence::fromString(rest, QKeySequence::PortableText);
            if (ks.isEmpty()) {
                ++failures_;
                log("FAIL  key: cannot parse '" + rest + "'");
            } else {
                // The test window is shown without taking focus, so Qt's shortcut map
                // (which needs an active window) is not used. Resolve the key the same
                // way: the action that owns it. Two owners = the ambiguity Qt would refuse.
                QList<QAction*> owners;
                for (QAction* a : w_->findChildren<QAction*>())
                    if (a->isEnabled() && a->shortcuts().contains(ks)) owners << a;
                if (owners.size() == 1) {
                    owners[0]->trigger();
                    log("key " + rest + " -> " + owners[0]->text().remove('&'));
                } else {
                    ++failures_;
                    log(QString("FAIL  key %1 has %2 actions").arg(rest).arg(owners.size()));
                }
            }
        } else if (cmd == "wait") {
            ++pc_;
            QTimer::singleShot(rest.toInt(), this, &TestDriver::step);
            return;
        } else if (cmd == "until" && t.size() >= 5) {
            // until <field> <op> <value, may hold spaces> <timeout-ms>
            QString got;
            const QString want = t.mid(3, t.size() - 4).join(' ');
            if (!untilDeadline_) untilDeadline_ = QDateTime::currentMSecsSinceEpoch() + t.last().toLongLong();
            if (check(t[1], t[2], want, &got)) {
                log(QString("PASS  until %1 %2 %3 (got %4)").arg(t[1], t[2], want, got));
                untilDeadline_ = 0;
            } else if (QDateTime::currentMSecsSinceEpoch() > untilDeadline_) {
                ++failures_;
                log(QString("FAIL  until %1 %2 %3 timed out (got %4)").arg(t[1], t[2], want, got));
                untilDeadline_ = 0;
            } else {
                QTimer::singleShot(20, this, &TestDriver::step);
                return;
            }
        } else if (cmd == "expect" && t.size() >= 4) {
            QString got;
            const QString want = t.mid(3).join(' ');
            if (check(t[1], t[2], want, &got)) log(QString("PASS  %1 %2 %3 (got %4)").arg(t[1], t[2], want, got));
            else { ++failures_; log(QString("FAIL  %1 %2 %3 (got %4)").arg(t[1], t[2], want, got)); }
        } else if (cmd == "grabwidget") {
            // grabwidget <objectName> <png>: a picture of one window or dialog of the app
            QWidget* found = nullptr;
            for (QWidget* top : QApplication::topLevelWidgets()) {
                if (top->objectName() == t.value(1)) found = top;
                else if (!found) found = top->findChild<QWidget*>(t.value(1));
            }
            const bool ok = found && found->isVisible() && found->grab().save(t.value(2));
            if (!ok) ++failures_;
            log(QString("%1  grabwidget %2 %3").arg(ok ? "PASS" : "FAIL", t.value(1), t.value(2)));
        } else if (cmd == "grab" || cmd == "grabwindow") {
            const bool ok = cmd == "grab" ? w_->testGrab(rest) : w_->grab().save(rest);
            if (!ok) ++failures_;
            log(QString("%1  grab %2").arg(ok ? "PASS" : "FAIL", rest));
        } else if (cmd == "keymap") {
            // Every key is owned by at most one action.
            QHash<QString, QStringList> owners;
            for (QAction* a : w_->findChildren<QAction*>())
                for (const QKeySequence& k : a->shortcuts()) owners[k.toString()] << a->text().remove('&');
            int clashes = 0, keys = 0;
            for (auto it = owners.begin(); it != owners.end(); ++it) {
                ++keys;
                if (it.value().size() > 1) {
                    ++clashes;
                    log("FAIL  key " + it.key() + " clashes: " + it.value().join(", "));
                }
            }
            failures_ += clashes;
            log(QString("%1  keymap: %2 keys, %3 clashes").arg(clashes ? "FAIL" : "PASS").arg(keys).arg(clashes));
        } else if (cmd == "wheel" || cmd == "dblclick" || cmd == "mdrag" || cmd == "ldrag" || cmd == "mousemove" ||
                   cmd == "mclick" || cmd == "sidebutton") {
            // Positions are fractions of the picture widget (0..1), so scripts work at any size.
            QWidget* v = w_->testView();
            auto at = [&](int i) { return QPointF(t.value(i).toDouble() * v->width(), t.value(i + 1).toDouble() * v->height()); };
            auto send = [&](QEvent::Type type, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
                QMouseEvent ev(type, p, v->mapToGlobal(p), b, bs, Qt::NoModifier);
                QApplication::sendEvent(v, &ev);
            };
            // Modifiers: ctrl, shift, alt, or several joined by "+" (ctrl+shift).
            auto modsOf = [](const QString& s) {
                Qt::KeyboardModifiers m;
                for (const QString& p : s.toLower().split('+', Qt::SkipEmptyParts)) {
                    if (p == "ctrl") m |= Qt::ControlModifier;
                    else if (p == "shift") m |= Qt::ShiftModifier;
                    else if (p == "alt") m |= Qt::AltModifier;
                }
                return m;
            };
            if (cmd == "mclick") {  // mclick x y [mods]: middle button down and up in place
                const QPointF p = at(1);
                const Qt::KeyboardModifiers mods = modsOf(t.value(3));
                QMouseEvent down(QEvent::MouseButtonPress, p, v->mapToGlobal(p), Qt::MiddleButton, Qt::MiddleButton, mods);
                QApplication::sendEvent(v, &down);
                QMouseEvent up(QEvent::MouseButtonRelease, p, v->mapToGlobal(p), Qt::MiddleButton, Qt::NoButton, mods);
                QApplication::sendEvent(v, &up);
            } else if (cmd == "sidebutton") {  // sidebutton back|forward [mods]: the mouse's side button, to the window
                const Qt::MouseButton b = t.value(1) == "forward" ? Qt::ForwardButton : Qt::BackButton;
                const QPointF p(w_->width() / 2.0, w_->height() / 2.0);
                QMouseEvent up(QEvent::MouseButtonRelease, p, w_->mapToGlobal(p), b, Qt::NoButton, modsOf(t.value(2)));
                QApplication::sendEvent(w_, &up);
            } else if (cmd == "wheel") {
                const QPointF p = at(1);
                const Qt::KeyboardModifiers mods = modsOf(t.value(4));
                QWheelEvent ev(p, v->mapToGlobal(p), QPoint(), QPoint(0, t.value(3).toInt()), Qt::NoButton, mods,
                               Qt::NoScrollPhase, false);
                QApplication::sendEvent(v, &ev);
            } else if (cmd == "dblclick") {
                const QPointF p = at(1);
                send(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
                send(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
                send(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
            } else if (cmd == "mdrag" || cmd == "ldrag") {
                const Qt::MouseButton btn = cmd == "ldrag" ? Qt::LeftButton : Qt::MiddleButton;
                const QPointF a = at(1), b = at(3);
                send(QEvent::MouseButtonPress, a, btn, btn);
                for (int k = 1; k <= 10; ++k)
                    send(QEvent::MouseMove, a + (b - a) * (k / 10.0), Qt::NoButton, btn);
                send(QEvent::MouseButtonRelease, b, btn, Qt::NoButton);
            } else {
                send(QEvent::MouseMove, at(1), Qt::NoButton, Qt::NoButton);
            }
            log(line);
        } else if (cmd == "sizing") {
            // sizing right|bottom|topleft <dx> <dy>: drag that window edge by dx, dy device
            // pixels the way Windows does (WM_SIZING, which the window may correct), then apply it.
#ifdef Q_OS_WIN
            const HWND h = reinterpret_cast<HWND>(w_->winId());
            RECT r;
            GetWindowRect(h, &r);
            const QString edge = t.value(1);
            const int dx = t.value(2).toInt(), dy = t.value(3).toInt();
            WPARAM side = WMSZ_RIGHT;
            if (edge == "right") r.right += dx;
            else if (edge == "bottom") { side = WMSZ_BOTTOM; r.bottom += dy; }
            else if (edge == "topleft") { side = WMSZ_TOPLEFT; r.left -= dx; r.top -= dy; }
            SendMessageW(h, WM_SIZING, side, reinterpret_cast<LPARAM>(&r));
            SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
            QApplication::processEvents();
#endif
            log(line);
        } else if (cmd == "clickat") {
            // clickat x y: a left click on the top widget at that point of the window
            // (fractions 0..1), the way a real click is routed (overlays first).
            const QPoint wp(int(t.value(1).toDouble() * w_->width()), int(t.value(2).toDouble() * w_->height()));
            QWidget* hit = w_->childAt(wp);
            if (!hit) hit = w_;
            const QPointF p = hit->mapFrom(w_, wp);
            QMouseEvent press(QEvent::MouseButtonPress, p, hit->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, p, hit->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(hit, &press);
            QApplication::sendEvent(hit, &release);
            log(line + " -> " + hit->metaObject()->className());
        } else if (cmd == "dropon") {
            // dropon A|B <path>: drag a file from outside onto the middle of compare slot A or B
            // and drop it, routed as Qt routes a real drop (the top widget at that point, then up
            // to the first one that accepts drops).
            CompareSlot* slot = nullptr;
            for (CompareSlot* s : w_->findChildren<CompareSlot*>())
                if (s->toolTip().startsWith(QString("Slot %1:").arg(t.value(1)))) slot = s;
            const QString path = rest.section(' ', 1);
            if (!slot || !slot->isVisible()) {
                ++failures_;
                log("FAIL  " + line + ": slot not visible");
            } else {
                const QPoint wp = slot->mapTo(w_, slot->rect().center());
                QWidget* hit = w_->childAt(wp);
                while (hit && !hit->acceptDrops()) hit = hit->parentWidget();
                if (!hit) hit = w_;
                QMimeData mime;
                mime.setUrls({QUrl::fromLocalFile(path)});
                const QPointF p = hit->mapFrom(w_, wp);
                QDragEnterEvent enter(p.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(hit, &enter);
                QDragMoveEvent move(p.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
                move.setAccepted(enter.isAccepted());  // as Qt does: a move starts with the enter's answer
                if (enter.isAccepted()) QApplication::sendEvent(hit, &move);
                const bool accepted = enter.isAccepted() && move.isAccepted();
                QDropEvent drop(p, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
                if (accepted) QApplication::sendEvent(hit, &drop);
                log(QString("%1 -> %2 (%3)").arg(line, hit->metaObject()->className(), accepted ? "accepted" : "REFUSED"));
            }
        } else if (cmd == "hover") {
            // hover <tooltip start> <png>: show that control button as under the mouse, save it,
            // and check its icon or letter is dark (readable on the light hover background).
            QToolButton* hit = nullptr;
            const QString want = t.value(1).replace('_', ' ');
            for (QToolButton* b : w_->findChildren<QToolButton*>())
                if (b->isVisible() && (b->toolTip().startsWith(want) || b->text() == want)) hit = b;
            if (!hit) {
                ++failures_;
                log("FAIL  " + line + ": no such button");
            } else {
                hit->setAttribute(Qt::WA_UnderMouse, true);
                QEvent enter(QEvent::Enter);
                QApplication::sendEvent(hit, &enter);
                QApplication::processEvents();
                QApplication::processEvents();
                const QImage img = hit->grab().toImage();
                img.save(t.value(2));
                // the darkest pixel of the drawing vs the button background (its corner)
                const QColor bg = img.pixelColor(3, img.height() / 2);  // inside the rounded hover area
                // The drawing (icon or letter) is the pixel farthest from the background:
                // dark on a light hover background, light on a dark one.
                const int back = qGray(bg.rgb());
                int ink = back;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x) {
                        const int g = qGray(img.pixel(x, y));
                        if (std::abs(g - back) > std::abs(ink - back)) ink = g;
                    }
                const bool ok = std::abs(ink - back) >= 90;
                if (!ok) ++failures_;
                log(QString("%1  hover %2: drawing %3, background %4").arg(ok ? "PASS" : "FAIL", want).arg(ink).arg(back));
                hit->setAttribute(Qt::WA_UnderMouse, false);
                QEvent leave(QEvent::Leave);
                QApplication::sendEvent(hit, &leave);
            }
        } else if (cmd == "clickbutton") {
            // A visible on-screen button with this text (e.g. "1:1", "Fit").
            QAbstractButton* hit = nullptr;
            for (auto* b : w_->findChildren<QAbstractButton*>())
                if (b->isVisible() && (b->text() == rest || (b->text().isEmpty() && b->toolTip().startsWith(rest)))) hit = b;
            if (hit) { hit->click(); log("PASS  clickbutton " + rest); }
            else { ++failures_; log("FAIL  clickbutton " + rest + ": no visible button"); }
        } else if (cmd == "pixel") {
            // pixel <x> <y> <name>: remember the colour at a picture position as pixel.<name>
            QWidget* v = w_->testView();
            const QImage img = w_->testGrabImage();
            const QPoint p = v->mapTo(v->parentWidget(), QPoint(int(t.value(1).toDouble() * v->width()),
                                                                  int(t.value(2).toDouble() * v->height())));
            const qreal dpr = img.devicePixelRatio();
            const QColor c = img.pixelColor(int(p.x() * dpr), int(p.y() * dpr));
            pixels_[t.value(3)] = c;
            log(QString("pixel %1 = %2 %3 %4").arg(t.value(3)).arg(c.red()).arg(c.green()).arg(c.blue()));
        } else if (cmd == "expectpixel") {
            // expectpixel <name> == <name2>|<r,g,b> <tolerance>
            const QColor a = pixels_.value(t.value(1));
            QColor b;
            if (pixels_.contains(t.value(3))) b = pixels_.value(t.value(3));
            else { const QStringList c = t.value(3).split(','); b = QColor(c.value(0).toInt(), c.value(1).toInt(), c.value(2).toInt()); }
            const int tol = t.value(4).toInt();
            const int d = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
            const bool same = d <= tol;
            const bool ok = t.value(2) == "==" ? same : !same;
            if (!ok) ++failures_;
            log(QString("%1  pixel %2 (%3,%4,%5) %6 %7 (%8,%9,%10) tol %11, diff %12")
                    .arg(ok ? "PASS" : "FAIL", t.value(1)).arg(a.red()).arg(a.green()).arg(a.blue())
                    .arg(t.value(2), t.value(3)).arg(b.red()).arg(b.green()).arg(b.blue()).arg(tol).arg(d));
        } else if (cmd == "export") {
            // export <preset> <output> [colour] [subs] [range=<start>:<end> seconds]
            // (starts it; wait with: until export.state != running <ms>)
            const QStringList flags = t.mid(3);
            double a = -1, b = -1;
            for (const QString& f : flags)
                if (f.startsWith("range=")) {
                    a = f.mid(6).section(':', 0, 0).toDouble();
                    b = f.mid(6).section(':', 1).toDouble();
                }
            const bool ok = w_->testExport(t.value(1), t.value(2), flags.contains("colour"), flags.contains("subs"), a, b,
                                           !flags.contains("noaudio"));
            if (!ok) ++failures_;
            log(QString("%1  export %2 -> %3").arg(ok ? "start" : "FAIL ", t.value(1), t.value(2)));
        } else if (cmd == "loadsub") {
            w_->testLoadSubtitle(rest);
            log(line);
        } else if (cmd == "substyle") {
            w_->testSubStyle(t.value(1), t.value(2));
            log(line);
        } else if (cmd == "probe") {
            // probe <file> <key> <op> <value>: ffprobe the output (test-only tool on PATH).
            // keys: codec, pix_fmt, width, height, r_frame_rate, frames, duration, audio_codec,
            //       color_primaries, color_transfer, color_space, exists
            const QString file = t.value(1);
            QMap<QString, QString> kv;
            kv["exists"] = QFile::exists(file) ? "yes" : "no";
            if (kv["exists"] == "yes") {
                auto run = [&](const QStringList& a) {
                    QProcess pr;
                    pr.start("ffprobe", a);
                    pr.waitForFinished(120000);
                    for (const QString& l : QString::fromUtf8(pr.readAllStandardOutput()).split('\n')) {
                        const int eq = l.indexOf('=');
                        if (eq > 0) kv.insert(l.left(eq).trimmed(), l.mid(eq + 1).trimmed());
                    }
                };
                run({"-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                     "stream=codec_name,pix_fmt,width,height,r_frame_rate,avg_frame_rate,nb_read_frames,color_primaries,color_transfer,color_space",
                     "-show_entries", "format=duration", "-of", "default=nw=1", file});
                kv["codec"] = kv.value("codec_name");
                kv["frames"] = kv.value("nb_read_frames");
                kv.remove("codec_name");
                QMap<QString, QString> v = kv;
                run({"-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name", "-of", "default=nw=1", file});
                v["audio_codec"] = kv.value("codec_name", "none") == v.value("codec") ? "none" : kv.value("codec_name", "none");
                kv = v;
            }
            const QString key = t.value(2), op = t.value(3), want = t.mid(4).join(' ');
            const QString got = kv.value(key);
            bool ok;
            bool na = false, nb = false;
            const double a = got.toDouble(&na), b = want.toDouble(&nb);
            if (op == "~" && na && nb) ok = std::abs(a - b) <= std::abs(b) * 0.03 + 1e-9;
            else if (op == "==") ok = got == want;
            else if (op == "!=") ok = got != want;
            else if (op == ">=" && na && nb) ok = a >= b;
            else ok = false;
            if (!ok) ++failures_;
            log(QString("%1  probe %2 %3 %4 %5 (got %6)").arg(ok ? "PASS" : "FAIL", QFileInfo(file).fileName(), key, op, want, got));
        } else if (cmd == "update") {
            // update check | update download   (never runs the installer)
            w_->testUpdate(t.value(1));
            log(line);
        } else if (cmd == "settings") {
            // settings open <page> | select <id> | set <key> | add <key> | clear | reset |
            //          resetall | filter <text> | newwindow on|off | apply | close
            log(line + "  -> " + w_->testSettings(t.mid(1)));
        } else if (cmd == "grade") {
            // grade show|hide|set <control> <value>|reset|saveas <path>|save
            QStringList args = t.mid(1);
            if (args.value(0) == "saveas" || args.value(0) == "grab" || args.value(0) == "still") args = {args.value(0), rest.section(' ', 1)};
            const QString err = w_->testGrade(args);
            if (err.isEmpty()) {
                log(line);
            } else {
                ++failures_;
                log("FAIL  " + line + ": " + err);
            }
        } else if (cmd == "perf") {
            // perf reset: clear the speed probes
            w_->testPerfReset();
            log(line);
        } else if (cmd == "compare") {
            // compare start <rowB> | stop | mode <1 flip..4 difference> | wipe <0..1> | offset <frames>
            log(line + "  -> " + w_->testCompare(t.mid(1)));
        } else if (cmd == "action") {
            // action <id>: trigger a menu action by its hotkey id (also ones with no key)
            w_->testAction(t.value(1));
            log(line);
        } else if (cmd == "mpvset") {
            // mpvset <property> <value...>: set a libmpv property (diagnostics)
            w_->testMpvSet(t.value(1), line.section(' ', 2));
            log(line);
        } else if (cmd == "lut") {
            // lut clear | lut file <path> | lut preset <source>|<target>
            LutChoice c;
            if (t.value(1) == "file") { c.kind = LutChoice::File; c.file = line.section(' ', 2); }
            else if (t.value(1) == "preset") {
                c.kind = LutChoice::Preset;
                const QString spec = line.section(' ', 2);
                c.source = spec.section('|', 0, 0);
                c.target = spec.section('|', 1);
            }
            w_->testSetLut(c);
            log(line);
        } else if (cmd == "state") {
            const QVariantMap st = w_->testState();
            for (auto it = st.begin(); it != st.end(); ++it) log("      " + it.key() + " = " + it.value().toString());
        } else {
            ++failures_;
            log("FAIL  unknown line: " + line);
        }
        ++pc_;
    }
    log(QString("DONE  %1 failure(s)").arg(failures_));
    QApplication::exit(failures_);
}
