#include "MpvEngine.h"

#include <QCoreApplication>
#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMap>
#include <QMouseEvent>
#include <QRegularExpression>
#include <numbers>
#include <QOpenGLContext>
#include <QWheelEvent>
#include <cmath>
#include <thread>

namespace {
void* getProcAddress(void*, const char* name) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void*>(ctx->getProcAddress(name)) : nullptr;
}

// Turns mpv's double fps into an exact rational for the common NTSC rates.
Rational toRational(double fps) {
    if (fps <= 0) return {};
    const double ntsc = fps * 1001.0 / 1000.0;
    if (std::abs(ntsc - std::round(ntsc)) < 1e-3 && std::abs(fps - std::round(fps)) > 1e-3)
        return {int(std::round(ntsc)) * 1000, 1001};
    if (std::abs(fps - std::round(fps)) < 1e-3) return {int(std::round(fps)), 1};
    return {int(std::round(fps * 1000)), 1000};
}
}  // namespace

// ---------------------------------------------------------------- MpvView

namespace {
// The same fit / zoom / pan / rotation as the sequence viewer.
const char* kCmpVertex = R"(#version 410 core
uniform vec2 halfImg;
uniform vec2 halfView;
uniform vec2 offset;
uniform mat2 rot;
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
    uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    gl_Position = vec4(rot * (p * halfImg) / halfView + offset, 0.0, 1.0);
}
)";
// A is the left half of mpv's packed frame, B the right half.
const char* kCmpFragmentHead = R"(#version 410 core
in vec2 uv;
out vec4 outColor;
uniform sampler2D frame2;
uniform float halfTexel;
vec4 shadeA(vec2 t) { return vec4(texture(frame2, vec2(clamp(t.x * 0.5, 0.0, 0.5 - halfTexel), t.y)).rgb, 1.0); }
vec4 shadeB(vec2 t) { return vec4(texture(frame2, vec2(clamp(0.5 + t.x * 0.5, 0.5 + halfTexel, 1.0), t.y)).rgb, 1.0); }
)";
const char* kCmpFragmentMain = R"(
void main() { outColor = compareColour(uv); }
)";
}  // namespace

MpvView::MpvView(mpv_handle* mpv, QWidget* parent) : QOpenGLWidget(parent), mpv_(mpv) {
    setFocusPolicy(Qt::NoFocus);
    // Tell mpv when a frame really reached the screen: its frame timing follows the
    // display instead of guessing.
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        if (render_) mpv_render_context_report_swap(render_);
    });
}

