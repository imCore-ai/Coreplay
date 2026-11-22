#include "seq/SequenceEngine.h"

#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <cmath>

SequenceEngine::SequenceEngine(QWidget* parentWidget, QObject* parent)
    : Engine(parent), view_(new SeqView(parentWidget)), cache_(new FrameCache(this)) {
    // RAM budget in GB, set in Play > Cache Size.
    QSettings st;
    const double gb = st.value("seq/cacheGB", 8.0).toDouble();
    budget_ = size_t(gb * double(size_t(1) << 30));
    cache_->setBudget(budget_);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &SequenceEngine::tick);
    watchCache(cache_);
}

// Both caches report; only the one that is playing counts.
void SequenceEngine::watchCache(FrameCache* c) {
    connect(c, &FrameCache::frameReady, this, [this, c](int index) { if (c == cache_) onFrameReady(index); });
}

void SequenceEngine::setCacheBudget(size_t bytes) {
    budget_ = bytes;
    cache_->setBudget(comparing() ? bytes / 2 : bytes);
    if (comparing()) cmpCache_->setBudget(bytes / 2);
}

int SequenceEngine::compareIndex(int index) const {
    return std::clamp(index + cmpOffset_, 0, std::max(0, cmpSeq_.count() - 1));
}

bool SequenceEngine::ready(int index) const {
    return cache_->get(index) && (!comparing() || cmpCache_->get(compareIndex(index)));
}

void SequenceEngine::playhead(int index, int dir) {
    cache_->setPlayhead(index, dir);
    if (comparing()) cmpCache_->setPlayhead(compareIndex(index), dir);
}

bool SequenceEngine::setCompare(const QString& pathB) {
    if (pathB.isEmpty()) {
        if (cmpCache_) cmpCache_->clear();
        cmpSeq_ = Sequence();
        cmpOffset_ = 0;
        cache_->setBudget(budget_);
        view_->setFrameB(nullptr);
        return true;
    }
    Sequence s = SequenceScanner::scan(pathB);
    if (!s.count()) return false;
    if (!cmpCache_) {
        cmpCache_ = new FrameCache(this);
        // B's frame for the frame on screen: show it when it arrives.
        connect(cmpCache_, &FrameCache::frameReady, this, [this](int i) {
            if (comparing() && i == compareIndex(index_)) view_->setFrameB(cmpCache_->get(i));
            emit cacheChanged();
        });
    }
    cmpSeq_ = s;
    cmpOffset_ = 0;
    setNext({});  // no gapless go-on while comparing
    cmpCache_->setSequence(cmpSeq_);
    cache_->setBudget(budget_ / 2);
    cmpCache_->setBudget(budget_ / 2);
    playhead(index_, 1);
    view_->setFrameB(cmpCache_->get(compareIndex(index_)));
    return true;
}

void SequenceEngine::setCompareOffset(int frames) {
    cmpOffset_ = frames;
    if (!comparing()) return;
    playhead(index_, 1);
    view_->setFrameB(cmpCache_->get(compareIndex(index_)));
}

// Gapless: the sequence that follows this one in the playlist.
void SequenceEngine::setNext(const QString& path) {
    if (path == nextPath_) return;
    nextPath_ = path;
    nextStarted_ = false;
    if (nextCache_) nextCache_->clear();
    nextSeq_ = path.isEmpty() ? Sequence() : SequenceScanner::scan(path);
    if (!nextSeq_.count()) nextPath_.clear();
}

// Near the end: decode the next sequence's first frames, in a smaller budget.
void SequenceEngine::startNext() {
    if (nextPath_.isEmpty() || nextStarted_) return;
    nextStarted_ = true;
    if (!nextCache_) {
        nextCache_ = new FrameCache(this);
        watchCache(nextCache_);
    }
    nextCache_->setBudget(std::min(budget_ / 4, size_t(2) << 30));
    nextCache_->setSequence(nextSeq_);
    nextCache_->setPlayhead(0, 1);
}

// The last frame was shown: go on to the next sequence on the same clock, no stop.
void SequenceEngine::goToNext() {
    startNext();
    std::swap(cache_, nextCache_);
    nextCache_->clear();
    nextCache_->setBudget(std::min(budget_ / 4, size_t(2) << 30));
    cache_->setBudget(budget_);
    seq_ = nextSeq_;
    const QString path = nextPath_;
    nextPath_.clear();
    nextSeq_ = Sequence();
    nextStarted_ = false;
    abA_ = abB_ = -1;
    fpsChecked_ = false;  // the new header gives the frame rate and colour space
    fpsFromFile_ = false;
    index_ = 0;
    cache_->setPlayhead(0, 1);
    emit advanced(path);
    emit durationChanged(duration());
    if (cache_->get(0)) {
        ++shown_;  // first: loaded() (from show) may restart the clock, and then frame 0 is its zero
        show(0);
    } else {
        wanted_ = 0;  // not decoded yet: tick waits for it
        emit positionChanged(position());
    }
}

