#include "seq/SeqView.h"

#include <OpenColorIO/OpenColorIO.h>

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QLabel>
#include <QSettings>
#include <QWheelEvent>
#include <cmath>
#include <cstring>
#include <numbers>

// Desktop-GL names that some Qt GL headers leave out.
#ifndef GL_R16
#define GL_R16 0x822A
#endif
#ifndef GL_RG16
#define GL_RG16 0x822C
#endif
#ifndef GL_RGB16
#define GL_RGB16 0x8054
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif

namespace OCIO = OCIO_NAMESPACE;

struct SeqView::OcioState {
    OCIO::ConstConfigRcPtr config;
    OCIO::GpuShaderDescRcPtr shader, shaderB;
    std::vector<std::pair<int, OCIO::GpuShaderDesc::UniformData>> uniforms;  // loc, data
};

namespace {
const char* kVertex = R"(#version 410 core
uniform vec2 halfImg;   // image half size in pixels, after fit and zoom
uniform vec2 halfView;  // view half size in pixels
uniform vec2 offset;    // pan, in NDC
uniform mat2 rot;
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
    uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    vec2 px = rot * (p * halfImg);
    gl_Position = vec4(px / halfView + offset, 0.0, 1.0);
}
)";

const char* kFragmentHead = R"(#version 410 core
in vec2 uv;
out vec4 outColor;
uniform sampler2D img;
uniform sampler2D imgB;  // A/B compare: B's frame
uniform float gain;
uniform int channel;   // 0 RGB, 1 R, 2 G, 3 B, 4 A
uniform int nch;
uniform int nchB;
uniform vec2 bScale;     // B fitted inside A's frame (keeps B's aspect)
)";

const char* kFragmentShade = R"(
vec4 shade(vec4 c, int n, bool b) {
    if (n == 1) c = vec4(c.rrr, 1.0);
    else if (n == 2) c = vec4(c.rrr, c.g);
    else if (n == 3) c.a = 1.0;
    if (channel == 4) return vec4(c.aaa, 1.0);
    c.rgb *= gain;
    vec4 o = b ? OCIODisplayB(c) : OCIODisplay(c);
    if (!b) o.rgb = coreplayGrade(o.rgb);  // the grade: on A, on the picture as shown
    // Solo AFTER the view / LUT: the channel of the picture as shown (as BunnyLab does).
    if (channel >= 1 && channel <= 3) { float v = o[channel - 1]; o = vec4(v, v, v, 1.0); }
    return vec4(o.rgb, 1.0);
}
vec4 shadeA(vec2 t) { return shade(texture(img, t), nch, false); }
vec4 shadeB(vec2 t) {
    vec2 b = (t - 0.5) * bScale + 0.5;
    if (nchB == 0 || b.x < 0.0 || b.x > 1.0 || b.y < 0.0 || b.y > 1.0) return vec4(0.0, 0.0, 0.0, 1.0);
    return shade(texture(imgB, b), nchB, true);
}
)";

const char* kFragmentMain = R"(
void main() { outColor = compareColour(uv); }
)";

GLenum glFormat(int ch) {
    switch (ch) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        default: return GL_RGBA;
    }
}

GLenum glInternal(const Frame& f) {
    static const GLenum u8[] = {GL_R8, GL_RG8, GL_RGB8, GL_RGBA8};
    static const GLenum u16[] = {GL_R16, GL_RG16, GL_RGB16, GL_RGBA16};
    static const GLenum f16[] = {GL_R16F, GL_RG16F, GL_RGB16F, GL_RGBA16F};
    static const GLenum f32[] = {GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F};
    const int i = std::clamp(f.channels, 1, 4) - 1;
    switch (f.type) {
        case PixelType::U8: return u8[i];
        case PixelType::U16: return u16[i];
        case PixelType::F16: return f16[i];
        case PixelType::F32: return f32[i];
    }
    return GL_RGBA8;
}

GLenum glType(PixelType t) {
    switch (t) {
        case PixelType::U8: return GL_UNSIGNED_BYTE;
        case PixelType::U16: return GL_UNSIGNED_SHORT;
        case PixelType::F16: return GL_HALF_FLOAT;
        case PixelType::F32: return GL_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}
}  // namespace

