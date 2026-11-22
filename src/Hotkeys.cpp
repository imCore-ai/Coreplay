#include "Hotkeys.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
constexpr auto C = Qt::ControlModifier;
constexpr auto S = Qt::ShiftModifier;
constexpr auto A = Qt::AltModifier;
constexpr auto K = Qt::KeypadModifier;

QKeySequence k(QKeyCombination c) { return QKeySequence(c); }
QKeySequence k(Qt::Key key) { return QKeySequence(key); }
}  // namespace

namespace Hotkeys {

const std::vector<HotkeyDef>& defaults() {
    static const std::vector<HotkeyDef> table = {
        // File
        {"file.quickopen", "&Quick Open File...", {k(C | Qt::Key_Q)}},
        {"file.open", "&Open File...", {k(C | Qt::Key_O)}},
        {"file.openfolder", "Open &Folder...", {}},
        {"file.reopen", "&Reopen File", {k(C | Qt::Key_E)}},
        {"file.recycle", "Move File to Recycle Bin", {k(Qt::Key_Delete)}},
        {"file.export", "&Export...", {k(C | S | Qt::Key_E)}},
        {"file.save", "&Save", {k(C | Qt::Key_S)}},                 // bakes the grade into the file
        {"file.saveas", "Save &As...", {k(C | S | Qt::Key_S)}},
        {"file.loadsub", "&Load Subtitle...", {k(C | Qt::Key_L)}},  // MPC-HC: Load Subtitles
        {"sub.style", "Subtitle &Style...", {}},
        {"file.saveimage", "Save &Image...", {k(A | Qt::Key_I)}},
        {"file.saveimageauto", "Save Image (auto)", {k(Qt::Key_F5)}},
        {"file.close", "&Close", {k(C | Qt::Key_C)}},
        {"file.properties", "P&roperties", {k(S | Qt::Key_F10)}},
        {"file.openlocation", "Open File &Location", {k(C | S | Qt::Key_F10)}},
        {"file.exit", "E&xit", {k(A | Qt::Key_X)}},
        // Play
        {"play.playpause", "&Play/Pause", {k(Qt::Key_Space), k(Qt::Key_MediaTogglePlayPause), k(Qt::Key_MediaPlay)}},
        {"play.stop", "&Stop", {k(Qt::Key_Period), k(Qt::Key_MediaStop)}},
        {"play.framestep", "F&rame Step", {k(Qt::Key_Right)}},
        {"play.framestepback", "Frame Step &Back", {k(Qt::Key_Left)}},
        {"play.goto", "&Go To...", {k(C | Qt::Key_G)}},
        {"play.incrate", "&Increase Rate", {k(C | Qt::Key_Up)}},
        {"play.decrate", "&Decrease Rate", {k(C | Qt::Key_Down)}},
        {"play.resetrate", "R&eset Rate", {k(C | Qt::Key_R)}},
        {"play.incauddelay", "Audio Delay +10 ms", {k(K | Qt::Key_Plus)}},
        {"play.decauddelay", "Audio Delay -10 ms", {k(K | Qt::Key_Minus)}},
        {"play.seekfwdsmall", "Jump Forward (small)", {}},
        {"play.seekbacksmall", "Jump Backward (small)", {}},
        {"play.seekfwdmed", "Jump Forward 5 s", {k(S | Qt::Key_Right)}},
        {"play.seekbackmed", "Jump Backward 5 s", {k(S | Qt::Key_Left)}},
        {"play.seekfwdlarge", "Jump Forward (large)", {}},
        {"play.seekbacklarge", "Jump Backward (large)", {}},
        {"play.seekkeyfwd", "Jump Forward (keyframe)", {}},
        {"play.seekkeyback", "Jump Backward (keyframe)", {}},
        {"play.seekbegin", "Jump to Beginning", {k(Qt::Key_Home)}},
        {"play.abmarka", "Set A-B Mark A", {k(Qt::Key_BracketLeft)}},
        {"play.abmarkb", "Set A-B Mark B", {k(Qt::Key_BracketRight)}},
        {"play.abclear", "Clear A-B Loop", {}},
        {"nav.next", "Next in &Playlist", {k(C | Qt::Key_PageDown), k(Qt::Key_MediaNext)}},
        {"nav.prev", "Previous in Pla&ylist", {k(C | Qt::Key_PageUp), k(Qt::Key_MediaPrevious)}},
        {"nav.nextfile", "&Next Clip in Folder", {k(Qt::Key_PageDown)}},
        {"nav.prevfile", "P&revious Clip in Folder", {k(Qt::Key_PageUp)}},
        // A/B compare (RV style)
        {"compare.toggle", "&Compare A/B", {k(C | S | Qt::Key_A)}},
        {"compare.flipmode", "A/B &Flip", {k(Qt::Key_F6)}},
        {"compare.wipe", "&Wipe", {k(Qt::Key_F7)}},
        {"compare.sidebyside", "&Side by Side", {k(Qt::Key_F8)}},
        {"compare.difference", "&Difference", {k(Qt::Key_F9)}},
        {"compare.swap", "S&wap A and B", {k(Qt::Key_Backslash)}},
        {"compare.offsetinc", "B One Frame &Later", {k(A | S | Qt::Key_Right)}},
        {"compare.offsetdec", "B One Frame &Earlier", {k(A | S | Qt::Key_Left)}},
        {"compare.offsetreset", "Reset B &Offset", {}},
        // View
        {"view.caption", "Hide &Menu", {k(C | Qt::Key_0)}},
        {"view.seeker", "See&k Bar", {k(C | Qt::Key_1)}},
        {"view.controls", "&Controls", {k(C | Qt::Key_2)}},
        {"view.information", "&Information", {k(C | Qt::Key_3)}},
        {"view.statistics", "S&tatistics", {k(C | Qt::Key_4)}},
        {"view.infooverlay", "Info &Overlay", {k(Qt::Key_Tab)}},
        {"view.grading", "Color &Grading...", {k(A | Qt::Key_G)}},
        {"view.status", "St&atus", {k(C | Qt::Key_5)}},
        {"view.playlist", "&Playlist", {k(C | Qt::Key_7)}},
        {"view.minimal", "Mi&nimal", {k(Qt::Key_1)}},
        {"view.compact", "Com&pact", {k(Qt::Key_2)}},
        {"view.normal", "No&rmal", {k(Qt::Key_3)}},
        {"view.fullscreen", "&Full Screen", {k(A | Qt::Key_Return), k(A | Qt::Key_Enter), k(Qt::Key_F11)}},
        {"view.exitfullscreen", "E&xit Full Screen", {k(Qt::Key_Escape)}},
        {"view.zoom25", "Zoom 25%", {k(A | Qt::Key_QuoteLeft)}},
        {"view.zoom50", "Zoom 50%", {k(A | Qt::Key_1)}},
        {"view.zoom100", "Zoom 100%", {k(A | Qt::Key_2)}},
        {"view.zoom200", "Zoom 200%", {k(A | Qt::Key_3)}},
        {"view.zoomfit", "Auto Fit", {k(A | Qt::Key_4)}},
        {"view.zoomfitlarger", "Auto Fit (Larger Only)", {k(A | Qt::Key_5)}},
        {"view.ontop", "Always On &Top", {k(C | Qt::Key_A)}},
        {"player.newwindow", "Open Each File in a &New Window", {}},
        {"pns.reset", "Pan&&Scan Reset", {k(K | Qt::Key_5)}},
        {"pns.incsize", "Increase Size", {k(K | Qt::Key_9)}},
        {"pns.incwidth", "Increase Width", {k(K | Qt::Key_6)}},
        {"pns.incheight", "Increase Height", {k(K | Qt::Key_8)}},
        {"pns.decsize", "Decrease Size", {k(K | Qt::Key_1)}},
        {"pns.decwidth", "Decrease Width", {k(K | Qt::Key_4)}},
        {"pns.decheight", "Decrease Height", {k(K | Qt::Key_2)}},
        {"pns.center", "Center", {k(C | K | Qt::Key_5)}},
        {"pns.left", "Move Left", {k(C | K | Qt::Key_4)}},
        {"pns.right", "Move Right", {k(C | K | Qt::Key_6)}},
        {"pns.up", "Move Up", {k(C | K | Qt::Key_8)}},
        {"pns.down", "Move Down", {k(C | K | Qt::Key_2)}},
        {"pns.upleft", "Move Up/Left", {k(C | K | Qt::Key_7)}},
        {"pns.upright", "Move Up/Right", {k(C | K | Qt::Key_9)}},
        {"pns.downleft", "Move Down/Left", {k(C | K | Qt::Key_1)}},
        {"pns.downright", "Move Down/Right", {k(C | K | Qt::Key_3)}},
        {"pns.rotatezp", "Rotate Clockwise", {k(C | Qt::Key_Right), k(A | K | Qt::Key_1)}},
        {"pns.rotatezm", "Rotate Counter-clockwise", {k(C | Qt::Key_Left), k(A | K | Qt::Key_3)}},
        // Audio / subtitles
        {"vol.up", "Volume &Up", {k(Qt::Key_Up), k(Qt::Key_VolumeUp)}},
        {"vol.down", "Volume &Down", {k(Qt::Key_Down), k(Qt::Key_VolumeDown)}},
        {"vol.mute", "&Mute", {k(C | Qt::Key_M), k(Qt::Key_VolumeMute)}},
        {"stream.audionext", "Next Audio Track", {k(Qt::Key_A)}},
        {"stream.audioprev", "Previous Audio Track", {k(S | Qt::Key_A)}},
        {"stream.subnext", "Next Subtitle", {k(Qt::Key_S)}},
        {"stream.subprev", "Previous Subtitle", {k(S | Qt::Key_S)}},
        {"stream.subonoff", "Subtitles On/Off", {k(Qt::Key_W)}},
        {"sub.prev", "Go to Previous Subtitle", {k(Qt::Key_Y)}},
        {"sub.next", "Go to Next Subtitle", {k(Qt::Key_U)}},
        {"sub.delaydown", "Subtitle Delay -", {k(S | Qt::Key_F2)}},  // MPC-HC: F1 (Coreplay: the hotkey list)
        {"sub.delayup", "Subtitle Delay +", {k(Qt::Key_F2)}},
        {"sub.posdown", "Subtitle Position Down", {k(C | S | K | Qt::Key_Minus)}},
        {"sub.posup", "Subtitle Position Up", {k(C | S | K | Qt::Key_Plus)}},
        {"sub.fontdec", "Subtitle Size -", {k(C | K | Qt::Key_Minus)}},
        {"sub.fontinc", "Subtitle Size +", {k(C | K | Qt::Key_Plus)}},
        // Misc
        {"osd.time", "OSD: Show Current Time", {k(C | Qt::Key_I)}},
        {"osd.filename", "OSD: Show File Name", {k(Qt::Key_N)}},
        {"view.stats", "Renderer Statistics", {k(C | Qt::Key_J)}},
        {"boss", "Boss Key", {}},  // MPC-HC uses B; Coreplay gives B to the blue channel
        {"menu", "Show Menu", {k(Qt::Key_Menu)}},
        {"options", "&Settings...", {k(Qt::Key_O)}},
        {"help.hotkeys", "&Hotkey List", {k(Qt::Key_F1)}},
        // Viewer zoom and LUTs (not in MPC-HC)
        {"zoom.fit", "Zoom to &Fit", {}},
        {"zoom.1to1", "Zoom &1:1 (actual pixels)", {}},
        {"lut.load", "&Load a LUT...", {}},
        {"lut.clear", "&Clear LUT", {}},
        // Image-sequence extras (not in MPC-HC)
        {"seq.fps", "Sequence &Frame Rate...", {}},
        // Channel solo, as in BunnyLab: press again for full colour
        {"channel.rgb", "Channel: All (RGB)", {}},
        {"channel.r", "Channel: &Red", {k(Qt::Key_R)}},
        {"channel.g", "Channel: &Green", {k(Qt::Key_G)}},
        {"channel.b", "Channel: &Blue", {k(Qt::Key_B)}},
        {"channel.a", "Channel: &Alpha", {}},
        {"seq.exposureup", "Exposure +1/2 stop", {}},
        {"seq.exposuredown", "Exposure -1/2 stop", {}},
        {"seq.exposurereset", "Exposure Reset", {}},
    };
    return table;
}

QString configPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/hotkeys.json";
}