MpvView::~MpvView() {
    makeCurrent();
    if (glReady_) {
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (fboTex_) glDeleteTextures(1, &fboTex_);
        if (prog_) glDeleteProgram(prog_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
    }
    if (render_) {
        // Stop mpv's render thread from posting updates to this object while it is
        // being destroyed, then free (which waits for the renderer).
        mpv_render_context_set_update_callback(render_, nullptr, nullptr);
        mpv_render_context_free(render_);
        render_ = nullptr;
    }
    doneCurrent();
}

void MpvView::initializeGL() {
    initializeOpenGLFunctions();
    glReady_ = true;
    // Software OpenGL (no usable GPU): mpv's high-quality scalers run on the CPU there.
    // Bilinear scaling and no extra passes (mpv's "fast" profile) made 1080p movies play
    // with 0 dropped frames in 5 s instead of ~60.
    const QByteArray renderer(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    software_ = renderer.contains("llvmpipe") || renderer.contains("softpipe") || renderer.contains("GDI Generic") ||
                renderer.contains("Basic Render");
    if (software_) {
        for (auto [k, v] : {std::pair{"scale", "bilinear"}, {"dscale", "bilinear"}, {"cscale", "bilinear"},
                            {"dither", "no"}, {"correct-downscaling", "no"}, {"linear-downscaling", "no"},
                            {"sigmoid-upscaling", "no"}, {"deband", "no"}, {"hdr-compute-peak", "no"}})
            mpv_set_property_string(mpv_, k, v);
    }
    mpv_opengl_init_params gl{getProcAddress, nullptr};
    // No MPV_RENDER_PARAM_ADVANCED_CONTROL: with it, any synchronous libmpv call on
    // this (the GUI) thread can deadlock against the renderer.
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (mpv_render_context_create(&render_, mpv_, params) < 0) {
        render_ = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(render_, &MpvView::onUpdate, this);
}

void MpvView::onUpdate(void* ctx) {
    // Called on an mpv thread.
    auto* self = static_cast<MpvView*>(ctx);
    QMetaObject::invokeMethod(self, [self] {
        if (self->render_ && (mpv_render_context_update(self->render_) & MPV_RENDER_UPDATE_FRAME))
            self->update();
    }, Qt::QueuedConnection);
}

void MpvView::setCompare(const CompareView& c, QSize packed) {
    cmp_ = c;
    packed_ = packed;
    update();
}

void MpvView::paintGL() {
    if (!render_) return;
    if (cmp_.mode != CompareMode::Off && packed_.width() > 1 && packed_.height() > 0) {
        paintCompare();
        return;
    }
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * dpr), int(height() * dpr), 0};
    int flip = 1;
    // Do not block until the frame's display time: Qt waits for vsync when it shows the
    // window, and the two waits added up (measured, 4K60 CPU-decoded: ~27 dropped frames
    // in 5 s with the block, ~13 without; with nvdec 0-1 against 0).
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(render_, params);
}

// Compare: mpv renders the packed frame at its own size into a texture; the shader
// then shows A / B / wipe / side by side / difference with the viewer's transform.
void MpvView::paintCompare() {
    if (!prog_) {
        auto compile = [this](GLenum type, const std::string& src) {
            GLuint s = glCreateShader(type);
            const char* p = src.c_str();
            glShaderSource(s, 1, &p, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048];
                glGetShaderInfoLog(s, sizeof log, nullptr, log);
                qWarning("Coreplay compare shader: %s", log);
            }
            return s;
        };
        const GLuint vs = compile(GL_VERTEX_SHADER, kCmpVertex);
        const GLuint fs = compile(GL_FRAGMENT_SHADER, std::string(kCmpFragmentHead) + compareGlsl() + kCmpFragmentMain);
        prog_ = glCreateProgram();
        glAttachShader(prog_, vs);
        glAttachShader(prog_, fs);
        glLinkProgram(prog_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glGenVertexArrays(1, &vao_);
    }
    if (fboSize_ != packed_) {
        if (!fbo_) glGenFramebuffers(1, &fbo_);
        if (!fboTex_) glGenTextures(1, &fboTex_);
        glBindTexture(GL_TEXTURE_2D, fboTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, packed_.width(), packed_.height(), 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex_, 0);
        fboSize_ = packed_;
    }
    mpv_opengl_fbo target{int(fbo_), packed_.width(), packed_.height(), GL_RGBA16F};
    int flip = 0;  // top row first in the texture, as the shader's uv expects
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &target},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(render_, params);

    const qreal dpr = devicePixelRatioF();
    const int vw = int(width() * dpr), vh = int(height() * dpr);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, vw, vh);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    const double aw = packed_.width() / 2.0, ah = packed_.height();
    const double pw = cmp_.mode == CompareMode::SideBySide ? 2.0 * aw : aw;
    const bool quarter = (std::abs(xf_.rotate) / 90) % 2 == 1;
    const double iw = quarter ? ah : pw, ih = quarter ? pw : ah;
    const double fit = std::min(vw / iw, vh / ih);
    const double hx = pw * 0.5 * fit * xf_.zoom * xf_.scaleX, hy = ah * 0.5 * fit * xf_.zoom * xf_.scaleY;
    const double a = -xf_.rotate * std::numbers::pi / 180.0;
    const float rot[4] = {float(std::cos(a)), float(std::sin(a)), float(-std::sin(a)), float(std::cos(a))};
    glUseProgram(prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fboTex_);
    glUniform1i(glGetUniformLocation(prog_, "frame2"), 0);
    glUniform1f(glGetUniformLocation(prog_, "halfTexel"), float(0.5 / packed_.width()));
    glUniform2f(glGetUniformLocation(prog_, "halfImg"), float(hx), float(hy));
    glUniform2f(glGetUniformLocation(prog_, "halfView"), vw * 0.5f, vh * 0.5f);
    glUniform2f(glGetUniformLocation(prog_, "offset"), float(xf_.panX * 2.0), float(-xf_.panY * 2.0));
    glUniformMatrix2fv(glGetUniformLocation(prog_, "rot"), 1, GL_FALSE, rot);
    glUniform1i(glGetUniformLocation(prog_, "cmpMode"), int(cmp_.mode));
    glUniform1i(glGetUniformLocation(prog_, "cmpSwap"), cmp_.swap ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog_, "cmpWipe"), cmp_.wipe);
    glUniform1f(glGetUniformLocation(prog_, "cmpLine"), float(0.75 / std::max(1.0, 2.0 * hx)));
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    lastHx_ = hx; lastOffX_ = xf_.panX * 2.0; lastOffY_ = -xf_.panY * 2.0; lastAngle_ = a; lastVw_ = vw; lastVh_ = vh;
}