SeqView::SeqView(QWidget* parent) : QOpenGLWidget(parent), ocio_(std::make_unique<OcioState>()) {
    setFocusPolicy(Qt::NoFocus);
    label_ = new QLabel(this);
    label_->setAlignment(Qt::AlignCenter);
    label_->setWordWrap(true);
    label_->setAttribute(Qt::WA_TransparentForMouseEvents);
    QPalette pal = label_->palette();
    pal.setColor(QPalette::WindowText, QColor(170, 170, 170));
    label_->setPalette(pal);
    QFont f = label_->font();
    f.setPixelSize(22);
    f.setBold(true);
    label_->setFont(f);
    label_->hide();
    try {
        // $OCIO wins when it is set (studio config); else the config built into OCIO.
        if (qEnvironmentVariableIsSet("OCIO")) {
            ocio_->config = OCIO::Config::CreateFromEnv();
            configName_ = qEnvironmentVariable("OCIO");
        } else {
            ocio_->config = OCIO::Config::CreateFromFile("ocio://studio-config-latest");
            configName_ = "OCIO built-in studio config";
        }
        // Display / view: the user's last choice; else a view WITHOUT tone mapping, so
        // a PNG or JPEG looks exactly like the file (the Nuke and RV default); else the
        // config's own default.
        QSettings st;
        display_ = st.value("colour/display").toString();
        if (!displays().contains(display_)) display_ = ocio_->config->getDefaultDisplay();
        const QStringList vs = views(display_);
        view_ = st.value("colour/view").toString();
        if (!vs.contains(view_)) {
            view_ = ocio_->config->getDefaultView(display_.toUtf8().constData());
            for (const char* n : {"Un-tone-mapped", "Standard", "Raw"})
                if (vs.contains(n)) { view_ = n; break; }
        }
        if (auto cs = ocio_->config->getColorSpace("scene_linear")) input_ = cs->getName();
    } catch (const OCIO::Exception& e) {
        configName_ = QString("OCIO error: %1").arg(e.what());
    }
}

