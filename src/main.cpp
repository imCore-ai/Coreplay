#include <QApplication>
#include <clocale>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QSettings>
#include <QPalette>
#include <QStyleHints>
#include <QStyleFactory>
#include <QProxyStyle>
#ifdef Q_OS_WIN
#include <QEvent>
#include <QAbstractNativeEventFilter>
#include <QTimer>
#include <QWidget>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

// One dark title bar on every Windows window of Coreplay, whatever the Windows theme. Qt sets
// the title bar from Windows' light / dark mode itself, also later (on activation, on a theme or
// setting change), so a dark bar set once when the window first showed went light again on a PC
// in light mode. Coreplay sets it again after each of those messages, after Qt has handled them.
// Attribute 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 2004 and later); 19 = the same on
// Windows 10 1809-1909. Windows 11 also: no accent backdrop under the bars, and the caption
// colour of the player's bars.
static void darkTitleBar(HWND h) {
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &dark, sizeof dark))) DwmSetWindowAttribute(h, 19, &dark, sizeof dark);
    const int none = 1;  // DWMSBT_NONE
    DwmSetWindowAttribute(h, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &none, sizeof none);
    const COLORREF caption = RGB(31, 31, 31);
    DwmSetWindowAttribute(h, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
}

static bool framedWindow(QWidget* w) {
    return w && w->isWindow() && !w->windowFlags().testFlag(Qt::FramelessWindowHint) &&
           !(w->windowType() == Qt::Popup || w->windowType() == Qt::ToolTip);
}

class WindowFrameFix : public QObject, public QAbstractNativeEventFilter {
public:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show && o->isWidgetType() && framedWindow(static_cast<QWidget*>(o)))
            darkTitleBar(reinterpret_cast<HWND>(static_cast<QWidget*>(o)->winId()));
        return false;
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const MSG* m = static_cast<const MSG*>(message);
        switch (m->message) {
            case WM_ACTIVATE: case WM_NCACTIVATE: case WM_SETTINGCHANGE: case WM_THEMECHANGED:
            case WM_DWMCOLORIZATIONCOLORCHANGED: case WM_DPICHANGED: {
                const HWND h = m->hwnd;
                // After Qt has handled the message (it may set the light title bar in it).
                QTimer::singleShot(0, qApp, [h] {
                    if (framedWindow(QWidget::find(reinterpret_cast<WId>(h)))) darkTitleBar(h);
                });
                break;
            }
        }
        return false;
    }
};
#endif
#include <QThread>
#ifndef Q_OS_WIN
#include <signal.h>
#endif
#include <QSurfaceFormat>
#include <QTimer>
#include <memory>

#include "I18n.h"
#include "MainWindow.h"
#ifdef COREPLAY_TESTS
#include "TestDriver.h"
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// Milliseconds since the OS created this process (DLL loading included).
qint64 msSinceProcessStart() {
#ifdef Q_OS_WIN
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimeAsFileTime(&now);
    auto t = [](FILETIME f) { return (qint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    return (t(now) - t(created)) / 10000;
#else
    return -1;
#endif
}

QString serverName() { return "Coreplay-" + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")); }

#ifdef Q_OS_WIN
// Makes each top-level window of a test run invisible and click-through when it appears:
// layered with alpha 0 (Windows still paints it) plus WS_EX_TRANSPARENT (mouse passes on).
class InvisibleTestWindows : public QObject {
public:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Show) {
            if (auto* w = qobject_cast<QWidget*>(o); w && w->isWindow()) {
                auto h = reinterpret_cast<HWND>(w->winId());
                const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
                SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
                SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
            }
        }
        return false;
    }
};
#endif

QStringList absolutePaths(const QStringList& args) {
    // Switches that take a value: the value is not a file to open.
    static const QStringList withValue = {"--test-script", "--test-log", "--test-slow-start", "--listen"};
    QStringList out;
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (withValue.contains(a)) { ++i; continue; }
        if (a.startsWith("--")) continue;  // --new, --add, --test-instance
        out << (QFileInfo::exists(a) ? QFileInfo(a).absoluteFilePath() : a);
    }
    return out;
}

