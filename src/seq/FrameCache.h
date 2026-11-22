#pragma once
// Decodes frames ahead of the playhead on worker threads and keeps them in RAM.
// Frames are taken in play order from the playhead; when the budget is full the
// frame farthest away in play order goes first, so the frames needed next stay.
#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "seq/ImageDecoder.h"
#include "seq/Sequence.h"

class FrameCache : public QObject {
    Q_OBJECT
public:
    explicit FrameCache(QObject* parent = nullptr);
    ~FrameCache() override;

    void shutdown();
    void setSequence(const Sequence& seq);
    void clear();
    void setBudget(size_t bytes);
    size_t budget() const { return budget_; }
    size_t usedBytes() const;

    // Playhead (index 0..count-1), play direction (+1 / -1) and loop range.
    void setPlayhead(int index, int dir);
    void setRange(int in, int out);

    FramePtr get(int index) const;               // nullptr when not decoded yet
    std::vector<bool> cachedMask() const;        // for the seek bar
    // Speed probe: decoded frames and their total decode time (all workers).
    std::pair<long long, long long> decodeStats() const { return {decodeCount_.load(), decodeNs_.load()}; }
    void resetStats() { decodeCount_ = 0; decodeNs_ = 0; }

signals:
    void frameReady(int index);

private:
    void worker();
    int distance(int index) const;               // play-order distance from playhead
    int nextToDecode() const;                    // -1 when nothing to do
    void evictFor(int index);

    Sequence seq_;
    int count_ = 0;
    int playhead_ = 0;
    int dir_ = 1;
    int in_ = 0, out_ = 0;
    size_t budget_ = size_t(4) << 30;
    size_t used_ = 0;
    size_t frameBytes_ = 0;                      // size of one frame once known
    unsigned generation_ = 0;

    mutable std::mutex m_;
    std::condition_variable cv_;
    std::unordered_map<int, FramePtr> frames_;
    std::unordered_set<int> inFlight_;
    std::vector<std::thread> threads_;
    std::atomic<bool> quit_{false};
    std::atomic<long long> decodeCount_{0}, decodeNs_{0};
};
