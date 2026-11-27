#include "Grade.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// The pure colour of a hue (HSL, s = 1, l = 0.5), as BunnyLab's hueToRgb.
void hueToRgb(double angle, double out[3]) {
    const double h = std::fmod(std::fmod(angle, 360.0) + 360.0, 360.0);
    const double x = 1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0);
    double r = 0, g = 0, b = 0;
    if (h < 60) r = 1, g = x;
    else if (h < 120) r = x, g = 1;
    else if (h < 180) g = 1, b = x;
    else if (h < 240) g = x, b = 1;
    else if (h < 300) r = x, b = 1;
    else r = 1, b = x;
    out[0] = r, out[1] = g, out[2] = b;
}

// Per-channel shift of a tone range: up to 40 % towards the wheel's colour.
void tintOffset(const GradeTint& t, float out[3]) {
    double c[3];
    hueToRgb(t.angle, c);
    for (int i = 0; i < 3; ++i) out[i] = float((c[i] - 0.5) * t.strength * 0.4);
}

const char* const kNames[18] = {"cpg_on", "cpg_b", "cpg_c", "cpg_s", "cpg_h", "cpg_g", "cpg_ts", "cpg_tm", "cpg_th",
                                "cpg_sr", "cpg_sg", "cpg_sb", "cpg_mr", "cpg_mg", "cpg_mb", "cpg_hr", "cpg_hg", "cpg_hb"};

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

// ---- the maths. glslFunction() below is the same, line by line.
void Grade::apply(float rgb[3]) const {
    float v[18];
    uniformValues(v);
    const float b = v[1], k = v[2], s = v[3], h = v[4], g = v[5];
    float c[3] = {rgb[0], rgb[1], rgb[2]};
    // CSS brightness(), contrast(): each step clamps, as the browser's filter chain does.
    for (float& x : c) x = clamp01(x * b);
    for (float& x : c) x = clamp01((x - 0.5f) * k + 0.5f);
    // CSS saturate() and hue-rotate() matrices (Filter Effects spec).
    const float S[9] = {0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s,
                        0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s,
                        0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s};
    const float co = std::cos(h), si = std::sin(h);
    const float H[9] = {0.213f + co * 0.787f - si * 0.213f, 0.715f - co * 0.715f - si * 0.715f, 0.072f - co * 0.072f + si * 0.928f,
                        0.213f - co * 0.213f + si * 0.143f, 0.715f + co * 0.285f + si * 0.140f, 0.072f - co * 0.072f - si * 0.283f,
                        0.213f - co * 0.213f - si * 0.787f, 0.715f - co * 0.715f + si * 0.715f, 0.072f + co * 0.928f + si * 0.072f};
    for (const float* M : {S, H}) {
        const float r = c[0], gg = c[1], bb = c[2];
        for (int i = 0; i < 3; ++i) c[i] = clamp01(M[3 * i] * r + M[3 * i + 1] * gg + M[3 * i + 2] * bb);
    }
    // Gamma, tone ranges and wheels: per channel, weighted by the channel's own value.
    for (int i = 0; i < 3; ++i) {
        const float t = c[i];
        const float base = std::pow(t, 1.0f / g);
        const float ws = (1 - t) * (1 - t), wm = 4 * t * (1 - t), wh = t * t;
        const float tone = v[6] * ws + v[7] * wm + v[8] * wh;
        const float tint = v[9 + i] * ws + v[12 + i] * wm + v[15 + i] * wh;
        rgb[i] = clamp01(base + tone + tint);
    }
}

const char* Grade::uniformsGlsl() {
    return "uniform float cpg_on, cpg_b, cpg_c, cpg_s, cpg_h, cpg_g, cpg_ts, cpg_tm, cpg_th,\n"
           "    cpg_sr, cpg_sg, cpg_sb, cpg_mr, cpg_mg, cpg_mb, cpg_hr, cpg_hg, cpg_hb;\n";
}