SeqView::~SeqView() {
    makeCurrent();
    releaseLuts();
    if (tex_) glDeleteTextures(1, &tex_);
    if (texB_) glDeleteTextures(1, &texB_);
    if (pbo_[0]) glDeleteBuffers(2, pbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    doneCurrent();
}

QStringList SeqView::colorSpaces() const {
    QStringList out;
    if (!ocio_->config) return out;
    for (int i = 0; i < ocio_->config->getNumColorSpaces(); ++i)
        out << ocio_->config->getColorSpaceNameByIndex(i);
    return out;
}

QStringList SeqView::displays() const {
    QStringList out;
    if (!ocio_->config) return out;
    for (int i = 0; i < ocio_->config->getNumDisplays(); ++i) out << ocio_->config->getDisplay(i);
    return out;
}

QStringList SeqView::views(const QString& display) const {
    QStringList out;
    if (!ocio_->config) return out;
    const QByteArray d = display.toUtf8();
    for (int i = 0; i < ocio_->config->getNumViews(d.constData()); ++i)
        out << ocio_->config->getView(d.constData(), i);
    return out;
}

QString SeqView::defaultInputFor(const Frame& f) const {
    if (!ocio_->config) return {};
    auto has = [&](const QString& n) -> QString {
        if (n.isEmpty()) return {};
        auto cs = ocio_->config->getColorSpace(n.toUtf8().constData());
        return cs ? QString(cs->getName()) : QString();
    };
    // 1. The file's own tag, when this config knows that name.
    if (QString n = has(f.colorSpace); !n.isEmpty()) return n;
    // 2. Float data is scene-linear; integer data is display-referred sRGB.
    if (f.isFloat()) return has("scene_linear");
    for (const char* n : {"sRGB - Texture", "srgb_tx", "sRGB", "color_picking"})
        if (QString r = has(n); !r.isEmpty()) return r;
    return has("default");
}

void SeqView::setLut(const LutChoice& lut) {
    if (lut == lut_) return;
    lut_ = lut;
    programDirty_ = true;
    update();
}

void SeqView::setInputSpace(const QString& cs) {
    if (cs == input_) return;
    input_ = cs;
    programDirty_ = true;
    update();
}

void SeqView::setDisplayView(const QString& display, const QString& view) {
    display_ = display;
    view_ = view;
    QSettings st;
    st.setValue("colour/display", display);
    st.setValue("colour/view", view);
    programDirty_ = true;
    update();
}

void SeqView::setFrame(FramePtr f) {
    frame_ = std::move(f);
    frameDirty_ = true;
    updateLabel();
    update();
}

void SeqView::initializeGL() {
    initializeOpenGLFunctions();
    glGenVertexArrays(1, &vao_);
    for (unsigned* t : {&tex_, &texB_}) {
        glGenTextures(1, t);
        glBindTexture(GL_TEXTURE_2D, *t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    programDirty_ = true;
}

void SeqView::releaseLuts() {
    for (auto& l : luts_) glDeleteTextures(1, &l.tex);
    luts_.clear();
}

void SeqView::buildProgram() {
    programDirty_ = false;
    releaseLuts();
    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
    ocio_->uniforms.clear();
    ocio_->shader.reset();
    ocio_->shaderB.reset();

    // One OCIO GPU function per picture: A always; B only while comparing (its own LUT).
    auto colourFunction = [&](const LutChoice& l, const char* fn, const char* prefix,
                              OCIO::GpuShaderDescRcPtr& out) -> std::string {
        std::string fallback = std::string("vec4 ") + fn +
                               "(vec4 inPixel) { return vec4(pow(max(inPixel.rgb, 0.0), vec3(1.0/2.2)), inPixel.a); }\n";
        if (!ocio_->config || (input_.isEmpty() && l.kind == LutChoice::None)) return fallback;
        try {
            OCIO::ConstProcessorRcPtr proc;
            if (l.kind != LutChoice::None) {
                proc = ::Lut::processor(l);
            } else {
                auto t = OCIO::DisplayViewTransform::Create();
                t->setSrc(input_.toUtf8().constData());
                t->setDisplay(display_.toUtf8().constData());
                t->setView(view_.toUtf8().constData());
                proc = ocio_->config->getProcessor(t);
            }
            auto gpu = proc->getDefaultGPUProcessor();
            auto desc = OCIO::GpuShaderDesc::CreateShaderDesc();
            desc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_4_0);
            desc->setFunctionName(fn);
            desc->setResourcePrefix(prefix);
            gpu->extractGpuShaderInfo(desc);
            out = desc;
            return desc->getShaderText();
        } catch (const std::exception& e) {
            emit colourError(QString("Colour transform failed: %1").arg(e.what()));
            return fallback;
        }
    };
    std::string ocioText = colourFunction(lut_, "OCIODisplay", "ocio_", ocio_->shader);
    if (cmp_.mode != CompareMode::Off) ocioText += colourFunction(lutB_, "OCIODisplayB", "ocioB_", ocio_->shaderB);
    else ocioText += "vec4 OCIODisplayB(vec4 c) { return OCIODisplay(c); }\n";

    auto compile = [&](GLenum type, const std::string& src) {
        GLuint s = glCreateShader(type);
        const char* p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            emit colourError(QString("Shader error: %1").arg(log));
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER,
                              std::string(kFragmentHead) + Grade::uniformsGlsl() + Grade::glslFunction() + ocioText + kFragmentShade + compareGlsl() + kFragmentMain);
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs);
    glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glUseProgram(prog_);
    glUniform1i(glGetUniformLocation(prog_, "img"), 0);
    glUniform1i(glGetUniformLocation(prog_, "imgB"), 1);

    int unit = 2;  // 0 = A, 1 = B
    for (OCIO::GpuShaderDescRcPtr* sd : {&ocio_->shader, &ocio_->shaderB}) {
        if (!*sd) continue;
        auto& d = **sd;
        for (unsigned i = 0; i < d.getNum3DTextures(); ++i, ++unit) {
            const char* texName = nullptr;
            const char* sampler = nullptr;
            unsigned edge = 0;
            OCIO::Interpolation interp = OCIO::INTERP_LINEAR;
            d.get3DTexture(i, texName, sampler, edge, interp);
            const float* values = nullptr;
            d.get3DTextureValues(i, values);
            GLuint t;
            glGenTextures(1, &t);
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_3D, t);
            const GLint filt = interp == OCIO::INTERP_NEAREST ? GL_NEAREST : GL_LINEAR;
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filt);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filt);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, edge, edge, edge, 0, GL_RGB, GL_FLOAT, values);
            glUniform1i(glGetUniformLocation(prog_, sampler), unit);
            luts_.push_back({t, GL_TEXTURE_3D, sampler});
        }
        for (unsigned i = 0; i < d.getNumTextures(); ++i, ++unit) {
            const char* texName = nullptr;
            const char* sampler = nullptr;
            unsigned w = 0, h = 0;
            OCIO::GpuShaderDesc::TextureType channel = OCIO::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
            OCIO::GpuShaderDesc::TextureDimensions dims = OCIO::GpuShaderDesc::TEXTURE_2D;
            OCIO::Interpolation interp = OCIO::INTERP_LINEAR;
            d.getTexture(i, texName, sampler, w, h, channel, dims, interp);
            const float* values = nullptr;
            d.getTextureValues(i, values);
            const bool red = channel == OCIO::GpuShaderDesc::TEXTURE_RED_CHANNEL;
            const GLenum target = dims == OCIO::GpuShaderDesc::TEXTURE_1D ? GL_TEXTURE_1D : GL_TEXTURE_2D;
            GLuint t;
            glGenTextures(1, &t);
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(target, t);
            const GLint filt = interp == OCIO::INTERP_NEAREST ? GL_NEAREST : GL_LINEAR;
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filt);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filt);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const GLenum ifmt = red ? GL_R32F : GL_RGB32F;
            const GLenum fmt = red ? GL_RED : GL_RGB;
            if (target == GL_TEXTURE_1D) {
                using TexImage1D = void(QOPENGLF_APIENTRYP)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void*);
                auto texImage1D = reinterpret_cast<TexImage1D>(context()->getProcAddress("glTexImage1D"));
                texImage1D(GL_TEXTURE_1D, 0, GLint(ifmt), GLsizei(w), 0, fmt, GL_FLOAT, values);
            }
            else
                glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_FLOAT, values);
            glUniform1i(glGetUniformLocation(prog_, sampler), unit);
            luts_.push_back({t, target, sampler});
        }
        for (unsigned i = 0; i < d.getNumUniforms(); ++i) {
            OCIO::GpuShaderDesc::UniformData data;
            const char* name = d.getUniform(i, data);
            ocio_->uniforms.emplace_back(glGetUniformLocation(prog_, name), data);
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

void SeqView::uploadTexture(const Frame& f, unsigned tex, int& w, int& h, unsigned& fmtOut) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const unsigned fmt = glInternal(f);
    if (f.width != w || f.height != h || fmt != fmtOut) {
        glTexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), f.width, f.height, 0, glFormat(f.channels),
                     glType(f.type), f.data.data());
        w = f.width;
        h = f.height;
        fmtOut = fmt;
    } else if (qEnvironmentVariableIsSet("COREPLAY_NO_PBO")) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, glFormat(f.channels), glType(f.type),
                        f.data.data());
    } else {
        // Through a pixel buffer: one copy into GPU-visible memory, then the GPU pulls it
        // (DMA) while this thread goes on. Two buffers in turn; each is orphaned first, so
        // the driver never waits for the GPU to finish with the previous frame.
        if (!pbo_[0]) glGenBuffers(2, pbo_);
        const GLsizeiptr size = GLsizeiptr(f.data.size());
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[pboNext_]);
        pboNext_ ^= 1;
        glBufferData(GL_PIXEL_UNPACK_BUFFER, size, nullptr, GL_STREAM_DRAW);
        void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        if (dst) {
            // One thread: this memory is write-combined; copying it from several threads in
            // parallel was 10x slower (measured: 4K half RGBA 129-159 ms against 14 ms).
            std::memcpy(dst, f.data.data(), size_t(size));
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, glFormat(f.channels), glType(f.type), nullptr);
        } else {
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.width, f.height, glFormat(f.channels), glType(f.type),
                            f.data.data());
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

