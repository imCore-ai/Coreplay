#pragma once
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

#include "Engine.h"

enum class PixelType { U8, U16, F16, F32 };

// One decoded frame, in the source's own bit depth, sized to the display window.
struct Frame {
    int width = 0;
    int height = 0;
    int channels = 0;            // 1..4 in memory (RGB is kept as RGBA)
    int sourceChannels = 0;      // in the file
    PixelType type = PixelType::U8;        // in memory (32-bit float is kept as half)
    PixelType sourceType = PixelType::U8;  // in the file
    std::vector<uint8_t> data;   // tightly packed rows, top row first
    bool missing = false;        // the file is not on disk
    QString error;               // non-empty when decoding failed
    Rational fps;                // from the file header (EXR framesPerSecond), if any
    QString colorSpace;          // the file's own colour-space tag, if any
    QString description;         // format, channels, compression
    QStringList metadata;        // every header attribute, "name: value"

    size_t bytes() const { return data.size(); }
    int bytesPerChannel() const {
        switch (type) {
            case PixelType::U8: return 1;
            case PixelType::U16: case PixelType::F16: return 2;
            case PixelType::F32: return 4;
        }
        return 1;
    }
    bool isFloat() const { return type == PixelType::F16 || type == PixelType::F32; }
};

using FramePtr = std::shared_ptr<const Frame>;

namespace ImageDecoder {
void init();                                   // once, at startup of the engine
FramePtr decode(const QString& path);
}