float MpvView::wipeAt(QPointF pos) const {
    const qreal dpr = devicePixelRatioF();
    const double nx = pos.x() * dpr / (lastVw_ * 0.5) - 1.0 - lastOffX_;
    const double ny = 1.0 - pos.y() * dpr / (lastVh_ * 0.5) - lastOffY_;
    const double px = nx * lastVw_ * 0.5, py = ny * lastVh_ * 0.5;
    const double c = std::cos(lastAngle_), s = std::sin(lastAngle_);
    return float(std::clamp((lastHx_ > 0 ? (c * px + s * py) / lastHx_ : 0.0) * 0.5 + 0.5, 0.0, 1.0));
}

void MpvView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && cmp_.mode == CompareMode::Wipe) {
        wipeDrag_ = false;
        e->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(e);
}

void MpvView::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && cmp_.mode == CompareMode::Wipe) {
        wipeDrag_ = true;
        cmp_.wipe = wipeAt(e->position());
        update();
        emit wipeMoved(cmp_.wipe);
        return;
    }
    QOpenGLWidget::mouseMoveEvent(e);
}

void MpvView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    afterDouble_ = true;  // the release that follows is not a click
    emit doubleClicked();
}

void MpvView::mouseReleaseEvent(QMouseEvent* e) {
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


// ---------------------------------------------------------------- MpvEngine

MpvEngine::MpvEngine(QWidget* parentWidget, QObject* parent) : Engine(parent) {
    mpv_ = mpv_create();
    auto opt = [&](const char* k, const char* v) { mpv_set_option_string(mpv_, k, v); };
    opt("vo", "libmpv");
    // Test runs are silent: the test clips carry 440 / 660 Hz tones, and a run played them
    // on the speakers of whoever sat at the PC. Sound is still decoded and timed (ao=null
    // keeps the audio clock), so audio checks stay real. COREPLAY_TEST_AUDIO=1 plays it.
    if (QCoreApplication::organizationName() == "CoreplayTest" && !qEnvironmentVariableIsSet("COREPLAY_TEST_AUDIO")) opt("ao", "null");
    opt("hwdec", "auto-safe");
    opt("keep-open", "yes");          // stay on the last frame; frame-back-step still works
    opt("idle", "yes");
    opt("osc", "no");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("hr-seek", "yes");            // exact seeks
    opt("hr-seek-framedrop", "no");
    opt("config", "no");              // do not read a user mpv.conf: Coreplay owns the settings
    opt("terminal", "no");
    opt("prefetch-playlist", "yes");  // gapless: the next file is opened before this one ends
    opt("ytdl", "no");                // web-video hooks held every file change for ~0.4 s
    opt("load-scripts", "no");        // no built-in Lua scripts: Coreplay draws its own UI
    opt("gapless-audio", "weak");
    // Diagnostics: COREPLAY_MPV_LOG=<file> writes the player's full mpv log (as
    // COREPLAY_EXPORT_LOG does for exports).
    if (qEnvironmentVariableIsSet("COREPLAY_MPV_LOG")) {
        opt("log-file", qEnvironmentVariable("COREPLAY_MPV_LOG").toUtf8().constData());
        opt("msg-level", "all=v");
    }
    mpv_initialize(mpv_);
    mpv_request_log_messages(mpv_, "warn");  // filter and decoder problems reach the OSD

    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "container-fps", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "dwidth", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, 0, "dheight", MPV_FORMAT_INT64);
    mpv_observe_property(mpv_, 0, "chapter-list", MPV_FORMAT_NONE);
    mpv_set_wakeup_callback(mpv_, [](void* ctx) {
        auto* self = static_cast<MpvEngine*>(ctx);
        QMetaObject::invokeMethod(self, [self] { self->drainEvents(); }, Qt::QueuedConnection);
    }, this);

    view_ = new MpvView(mpv_, parentWidget);
}

MpvEngine::~MpvEngine() {
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    delete view_;  // frees the render context first, with its GL context current
    // Terminate on a thread of its own: on Windows, libmpv's teardown un-initializes
    // COM on the calling thread one time too many. On the GUI thread that removed
    // Qt's own COM apartment, and Qt then crashed at exit releasing its COM objects
    // (found with a COM probe: initialized before this call, gone after it).
    std::thread([h = mpv_] { mpv_terminate_destroy(h); }).join();
    mpv_ = nullptr;
}