void SeqView::uploadFrame() {
    frameDirty_ = false;
    if (!frame_ || frame_->missing || !frame_->error.isEmpty() || frame_->data.empty()) return;
    QElapsedTimer t;
    t.start();
    uploadTexture(*frame_, tex_, texW_, texH_, texFmt_);
    uploadNs += t.nsecsElapsed();
    ++uploads;
}

void SeqView::setFrameB(FramePtr f) {
    frameB_ = std::move(f);
    frameBDirty_ = true;
    update();
}

void SeqView::setLutB(const LutChoice& lut) {
    if (lut == lutB_) return;
    lutB_ = lut;
    programDirty_ = true;
    update();
}

void SeqView::setCompare(const CompareView& c) {
    if (c == cmp_) return;
    if ((c.mode == CompareMode::Off) != (cmp_.mode == CompareMode::Off)) programDirty_ = true;  // B's colour function
    cmp_ = c;
    if (c.mode == CompareMode::Off) frameB_.reset();
    update();
}

void SeqView::paintGL() {
    QElapsedTimer paintClock;
    paintClock.start();
    struct Done { SeqView* v; QElapsedTimer& t; ~Done() { v->paintNs += t.nsecsElapsed(); ++v->paints; } } done{this, paintClock};
    const qreal dpr = devicePixelRatioF();
    const int vw = int(width() * dpr), vh = int(height() * dpr);
    glViewport(0, 0, vw, vh);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (programDirty_) buildProgram();
    if (frameDirty_) uploadFrame();
    if (frameBDirty_) {
        frameBDirty_ = false;
        if (frameB_ && !frameB_->missing && frameB_->error.isEmpty() && !frameB_->data.empty()) {
            uploadTexture(*frameB_, texB_, texBW_, texBH_, texBFmt_);
        }
    }
    const bool comparing = cmp_.mode != CompareMode::Off;
    const bool bOk = comparing && frameB_ && !frameB_->missing && frameB_->error.isEmpty() && texBW_ > 0;

    const bool drawable = frame_ && !frame_->missing && frame_->error.isEmpty() && texW_ > 0;
    if (drawable) {
        glUseProgram(prog_);
        int unit = 2;
        for (const auto& l : luts_) {
            glActiveTexture(GL_TEXTURE0 + unit++);
            glBindTexture(l.target, l.tex);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texB_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_);
        for (auto& [loc, data] : ocio_->uniforms) {
            if (loc < 0) continue;
            switch (data.m_type) {
                case OCIO::UNIFORM_DOUBLE: glUniform1f(loc, float(data.m_getDouble())); break;
                case OCIO::UNIFORM_BOOL: glUniform1i(loc, data.m_getBool() ? 1 : 0); break;
                case OCIO::UNIFORM_FLOAT3: {
                    const auto& v = data.m_getFloat3();
                    glUniform3f(loc, v[0], v[1], v[2]);
                    break;
                }
                case OCIO::UNIFORM_VECTOR_FLOAT:
                    glUniform1fv(loc, data.m_vectorFloat.m_getSize(), data.m_vectorFloat.m_getVector());
                    break;
                case OCIO::UNIFORM_VECTOR_INT:
                    glUniform1iv(loc, data.m_vectorInt.m_getSize(), data.m_vectorInt.m_getVector());
                    break;
                default: break;
            }
        }
        // Fit the picture in the view (keeping its aspect), then pan & scan.
        // Side by Side draws a picture twice as wide: A beside B.
        const double pw = cmp_.mode == CompareMode::SideBySide ? 2.0 * texW_ : texW_;
        const bool quarter = (std::abs(xf_.rotate) / 90) % 2 == 1;
        const double iw = quarter ? texH_ : pw, ih = quarter ? pw : texH_;
        const double fit = std::min(vw / iw, vh / ih);
        const double hx = pw * 0.5 * fit * xf_.zoom * xf_.scaleX;
        const double hy = texH_ * 0.5 * fit * xf_.zoom * xf_.scaleY;
        const double a = -xf_.rotate * std::numbers::pi / 180.0;  // clockwise
        const float rot[4] = {float(std::cos(a)), float(std::sin(a)), float(-std::sin(a)), float(std::cos(a))};
        glUniform2f(glGetUniformLocation(prog_, "halfImg"), float(hx), float(hy));
        glUniform2f(glGetUniformLocation(prog_, "halfView"), vw * 0.5f, vh * 0.5f);
        glUniform2f(glGetUniformLocation(prog_, "offset"), float(xf_.panX * 2.0), float(-xf_.panY * 2.0));
        glUniformMatrix2fv(glGetUniformLocation(prog_, "rot"), 1, GL_FALSE, rot);
        glUniform1f(glGetUniformLocation(prog_, "gain"), float(std::pow(2.0, exposure_)));
        glUniform1i(glGetUniformLocation(prog_, "channel"), channel_);
        float gv[Grade::kUniforms];
        grade_.uniformValues(gv);
        for (int i = 0; i < Grade::kUniforms; ++i) glUniform1f(glGetUniformLocation(prog_, Grade::uniformNames()[i]), gv[i]);
        glUniform1i(glGetUniformLocation(prog_, "nch"), frame_->channels);
        glUniform1i(glGetUniformLocation(prog_, "nchB"), bOk ? frameB_->channels : 0);
        float bsx = 1.f, bsy = 1.f;  // B fitted inside A's frame
        if (bOk) {
            const double ra = double(texW_) / texH_, rb = double(texBW_) / texBH_;
            if (rb > ra) bsy = float(rb / ra);
            else bsx = float(ra / rb);
        }
        glUniform2f(glGetUniformLocation(prog_, "bScale"), bsx, bsy);
        glUniform1i(glGetUniformLocation(prog_, "cmpMode"), int(cmp_.mode));
        glUniform1i(glGetUniformLocation(prog_, "cmpSwap"), cmp_.swap ? 1 : 0);
        glUniform1f(glGetUniformLocation(prog_, "cmpWipe"), cmp_.wipe);
        glUniform1f(glGetUniformLocation(prog_, "cmpLine"), float(0.75 / std::max(1.0, 2.0 * hx)));
        lastHx_ = hx; lastHy_ = hy; lastOffX_ = xf_.panX * 2.0; lastOffY_ = -xf_.panY * 2.0;
        lastAngle_ = a; lastVw_ = vw; lastVh_ = vh;
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }
}

