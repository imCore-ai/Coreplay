#pragma once
// Colour grading, the same controls and maths as BunnyLab's grading panel:
// brightness, contrast, saturation, hue (as the CSS filters of the same names, in
// that order), then gamma, shadows / midtones / highlights and a colour wheel per
// tone range (BunnyLab's per-channel curve).
//
// One definition, three users: apply() (CPU: baked .cube for export / save, and
// stills), glslFunction() (the image-sequence viewer) and mpvShader() (movies).
// The grade works on the picture as shown (display values 0..1, after the LUT).
#include <QJsonObject>
#include <QString>

struct GradeTint {
    double angle = 0;     // 0..360, hue of the wheel (0 = red at 12 o'clock)
    double strength = 0;  // 0..1, distance from the centre
    bool operator==(const GradeTint&) const = default;
};

struct Grade {
    double brightness = 100;  // 0..200
    double contrast = 100;    // 0..200
    double gamma = 1;         // 0.1..3
    double saturation = 100;  // 0..200
    double hue = 0;           // 0..360 degrees
    double shadows = 0, midtones = 0, highlights = 0;  // -100..100
    GradeTint shadowsTint, midtonesTint, highlightsTint;

    bool operator==(const Grade&) const = default;
    bool isIdentity() const { return *this == Grade(); }
    void apply(float rgb[3]) const;            // in place, 0..1 out
    QString summary() const;                   // "Contrast 120, Saturation 80, ..." (tests, title tips)
    QJsonObject toJson() const;
    static Grade fromJson(const QJsonObject& o);

    // GLSL: `vec3 coreplayGrade(vec3 c)` reading the uniforms of uniformsGlsl().
    static const char* glslFunction();
    static const char* uniformsGlsl();
    // The 18 uniform values (cpg_on first: 0 = identity, the picture is not touched)
    // and their names, in the same order.
    static constexpr int kUniforms = 18;
    void uniformValues(float out[kUniforms]) const;
    static const char* const* uniformNames();

    // mpv user shader (HOOK MAIN) with this grade's values as constants. A new file name
    // for every `slot` (a number that only goes up): mpv reads a shader file once per name
    // and keeps it, so a reused name would keep showing the old grade. `leftHalfOnly`:
    // compare mode (A|B packed), grade A only. Empty when it cannot be written.
    QString writeMpvShader(int slot, bool leftHalfOnly) const;

    // Bakes the grade into a .cube (N^3, domain 0..1); empty + *error on failure.
    QString bakeCube(QString* error, int n = 65) const;
};
