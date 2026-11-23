#pragma once
// One transport interface. MainWindow drives either engine through it:
// MpvEngine (every movie container/codec) or SequenceEngine (image sequences).
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>

struct Rational {
    int num = 0;
    int den = 1;
    bool valid() const { return num > 0 && den > 0; }
    double value() const { return valid() ? double(num) / double(den) : 0.0; }
};

// Pan & scan state shared by both views (MPC-HC numpad keys).
struct ViewTransform {
    double zoom = 1.0;    // uniform scale
    double scaleX = 1.0;  // extra width scale
    double scaleY = 1.0;  // extra height scale
    double panX = 0.0;    // in units of the view width (-1..1)
    double panY = 0.0;
    int rotate = 0;       // degrees, multiple of 90
};

class Engine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~Engine() override = default;

    virtual QWidget* view() = 0;
    virtual bool open(const QString& path) = 0;
    virtual void close() = 0;

    virtual void setPaused(bool p) = 0;
    virtual bool paused() const = 0;
    virtual void stop() = 0;                       // pause and go to start

    virtual void seekAbsolute(double seconds) = 0;
    virtual void seekRelative(double seconds) = 0;
    virtual void seekKeyframe(int dir) = 0;
    virtual void frameStep(int dir) = 0;

    virtual double position() const = 0;           // seconds
    virtual double duration() const = 0;           // seconds
    virtual Rational fps() const = 0;              // 0/1 when unknown
    virtual qint64 frameNumber() const = 0;        // displayed frame, -1 if unknown

    virtual void setSpeed(double s) = 0;
    virtual double speed() const = 0;

    virtual void setVolume(int v) = 0;             // 0..100
    virtual void setMuted(bool m) = 0;
    virtual void adjustAudioDelay(double) {}
    virtual void cycleAudio(int) {}
    virtual void cycleSubtitle(int) {}
    virtual void toggleSubtitles() {}
    virtual void adjustSubDelay(double) {}
    virtual void adjustSubPos(int) {}
    virtual void adjustSubScale(double) {}
    virtual void gotoSub(int) {}
    virtual bool chapterStep(int) { return false; }  // false when the file has no chapters

    virtual void setAbLoop(double a, double b) = 0; // <0 clears that point
    virtual void setLoopFile(bool loop) = 0;

    virtual void setViewTransform(const ViewTransform& t) = 0;
    // Channel solo: 0 all, 1 R, 2 G, 3 B, 4 A. The channel of the picture as shown
    // (after the colour transform / LUT), displayed as grey.
    virtual void setChannel(int c) = 0;
    virtual QSize mediaSize() const = 0;

    virtual bool saveImage(const QString& path) = 0;
    virtual QString info() const = 0;              // multi-line properties text
    virtual QStringList metadata() const { return {}; }  // "key: value" tags of the file
    // Gapless playlists: the clip to go on to, with no stop, when this one ends ("" = none).
    virtual void setNext(const QString&) {}

signals:
    void loaded();
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void endReached();
    void advanced(const QString& path);             // went on to the setNext() clip by itself
    void message(const QString& text);              // OSD text
    void cacheChanged();
    void mediaSizeChanged();                        // the picture's size is known or changed
};
