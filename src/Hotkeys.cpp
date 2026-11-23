#include "Hotkeys.h"

#include <QCoreApplication>
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
QKeySequence m(int key, Qt::KeyboardModifiers mods = Qt::NoModifier) { return Hotkeys::mouseKey(key, mods); }
}  // namespace

namespace Hotkeys {

const std::vector<HotkeyDef>& defaults() {
    static const std::vector<HotkeyDef> table = {
        // File
        {"file.quickopen", QT_TRANSLATE_NOOP("Hotkeys", "&Quick Open File..."), {k(C | Qt::Key_Q)}},
        {"file.open", QT_TRANSLATE_NOOP("Hotkeys", "&Open File..."), {k(C | Qt::Key_O)}},
        {"file.openfolder", QT_TRANSLATE_NOOP("Hotkeys", "Open &Folder..."), {}},
        {"file.reopen", QT_TRANSLATE_NOOP("Hotkeys", "&Reopen File"), {k(C | Qt::Key_E)}},
        {"file.recycle", QT_TRANSLATE_NOOP("Hotkeys", "Move File to Recycle Bin"), {k(Qt::Key_Delete)}},
        {"file.export", QT_TRANSLATE_NOOP("Hotkeys", "&Export..."), {k(C | S | Qt::Key_E)}},
        {"file.save", QT_TRANSLATE_NOOP("Hotkeys", "&Save"), {k(C | Qt::Key_S)}},                 // bakes the grade into the file
        {"file.saveas", QT_TRANSLATE_NOOP("Hotkeys", "Save &As..."), {k(C | S | Qt::Key_S)}},
        {"file.loadsub", QT_TRANSLATE_NOOP("Hotkeys", "&Load Subtitle..."), {k(C | Qt::Key_L)}},  // MPC-HC: Load Subtitles
        {"sub.style", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle &Style..."), {}},
        {"file.saveimage", QT_TRANSLATE_NOOP("Hotkeys", "Save &Image..."), {k(A | Qt::Key_I)}},
        {"file.saveimageauto", QT_TRANSLATE_NOOP("Hotkeys", "Save Image (auto)"), {k(Qt::Key_F5)}},
        {"file.close", QT_TRANSLATE_NOOP("Hotkeys", "&Close"), {k(C | Qt::Key_C)}},
        {"file.properties", QT_TRANSLATE_NOOP("Hotkeys", "P&roperties"), {k(S | Qt::Key_F10)}},
        {"file.openlocation", QT_TRANSLATE_NOOP("Hotkeys", "Open File &Location"), {k(C | S | Qt::Key_F10)}},
        {"file.exit", QT_TRANSLATE_NOOP("Hotkeys", "E&xit"), {k(A | Qt::Key_X)}},
        // Play
        {"play.playpause", QT_TRANSLATE_NOOP("Hotkeys", "&Play/Pause"), {k(Qt::Key_Space), k(Qt::Key_MediaTogglePlayPause), k(Qt::Key_MediaPlay)}},
        {"play.stop", QT_TRANSLATE_NOOP("Hotkeys", "&Stop"), {k(Qt::Key_Period), k(Qt::Key_MediaStop)}},
        {"play.framestep", QT_TRANSLATE_NOOP("Hotkeys", "F&rame Step"), {k(Qt::Key_Right)}},
        {"play.framestepback", QT_TRANSLATE_NOOP("Hotkeys", "Frame Step &Back"), {k(Qt::Key_Left)}},
        {"play.goto", QT_TRANSLATE_NOOP("Hotkeys", "&Go To..."), {k(C | Qt::Key_G)}},
        {"play.incrate", QT_TRANSLATE_NOOP("Hotkeys", "&Increase Rate"), {k(C | Qt::Key_Up)}},
        {"play.decrate", QT_TRANSLATE_NOOP("Hotkeys", "&Decrease Rate"), {k(C | Qt::Key_Down)}},
        {"play.resetrate", QT_TRANSLATE_NOOP("Hotkeys", "R&eset Rate"), {k(C | Qt::Key_R)}},
        {"play.incauddelay", QT_TRANSLATE_NOOP("Hotkeys", "Audio Delay +10 ms"), {k(K | Qt::Key_Plus)}},
        {"play.decauddelay", QT_TRANSLATE_NOOP("Hotkeys", "Audio Delay -10 ms"), {k(K | Qt::Key_Minus)}},
        {"play.seekfwdsmall", QT_TRANSLATE_NOOP("Hotkeys", "Jump Forward (small)"), {}},
        {"play.seekbacksmall", QT_TRANSLATE_NOOP("Hotkeys", "Jump Backward (small)"), {}},
        {"play.seekfwdmed", QT_TRANSLATE_NOOP("Hotkeys", "Jump Forward 5 s"), {k(S | Qt::Key_Right)}},
        {"play.seekbackmed", QT_TRANSLATE_NOOP("Hotkeys", "Jump Backward 5 s"), {k(S | Qt::Key_Left)}},
        {"play.seekfwdlarge", QT_TRANSLATE_NOOP("Hotkeys", "Jump Forward (large)"), {m(Hotkeys::WheelUp, C)}},
        {"play.seekbacklarge", QT_TRANSLATE_NOOP("Hotkeys", "Jump Backward (large)"), {m(Hotkeys::WheelDown, C)}},
        {"play.seekkeyfwd", QT_TRANSLATE_NOOP("Hotkeys", "Jump Forward (keyframe)"), {}},
        {"play.seekkeyback", QT_TRANSLATE_NOOP("Hotkeys", "Jump Backward (keyframe)"), {}},
        {"play.seekbegin", QT_TRANSLATE_NOOP("Hotkeys", "Jump to Beginning"), {k(Qt::Key_Home)}},
        {"play.abmarka", QT_TRANSLATE_NOOP("Hotkeys", "Set A-B Mark A"), {k(Qt::Key_BracketLeft)}},
        {"play.abmarkb", QT_TRANSLATE_NOOP("Hotkeys", "Set A-B Mark B"), {k(Qt::Key_BracketRight)}},
        {"play.abclear", QT_TRANSLATE_NOOP("Hotkeys", "Clear A-B Loop"), {}},
        {"nav.next", QT_TRANSLATE_NOOP("Hotkeys", "Next in &Playlist"), {k(C | Qt::Key_PageDown), k(Qt::Key_MediaNext), m(Hotkeys::MouseForward)}},
        {"nav.prev", QT_TRANSLATE_NOOP("Hotkeys", "Previous in Pla&ylist"), {k(C | Qt::Key_PageUp), k(Qt::Key_MediaPrevious), m(Hotkeys::MouseBack)}},
        {"nav.nextfile", QT_TRANSLATE_NOOP("Hotkeys", "&Next Clip in Folder"), {k(Qt::Key_PageDown)}},
        {"nav.prevfile", QT_TRANSLATE_NOOP("Hotkeys", "P&revious Clip in Folder"), {k(Qt::Key_PageUp)}},
        // A/B compare (RV style)
        {"compare.toggle", QT_TRANSLATE_NOOP("Hotkeys", "&Compare A/B"), {k(C | S | Qt::Key_A)}},
        {"compare.flipmode", QT_TRANSLATE_NOOP("Hotkeys", "A/B &Flip"), {k(Qt::Key_F6)}},
        {"compare.wipe", QT_TRANSLATE_NOOP("Hotkeys", "&Wipe"), {k(Qt::Key_F7)}},
        {"compare.sidebyside", QT_TRANSLATE_NOOP("Hotkeys", "&Side by Side"), {k(Qt::Key_F8)}},
        {"compare.difference", QT_TRANSLATE_NOOP("Hotkeys", "&Difference"), {k(Qt::Key_F9)}},
        {"compare.swap", QT_TRANSLATE_NOOP("Hotkeys", "S&wap A and B"), {k(Qt::Key_Backslash)}},
        {"compare.offsetinc", QT_TRANSLATE_NOOP("Hotkeys", "B One Frame &Later"), {k(A | S | Qt::Key_Right)}},
        {"compare.offsetdec", QT_TRANSLATE_NOOP("Hotkeys", "B One Frame &Earlier"), {k(A | S | Qt::Key_Left)}},
        {"compare.offsetreset", QT_TRANSLATE_NOOP("Hotkeys", "Reset B &Offset"), {}},
        // View
        {"view.caption", QT_TRANSLATE_NOOP("Hotkeys", "Hide &Menu"), {k(C | Qt::Key_0)}},
        {"view.seeker", QT_TRANSLATE_NOOP("Hotkeys", "See&k Bar"), {k(C | Qt::Key_1)}},
        {"view.controls", QT_TRANSLATE_NOOP("Hotkeys", "&Controls"), {k(C | Qt::Key_2)}},
        {"view.information", QT_TRANSLATE_NOOP("Hotkeys", "&Information"), {k(C | Qt::Key_3)}},
        {"view.statistics", QT_TRANSLATE_NOOP("Hotkeys", "S&tatistics"), {k(C | Qt::Key_4)}},
        {"view.infooverlay", QT_TRANSLATE_NOOP("Hotkeys", "Info &Overlay"), {k(Qt::Key_Tab)}},
        {"view.grading", QT_TRANSLATE_NOOP("Hotkeys", "Color &Grading..."), {k(A | Qt::Key_G)}},
        {"view.status", QT_TRANSLATE_NOOP("Hotkeys", "St&atus"), {k(C | Qt::Key_5)}},
        {"view.playlist", QT_TRANSLATE_NOOP("Hotkeys", "&Playlist"), {k(C | Qt::Key_7)}},
        {"view.minimal", QT_TRANSLATE_NOOP("Hotkeys", "Mi&nimal"), {k(Qt::Key_1)}},
        {"view.compact", QT_TRANSLATE_NOOP("Hotkeys", "Com&pact"), {k(Qt::Key_2)}},
        {"view.normal", QT_TRANSLATE_NOOP("Hotkeys", "No&rmal"), {k(Qt::Key_3)}},
        {"view.fullscreen", QT_TRANSLATE_NOOP("Hotkeys", "&Full Screen"), {k(A | Qt::Key_Return), k(A | Qt::Key_Enter), k(Qt::Key_F11)}},
        {"view.exitfullscreen", QT_TRANSLATE_NOOP("Hotkeys", "E&xit Full Screen"), {k(Qt::Key_Escape)}},
        {"view.zoom25", QT_TRANSLATE_NOOP("Hotkeys", "Zoom 25%"), {k(A | Qt::Key_QuoteLeft)}},
        {"view.zoom50", QT_TRANSLATE_NOOP("Hotkeys", "Zoom 50%"), {k(A | Qt::Key_1)}},
        {"view.zoom100", QT_TRANSLATE_NOOP("Hotkeys", "Zoom 100%"), {k(A | Qt::Key_2)}},
        {"view.zoom200", QT_TRANSLATE_NOOP("Hotkeys", "Zoom 200%"), {k(A | Qt::Key_3)}},
        {"view.zoomfit", QT_TRANSLATE_NOOP("Hotkeys", "Auto Fit"), {k(A | Qt::Key_4)}},
        {"view.zoomfitlarger", QT_TRANSLATE_NOOP("Hotkeys", "Auto Fit (Larger Only)"), {k(A | Qt::Key_5)}},
        {"view.ontop", QT_TRANSLATE_NOOP("Hotkeys", "Always On &Top"), {k(C | Qt::Key_A)}},
        {"player.newwindow", QT_TRANSLATE_NOOP("Hotkeys", "Open Each File in a &New Window"), {}},
        //: "&&" shows one "&"; keep it doubled.
        {"pns.reset", QT_TRANSLATE_NOOP("Hotkeys", "Pan&&Scan Reset"), {k(K | Qt::Key_5)}},
        {"pns.incsize", QT_TRANSLATE_NOOP("Hotkeys", "Increase Size"), {k(K | Qt::Key_9)}},
        {"pns.incwidth", QT_TRANSLATE_NOOP("Hotkeys", "Increase Width"), {k(K | Qt::Key_6)}},
        {"pns.incheight", QT_TRANSLATE_NOOP("Hotkeys", "Increase Height"), {k(K | Qt::Key_8)}},
        {"pns.decsize", QT_TRANSLATE_NOOP("Hotkeys", "Decrease Size"), {k(K | Qt::Key_1)}},
        {"pns.decwidth", QT_TRANSLATE_NOOP("Hotkeys", "Decrease Width"), {k(K | Qt::Key_4)}},
        {"pns.decheight", QT_TRANSLATE_NOOP("Hotkeys", "Decrease Height"), {k(K | Qt::Key_2)}},
        {"pns.center", QT_TRANSLATE_NOOP("Hotkeys", "Center"), {k(C | K | Qt::Key_5)}},
        {"pns.left", QT_TRANSLATE_NOOP("Hotkeys", "Move Left"), {k(C | K | Qt::Key_4)}},
        {"pns.right", QT_TRANSLATE_NOOP("Hotkeys", "Move Right"), {k(C | K | Qt::Key_6)}},
        {"pns.up", QT_TRANSLATE_NOOP("Hotkeys", "Move Up"), {k(C | K | Qt::Key_8)}},
        {"pns.down", QT_TRANSLATE_NOOP("Hotkeys", "Move Down"), {k(C | K | Qt::Key_2)}},
        {"pns.upleft", QT_TRANSLATE_NOOP("Hotkeys", "Move Up/Left"), {k(C | K | Qt::Key_7)}},
        {"pns.upright", QT_TRANSLATE_NOOP("Hotkeys", "Move Up/Right"), {k(C | K | Qt::Key_9)}},
        {"pns.downleft", QT_TRANSLATE_NOOP("Hotkeys", "Move Down/Left"), {k(C | K | Qt::Key_1)}},
        {"pns.downright", QT_TRANSLATE_NOOP("Hotkeys", "Move Down/Right"), {k(C | K | Qt::Key_3)}},
        {"pns.rotatezp", QT_TRANSLATE_NOOP("Hotkeys", "Rotate Clockwise"), {k(C | Qt::Key_Right), k(A | K | Qt::Key_1)}},
        {"pns.rotatezm", QT_TRANSLATE_NOOP("Hotkeys", "Rotate Counter-clockwise"), {k(C | Qt::Key_Left), k(A | K | Qt::Key_3)}},
        // Audio / subtitles
        {"vol.up", QT_TRANSLATE_NOOP("Hotkeys", "Volume &Up"), {k(Qt::Key_Up), k(Qt::Key_VolumeUp)}},
        {"vol.down", QT_TRANSLATE_NOOP("Hotkeys", "Volume &Down"), {k(Qt::Key_Down), k(Qt::Key_VolumeDown)}},
        {"vol.mute", QT_TRANSLATE_NOOP("Hotkeys", "&Mute"), {k(C | Qt::Key_M), k(Qt::Key_VolumeMute)}},
        {"stream.audionext", QT_TRANSLATE_NOOP("Hotkeys", "Next Audio Track"), {k(Qt::Key_A)}},
        {"stream.audioprev", QT_TRANSLATE_NOOP("Hotkeys", "Previous Audio Track"), {k(S | Qt::Key_A)}},
        {"stream.subnext", QT_TRANSLATE_NOOP("Hotkeys", "Next Subtitle"), {k(Qt::Key_S)}},
        {"stream.subprev", QT_TRANSLATE_NOOP("Hotkeys", "Previous Subtitle"), {k(S | Qt::Key_S)}},
        {"stream.subonoff", QT_TRANSLATE_NOOP("Hotkeys", "Subtitles On/Off"), {k(Qt::Key_W)}},
        {"sub.prev", QT_TRANSLATE_NOOP("Hotkeys", "Go to Previous Subtitle"), {k(Qt::Key_Y)}},
        {"sub.next", QT_TRANSLATE_NOOP("Hotkeys", "Go to Next Subtitle"), {k(Qt::Key_U)}},
        {"sub.delaydown", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Delay -"), {k(S | Qt::Key_F2)}},  // MPC-HC: F1 (Coreplay: the hotkey list)
        {"sub.delayup", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Delay +"), {k(Qt::Key_F2)}},
        {"sub.posdown", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Position Down"), {k(C | S | K | Qt::Key_Minus)}},
        {"sub.posup", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Position Up"), {k(C | S | K | Qt::Key_Plus)}},
        {"sub.fontdec", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Size -"), {k(C | K | Qt::Key_Minus)}},
        {"sub.fontinc", QT_TRANSLATE_NOOP("Hotkeys", "Subtitle Size +"), {k(C | K | Qt::Key_Plus)}},
        // Misc
        //: "OSD" (on-screen display) may stay in English.
        {"osd.time", QT_TRANSLATE_NOOP("Hotkeys", "OSD: Show Current Time"), {k(C | Qt::Key_I)}},
        {"osd.filename", QT_TRANSLATE_NOOP("Hotkeys", "OSD: Show File Name"), {k(Qt::Key_N)}},
        {"view.stats", QT_TRANSLATE_NOOP("Hotkeys", "Renderer Statistics"), {k(C | Qt::Key_J)}},
        {"boss", QT_TRANSLATE_NOOP("Hotkeys", "Boss Key"), {}},  // MPC-HC uses B; Coreplay gives B to the blue channel
        {"menu", QT_TRANSLATE_NOOP("Hotkeys", "Show Menu"), {k(Qt::Key_Menu)}},
        {"options", QT_TRANSLATE_NOOP("Hotkeys", "&Settings..."), {k(Qt::Key_O)}},
        {"help.hotkeys", QT_TRANSLATE_NOOP("Hotkeys", "&Hotkey List"), {k(Qt::Key_F1)}},
        //: "Coreplay" is the product name; keep it in English.
        {"help.about", QT_TRANSLATE_NOOP("Hotkeys", "&About Coreplay"), {}},
        // Viewer zoom and LUTs (not in MPC-HC)
        {"zoom.in", QT_TRANSLATE_NOOP("Hotkeys", "Zoom &In"), {m(Hotkeys::WheelUp)}},
        {"zoom.out", QT_TRANSLATE_NOOP("Hotkeys", "Zoom &Out"), {m(Hotkeys::WheelDown)}},
        {"zoom.fit", QT_TRANSLATE_NOOP("Hotkeys", "Zoom to &Fit"), {}},
        {"zoom.1to1", QT_TRANSLATE_NOOP("Hotkeys", "Zoom &1:1 (actual pixels)"), {}},
        {"lut.load", QT_TRANSLATE_NOOP("Hotkeys", "&Load a LUT..."), {}},
        {"lut.clear", QT_TRANSLATE_NOOP("Hotkeys", "&Clear LUT"), {}},
        // Image-sequence extras (not in MPC-HC)
        {"seq.fps", QT_TRANSLATE_NOOP("Hotkeys", "Sequence &Frame Rate..."), {}},
        // Channel solo, as in BunnyLab: press again for full colour
        {"channel.rgb", QT_TRANSLATE_NOOP("Hotkeys", "Channel: All (RGB)"), {}},
        {"channel.r", QT_TRANSLATE_NOOP("Hotkeys", "Channel: &Red"), {k(Qt::Key_R)}},
        {"channel.g", QT_TRANSLATE_NOOP("Hotkeys", "Channel: &Green"), {k(Qt::Key_G)}},
        {"channel.b", QT_TRANSLATE_NOOP("Hotkeys", "Channel: &Blue"), {k(Qt::Key_B)}},
        {"channel.a", QT_TRANSLATE_NOOP("Hotkeys", "Channel: &Alpha"), {}},
        {"seq.exposureup", QT_TRANSLATE_NOOP("Hotkeys", "Exposure +1/2 stop"), {}},
        {"seq.exposuredown", QT_TRANSLATE_NOOP("Hotkeys", "Exposure -1/2 stop"), {}},
        {"seq.exposurereset", QT_TRANSLATE_NOOP("Hotkeys", "Exposure Reset"), {}},
    };
    return table;
}

namespace {
const struct { int key; const char* name; } kMouseNames[] = {
    {WheelUp, "Wheel Up"}, {WheelDown, "Wheel Down"}, {MiddleClick, "Middle Click"},
    {MouseBack, "Mouse Back"}, {MouseForward, "Mouse Forward"}};
}  // namespace

QKeySequence mouseKey(int key, Qt::KeyboardModifiers mods) {
    mods &= Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key)));
}

bool isMouse(const QKeySequence& ks) {
    if (ks.count() != 1) return false;
    const int key = ks[0].key();
    return key >= WheelUp && key <= MouseForward;
}

QString text(const QKeySequence& ks, bool native) {
    if (!isMouse(ks)) return ks.toString(native ? QKeySequence::NativeText : QKeySequence::PortableText);
    const Qt::KeyboardModifiers mods = ks[0].keyboardModifiers();
    QString s;
    if (mods & Qt::ControlModifier) s += "Ctrl+";
    if (mods & Qt::ShiftModifier) s += "Shift+";
    if (mods & Qt::AltModifier) s += "Alt+";
    if (mods & Qt::MetaModifier) s += "Meta+";
    for (const auto& n : kMouseNames)
        if (n.key == ks[0].key()) return s + n.name;
    return s;
}

QKeySequence fromText(const QString& in) {
    const QString s = in.trimmed();
    for (const auto& n : kMouseNames) {
        if (!s.endsWith(n.name, Qt::CaseInsensitive)) continue;
        Qt::KeyboardModifiers mods;
        for (const QString& part : s.left(s.size() - int(qstrlen(n.name))).split('+', Qt::SkipEmptyParts)) {
            const QString p = part.trimmed().toLower();
            if (p == "ctrl") mods |= Qt::ControlModifier;
            else if (p == "shift") mods |= Qt::ShiftModifier;
            else if (p == "alt") mods |= Qt::AltModifier;
            else if (p == "meta") mods |= Qt::MetaModifier;
            else return {};
        }
        return mouseKey(n.key, mods);
    }
    return QKeySequence::fromString(s, QKeySequence::PortableText);
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
        for (const auto& ks : d.keys) arr.append(text(ks, false));
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
            QKeySequence ks = fromText(v.toString());
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
        for (const auto& ks : keys[i].keys) arr.append(text(ks, false));
        root.insert(keys[i].id, arr);
    }
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0;
}

}  // namespace Hotkeys