// Sends hand-over messages, one at a time and in order, to a second window that the
// running one started (View > Open Each File in a New Window). That window takes a
// moment to start, so a message waits until its server answers (up to ~30 s).
class Forwarder : public QObject {
public:
    Forwarder(QString name, QObject* parent) : QObject(parent), name_(std::move(name)) {}
    void send(const QByteArray& msg) {
        queue_ << msg;
        if (queue_.size() == 1) next();
    }

private:
    void next() {
        if (queue_.isEmpty()) return;
        if (++tries_ > 300) { queue_.clear(); return; }  // that window never came up
        auto* s = new QLocalSocket(this);
        connect(s, &QLocalSocket::connected, s, [s, this] { s->write(queue_.first()); });
        connect(s, &QLocalSocket::readyRead, s, [s, this] {
            if (!s->readAll().startsWith("ok")) return;
            s->disconnect();  // no error callback for the close that follows
            s->disconnectFromServer();
            s->deleteLater();
            queue_.removeFirst();
            tries_ = 0;
            next();
        });
        connect(s, &QLocalSocket::errorOccurred, s, [s, this] {
            s->disconnect();
            s->deleteLater();
            QTimer::singleShot(100, this, [this] { next(); });
        });
        s->connectToServer(name_);
    }
    QString name_;
    QList<QByteArray> queue_;
    int tries_ = 0;
};
}  // namespace

#if defined(COREPLAY_TESTS) && defined(Q_OS_WIN)
#include <dbghelp.h>
#include <objbase.h>
#include <cstdio>
// Test builds: a crash writes its stack (with symbols) to coreplay_crash.txt in the
// working folder, so an end-to-end failure can be diagnosed without a debugger.
static LONG WINAPI writeCrashStack(EXCEPTION_POINTERS* ep) {
    FILE* f = nullptr;
    if (fopen_s(&f, "coreplay_crash.txt", "w") != 0 || !f) return EXCEPTION_CONTINUE_SEARCH;
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(proc, nullptr, TRUE);
    std::fprintf(f, "exception 0x%08lx at %p\n", ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr,
                                          SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i) {
        char buf[sizeof(SYMBOL_INFO) + 512] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD ld = 0;
        char mod[MAX_PATH] = "?";
        GetModuleFileNameA(HMODULE(SymGetModuleBase64(proc, sf.AddrPC.Offset)), mod, MAX_PATH);
        const bool hasSym = SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym);
        const bool hasLine = SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ld, &line);
        const DWORD64 base = SymGetModuleBase64(proc, sf.AddrPC.Offset);
        std::fprintf(f, "%2d %s+0x%llx  %s+0x%llx %s:%lu\n", i,
                     std::strrchr(mod, '\\') ? std::strrchr(mod, '\\') + 1 : mod,
                     (unsigned long long)(sf.AddrPC.Offset - base), hasSym ? sym->Name : "?",
                     (unsigned long long)disp, hasLine ? line.FileName : "", hasLine ? line.LineNumber : 0ul);
    }
    std::fclose(f);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char* argv[]) {
#if defined(COREPLAY_TESTS) && defined(Q_OS_WIN)
    SetUnhandledExceptionFilter(writeCrashStack);
#endif
    const qint64 mainEnterMs = msSinceProcessStart();  // DLL loading and process set-up
    // One GL context type for both views. 4.1 core is the newest macOS has.
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Software OpenGL when an earlier start found no usable GPU driver (or the user
    // chose it in Settings). Must be set before the application object exists.
    bool testRun = false;  // test runs keep their own settings (CoreplayTest)
    for (int i = 1; i < argc; ++i) testRun |= QByteArray(argv[i]) == "--test-script";
    if (QSettings(testRun ? "CoreplayTest" : "Coreplay", "Coreplay").value("gl/software", false).toBool())
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QApplication app(argc, argv);
    qApp->setProperty("processStartEpochMs", QDateTime::currentMSecsSinceEpoch() - msSinceProcessStart());
    QApplication::setOrganizationName("Coreplay");
    QApplication::setApplicationName("Coreplay");
    QApplication::setApplicationVersion(COREPLAY_VERSION);
    QApplication::setWindowIcon(QIcon(":/coreplay.png"));
    // The window frame too: dark title bars on macOS (Qt switches the app to the dark
    // appearance) and on Windows in light mode (Qt would set the title bar back to light).
    // Before the palette: a scheme change rebuilds the palette, and ours must win.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    // One fixed dark look on every system, whatever its light / dark setting or desktop theme:
    // Fusion with Coreplay's own palette. (Windows used to follow its light mode: the bars and
    // menus turned light grey there.) Accents stay grey: no accent colour anywhere.
    // Fusion underlines the menu letters (File, View) all the time; the player shows them clean.
    struct CleanMenus : QProxyStyle {
        using QProxyStyle::QProxyStyle;
        int styleHint(StyleHint h, const QStyleOption* o, const QWidget* w, QStyleHintReturn* r) const override {
            return h == SH_UnderlineShortcut ? 0 : QProxyStyle::styleHint(h, o, w, r);
        }
    };
    QApplication::setStyle(new CleanMenus(QStyleFactory::create("Fusion")));
    {
        QPalette p;
        const QColor window(31, 31, 31), base(25, 25, 25), alt(37, 37, 37), button(43, 43, 43);
        const QColor text(224, 224, 224), dim(122, 122, 122);
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, alt);
        p.setColor(QPalette::ToolTipBase, button);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::PlaceholderText, dim);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Light, QColor(70, 70, 70));
        p.setColor(QPalette::Midlight, QColor(55, 55, 55));
        p.setColor(QPalette::Mid, QColor(40, 40, 40));
        p.setColor(QPalette::Dark, QColor(20, 20, 20));
        p.setColor(QPalette::Shadow, Qt::black);
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            p.setColor(group, QPalette::Highlight, QColor(110, 110, 110));
            p.setColor(group, QPalette::HighlightedText, QColor(0, 0, 0));
            p.setColor(group, QPalette::Accent, QColor(130, 130, 130));
            p.setColor(group, QPalette::Link, QColor(170, 170, 170));
            p.setColor(group, QPalette::LinkVisited, QColor(140, 140, 140));
        }
        for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, role, dim);
        QApplication::setPalette(p);
    }
    // mpv needs the C numeric locale (it parses "0.5" in its options).
    std::setlocale(LC_NUMERIC, "C");