SequenceEngine::~SequenceEngine() {
    // Join the decode threads first: none may emit frameReady into a half-destroyed engine.
    timer_.stop();
    disconnect(cache_, nullptr, this, nullptr);
    cache_->shutdown();
    if (cmpCache_) {
        disconnect(cmpCache_, nullptr, this, nullptr);
        cmpCache_->shutdown();
    }
    if (nextCache_) {
        disconnect(nextCache_, nullptr, this, nullptr);
        nextCache_->shutdown();
    }
}

bool SequenceEngine::open(const QString& path) {
    timer_.stop();
    setNext({});
    seq_ = SequenceScanner::scan(path);
    cache_->setSequence(seq_);
    abA_ = abB_ = -1;
    fpsChecked_ = false;
    fpsFromFile_ = false;
    QSettings st;
    const Rational def{st.value("seq/defaultFpsNum", 24).toInt(), st.value("seq/defaultFpsDen", 1).toInt()};
    fps_ = def.valid() ? def : Rational{24, 1};
    measuredFps_ = 0;
    // Start at the clicked frame, the way RV and DJV do.
    const QString name = QFileInfo(path).fileName();
    index_ = 0;
    if (seq_.padding > 0) {
        const QString digits = name.mid(seq_.prefix.size(), name.size() - seq_.prefix.size() - seq_.suffix.size());
        bool ok = false;
        const int fr = digits.toInt(&ok);
        if (ok) index_ = std::clamp(fr - seq_.first, 0, seq_.count() - 1);
    }
    paused_ = true;
    wanted_ = index_;
    playhead(index_, 1);
    view_->setFrame(nullptr);
    view_->setOverlay("Loading…");
    emit durationChanged(duration());
    emit positionChanged(position());
    return true;
}

void SequenceEngine::close() {
    timer_.stop();
    cache_->clear();
    setNext({});
    seq_ = Sequence();
    view_->setFrame(nullptr);
    view_->setOverlay({});
}

int SequenceEngine::loopIn() const { return abA_ >= 0 ? abA_ : 0; }
int SequenceEngine::loopOut() const { return abB_ >= 0 ? abB_ : seq_.count() - 1; }

void SequenceEngine::show(int index) {
    FramePtr f = cache_->get(index);
    if (!f) return;
    if (comparing()) view_->setFrameB(cmpCache_->get(compareIndex(index)));
    index_ = index;
    wanted_ = -1;
    view_->setOverlay({});
    view_->setFrame(f);
    if (!fpsChecked_ && !f->missing && f->error.isEmpty()) {
        fpsChecked_ = true;
        if (f->fps.valid()) {
            fps_ = f->fps;
            fpsFromFile_ = true;
            emit durationChanged(duration());
        }
        view_->setInputSpace(view_->defaultInputFor(*f));  // per sequence; the menu overrides it
        emit loaded();
    }
    playhead(index_, 1);
    emit positionChanged(position());
}

void SequenceEngine::onFrameReady(int index) {
    if (index == wanted_) show(index);
    emit cacheChanged();
}

void SequenceEngine::restartClock() {
    clock_.restart();
    shown_ = 0;
    rateClock_.restart();
    rateFrames_ = 0;
}

void SequenceEngine::setPaused(bool p) {
    if (!seq_.count() || p == paused_) return;
    paused_ = p;
    if (paused_) {
        timer_.stop();
    } else {
        if (index_ >= loopOut() && !loop_ && abB_ < 0) show(loopIn());
        restartClock();
        const double period = 1000.0 / (fps_.value() * speed_);
        timer_.start(std::max(1, int(period / 4)));
    }
    emit pausedChanged(paused_);
}

void SequenceEngine::tick() {
    const double rate = fps_.value() * speed_;
    const qint64 due = qint64(std::floor(clock_.nsecsElapsed() * 1e-9 * rate));
    if (shown_ >= due) return;
    int next = index_ + 1;
    // The last seconds: start decoding the next sequence (gapless playlists).
    if (!nextPath_.isEmpty() && !nextStarted_ && !loop_ && abB_ < 0 && !comparing() &&
        seq_.count() - 1 - index_ <= std::max(12, int(fps_.value() * 3)))
        startNext();
    if (index_ >= loopOut()) {
        if (!loop_ && abB_ < 0 && !nextPath_.isEmpty()) {
            goToNext();
            return;
        }
        if (!loop_ && abB_ < 0) {
            setPaused(true);
            emit endReached();
            return;
        }
        next = loopIn();
    }
    if (!ready(next)) {
        // Not decoded yet (A, or B when comparing): wait for it. The clock starts again from this frame.
        clock_.restart();
        shown_ = 0;
        return;
    }
    show(next);
    ++shown_;
    ++rateFrames_;
    if (rateClock_.elapsed() >= 1000) {
        measuredFps_ = rateFrames_ * 1000.0 / rateClock_.elapsed();
        rateClock_.restart();
        rateFrames_ = 0;
    }
}

