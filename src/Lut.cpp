#include "Lut.h"

#include <OpenColorIO/OpenColorIO.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace {
// Menu name -> colour space in OCIO's built-in studio config.
struct Source { const char* menu; const char* space; };
const Source kSources[] = {
    {"Linear Rec.709 (sRGB primaries)", "Linear Rec.709 (sRGB)"},
    {"Linear Rec.2020", "Linear Rec.2020"},
    {"ACEScg (linear AP1)", "ACEScg"},
    {"ACEScct", "ACEScct"},
    {"ARRI LogC3 (EI800)", "ARRI LogC3 (EI800)"},
    {"ARRI LogC4", "ARRI LogC4"},
    {"Sony S-Log3 S-Gamut3", "S-Log3 S-Gamut3"},
    {"Sony S-Log3 S-Gamut3.Cine", "S-Log3 S-Gamut3.Cine"},
    {"Sony S-Log3 Venice S-Gamut3.Cine", "S-Log3 Venice S-Gamut3.Cine"},
    {"Blackmagic Film WideGamut Gen5", "BMDFilm WideGamut Gen5"},
    {"DaVinci Intermediate WideGamut", "DaVinci Intermediate WideGamut"},
    {"RED Log3G10 REDWideGamutRGB", "Log3G10 REDWideGamutRGB"},
    {"Canon Log 2 Cinema Gamut", "CanonLog2 CinemaGamut D55"},
    {"Canon Log 3 Cinema Gamut", "CanonLog3 CinemaGamut D55"},
    {"Panasonic V-Log V-Gamut", "V-Log V-Gamut"},
    {"DJI D-Log D-Gamut", "D-Log D-Gamut"},
    {"Apple Log", "Apple Log"},
};

// Presets always use the studio config (it has every camera space), whatever
// $OCIO points at.
OCIO::ConstConfigRcPtr studio() {
    static OCIO::ConstConfigRcPtr cfg = OCIO::Config::CreateFromFile("ocio://studio-config-latest");
    return cfg;
}

const char* spaceFor(const QString& menu) {
    for (const auto& s : kSources)
        if (menu == s.menu) return s.space;
    throw std::runtime_error(QCoreApplication::translate("Lut", "Unknown LUT preset source: %1").arg(menu).toStdString());
}
}  // namespace

QString LutChoice::label() const {
    switch (kind) {
        case File: return QFileInfo(file).fileName();
        case Preset: return source + " to " + target;
        case View: return source + " -> " + display + " / " + view;
        default: return "None";
    }
}