void MpvEngine::command(const QStringList& args) {
    std::vector<QByteArray> bytes;
    for (const auto& a : args) bytes.push_back(a.toUtf8());
    std::vector<const char*> argv;
    for (const auto& b : bytes) argv.push_back(b.constData());
    argv.push_back(nullptr);
    mpv_command_async(mpv_, 0, argv.data());
}

// Asynchronous, like command(): a synchronous set on the GUI thread waits for mpv's core,
// and the core can be waiting for this thread to draw (at a file change that stalled
// playback ~0.2 s per call). mpv copies the value; requests keep their order.
void MpvEngine::setProp(const char* name, const QString& value) {
    const QByteArray v = value.toUtf8();
    const char* data = v.constData();
    mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_STRING, &data);
}

QString MpvEngine::getString(const char* name) const {
    char* s = mpv_get_property_string(mpv_, name);
    if (!s) return {};
    QString out = QString::fromUtf8(s);
    mpv_free(s);
    return out;
}

void MpvEngine::drainEvents() {
    while (mpv_) {
        mpv_event* e = mpv_wait_event(mpv_, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* p = static_cast<mpv_event_property*>(e->data);
                const QByteArray name(p->name);
                const bool has = p->format != MPV_FORMAT_NONE && p->data;
                if (name == "time-pos") {
                    pos_ = has ? *static_cast<double*>(p->data) : 0.0;
                    emit positionChanged(pos_);
                } else if (name == "duration") {
                    dur_ = has ? *static_cast<double*>(p->data) : 0.0;
                    emit durationChanged(dur_);
                } else if (name == "pause") {
                    paused_ = has && *static_cast<int*>(p->data);
                    emit pausedChanged(paused_);
                } else if (name == "eof-reached") {
                    if (has && *static_cast<int*>(p->data)) emit endReached();
                } else if (name == "chapter-list") {
                    emit chaptersChanged();
                } else if (name == "container-fps") {
                    const double f = has ? *static_cast<double*>(p->data) : 0.0;
                    if (f > 0 || !comparing()) fps_ = f;  // the compare graph has no rate of its own: keep A's
                } else if (name == "dwidth" || name == "dheight") {
                    // Read both: mpv reports a change only against the last value it
                    // reported, and merges quick changes. Two fast file changes from the
                    // A|B frame (1280x360) to a 1280x720 clip reported only the height,
                    // and the width stayed unknown (picture size -1).
                    int64_t w = 0, h = 0;
                    mpv_get_property(mpv_, "dwidth", MPV_FORMAT_INT64, &w);
                    mpv_get_property(mpv_, "dheight", MPV_FORMAT_INT64, &h);
                    const QSize was = size_;
                    size_ = QSize(int(w), int(h));
                    syncCompareView();
                    if (size_ != was && !size_.isEmpty()) emit mediaSizeChanged();
                }
                break;
            }
            case MPV_EVENT_LOG_MESSAGE: {
                auto* m = static_cast<mpv_event_log_message*>(e->data);
                const QString text = QString::fromUtf8(m->text).trimmed();
                // Show the errors a user can act on (a shader or filter that fails, a
                // file that cannot open). The renderer's format probing reports harmless
                // OpenGL errors on some drivers; those stay in lastLog only.
                const QByteArray prefix(m->prefix);
                const bool actionable = prefix == "vf" || prefix == "lavfi" || prefix == "ffmpeg" || prefix == "cplayer" ||
                                        prefix == "user_shader";
                // Hardware-decoding probes (no NVIDIA driver, no VA-API...): mpv goes on with
                // software decoding by itself, so nothing for the user to act on.
                static const QRegularExpression hwProbe(
                    "cuda|nvdec|cuvid|vaapi|libva|vdpau|vulkan|d3d11|dxva|qsv|v4l2|hwaccel|hwdec|hardware",
                    QRegularExpression::CaseInsensitiveOption);
                // "NULL: ..." comes from ffmpeg's bitstream parsers (no component name): they
                // skip damaged or empty data and go on ("missing picture in access unit" on
                // an ordinary H.264 movie). The movie plays; showing it read as a failure.
                const bool parserNote = prefix == "ffmpeg" && text.startsWith("NULL:");
                if (m->log_level <= MPV_LOG_LEVEL_ERROR && actionable && !parserNote && !text.isEmpty() &&
                    !text.contains(hwProbe))
                    emit message(QString("%1: %2").arg(QString::fromUtf8(m->prefix), text));
                lastLog_ = QString("[%1] %2").arg(QString::fromUtf8(m->prefix), text);
                break;
            }
            case MPV_EVENT_START_FILE:
                if (expectStarts_ > 0) {
                    --expectStarts_;
                } else if (!next_.isEmpty()) {  // mpv went on to the queued file by itself
                    const QString p = next_;
                    next_.clear();
                    emit advanced(p);
                }
                break;
            case MPV_EVENT_FILE_LOADED:
                emit loaded();
                break;
            case MPV_EVENT_END_FILE: {
                auto* ef = static_cast<mpv_event_end_file*>(e->data);
                if (ef->reason == MPV_END_FILE_REASON_ERROR)
                    emit message(tr("Cannot play: %1").arg(mpv_error_string(ef->error)));
                break;
            }
            default:
                break;
        }
    }
}