#ifdef Q_OS_WIN
    // Every window (player, dialogs) gets a plain dark title bar: no Windows 11 backdrop,
    // which tints the title bar and the menu bar with the accent colour.
    static WindowFrameFix frameFix;
    app.installEventFilter(&frameFix);
    app.installNativeEventFilter(&frameFix);
#endif
    const QStringList files = absolutePaths(app.arguments().mid(1));
    const QStringList args = app.arguments();
    const int scriptAt = args.indexOf("--test-script");
    const bool testing = scriptAt > 0 && scriptAt + 1 < args.size();
    if (testing) QApplication::setOrganizationName("CoreplayTest");  // keep the user's settings untouched
    I18n::install(app, testing);  // the interface language (Settings > General > Language)
    // --test-instance: a test run that still acts as the single instance (it receives
    // hand-overs), so the hand-over can be tested end to end.
    const bool testInstance = args.contains("--test-instance");
    const bool newWindow = (testing && !testInstance) || args.contains("--new");
    const bool add = args.contains("--add");  // Explorer's "Add to Coreplay playlist"
    // --listen <name>: a second window started by the running one; it takes the rest of
    // its file selection from that window under this name.
    const int listenAt = args.indexOf("--listen");
    const QString listenName = listenAt > 0 && listenAt + 1 < args.size() ? args[listenAt + 1] : QString();
    qApp->setProperty("listenName", listenName);

    // One instance: a later launch hands its files to the running window (MPC-HC default).
    // The first launch holds a lock file; a later one that finds the lock waits for that
    // window to listen (it may still be starting - Explorer starts one process per file
    // when several are opened at once) instead of opening a second window.
    std::unique_ptr<QLockFile> primary;
    if (!newWindow) {
        primary = std::make_unique<QLockFile>(QDir::tempPath() + "/" + serverName() + ".lock");
        primary->setStaleLockTime(0);  // stale only when its process is gone, never by age
        if (!primary->tryLock(0)) {
            // The header carries this launch's start time: launches that started together
            // (one multi-file selection) are grouped however late the window reads them.
            const qint64 launched = QDateTime::currentMSecsSinceEpoch() - msSinceProcessStart();
            const QByteArray msg = (QString(add ? "#add %1\n" : "#open %1\n").arg(launched) + files.join('\n') +
                                    "\n\x04").toUtf8();
            // Wait while the lock's owner is alive (a cold first start, with antivirus
            // scanning new files, can take seconds); take over only if it has died.
            qint64 ownerPid = 0;
            QString host, app;
            primary->getLockInfo(&ownerPid, &host, &app);
            auto ownerAlive = [ownerPid] {
#ifdef Q_OS_WIN
                HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(ownerPid));
                if (!h) return false;
                const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
                CloseHandle(h);
                return alive;
#else
                return ownerPid > 0 && ::kill(pid_t(ownerPid), 0) == 0;
#endif
            };
            for (int i = 0; i < 400 && (i < 20 || ownerAlive()); ++i) {  // up to 20 s while it lives
                QLocalSocket probe;
                probe.connectToServer(serverName());
                if (probe.waitForConnected(100)) {
#ifdef Q_OS_WIN
                    AllowSetForegroundWindow(ASFW_ANY);  // let the running window come to the front
#endif
                    probe.write(msg);
                    probe.waitForBytesWritten(1000);
                    // Delivered only when the window says so (it may be busy for seconds).
                    if (probe.waitForReadyRead(20000) && probe.readAll().startsWith("ok")) {
                        probe.disconnectFromServer();
                        return 0;
                    }
                    probe.abort();
                    continue;  // not confirmed: try again
                }
                QThread::msleep(50);
            }
            // The lock's owner never answered: it is gone. Take over.
            primary->removeStaleLockFile();
            primary->tryLock(0);
        }
    }

