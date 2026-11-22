#pragma once
// Default key table copied from MPC-HC (clsid2/mpc-hc, src/mpc-hc/AppSettings.cpp,
// default_wmcmds[]). Users override any key in hotkeys.json in the config folder.
#include <QKeySequence>
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
// Writes hotkeys.json with only the commands whose keys differ from the defaults.
bool saveOverrides(const std::vector<HotkeyDef>& keys);
}  // namespace Hotkeys