bool MpvEngine::open(const QString& path) {
    if (comparing() || cmpLeaving_) {
        cmpPath_.clear();
        cmpLeaving_ = false;
        view_->setCompare(CompareView{}, {});
    }
    pos_ = dur_ = fps_ = 0;
    size_ = {};
    next_.clear();
    ++expectStarts_;
    command({"loadfile", QFileInfo(path).absoluteFilePath(), "replace"});  // also empties the queue
    setPaused(false);
    return true;
}

void MpvEngine::close() {
    next_.clear();
    command({"stop"});
}

void MpvEngine::setNext(const QString& path) {
    if (comparing() && !path.isEmpty()) return;  // no go-on while comparing
    if (path == next_) return;
    next_ = path;
    command({"playlist-clear"});  // keeps the current file
    if (!path.isEmpty()) command({"loadfile", QFileInfo(path).absoluteFilePath(), "append"});
}

// paused_ takes the new state at once; mpv's own report (the "pause" observer) confirms it.
// Waiting for that report left a gap: compare started right after a file opened read the
// old "paused" and reopened the movie paused (t22, about 1 run in 5).
void MpvEngine::setPaused(bool p) {
    paused_ = p;
    setProp("pause", p ? "yes" : "no");
}

void MpvEngine::stop() {
    setPaused(true);
    command({"seek", "0", "absolute+exact"});
}

void MpvEngine::seekAbsolute(double s) { command({"seek", QString::number(std::max(0.0, s), 'f', 6), "absolute+exact"}); }
void MpvEngine::seekRelative(double s) { command({"seek", QString::number(s, 'f', 6), "relative+exact"}); }
void MpvEngine::seekKeyframe(int dir) { command({"seek", dir > 0 ? "0.05" : "-0.05", "relative+keyframes"}); }
void MpvEngine::frameStep(int dir) { command({dir >= 0 ? "frame-step" : "frame-back-step"}); }

Rational MpvEngine::fps() const { return toRational(fps_); }

// mpv sends no change events for estimated-frame-number; time-pos x frame rate is
// the same number for a constant-rate file.
qint64 MpvEngine::frameNumber() const {
    if (fps_ <= 0 || dur_ <= 0) return -1;
    return std::llround(pos_ * fps_);
}

void MpvEngine::setSpeed(double s) {
    speed_ = std::clamp(s, 1.0 / 16.0, 16.0);
    setProp("speed", QString::number(speed_));
}

void MpvEngine::setVolume(int v) { setProp("volume", QString::number(std::clamp(v, 0, 100))); }
void MpvEngine::setMuted(bool m) { setProp("mute", m ? "yes" : "no"); }
void MpvEngine::adjustAudioDelay(double d) { command({"add", "audio-delay", QString::number(d)}); }
void MpvEngine::cycleAudio(int dir) { command({"cycle", "aid", dir >= 0 ? "up" : "down"}); }
void MpvEngine::cycleSubtitle(int dir) { command({"cycle", "sid", dir >= 0 ? "up" : "down"}); }
void MpvEngine::toggleSubtitles() { command({"cycle", "sub-visibility"}); }
void MpvEngine::adjustSubDelay(double d) { command({"add", "sub-delay", QString::number(d)}); }
void MpvEngine::adjustSubPos(int d) { command({"add", "sub-pos", QString::number(d)}); }
void MpvEngine::adjustSubScale(double d) { command({"add", "sub-scale", QString::number(d)}); }
void MpvEngine::gotoSub(int dir) { command({"sub-seek", QString::number(dir >= 0 ? 1 : -1)}); }