std::vector<HotkeyDef> load() {
    std::vector<HotkeyDef> out = defaults();
    const QString path = configPath();

    // Reference copy of every default key, rewritten when the defaults change.
    QJsonObject all;
    for (const auto& d : out) {
        QJsonArray arr;
        for (const auto& ks : d.keys) arr.append(ks.toString(QKeySequence::PortableText));
        all.insert(d.id, arr);
    }
    const QByteArray allBytes = QJsonDocument(all).toJson(QJsonDocument::Indented);
    QFile ref(QFileInfo(path).absolutePath() + "/hotkeys.defaults.json");
    if (!ref.open(QIODevice::ReadOnly) || ref.readAll() != allBytes) {
        ref.close();
        if (ref.open(QIODevice::WriteOnly)) ref.write(allBytes);
    }
    ref.close();

    QFile f(path);
    QJsonObject root;
    if (f.open(QIODevice::ReadOnly)) root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.value("_format").toInt() != 2) {
        // First run, or a file from v0.1 (a full copy of the old defaults, which
        // would pin the old keys): keep it aside and start an overrides-only file.
        if (f.exists()) {
            QFile::remove(path + ".old");
            QFile::rename(path, path + ".old");
        }
        root = QJsonObject{{"_format", 2},
                           {"_help", "Put only the keys you change here, e.g. \"play.playpause\": [\"Space\", \"K\"]. "
                                     "Every command and its default keys: hotkeys.defaults.json"}};
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return out;
    }
    for (auto& d : out) {
        if (!root.contains(d.id)) continue;
        d.keys.clear();
        for (const auto& v : root.value(d.id).toArray()) {
            QKeySequence ks = QKeySequence::fromString(v.toString(), QKeySequence::PortableText);
            if (!ks.isEmpty()) d.keys.append(ks);
        }
    }
    return out;
}

bool saveOverrides(const std::vector<HotkeyDef>& keys) {
    QJsonObject root{{"_format", 2},
                     {"_help", "Put only the keys you change here, e.g. \"play.playpause\": [\"Space\", \"K\"]. "
                               "Every command and its default keys: hotkeys.defaults.json"}};
    const auto& defs = defaults();
    for (size_t i = 0; i < keys.size() && i < defs.size(); ++i) {
        if (keys[i].keys == defs[i].keys) continue;
        QJsonArray arr;
        for (const auto& ks : keys[i].keys) arr.append(ks.toString(QKeySequence::PortableText));
        root.insert(keys[i].id, arr);
    }
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0;
}

}  // namespace Hotkeys