namespace Lut {

QStringList presetSources() {
    QStringList out;
    for (const auto& s : kSources) out << s.menu;
    return out;
}

QStringList presetTargets() { return {"sRGB", "Rec.709", "Rec.2020"}; }

QString fileFilter() {
    return QCoreApplication::translate("Lut", "LUT files") + " (*.cube *.3dl *.csp *.spi1d *.spi3d *.spimtx *.cc *.ccc *.cdl *.clf *.ctf *.lut *.mga *.m3d *.icc *.icm *.vf *.itx *.look *.cub *.1dl *.hdl *.cms);;" + QCoreApplication::translate("Lut", "All files") + " (*)";
}

std::shared_ptr<const OCIO::Processor> processor(const LutChoice& c) {
    try {
        auto cfg = studio();
        if (c.kind == LutChoice::File) {
            auto t = OCIO::FileTransform::Create();
            t->setSrc(QDir::toNativeSeparators(c.file).toUtf8().constData());
            t->setInterpolation(OCIO::INTERP_BEST);
            return cfg->getProcessor(t);
        }
        if (c.kind == LutChoice::View) {
            // The viewer's own transform, from the same config the viewer uses.
            OCIO::ConstConfigRcPtr vc = qEnvironmentVariableIsSet("OCIO") ? OCIO::Config::CreateFromEnv() : cfg;
            auto t = OCIO::DisplayViewTransform::Create();
            t->setSrc(c.source.toUtf8().constData());
            t->setDisplay(c.display.toUtf8().constData());
            t->setView(c.view.toUtf8().constData());
            return vc->getProcessor(t);
        }
        if (c.kind == LutChoice::Preset) {
            const char* src = spaceFor(c.source);
            // Colorimetric conversion (no tone mapping), then the target's encoding.
            if (c.target == "sRGB") return cfg->getProcessor(src, "sRGB Encoded Rec.709 (sRGB)");
            if (c.target == "Rec.709") return cfg->getProcessor(src, "Gamma 2.4 Encoded Rec.709");  // BT.1886
            if (c.target == "Rec.2020") {
                // The studio config has no SDR Rec.2020 encoding: linear Rec.2020, then
                // BT.1886 (inverse 2.4 power, negatives clamped).
                auto g = OCIO::GroupTransform::Create();
                auto cs = OCIO::ColorSpaceTransform::Create();
                cs->setSrc(src);
                cs->setDst("Linear Rec.2020");
                g->appendTransform(cs);
                auto ex = OCIO::ExponentTransform::Create();
                const double gamma[4] = {2.4, 2.4, 2.4, 1.0};
                ex->setValue(gamma);
                ex->setDirection(OCIO::TRANSFORM_DIR_INVERSE);
                g->appendTransform(ex);
                return cfg->getProcessor(g);
            }
            throw std::runtime_error(QCoreApplication::translate("Lut", "Unknown LUT target: %1").arg(c.target).toStdString());
        }
        return {};
    } catch (const OCIO::Exception& e) {
        throw std::runtime_error(e.what());
    }
}

namespace {
constexpr int kGrid = 65;

// The transform sampled on an N^3 grid over 0..1, red changing fastest.
std::vector<float> sampleGrid(const LutChoice& c) {
    auto proc = processor(c);
    if (!proc) return {};
    auto cpu = proc->getOptimizedCPUProcessor(OCIO::OPTIMIZATION_DEFAULT);
    const int N = kGrid;
    std::vector<float> rgb(size_t(N) * N * N * 3);
    size_t i = 0;
    for (int b = 0; b < N; ++b)
        for (int g = 0; g < N; ++g)
            for (int r = 0; r < N; ++r) {
                rgb[i++] = float(r) / (N - 1);
                rgb[i++] = float(g) / (N - 1);
                rgb[i++] = float(b) / (N - 1);
            }
    OCIO::PackedImageDesc img(rgb.data(), long(N) * N * N, 1, 3);
    cpu->apply(img);
    return rgb;
}

QString cachePath(const LutChoice& c, const char* ext) {
    const QByteArray key = (c.file + "|" + c.source + "|" + c.target + "|" + c.display + "|" + c.view + "|" +
                            QString::number(QFileInfo(c.file).lastModified().toMSecsSinceEpoch()))
                               .toUtf8();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/luts";
    QDir().mkpath(dir);
    return dir + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16) + ext;
}

// mpv's rgba16 texture: 16-bit normalised. Display-referred LUT output is 0..1.
// (mpv's rgba16f wants 32-bit float input data, twice the size, for no gain here.)
uint16_t toUnorm16(float f) {
    return uint16_t(std::lround(std::clamp(f, 0.0f, 1.0f) * 65535.0f));
}

bool writeAtomically(const QString& path, const QByteArray& bytes, QString* error) {
    QFile f(path + ".tmp");
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QCoreApplication::translate("Lut", "Cannot write %1").arg(f.fileName());
        return false;
    }
    f.write(bytes);
    f.close();
    QFile::remove(path);
    return QFile::rename(path + ".tmp", path);
}
}  // namespace

QString bakeCube(const LutChoice& c, QString* error) {
    try {
        const QString path = cachePath(c, ".cube");
        if (QFileInfo::exists(path)) return path;
        const std::vector<float> rgb = sampleGrid(c);
        if (rgb.empty()) return {};
        QByteArray out;
        out.reserve(int(rgb.size()) * 10);
        out += "TITLE \"Coreplay " + c.label().toUtf8() + "\"\nLUT_3D_SIZE " + QByteArray::number(kGrid) + "\n";
        for (size_t k = 0; k < rgb.size(); k += 3) {
            out += QByteArray::number(rgb[k], 'f', 6) + ' ' + QByteArray::number(rgb[k + 1], 'f', 6) + ' ' +
                   QByteArray::number(rgb[k + 2], 'f', 6) + '\n';
        }
        return writeAtomically(path, out, error) ? path : QString();
    } catch (const std::exception& e) {
        *error = e.what();
        return {};
    }
}