bool MpvEngine::chapterStep(int dir) {
    int64_t n = 0;
    if (mpv_get_property(mpv_, "chapters", MPV_FORMAT_INT64, &n) < 0 || n <= 0) return false;
    command({"add", "chapter", dir >= 0 ? "1" : "-1"});
    return true;
}

QList<Chapter> MpvEngine::chapters() const {
    QList<Chapter> out;
    int64_t n = 0;
    if (mpv_get_property(mpv_, "chapter-list/count", MPV_FORMAT_INT64, &n) < 0) return out;
    for (int64_t i = 0; i < n; ++i) {
        Chapter c;
        c.title = getString(QString("chapter-list/%1/title").arg(i).toUtf8().constData());
        mpv_get_property(mpv_, QString("chapter-list/%1/time").arg(i).toUtf8().constData(), MPV_FORMAT_DOUBLE, &c.time);
        out << c;
    }
    return out;
}

int MpvEngine::chapter() const {
    int64_t c = -1;
    if (mpv_get_property(mpv_, "chapter", MPV_FORMAT_INT64, &c) < 0) return -1;
    return int(c);
}

void MpvEngine::gotoChapter(int i) { setProp("chapter", QString::number(i)); }

void MpvEngine::setAbLoop(double a, double b) {
    setProp("ab-loop-a", a < 0 ? "no" : QString::number(a, 'f', 6));
    setProp("ab-loop-b", b < 0 ? "no" : QString::number(b, 'f', 6));
}

void MpvEngine::setLoopFile(bool loop) { setProp("loop-file", loop ? "inf" : "no"); }

void MpvEngine::addSubtitle(const QString& path) {
    command({"sub-add", QFileInfo(path).absoluteFilePath(), "select"});
}

void MpvEngine::applySubStyle(const QList<QPair<QByteArray, QString>>& options) {
    for (const auto& [k, v] : options) setProp(k.constData(), v);
}

void MpvEngine::setLut(const QString& shaderPath) {
    lutShader_ = shaderPath;
    applyShaders();
}

// Channel solo on the GPU: an mpv user shader on the final OUTPUT (after mpv's
// colour conversion and the LUT), so hardware decoding stays on and no CPU is used.
void MpvEngine::setChannel(int c) {
    channelShader_.clear();
    if (c >= 1 && c <= 4) {
        static const char* swz[] = {"rrr", "ggg", "bbb", "aaa"};
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/shaders";
        QDir().mkpath(dir);
        channelShader_ = QString("%1/channel_%2.glsl").arg(dir, swz[c - 1]);
        QFile f(channelShader_);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QString("//!HOOK OUTPUT\n//!BIND HOOKED\n//!DESC Coreplay channel %1\n"
                            "vec4 hook() {\n    vec4 c = HOOKED_texOff(0);\n    return vec4(c.%1, 1.0);\n}\n")
                        .arg(swz[c - 1]).toUtf8());
        }
    }
    applyShaders();
}

void MpvEngine::setGrade(const Grade& g) {
    grade_ = g;
    gradeShader_ = g.isIdentity() ? QString() : g.writeMpvShader(++gradeSlot_, comparing());
    applyShaders();
}

// GPU shaders on mpv's renderer: the LUT and then the grade on MAIN, the channel on OUTPUT.
void MpvEngine::applyShaders() {
    QStringList list;
    if (!lutShader_.isEmpty()) list << QDir::fromNativeSeparators(lutShader_);
    if (!gradeShader_.isEmpty()) list << QDir::fromNativeSeparators(gradeShader_);
    if (!channelShader_.isEmpty()) list << QDir::fromNativeSeparators(channelShader_);
#ifdef Q_OS_WIN
    setProp("glsl-shaders", list.join(';'));
#else
    setProp("glsl-shaders", list.join(':'));
#endif
}

void MpvEngine::setCompareView(const CompareView& c) {
    cmpView_ = c;
    syncCompareView();
}

void MpvEngine::syncCompareView() {
    if (comparing()) {
        if (size_ == cmpPacked_) view_->setCompare(cmpView_, size_);
    } else if (cmpLeaving_ && !size_.isEmpty() && size_ != cmpPacked_) {
        cmpLeaving_ = false;
        view_->setCompare(CompareView{}, {});
    }
}

// mpv's own option-list quoting: %<bytes>%<text>, so commas and brackets pass unchanged.
static QString quoted(const QString& s) { return QString("%%1%").arg(s.toUtf8().size()) + s; }

