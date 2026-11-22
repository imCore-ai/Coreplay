#pragma once
// End-to-end test driver (test builds only: -DCOREPLAY_TESTS=ON).
//   coreplay.exe --test-script script.txt --test-log result.txt
// Runs the script against the real window: hotkeys go through Qt's own shortcut
// handling (QTest), exactly like a key press. Exit code = number of failures.
//
// Script lines:
//   open <path> [<path>...]       open (replaces the playlist)
//   append <path>                 append to the playlist
//   key <keys>                    e.g. Space, Ctrl+Right, Alt+Return, Num+5, PgDown
//   wait <ms>
//   until <field> <op> <value> <timeout-ms>
//   expect <field> <op> <value>   ops: == != >= <= > < ~ (within 3 %)
//   grab <png>                    save the view as the user sees it
//   grabwindow <png>              save the whole window (menus, playlist, status bar)
//   state                         write every field to the log
//   wheel <x> <y> <delta> [ctrl]  mouse wheel over the picture (x, y = 0..1 of the picture)
//   mdrag <x0> <y0> <x1> <y1>     middle-button drag
//   dblclick <x> <y> / mousemove <x> <y>
//   clickbutton <text>            click a visible on-screen button ("1:1", "Fit")
//   pixel <x> <y> <name>          remember the colour shown there
//   expectpixel <name> ==|!= <name2 or r,g,b> <tolerance>
//   lut clear | lut file <path> | lut preset <source>|<target>
//   export <preset> <output> [colour] [subs] [range=a:b]   then: until export.state != running <ms>
//   probe <file> <key> <op> <value>   ffprobe the output (codec, frames, r_frame_rate, ...)
//   loadsub <path> / substyle <key> <value>
//   # comment
#include <QObject>
#include <QStringList>
#include <QColor>
#include <QHash>
#include <QVariantMap>

class MainWindow;

class TestDriver : public QObject {
    Q_OBJECT
public:
    TestDriver(MainWindow* w, const QString& script, const QString& log);
    void start();

private:
    void step();
    bool check(const QString& field, const QString& op, const QString& value, QString* got);
    void log(const QString& line);

    MainWindow* w_;
    QStringList lines_;
    int pc_ = 0;
    int failures_ = 0;
    QString logPath_;
    qint64 untilDeadline_ = 0;
    QHash<QString, QColor> pixels_;
};