const char* Grade::glslFunction() {
    return R"(
vec3 coreplayGrade(vec3 c) {
    if (cpg_on < 0.5) return c;
    c = clamp(c * cpg_b, 0.0, 1.0);
    c = clamp((c - 0.5) * cpg_c + 0.5, 0.0, 1.0);
    float s = cpg_s;
    mat3 S = transpose(mat3(0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s,
                            0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s,
                            0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s));
    float co = cos(cpg_h), si = sin(cpg_h);
    mat3 H = transpose(mat3(0.213 + co * 0.787 - si * 0.213, 0.715 - co * 0.715 - si * 0.715, 0.072 - co * 0.072 + si * 0.928,
                            0.213 - co * 0.213 + si * 0.143, 0.715 + co * 0.285 + si * 0.140, 0.072 - co * 0.072 - si * 0.283,
                            0.213 - co * 0.213 - si * 0.787, 0.715 - co * 0.715 + si * 0.715, 0.072 + co * 0.928 + si * 0.072));
    c = clamp(S * c, 0.0, 1.0);
    c = clamp(H * c, 0.0, 1.0);
    vec3 ws = (1.0 - c) * (1.0 - c), wm = 4.0 * c * (1.0 - c), wh = c * c;
    vec3 tone = cpg_ts * ws + cpg_tm * wm + cpg_th * wh;
    vec3 tint = vec3(cpg_sr, cpg_sg, cpg_sb) * ws + vec3(cpg_mr, cpg_mg, cpg_mb) * wm + vec3(cpg_hr, cpg_hg, cpg_hb) * wh;
    return clamp(pow(c, vec3(1.0 / cpg_g)) + tone + tint, 0.0, 1.0);
}
)";
}

const char* const* Grade::uniformNames() { return kNames; }

void Grade::uniformValues(float v[18]) const {
    v[0] = isIdentity() ? 0.0f : 1.0f;
    v[1] = float(brightness / 100.0);
    v[2] = float(contrast / 100.0);
    v[3] = float(saturation / 100.0);
    v[4] = float(hue * kPi / 180.0);
    v[5] = float(std::max(0.01, gamma));
    v[6] = float(shadows / 200.0);
    v[7] = float(midtones / 200.0);
    v[8] = float(highlights / 200.0);
    tintOffset(shadowsTint, v + 9);
    tintOffset(midtonesTint, v + 12);
    tintOffset(highlightsTint, v + 15);
}

QString Grade::summary() const {
    if (isIdentity()) return QCoreApplication::translate("Grade", "none");
    QStringList l;
    auto num = [&](const char* n, double v, double def) {
        if (v != def) l << QString("%1 %2").arg(QCoreApplication::translate("Grade", n)).arg(v);
    };
    num(QT_TRANSLATE_NOOP("Grade", "Brightness"), brightness, 100);
    num(QT_TRANSLATE_NOOP("Grade", "Contrast"), contrast, 100);
    num(QT_TRANSLATE_NOOP("Grade", "Gamma"), gamma, 1);
    num(QT_TRANSLATE_NOOP("Grade", "Saturation"), saturation, 100);
    num(QT_TRANSLATE_NOOP("Grade", "Hue"), hue, 0);
    num(QT_TRANSLATE_NOOP("Grade", "Shadows"), shadows, 0);
    num(QT_TRANSLATE_NOOP("Grade", "Midtones"), midtones, 0);
    num(QT_TRANSLATE_NOOP("Grade", "Highlights"), highlights, 0);
    auto tint = [&](const char* n, const GradeTint& t) {
        if (t.strength > 0)
            l << QCoreApplication::translate("Grade", "%1 tint %2/%3").arg(QCoreApplication::translate("Grade", n)).arg(t.angle).arg(t.strength);
    };
    tint(QT_TRANSLATE_NOOP("Grade", "Shadows"), shadowsTint);
    tint(QT_TRANSLATE_NOOP("Grade", "Midtones"), midtonesTint);
    tint(QT_TRANSLATE_NOOP("Grade", "Highlights"), highlightsTint);
    return l.join(", ");
}