bool MpvEngine::setCompare(const QString& pathB, int offsetFrames) {
    const QString pathA = getString("path");
    if (pathA.isEmpty()) return false;
    const double pos = pos_;
    const bool wasPaused = paused_;
    QString opts = QString("start=%1,pause=%2").arg(pos, 0, 'f', 6).arg(wasPaused ? "yes" : "no");
    if (pathB.isEmpty()) {
        if (!comparing()) return true;
        cmpPath_.clear();
        cmpLeaving_ = true;  // the view goes back when mpv shows A alone again
    } else {
        // A's display size: B is scaled to it, then both are packed side by side.
        const QSize a = comparing() ? QSize(size_.width() / 2, size_.height()) : size_;
        if (a.isEmpty()) return false;
        int videos = 0;
        bool audio = false;
        const int n = getString("track-list/count").toInt();
        for (int i = 0; i < n; ++i) {
            if (getString(QString("track-list/%1/external").arg(i).toUtf8().constData()) == "yes") continue;
            const QString type = getString(QString("track-list/%1/type").arg(i).toUtf8().constData());
            if (type == "video") ++videos;
            else if (type == "audio") audio = true;
        }
        const double fps = fps_ > 0 ? fps_ : 24.0;
        const QString common = QString("scale=%1:%2,setsar=1,format=yuv444p16le").arg(a.width()).arg(a.height());
        QString graph = QString("[vid1]%1[a];[vid%2]%1,setpts=PTS%3%4/TB[b];[a][b]hstack=inputs=2[vo]")
                            .arg(common).arg(std::max(1, videos) + 1)
                            .arg(offsetFrames > 0 ? "-" : "+").arg(std::abs(offsetFrames) / fps, 0, 'f', 6);
        if (audio) graph += ";[aid1]anull[ao]";  // A's sound
        // lavfi filters take frames in system memory: decode on the GPU, then copy.
        opts += ",hwdec=auto-copy,external-files=" + quoted(QFileInfo(pathB).absoluteFilePath()) +
                ",lavfi-complex=" + quoted(graph);
        cmpPath_ = pathB;
        cmpPacked_ = QSize(2 * a.width(), a.height());
        cmpLeaving_ = false;
    }
    next_.clear();
    ++expectStarts_;
    command({"loadfile", pathA, "replace", "-1", opts});
    if (!grade_.isIdentity()) setGrade(grade_);  // compare on / off: grade A only, or the whole frame
    return true;
}

void MpvEngine::setViewTransform(const ViewTransform& t) {
    xf_ = t;
    if (comparing()) {  // the compare shader draws the transform; mpv shows the packed frame plainly
        view_->setTransform(t);
        for (const char* p : {"video-zoom", "video-pan-x", "video-pan-y", "video-rotate"}) setProp(p, "0");
        setProp("video-scale-x", "1");
        setProp("video-scale-y", "1");
        return;
    }
    view_->setTransform(ViewTransform{});
    // Coreplay pans in units of the VIEW size; mpv pans in units of the scaled video.
    double sx = 1.0, sy = 1.0;
    if (view_ && !size_.isEmpty() && view_->width() > 0 && view_->height() > 0) {
        const double fit = std::min(double(view_->width()) / size_.width(), double(view_->height()) / size_.height());
        sx = view_->width() / (size_.width() * fit * t.zoom * t.scaleX);
        sy = view_->height() / (size_.height() * fit * t.zoom * t.scaleY);
    }
    setProp("video-zoom", QString::number(std::log2(std::max(1e-3, t.zoom))));
    setProp("video-scale-x", QString::number(t.scaleX));
    setProp("video-scale-y", QString::number(t.scaleY));
    setProp("video-pan-x", QString::number(t.panX * sx));
    setProp("video-pan-y", QString::number(t.panY * sy));
    setProp("video-rotate", QString::number(((t.rotate % 360) + 360) % 360));
}

QSize MpvEngine::mediaSize() const { return comparing() ? QSize(size_.width() / 2, size_.height()) : size_; }

bool MpvEngine::saveImage(const QString& path) {
    const QByteArray p = path.toUtf8();
    const char* argv[] = {"screenshot-to-file", p.constData(), "video", nullptr};
    return mpv_command(mpv_, argv) >= 0;
}

