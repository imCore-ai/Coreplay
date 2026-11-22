// Test tool: writes an EXR sequence whose header carries framesPerSecond, with a
// data window smaller than the display window, so both header paths get tested.
//   mkexr <out_pattern_with_%04d> <first> <count> <fps_num> <fps_den>
#include <OpenImageIO/imageio.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: mkexr <pattern> <first> <count> <num> <den>\n");
        return 2;
    }
    const int first = std::atoi(argv[2]), count = std::atoi(argv[3]);
    const int fps[2] = {std::atoi(argv[4]), std::atoi(argv[5])};
    const int W = 1920, H = 1080, dx = 100, dy = 50, dw = 1720, dh = 980;
    for (int i = 0; i < count; ++i) {
        char name[1024];
        std::snprintf(name, sizeof name, argv[1], first + i);
        OIIO::ImageSpec spec(dw, dh, 4, OIIO::TypeDesc::HALF);
        spec.x = dx;
        spec.y = dy;
        spec.full_x = 0;
        spec.full_y = 0;
        spec.full_width = W;
        spec.full_height = H;
        spec.attribute("compression", "piz");
        spec.attribute("FramesPerSecond", OIIO::TypeRational, fps);
        std::vector<float> px(size_t(dw) * dh * 4);
        // Linear ramp left->right, a bar that moves one step per frame, alpha 1.
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                float* p = &px[(size_t(y) * dw + x) * 4];
                const float ramp = float(x) / dw;
                const bool bar = (x / 20) == (i % (dw / 20));
                p[0] = bar ? 4.0f : ramp;      // super-white bar tests values > 1
                p[1] = ramp * 0.5f;
                p[2] = float(y) / dh * 0.25f;
                p[3] = 1.0f;
            }
        auto out = OIIO::ImageOutput::create(name);
        if (!out || !out->open(name, spec) || !out->write_image(OIIO::TypeDesc::FLOAT, px.data())) {
            std::fprintf(stderr, "cannot write %s: %s\n", name, OIIO::geterror().c_str());
            return 1;
        }
        out->close();
    }
    return 0;
}
