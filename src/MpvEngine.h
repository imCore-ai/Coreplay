#pragma once
#include <QOpenGLExtraFunctions>
#include <QOpenGLWidget>
#include <QPointer>

#include "Compare.h"
#include "Engine.h"
#include "Grade.h"

struct mpv_handle;
struct mpv_render_context;

// Draws libmpv frames into a Qt OpenGL widget (mpv render API).
class MpvView : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    MpvView(mpv_handle* mpv, QWidget* parent);
    ~MpvView() override;
    // A/B compare: mpv draws A and B side by side into one frame (packed); this view
    // renders that frame into its own buffer and shows it through the compare shader.
    void setCompare(const CompareView& c, QSize packed);
    const CompareView& compare() const { return cmp_; }
    bool software() const { return software_; }   // drawn by software OpenGL (fast scaling on)
    void setTransform(const ViewTransform& t) { xf_ = t; update(); }

signals:
    void doubleClicked();
    void clicked();
    void contextMenu(const QPoint& globalPos);
    void needUpdate();
    void wipeMoved(float wipe);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    static void onUpdate(void* ctx);
    void paintCompare();
    float wipeAt(QPointF pos) const;
    CompareView cmp_;
    QSize packed_;
    ViewTransform xf_;
    unsigned fbo_ = 0, fboTex_ = 0, prog_ = 0, vao_ = 0;
    QSize fboSize_;
    bool glReady_ = false, wipeDrag_ = false, software_ = false;
    double lastHx_ = 0, lastOffX_ = 0, lastOffY_ = 0, lastAngle_ = 0, lastVw_ = 1, lastVh_ = 1;
    mpv_handle* mpv_;
    mpv_render_context* render_ = nullptr;
    bool afterDouble_ = false;
};

class MpvEngine : public Engine {
    Q_OBJECT
public:
    explicit MpvEngine(QWidget* parentWidget, QObject* parent = nullptr);
    ~MpvEngine() override;

    QWidget* view() override { return view_; }
    MpvView* mpvView() { return view_; }
    bool open(const QString& path) override;
    void close() override;

    void setPaused(bool p) override;
    bool paused() const override { return paused_; }
    void stop() override;

    void seekAbsolute(double seconds) override;
    void seekRelative(double seconds) override;
    void seekKeyframe(int dir) override;
    void frameStep(int dir) override;

    double position() const override { return pos_; }
    double duration() const override { return dur_; }
    Rational fps() const override;
    qint64 frameNumber() const override;

    void setSpeed(double s) override;
    double speed() const override { return speed_; }
    void setVolume(int v) override;
    void setMuted(bool m) override;
    void adjustAudioDelay(double d) override;
    void cycleAudio(int dir) override;
    void cycleSubtitle(int dir) override;
    void toggleSubtitles() override;
    void adjustSubDelay(double d) override;
    void adjustSubPos(int d) override;
    void adjustSubScale(double d) override;
    void gotoSub(int dir) override;
    bool chapterStep(int dir) override;

    void setAbLoop(double a, double b) override;
    void setLoopFile(bool loop) override;
    void setViewTransform(const ViewTransform& t) override;
    void setChannel(int c) override;
    QSize mediaSize() const override;
    bool saveImage(const QString& path) override;
    QString info() const override;
    void setNext(const QString& path) override;
    QStringList metadata() const override;
    QString property(const char* name) const { return getString(name); }
    void setLut(const QString& shaderPath);   // empty = no LUT (see Lut::bakeMpvShader)
    // The colour grade, after the LUT. Changing values only sets shader params (no rebuild).
    void setGrade(const Grade& g);
    QString lastLog() const { return lastLog_; }
    void setPropertyString(const char* name, const QString& v) { setProp(name, v); }
    void addSubtitle(const QString& path);   // load and show it
    // A/B compare with movie B ("" ends it): reopens A with B as a second input, at
    // the same time, both decoded in step by mpv (frame-locked). offset = B frames ahead.
    bool setCompare(const QString& pathB, int offsetFrames);
    bool comparing() const { return !cmpPath_.isEmpty(); }
    void setCompareView(const CompareView& c);
    MpvView* mpvView() const { return view_; }
    void applySubStyle(const QList<QPair<QByteArray, QString>>& options);

private:
    void command(const QStringList& args);
    void setProp(const char* name, const QString& value);
    QString getString(const char* name) const;
    void drainEvents();
    void applyShaders();

    mpv_handle* mpv_ = nullptr;
    QPointer<MpvView> view_;
    double pos_ = 0, dur_ = 0, fps_ = 0, speed_ = 1.0;
    bool paused_ = true;
    QSize size_;
    QString lutShader_, channelShader_;
    Grade grade_;
    QString gradeShader_;   // the current grade shader file ("" = no grade)
    int gradeSlot_ = 0;
    QString next_;               // queued in mpv's playlist after the current file
    QString cmpPath_;            // B, while comparing
    CompareView cmpView_;
    ViewTransform xf_;
    // The view changes over only when mpv's output has the size that goes with it: the
    // packed A|B frame when compare starts, A's own size when it ends. Before that, mpv
    // still shows the old frame, and the compare shader would split A into two halves.
    QSize cmpPacked_;
    bool cmpLeaving_ = false;
    void syncCompareView();
    int expectStarts_ = 0;       // file starts that come from open(), not from the queue
    QString lastLog_;
};