QString MpvEngine::info() const {
    QStringList l;
    l << tr("File: %1").arg(getString("path"));
    l << tr("Container: %1").arg(getString("file-format"));
    l << tr("Video: %1  %2x%3  %4").arg(getString("video-codec")).arg(size_.width()).arg(size_.height())
             .arg(getString("video-params/pixelformat"));
    const Rational r = fps();
    l << tr("Frame rate: %1/%2 = %3 fps").arg(r.num).arg(r.den).arg(fps_, 0, 'f', 3);
    l << tr("Colour: %1 / %2 / %3")
             .arg(getString("video-params/colormatrix"), getString("video-params/primaries"), getString("video-params/gamma"));
    l << tr("Hardware decoding: %1").arg(getString("hwdec-current"));
    l << tr("Audio: %1  %2 Hz  %3").arg(getString("audio-codec-name"), getString("audio-params/samplerate"),
                                             getString("audio-params/hr-channels"));
    l << tr("Duration: %1 s").arg(dur_, 0, 'f', 3);
    const QString aspect = getString("video-params/aspect"), rotate = getString("video-params/rotate");
    if (!aspect.isEmpty()) l << tr("Display aspect: %1").arg(aspect) + (rotate.isEmpty() || rotate == "0" ? QString() : tr("   rotated %1").arg(rotate));
    if (const QString peak = getString("video-params/sig-peak"); !peak.isEmpty() && peak.toDouble() > 1.0)
        l << tr("HDR peak: %1 x SDR white").arg(peak);
    const double vbr = getString("video-bitrate").toDouble(), abr = getString("audio-bitrate").toDouble();
    if (vbr > 0 || abr > 0) l << tr("Bit rate: video %1 Mb/s   audio %2 kb/s").arg(vbr / 1e6, 0, 'f', 2).arg(abr / 1e3, 0, 'f', 0);
    if (const QString ch = getString("chapters"); ch.toInt() > 0) l << tr("Chapters: %1").arg(ch);
    if (const QString drop = getString("frame-drop-count"); drop.toInt() > 0) l << tr("Dropped frames: %1").arg(drop);
    // Every track the container has: type, codec, language, title, size or audio layout.
    mpv_node tracks{};
    if (mpv_get_property(mpv_, "track-list", MPV_FORMAT_NODE, &tracks) >= 0 && tracks.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < tracks.u.list->num; ++i) {
            const mpv_node& t = tracks.u.list->values[i];
            if (t.format != MPV_FORMAT_NODE_MAP) continue;
            QMap<QString, QString> f;
            for (int k = 0; k < t.u.list->num; ++k) {
                const mpv_node& v = t.u.list->values[k];
                QString s;
                if (v.format == MPV_FORMAT_STRING) s = QString::fromUtf8(v.u.string);
                else if (v.format == MPV_FORMAT_INT64) s = QString::number(v.u.int64);
                else if (v.format == MPV_FORMAT_FLAG) s = v.u.flag ? "yes" : "no";
                else if (v.format == MPV_FORMAT_DOUBLE) s = QString::number(v.u.double_);
                f[QString::fromUtf8(t.u.list->keys[k])] = s;
            }
            QString type = f.value("type");
            if (!type.isEmpty()) type[0] = type[0].toUpper();
            QString line = tr("%1 track %2: %3").arg(type, f.value("id"), f.value("codec"));
            if (f.contains("demux-w")) line += QString(" %1x%2").arg(f.value("demux-w"), f.value("demux-h"));
            if (f.contains("demux-channel-count")) line += QString(" %1 ch").arg(f.value("demux-channel-count"));
            if (f.contains("demux-samplerate")) line += QString(" %1 Hz").arg(f.value("demux-samplerate"));
            if (f.contains("lang")) line += " [" + f.value("lang") + "]";
            if (f.contains("title")) line += " \"" + f.value("title") + "\"";
            if (f.value("external") == "yes") line += tr(" (external)");
            if (f.value("selected") == "yes") line += " *";
            l << line;
        }
    }
    mpv_free_node_contents(&tracks);
    return l.join('\n');
}

QStringList MpvEngine::metadata() const {
    QStringList out;
    mpv_node m{};
    if (mpv_get_property(mpv_, "metadata", MPV_FORMAT_NODE, &m) >= 0 && m.format == MPV_FORMAT_NODE_MAP) {
        for (int i = 0; i < m.u.list->num; ++i) {
            const mpv_node& v = m.u.list->values[i];
            if (v.format == MPV_FORMAT_STRING)
                out << QString::fromUtf8(m.u.list->keys[i]) + ": " + QString::fromUtf8(v.u.string).simplified();
        }
    }
    mpv_free_node_contents(&m);
    return out;
}