#ifdef COREPLAY_TESTS
    // --test-slow-start <ms>: a slow cold start (antivirus scanning a fresh install), after
    // the lock is taken, so the hand-over wait can be tested.
    if (const int i = args.indexOf("--test-slow-start"); i > 0 && i + 1 < args.size())
        QThread::msleep(args[i + 1].toULong());
#endif
    // On the heap so the test path can destroy it before QApplication.
    std::unique_ptr<MainWindow> window = std::make_unique<MainWindow>();
    MainWindow& w = *window;
    if (testing) w.setAttribute(Qt::WA_ShowWithoutActivating);  // never take the user's focus
#ifdef Q_OS_WIN
    // Test windows are invisible and let clicks through, so a test run never covers the
    // user's work: every window (player, dialogs, full screen) stays where it is and is
    // painted as usual (so the checks and grabs are real), but fully transparent.
    // COREPLAY_TEST_VISIBLE=1 shows them.
    static InvisibleTestWindows invisible;
    if (testing && !qEnvironmentVariableIsSet("COREPLAY_TEST_VISIBLE")) app.installEventFilter(&invisible);
#endif
    w.show();
    if (!listenName.isEmpty()) w.move(w.pos() + QPoint(32, 32));  // not exactly over the first window
    qApp->setProperty("mainEnterMs", mainEnterMs);
    qApp->setProperty("windowShownMs", msSinceProcessStart());
    QTimer::singleShot(0, &w, [] { qApp->setProperty("startupMs", msSinceProcessStart()); });

    QLocalServer server;
    // Opens whose launches STARTED within 1.5 s of the previous open are one multi-file
    // selection (Explorer starts one process per file): they are added, not replacing
    // each other. Launch times, not arrival times: a busy window may read them late.
    auto lastOpen = std::make_shared<qint64>(
        files.isEmpty() ? 0 : QDateTime::currentMSecsSinceEpoch() - msSinceProcessStart());
    const bool owner = !newWindow;  // the window Explorer's launches hand their files to
    struct Group { qint64 launched = 0; Forwarder* to = nullptr; };
    auto group = std::make_shared<Group>();  // the selection that went to a second window
    if (owner || !listenName.isEmpty()) {
        const QString name = owner ? serverName() : listenName;
        QLocalServer::removeServer(name);
        server.listen(name);
        // Take EVERY waiting connection: while the window is busy (loading libmpv on the
        // first open) several launches pile up, and Qt signals once for the pile.
        QObject::connect(&server, &QLocalServer::newConnection, &w, [&server, &w, lastOpen, testing, owner, group] {
            while (QLocalSocket* s = server.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                auto process = [s, buf, &w, lastOpen, testing, owner, group] {
                    buf->append(s->readAll());
                    if (!buf->endsWith('\x04')) return;
                    QStringList lines = QString::fromUtf8(buf->chopped(1)).split('\n', Qt::SkipEmptyParts);
                    buf->clear();
                    const QString header = lines.isEmpty() ? QString() : lines.first();
                    const bool addMsg = header.startsWith("#add");
                    const qint64 launched = header.section(' ', 1, 1).toLongLong();
                    if (header.startsWith('#')) lines.removeFirst();
                    const bool append = addMsg || (*lastOpen && launched && std::llabs(launched - *lastOpen) < 1500);
                    if (launched) *lastOpen = launched;  // an add marks the selection too: it may arrive first
                    s->write("ok\n");  // the launch waits for this before it exits
                    s->flush();
                    auto note = [](const QString& what, int n) {
                        qApp->setProperty("handovers", qApp->property("handovers").toString() + QString("%1:%2 ").arg(what).arg(n));
                    };
                    if (owner && !lines.isEmpty() && !addMsg) {
                        // A new selection while a file plays, with the setting on: a second window.
                        if (!append && MainWindow::newWindowPerFile() && w.hasMedia()) {
                            const QString name = serverName() + "-" + QString::number(launched ? launched : QDateTime::currentMSecsSinceEpoch());
                            QStringList cargs{"--new", "--listen", name};
                            if (testing && qEnvironmentVariableIsSet("COREPLAY_TEST_CHILD_SCRIPT"))
                                cargs << "--test-script" << qEnvironmentVariable("COREPLAY_TEST_CHILD_SCRIPT") << "--test-log"
                                      << qEnvironmentVariable("COREPLAY_TEST_CHILD_LOG");
#ifdef Q_OS_WIN
                            AllowSetForegroundWindow(ASFW_ANY);  // the new window comes to the front
#endif
                            QProcess::startDetached(QCoreApplication::applicationFilePath(), cargs + lines);
                            group->launched = launched;
                            group->to = new Forwarder(name, &w);
                            note("newwindow", lines.size());
                            return;
                        }
                        // The rest of that selection follows it to the second window.
                        if (append && group->to && launched && std::llabs(launched - group->launched) < 1500) {
                            group->to->send(("#add 0\n" + lines.join('\n') + "\n\x04").toUtf8());
                            note("forward", lines.size());
                            return;
                        }
                    }
                    if (!lines.isEmpty()) w.openPaths(lines, append);
                    // Hand-overs received, for the tests: "open"/"add"/"append" + file count.
                    qApp->setProperty("handovers", qApp->property("handovers").toString() +
                                                       QString("%1:%2 ").arg(addMsg ? "add" : append ? "append" : "open").arg(lines.size()));
                    if (testing) return;  // test runs never take the user's focus
                    w.setWindowState((w.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
                    w.raise();
                    w.activateWindow();
                };
                QObject::connect(s, &QLocalSocket::readyRead, s, process);
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
                if (s->bytesAvailable() > 0) process();  // data that came before the connect
            }
        });
    }

#ifdef COREPLAY_TESTS
    if (testing) {
        const int logAt = args.indexOf("--test-log");
        // A second window from "Open each file in a new window" opens its files as a real one does.
        if (!listenName.isEmpty() && !files.isEmpty()) w.openPaths(files);
        auto* t = new TestDriver(&w, args[scriptAt + 1], logAt > 0 ? args[logAt + 1] : "coreplay_test.log");
        t->start();
        const int rc = app.exec();
        window.reset();  // the engines (libmpv included) shut down here
#ifdef Q_OS_WIN
        {   // Guard: the GUI thread must still be in COM after the window (and libmpv)
            // shut down; if a library removed it, Qt crashes releasing its COM objects.
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (hr == S_FALSE) {
                CoUninitialize();  // still initialized: undo this probe's extra count
            } else {
                // Lost (S_OK): keep this fresh apartment so Qt can exit, and fail the run.
                const int logAt = args.indexOf("--test-log");
                if (logAt > 0) {
                    QFile f(args[logAt + 1]);
                    if (f.open(QIODevice::Append | QIODevice::Text))
                        f.write("FAIL  the GUI thread lost its COM apartment during shutdown\n");
                }
                return rc + 1;
            }
        }
#endif
        return rc;
    }
#endif
    if (!files.isEmpty()) w.openPaths(files);
    return app.exec();
}
