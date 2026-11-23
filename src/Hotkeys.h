#pragma once
// Default key table copied from MPC-HC (clsid2/mpc-hc, src/mpc-hc/AppSettings.cpp,
// default_wmcmds[]). Users override any key in hotkeys.json in the config folder.
#include <QKeySequence>
#include <Qt>
#include <QList>
#include <QString>
#include <vector>

struct HotkeyDef {
    const char* id;          // stable command id, used in hotkeys.json
    const char* label;       // menu text
    QList<QKeySequence> keys;
};

namespace Hotkeys {
const std::vector<HotkeyDef>& defaults();
// Defaults merged with the user's hotkeys.json. Writes the file with the
// defaults on first run so the user can edit it.
std::vector<HotkeyDef> load();
QString configPath();
// Mouse keys: the wheel and the extra buttons, bound like keyboard keys (with Ctrl, Shift,
// Alt, Meta). They live in the same key lists, as key codes no keyboard sends, and are
// written as text ("Ctrl+Wheel Up", "Middle Click") in hotkeys.json and on screen.
enum MouseKey { WheelUp = 0x01FF0001, WheelDown, MiddleClick, MouseBack, MouseForward };
QKeySequence mouseKey(int key, Qt::KeyboardModifiers mods);
bool isMouse(const QKeySequence& ks);
// Keyboard keys as Qt writes them (native: for the screen; else portable: for the file);
// mouse keys by their names.
QString text(const QKeySequence& ks, bool native);
QKeySequence fromText(const QString& s);
// Writes hotkeys.json with only the commands whose keys differ from the defaults.
bool saveOverrides(const std::vector<HotkeyDef>& keys);
}  // namespace Hotkeys