void SequenceEngine::stop() {
    setPaused(true);
    seekFrameIndex(0);
}

void SequenceEngine::seekFrameIndex(int index) {
    if (!seq_.count()) return;
    index = std::clamp(index, 0, seq_.count() - 1);
    playhead(index, 1);
    if (cache_->get(index)) {
        show(index);
    } else {
        wanted_ = index;
        index_ = index;
        emit positionChanged(position());
    }
    if (!paused_) restartClock();
}

void SequenceEngine::seekAbsolute(double seconds) {
    seekFrameIndex(int(std::llround(seconds * fps_.value())));
}

void SequenceEngine::seekRelative(double seconds) {
    seekFrameIndex(index_ + int(std::llround(seconds * fps_.value())));
}

void SequenceEngine::frameStep(int dir) {
    if (!seq_.count()) return;
    setPaused(true);
    int i = index_ + (dir >= 0 ? 1 : -1);
    if (i < 0 || i >= seq_.count()) return;
    playhead(i, dir >= 0 ? 1 : -1);
    if (cache_->get(i)) show(i);
    else { wanted_ = i; index_ = i; emit positionChanged(position()); }
    playhead(i, dir >= 0 ? 1 : -1);
}

double SequenceEngine::position() const { return fps_.valid() ? index_ / fps_.value() : 0.0; }
double SequenceEngine::duration() const { return fps_.valid() ? seq_.count() / fps_.value() : 0.0; }

void SequenceEngine::setSpeed(double s) {
    speed_ = std::clamp(s, 1.0 / 16.0, 16.0);
    if (!paused_) {
        restartClock();
        timer_.start(std::max(1, int(1000.0 / (fps_.value() * speed_) / 4)));
    }
}

void SequenceEngine::setFps(Rational r, bool fromUser) {
    if (!r.valid()) return;
    fps_ = r;
    if (fromUser) fpsFromFile_ = false;
    if (!paused_) {
        restartClock();
        timer_.start(std::max(1, int(1000.0 / (fps_.value() * speed_) / 4)));
    }
    emit durationChanged(duration());
    emit positionChanged(position());
}

void SequenceEngine::setAbLoop(double a, double b) {
    abA_ = a < 0 ? -1 : std::clamp(int(std::llround(a * fps_.value())), 0, seq_.count() - 1);
    abB_ = b < 0 ? -1 : std::clamp(int(std::llround(b * fps_.value())), 0, seq_.count() - 1);
    if (abA_ >= 0 && abB_ >= 0 && abB_ < abA_) std::swap(abA_, abB_);
    cache_->setRange(loopIn(), loopOut());
}

QSize SequenceEngine::mediaSize() const {
    FramePtr f = view_->frame();
    return f && f->width ? QSize(f->width, f->height) : QSize();
}

bool SequenceEngine::saveImage(const QString& path) {
    QImage img = view_->grabFramebuffer();
    return !img.isNull() && img.save(path);
}

QStringList SequenceEngine::metadata() const {
    FramePtr f = view_->frame();
    return f ? f->metadata : QStringList();
}

QString SequenceEngine::info() const {
    FramePtr f = view_->frame();
    QStringList l;
    l << "Sequence: " + seq_.pattern();
    l << QString("Frames: %1-%2 (%3 frames, %4 missing)").arg(seq_.first).arg(seq_.last).arg(seq_.count()).arg(seq_.missingCount());
    l << QString("Frame rate: %1/%2 = %3 fps (%4)")
             .arg(fps_.num).arg(fps_.den).arg(fps_.value(), 0, 'f', 3)
             .arg(fpsFromFile_ ? "from the file header" : "NOT in the file - set in Play > Sequence Frame Rate");
    if (f) {
        l << "Image: " + f->description;
        l << "File colour tag: " + (f->colorSpace.isEmpty() ? QString("none") : f->colorSpace);
    }
    l << "Input colour space: " + view_->inputSpace();
    l << "Display / view: " + view_->display() + " / " + view_->viewName();
    l << "OCIO config: " + view_->configName();
    l << QString("Cache: %1 / %2 GB").arg(cache_->usedBytes() / double(1 << 30), 0, 'f', 2).arg(cache_->budget() / double(1 << 30), 0, 'f', 1);
    return l.join('\n');
}
