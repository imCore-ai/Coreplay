#pragma once
#include <QElapsedTimer>
#include <QTimer>

#include "Engine.h"
#include "seq/FrameCache.h"
#include "seq/SeqView.h"
#include "seq/Sequence.h"

// Plays image sequences from the RAM cache. While playing it shows EVERY frame in
// order: when the next frame is not decoded yet, playback waits for it (it never
// skips), and the status bar shows the real playback rate.
class SequenceEngine : public Engine {
    Q_OBJECT
public:
    explicit SequenceEngine(QWidget* parentWidget, QObject* parent = nullptr);
    ~SequenceEngine() override;

    QWidget* view() override { return view_; }
    SeqView* seqView() { return view_; }
    bool open(const QString& path) override;
    void close() override;

    void setPaused(bool p) override;
    bool paused() const override { return paused_; }
    void stop() override;

    void seekAbsolute(double seconds) override;
    void seekRelative(double seconds) override;
    void seekKeyframe(int dir) override { frameStep(dir); }
    void frameStep(int dir) override;
    void seekFrameIndex(int index);

    double position() const override;
    double duration() const override;
    Rational fps() const override { return fps_; }
    qint64 frameNumber() const override { return seq_.count() ? seq_.first + index_ : -1; }

    void setSpeed(double s) override;
    double speed() const override { return speed_; }
    void setVolume(int) override {}
    void setMuted(bool) override {}

    void setAbLoop(double a, double b) override;
    void setLoopFile(bool loop) override { loop_ = loop; }
    void setViewTransform(const ViewTransform& t) override { view_->setTransform(t); }
    void setChannel(int c) override { view_->setChannel(c); }
    QSize mediaSize() const override;
    bool saveImage(const QString& path) override;
    QString info() const override;
    void setNext(const QString& path) override;
    QStringList metadata() const override;

    // Image-sequence controls
    void setFps(Rational r, bool fromUser);
    bool fpsFromFile() const { return fpsFromFile_; }
    const Sequence& sequence() const { return seq_; }
    int index() const { return index_; }
    double measuredFps() const { return measuredFps_; }
    FrameCache* cache() { return cache_; }
    void setCacheBudget(size_t bytes);
    // A/B compare: B plays on A's frame index (+ offset), frame-locked: a frame shows
    // only when both are decoded. Each gets half of the cache.
    bool setCompare(const QString& pathB);   // "" ends it; false if B is not a sequence
    void setCompareOffset(int frames);
    int compareOffset() const { return cmpOffset_; }
    bool comparing() const { return cmpCache_ && cmpSeq_.count() > 0; }
    const Sequence& compareSequence() const { return cmpSeq_; }

private:
    void tick();
    void show(int index);
    void onFrameReady(int index);
    void restartClock();
    int loopIn() const;
    int loopOut() const;

    SeqView* view_;
    FrameCache* cache_;
    FrameCache* nextCache_ = nullptr;  // the next sequence's first frames, decoded near the end
    Sequence nextSeq_;
    QString nextPath_;
    bool nextStarted_ = false;
    size_t budget_ = 0;
    void watchCache(FrameCache* c);
    void startNext();
    void goToNext();
    void playhead(int index, int dir);   // A's cache, and B's when comparing
    int compareIndex(int index) const;
    bool ready(int index) const;         // A (and B) decoded
    FrameCache* cmpCache_ = nullptr;
    Sequence cmpSeq_;
    int cmpOffset_ = 0;
    QTimer timer_;
    QElapsedTimer clock_;
    Sequence seq_;
    Rational fps_{24, 1};
    bool fpsFromFile_ = false;
    bool fpsChecked_ = false;
    int index_ = 0;
    int wanted_ = -1;           // frame waited for while paused
    qint64 shown_ = 0;          // frames shown since the clock started
    bool paused_ = true;
    double speed_ = 1.0;
    bool loop_ = false;
    int abA_ = -1, abB_ = -1;
    double measuredFps_ = 0.0;
    QElapsedTimer rateClock_;
    int rateFrames_ = 0;
};
