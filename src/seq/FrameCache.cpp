#include "seq/FrameCache.h"

#include <algorithm>
#include <chrono>

FrameCache::FrameCache(QObject* parent) : QObject(parent) {
    ImageDecoder::init();
    const unsigned hc = std::max(2u, std::thread::hardware_concurrency());
    const unsigned n = std::max(1u, hc - 1);
    for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { worker(); });
}

FrameCache::~FrameCache() { shutdown(); }

// Stops and joins the decode threads. Call it while the receivers of frameReady
// still exist; the destructor calls it too.
void FrameCache::shutdown() {
    quit_ = true;
    cv_.notify_all();
    for (auto& t : threads_)
        if (t.joinable()) t.join();
}

void FrameCache::setSequence(const Sequence& seq) {
    std::lock_guard l(m_);
    seq_ = seq;
    count_ = seq.count();
    frames_.clear();
    used_ = 0;
    frameBytes_ = 0;
    playhead_ = 0;
    dir_ = 1;
    in_ = 0;
    out_ = count_ - 1;
    ++generation_;
    cv_.notify_all();
}

void FrameCache::clear() {
    std::lock_guard l(m_);
    seq_ = Sequence();
    count_ = 0;
    frames_.clear();
    used_ = 0;
    ++generation_;
}

void FrameCache::setBudget(size_t bytes) {
    std::lock_guard l(m_);
    budget_ = bytes;
    cv_.notify_all();
}

size_t FrameCache::usedBytes() const {
    std::lock_guard l(m_);
    return used_;
}

void FrameCache::setPlayhead(int index, int dir) {
    std::lock_guard l(m_);
    if (index == playhead_ && dir == dir_) return;
    playhead_ = index;
    dir_ = dir >= 0 ? 1 : -1;
    cv_.notify_all();
}

void FrameCache::setRange(int in, int out) {
    std::lock_guard l(m_);
    in_ = std::clamp(in, 0, std::max(0, count_ - 1));
    out_ = std::clamp(out, in_, std::max(0, count_ - 1));
    cv_.notify_all();
}

FramePtr FrameCache::get(int index) const {
    std::lock_guard l(m_);
    auto it = frames_.find(index);
    return it == frames_.end() ? nullptr : it->second;
}

std::vector<bool> FrameCache::cachedMask() const {
    std::lock_guard l(m_);
    std::vector<bool> mask(size_t(std::max(0, count_)), false);
    for (const auto& [i, f] : frames_) mask[size_t(i)] = true;
    return mask;
}

// Play-order distance inside the loop range [in_, out_]. Frames outside the range
// come after every frame inside it.
int FrameCache::distance(int index) const {
    const int len = out_ - in_ + 1;
    if (index < in_ || index > out_) return len + std::abs(index - playhead_);
    int d = (index - playhead_) * dir_;
    d %= len;
    if (d < 0) d += len;
    return d;
}

int FrameCache::nextToDecode() const {
    if (count_ <= 0) return -1;
    // Everything decoded: nothing to look for. (Each shown frame moves the playhead and
    // wakes every worker; without this they all scanned the whole range under the lock
    // the GUI thread takes to fetch the frame it shows.)
    if (int(frames_.size()) >= count_) return -1;
    const int len = out_ - in_ + 1;
    const int ph = std::clamp(playhead_, in_, out_);
    // A sequence just opened, nothing decoded yet: the frame to show first decodes ALONE, on
    // the decoder's whole thread pool, before the other workers start. When all workers
    // started at once, that frame shared the CPU with them and came last with the batch
    // (4K PIZ: 1.25 s to the first picture; alone it takes about 75 ms).
    if (frames_.empty() && inFlight_.count(ph)) return -1;
    for (int d = 0; d < len; ++d) {
        int i = ph + d * dir_;
        i = in_ + ((i - in_) % len + len) % len;
        if (frames_.count(i) || inFlight_.count(i)) continue;
        // Budget: stop when this frame would push out a frame needed sooner.
        if (frameBytes_ > 0 && used_ + frameBytes_ * (inFlight_.size() + 1) > budget_) {
            int worst = -1;
            for (const auto& [k, f] : frames_) worst = std::max(worst, distance(k));
            if (worst <= d) return -1;
        }
        return i;
    }
    return -1;
}

void FrameCache::evictFor(int index) {
    const int need = distance(index);
    while (used_ > budget_ && !frames_.empty()) {
        auto worst = frames_.end();
        int wd = -1;
        for (auto it = frames_.begin(); it != frames_.end(); ++it) {
            const int d = distance(it->first);
            if (d > wd) { wd = d; worst = it; }
        }
        if (wd <= need) break;
        used_ -= worst->second->bytes();
        frames_.erase(worst);
    }
}

void FrameCache::worker() {
    while (!quit_) {
        int index;
        QString path;
        unsigned gen;
        {
            std::unique_lock l(m_);
            cv_.wait(l, [&] { return quit_ || nextToDecode() >= 0; });
            if (quit_) return;
            index = nextToDecode();
            inFlight_.insert(index);
            path = seq_.pathFor(seq_.first + index);
            gen = generation_;
        }
        const auto t0 = std::chrono::steady_clock::now();
        FramePtr f = ImageDecoder::decode(path);
        decodeNs_ += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        ++decodeCount_;
        bool stored = false;
        {
            std::lock_guard l(m_);
            inFlight_.erase(index);
            if (gen == generation_ && !frames_.count(index)) {
                frames_[index] = f;
                used_ += f->bytes();
                if (!f->missing && f->error.isEmpty()) frameBytes_ = std::max(frameBytes_, f->bytes());
                evictFor(index);
                stored = frames_.count(index) > 0;
            }
        }
        cv_.notify_all();
        if (stored) emit frameReady(index);
    }
}