QJsonObject Grade::toJson() const {
    auto t = [](const GradeTint& x) { return QJsonObject{{"angle", x.angle}, {"strength", x.strength}}; };
    return {{"brightness", brightness}, {"contrast", contrast}, {"gamma", gamma}, {"saturation", saturation},
            {"hue", hue}, {"shadows", shadows}, {"midtones", midtones}, {"highlights", highlights},
            {"shadowsTint", t(shadowsTint)}, {"midtonesTint", t(midtonesTint)}, {"highlightsTint", t(highlightsTint)}};
}

Grade Grade::fromJson(const QJsonObject& o) {
    Grade g;
    auto t = [](const QJsonValue& v) {
        const QJsonObject x = v.toObject();
        return GradeTint{x.value("angle").toDouble(0), x.value("strength").toDouble(0)};
    };
    g.brightness = o.value("brightness").toDouble(100);
    g.contrast = o.value("contrast").toDouble(100);
    g.gamma = o.value("gamma").toDouble(1);
    g.saturation = o.value("saturation").toDouble(100);
    g.hue = o.value("hue").toDouble(0);
    g.shadows = o.value("shadows").toDouble(0);
    g.midtones = o.value("midtones").toDouble(0);
    g.highlights = o.value("highlights").toDouble(0);
    g.shadowsTint = t(o.value("shadowsTint"));
    g.midtonesTint = t(o.value("midtonesTint"));
    g.highlightsTint = t(o.value("highlightsTint"));
    return g;
}

// ---- movies: an mpv user shader (HOOK MAIN) with the values written in as constants.
// Not shader params: the libmpv of Linux distributions renders with mpv's own (older)
// renderer, which does not read //!PARAM. One method for every platform.
QString Grade::writeMpvShader(int slot, bool leftHalfOnly) const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/shaders";
    QDir().mkpath(dir);
    // mpv keeps each shader file it has read, by name: every grade gets its own name (this
    // process's id, so two Coreplay windows never share one). The file from two grades ago
    // is not in mpv's list any more: remove it.
    const QString name = QString("%1/grade_%2_%3.glsl").arg(dir).arg(QCoreApplication::applicationPid());
    QFile::remove(name.arg(slot - 2));
    const QString path = name.arg(slot);
    float v[kUniforms];
    uniformValues(v);
    QByteArray text = "//!HOOK MAIN\n//!BIND HOOKED\n//!DESC Coreplay grade\n";
    for (int i = 0; i < kUniforms; ++i)
        text += QByteArray("#define ") + kNames[i] + " " + QByteArray::number(double(v[i]), 'g', 9) + "\n";
    text += glslFunction();
    text += "vec4 hook() {\n"
            "    vec4 c = HOOKED_texOff(0);\n";
    if (leftHalfOnly) text += "    if (HOOKED_pos.x >= 0.5) return c;  // compare: B stays as it is\n";
    text += "    return vec4(coreplayGrade(c.rgb), c.a);\n"
            "}\n";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(text);
    return path;
}

QString Grade::bakeCube(QString* error, int n) const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/luts";
    QDir().mkpath(dir);
    QTemporaryFile f(dir + "/grade_XXXXXX.cube");
    f.setAutoRemove(false);
    if (!f.open()) {
        if (error) *error = QCoreApplication::translate("Grade", "Cannot write %1").arg(dir);
        return {};
    }
    QByteArray out;
    out.reserve(n * n * n * 30);
    out += "TITLE \"Coreplay grade\"\nLUT_3D_SIZE " + QByteArray::number(n) + "\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    char line[64];
    for (int b = 0; b < n; ++b)          // .cube order: red changes fastest
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                float c[3] = {float(r) / (n - 1), float(g) / (n - 1), float(b) / (n - 1)};
                apply(c);
                const int len = std::snprintf(line, sizeof line, "%.6f %.6f %.6f\n", c[0], c[1], c[2]);
                out.append(line, len);
            }
    f.write(out);
    return f.fileName();
}