// Text over the picture (missing frame, unreadable frame, loading) is a plain
// label: QPainter text on a core-profile GL surface comes out garbled.
void SeqView::updateLabel() {
    QString text = overlay_;
    if (frame_ && frame_->missing) text = "MISSING FRAME";
    else if (frame_ && !frame_->error.isEmpty()) text = "CANNOT READ FRAME\n" + frame_->error;
    label_->setText(text);
    label_->setVisible(!text.isEmpty());
    label_->setGeometry(rect());
}

void SeqView::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    label_->setGeometry(rect());
}

void SeqView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    afterDouble_ = true;  // the release that follows is not a click
    emit doubleClicked();
}

// The mouse position as a 0..1 position across the drawn picture (the wipe line).
float SeqView::wipeAt(QPointF pos) const {
    const qreal dpr = devicePixelRatioF();
    const double nx = pos.x() * dpr / (lastVw_ * 0.5) - 1.0 - lastOffX_;
    const double ny = 1.0 - pos.y() * dpr / (lastVh_ * 0.5) - lastOffY_;
    const double px = nx * lastVw_ * 0.5, py = ny * lastVh_ * 0.5;
    // Undo the rotation (rot is a column-major [cos, sin; -sin, cos]).
    const double c = std::cos(lastAngle_), s = std::sin(lastAngle_);
    const double lx = c * px + s * py;
    return float(std::clamp((lastHx_ > 0 ? lx / lastHx_ : 0.0) * 0.5 + 0.5, 0.0, 1.0));
}

void SeqView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && cmp_.mode == CompareMode::Wipe) {
        wipeDrag_ = false;
        e->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(e);
}

void SeqView::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && cmp_.mode == CompareMode::Wipe) {
        wipeDrag_ = true;
        cmp_.wipe = wipeAt(e->position());
        update();
        emit wipeMoved(cmp_.wipe);
        return;
    }
    QOpenGLWidget::mouseMoveEvent(e);
}

void SeqView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && wipeDrag_) {  // a wipe drag is not a click
        wipeDrag_ = false;
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (!afterDouble_) emit clicked();
        afterDouble_ = false;
    }
    else if (e->button() == Qt::RightButton) emit contextMenu(e->globalPosition().toPoint());
    else e->ignore();  // back/forward buttons go to the main window
}