// An mpv user shader carrying the LUT as a 3D texture (RGBA 16-bit), hooked on
// MAIN: the RGB picture in the file's own encoding, right after YUV->RGB and
// before scaling. The GPU does it, so hardware decoding stays zero-copy.
// One LUT as an mpv //!TEXTURE block named `name` (empty if the transform has no grid).
static QByteArray textureBlock(const LutChoice& c, const char* name) {
    const std::vector<float> rgb = sampleGrid(c);
    if (rgb.empty()) return {};
    const int N = kGrid;
    QByteArray out;
    out.reserve(N * N * N * 16 + 1024);
    out += QByteArray("//!TEXTURE ") + name + "\n//!SIZE " + QByteArray::number(N) + " " + QByteArray::number(N) + " " +
           QByteArray::number(N) + "\n//!FORMAT rgba16\n//!FILTER LINEAR\n//!BORDER CLAMP\n";
    static const char* hex = "0123456789abcdef";
    auto put = [&](uint16_t h) {  // little-endian bytes, as hex
        const uint8_t lo = h & 0xff, hi = h >> 8;
        out += hex[lo >> 4];
        out += hex[lo & 15];
        out += hex[hi >> 4];
        out += hex[hi & 15];
    };
    for (size_t k = 0; k < rgb.size(); k += 3) {
        put(toUnorm16(rgb[k]));
        put(toUnorm16(rgb[k + 1]));
        put(toUnorm16(rgb[k + 2]));
        put(0xffff);
    }
    out += "\n\n";
    return out;
}

QString bakeMpvCompareShader(const LutChoice& a, const LutChoice& b, QString* error) {
    try {
        const QString pa = cachePath(a, ""), pb = cachePath(b, "");
        const QString path = QFileInfo(pa).absolutePath() + "/cmp_" + QFileInfo(pa).fileName() + "_" +
                             QFileInfo(pb).fileName() + (a.kind == LutChoice::None ? "n" : "a") +
                             (b.kind == LutChoice::None ? "n" : "b") + ".glsl";
        if (QFileInfo::exists(path)) return path;
        QByteArray out;
        if (a.kind != LutChoice::None) out += textureBlock(a, "COREPLAY_LUT_A");
        if (b.kind != LutChoice::None) out += textureBlock(b, "COREPLAY_LUT_B");
        const int N = kGrid;
        out += "//!HOOK MAIN\n//!BIND HOOKED\n";
        if (a.kind != LutChoice::None) out += "//!BIND COREPLAY_LUT_A\n";
        if (b.kind != LutChoice::None) out += "//!BIND COREPLAY_LUT_B\n";
        out += "//!DESC Coreplay compare LUTs\nvec4 hook() {\n"
               "    vec4 c = HOOKED_texOff(0);\n"
               "    const float n = " + QByteArray::number(N) + ".0;\n"
               "    vec3 p = clamp(c.rgb, 0.0, 1.0) * ((n - 1.0) / n) + 0.5 / n;\n";
        // Left half = A, right half = B (the packed frame).
        out += "    if (HOOKED_pos.x < 0.5) {\n";
        if (a.kind != LutChoice::None) out += "        return vec4(texture(COREPLAY_LUT_A, p).rgb, c.a);\n";
        out += "        return c;\n    }\n";
        if (b.kind != LutChoice::None) out += "    return vec4(texture(COREPLAY_LUT_B, p).rgb, c.a);\n";
        else out += "    return c;\n";
        out += "}\n";
        return writeAtomically(path, out, error) ? path : QString();
    } catch (const std::exception& e) {
        *error = e.what();
        return {};
    }
}

QString bakeMpvShader(const LutChoice& c, QString* error) {
    try {
        const QString path = cachePath(c, ".glsl");
        if (QFileInfo::exists(path)) return path;
        QByteArray out = textureBlock(c, "COREPLAY_LUT");
        if (out.isEmpty()) return {};
        const int N = kGrid;
        out += "//!HOOK MAIN\n//!BIND HOOKED\n//!BIND COREPLAY_LUT\n//!DESC Coreplay LUT " +
               c.label().toUtf8() +
               "\nvec4 hook() {\n"
               "    vec4 c = HOOKED_texOff(0);\n"
               "    const float n = " + QByteArray::number(N) + ".0;\n"
               "    vec3 p = clamp(c.rgb, 0.0, 1.0) * ((n - 1.0) / n) + 0.5 / n;\n"
               "    return vec4(texture(COREPLAY_LUT, p).rgb, c.a);\n"
               "}\n";
        return writeAtomically(path, out, error) ? path : QString();
    } catch (const std::exception& e) {
        *error = e.what();
        return {};
    }
}

}  // namespace Lut
