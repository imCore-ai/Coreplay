#include "seq/ImageDecoder.h"

#include <OpenImageIO/imageio.h>

#include <QFileInfo>
#include <algorithm>
#include <cstring>

namespace ImageDecoder {

void init() {
    // Frame-level parallelism comes from FrameCache's workers. One thread per
    // file avoids oversubscribing the CPU.
    OIIO::attribute("threads", 1);
    OIIO::attribute("exr_threads", 1);  // measured: 1, 8 and 24 give the same first-frame time and cache fill
}

FramePtr decode(const QString& path) {
    auto f = std::make_shared<Frame>();
    if (!QFileInfo::exists(path)) {
        f->missing = true;
        return f;
    }
    const std::string p = path.toUtf8().toStdString();
    auto in = OIIO::ImageInput::open(p);
    if (!in) {
        f->error = QString::fromStdString(OIIO::geterror());
        return f;
    }
    const OIIO::ImageSpec& spec = in->spec();
    const int srcCh = std::min(spec.nchannels, 4);
    // RGB is stored as RGBA (alpha = 1): the GPU driver then takes the data as it is.
    // A 3-channel upload is converted on the CPU by the driver, inside the GUI thread
    // (measured: 4K half RGB 27 ms per frame).
    const int nch = srcCh == 3 ? 4 : srcCh;

    // In memory: 8 bit stays 8 bit, 10..16 bit become 16 bit, half stays half, and
    // 32-bit float becomes half: enough for the display (OCIO, exposure), and half the
    // RAM and upload of 32-bit (twice as many frames fit in the cache).
    OIIO::TypeDesc fmt;
    switch (spec.format.basetype) {
        case OIIO::TypeDesc::HALF: fmt = OIIO::TypeDesc::HALF; f->type = f->sourceType = PixelType::F16; break;
        case OIIO::TypeDesc::FLOAT:
        case OIIO::TypeDesc::DOUBLE: fmt = OIIO::TypeDesc::HALF; f->type = PixelType::F16; f->sourceType = PixelType::F32; break;
        case OIIO::TypeDesc::UINT8:
        case OIIO::TypeDesc::INT8: fmt = OIIO::TypeDesc::UINT8; f->type = f->sourceType = PixelType::U8; break;
        default: fmt = OIIO::TypeDesc::UINT16; f->type = f->sourceType = PixelType::U16; break;
    }

    // Display window. A data window smaller than it is placed inside it; the rest
    // stays zero (transparent black), as in Nuke and RV.
    const int fw = spec.full_width > 0 ? spec.full_width : spec.width;
    const int fh = spec.full_height > 0 ? spec.full_height : spec.height;
    f->width = fw;
    f->height = fh;
    f->channels = nch;
    f->sourceChannels = spec.nchannels;
    const size_t bpc = size_t(f->bytesPerChannel());
    const size_t px = size_t(nch) * bpc;
    const size_t rowBytes = size_t(fw) * px;
    const int ox = spec.x - spec.full_x;
    const int oy = spec.y - spec.full_y;
    const bool fills = ox == 0 && oy == 0 && spec.width == fw && spec.height == fh;
    // Alpha = 1 where the file has none (RGB), else zero (outside the data window).
    if (srcCh == 3 && fills) {
        f->data.resize(rowBytes * size_t(fh));
    } else {
        f->data.assign(rowBytes * size_t(fh), 0);
    }
    if (srcCh == 3) {
        uint8_t one[4] = {};
        switch (f->type) {
            case PixelType::U8: one[0] = 0xff; break;
            case PixelType::U16: one[0] = one[1] = 0xff; break;
            case PixelType::F16: one[0] = 0x00; one[1] = 0x3c; break;  // 1.0 in half
            case PixelType::F32: break;
        }
        uint8_t* d = f->data.data() + 3 * bpc;
        const size_t n = size_t(fw) * size_t(fh);
        for (size_t k = 0; k < n; ++k, d += px) std::memcpy(d, one, bpc);
    }

    // Read straight into the frame when the image fills it (the usual case): no
    // second copy. The channels go to every nch-th slot (xstride), so RGB lands in RGBA.
    if (fills) {
        if (!in->read_image(0, 0, 0, srcCh, fmt, f->data.data(), OIIO::stride_t(px))) {
            f->error = QString::fromStdString(in->geterror());
            return f;
        }
    } else {
        std::vector<uint8_t> tmp(size_t(spec.width) * size_t(spec.height) * px);
        if (srcCh == 3) {  // the same alpha = 1 in the part that has data
            for (size_t k = 0; k < size_t(spec.width) * spec.height; ++k)
                std::memcpy(tmp.data() + k * px + 3 * bpc, f->data.data() + 3 * bpc, bpc);
        }
        if (!in->read_image(0, 0, 0, srcCh, fmt, tmp.data(), OIIO::stride_t(px))) {
            f->error = QString::fromStdString(in->geterror());
            return f;
        }
        // Outside the data window: transparent black (also alpha 0).
        if (srcCh == 3) std::fill(f->data.begin(), f->data.end(), uint8_t(0));
        for (int y = 0; y < spec.height; ++y) {
            const int dy = y + oy;
            if (dy < 0 || dy >= fh) continue;
            const int x0 = std::max(0, -ox);
            const int x1 = std::min(spec.width, fw - ox);
            if (x1 <= x0) continue;
            std::memcpy(f->data.data() + size_t(dy) * rowBytes + size_t(x0 + ox) * px,
                        tmp.data() + (size_t(y) * spec.width + x0) * px, size_t(x1 - x0) * px);
        }
    }

    if (const OIIO::ParamValue* fps = spec.find_attribute("FramesPerSecond", OIIO::TypeRational)) {
        const int* r = static_cast<const int*>(fps->data());
        f->fps = {r[0], r[1]};
    }
    f->colorSpace = QString::fromStdString(std::string(spec.get_string_attribute("oiio:ColorSpace")));
    f->description = QString("%1  %2x%3  %4 ch  %5  %6")
                         .arg(QString::fromUtf8(in->format_name()))
                         .arg(spec.width).arg(spec.height).arg(spec.nchannels)
                         .arg(QString::fromStdString(spec.format.c_str()))
                         .arg(QString::fromStdString(std::string(spec.get_string_attribute("compression"))));
    // The whole header, for the info overlay: channels, windows, then every attribute.
    QStringList names;
    for (const auto& n : spec.channelnames) names << QString::fromStdString(n);
    f->metadata << "channels: " + names.join(',');
    f->metadata << QString("data window: %1,%2 %3x%4   display window: %5,%6 %7x%8")
                       .arg(spec.x).arg(spec.y).arg(spec.width).arg(spec.height)
                       .arg(spec.full_x).arg(spec.full_y).arg(fw).arg(fh);
    if (spec.tile_width > 0) f->metadata << QString("tiles: %1x%2").arg(spec.tile_width).arg(spec.tile_height);
    for (const OIIO::ParamValue& p : spec.extra_attribs) {
        QString v = QString::fromStdString(p.get_string(16)).simplified();
        if (v.size() > 160) v = v.left(157) + "...";
        f->metadata << QString::fromStdString(p.name().string()) + ": " + v;
    }
    in->close();
    return f;
}

}  // namespace ImageDecoder
