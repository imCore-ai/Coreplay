#pragma once
// LUTs shown on both engines: a LUT file the user loads, or a preset conversion
// from a camera / working space to sRGB, Rec.709 or Rec.2020.
// Image sequences run the transform on the GPU (OCIO shader); movies get the same
// transform baked into a 65^3 3D texture in an mpv GPU shader. Export (CPU) uses
// a baked .cube with FFmpeg's lut3d.
#include <QString>
#include <QStringList>
#include <memory>

#include <OpenColorIO/OpenColorABI.h>  // OCIO_NAMESPACE only

namespace OCIO_NAMESPACE {
class Processor;
}

struct LutChoice {
    enum Kind { None, File, Preset, View } kind = None;
    QString file;     // Kind::File
    QString source;   // Preset: preset source name (see Lut::presetSources); View: input colour space
    QString target;   // Kind::Preset: "sRGB", "Rec.709" or "Rec.2020"
    QString display;  // Kind::View: OCIO display
    QString view;     // Kind::View: OCIO view
    QString label() const;
    bool operator==(const LutChoice& o) const {
        return kind == o.kind && file == o.file && source == o.source && target == o.target &&
               display == o.display && view == o.view;
    }
};

namespace Lut {
// Menu names of the preset sources, in menu order.
QStringList presetSources();
QStringList presetTargets();
// File types the loader accepts (OCIO FileTransform formats).
QString fileFilter();
// The transform for a choice; throws std::exception with a readable message.
std::shared_ptr<const OCIO_NAMESPACE::Processor> processor(const LutChoice& c);
// Bakes the choice into a .cube (65^3, input domain 0..1) and returns its path.
// Empty with *error set when it cannot.
QString bakeCube(const LutChoice& c, QString* error);
// The same transform as an mpv user shader (3D texture, GPU). Used for playback.
QString bakeMpvShader(const LutChoice& c, QString* error);
// A/B compare of movies: mpv shows A and B packed side by side in one frame; this
// shader runs A's LUT on the left half and B's on the right (None = unchanged).
QString bakeMpvCompareShader(const LutChoice& a, const LutChoice& b, QString* error);
}  // namespace Lut
