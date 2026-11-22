#pragma once
// A/B compare, as in RV: two clips on one timeline, frame-locked, shown by the viewer's
// own shader. Flip shows A or B whole; Wipe splits the picture at a line you drag;
// Side by Side puts them next to each other; Difference shows only what changed.
#include <QString>

enum class CompareMode { Off = 0, Flip = 1, Wipe = 2, SideBySide = 3, Difference = 4 };

struct CompareView {
    CompareMode mode = CompareMode::Off;
    bool swap = false;     // Flip: true shows B. Wipe / Side by Side: B on the left.
    float wipe = 0.5f;     // wipe line, 0..1 across the picture
    bool operator==(const CompareView&) const = default;
};

inline QString compareModeName(CompareMode m) {
    switch (m) {
        case CompareMode::Flip: return "A/B Flip";
        case CompareMode::Wipe: return "Wipe";
        case CompareMode::SideBySide: return "Side by Side";
        case CompareMode::Difference: return "Difference";
        default: return "Off";
    }
}

// The GLSL that picks A, B, the wipe line or the difference for one output pixel.
// The viewer defines: vec4 shadeA(vec2 t), vec4 shadeB(vec2 t) (display colour), and
// the uniforms below. uv is 0..1 over the drawn picture (both halves in Side by Side).
inline const char* compareGlsl() {
    return R"(
uniform int cmpMode;     // 0 off, 1 flip, 2 wipe, 3 side by side, 4 difference
uniform int cmpSwap;
uniform float cmpWipe;
uniform float cmpLine;   // wipe line half width, in uv
vec4 compareColour(vec2 uv) {
    if (cmpMode == 0) return shadeA(uv);
    if (cmpMode == 1) return cmpSwap == 1 ? shadeB(uv) : shadeA(uv);
    if (cmpMode == 3) {
        float u = uv.x * 2.0;
        bool left = u < 1.0;
        vec2 t = vec2(left ? u : u - 1.0, uv.y);
        return (left != (cmpSwap == 1)) ? shadeA(t) : shadeB(t);
    }
    if (cmpMode == 4) return vec4(abs(shadeA(uv).rgb - shadeB(uv).rgb), 1.0);
    if (abs(uv.x - cmpWipe) < cmpLine) return vec4(0.75, 0.75, 0.75, 1.0);
    bool left = uv.x < cmpWipe;
    return (left != (cmpSwap == 1)) ? shadeA(uv) : shadeB(uv);
}
)";
}
