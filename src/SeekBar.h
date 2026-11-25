#pragma once
#include <QWidget>
#include <vector>

// Seek bar. For image sequences it also shows which frames are in the RAM cache
// (green) and which are missing on disk (red), plus the A-B loop range. For a movie with
// chapters, a mark at the start of each chapter.
class SeekBar : public QWidget {
    Q_OBJECT
public:
    explicit SeekBar(QWidget* parent = nullptr);
    void setDuration(double s) { dur_ = s; update(); }
    void setPosition(double s) { if (!dragging_) { pos_ = s; update(); } }
    void setCacheMask(std::vector<bool> cached, std::vector<bool> missing);
    void setAb(double a, double b) { a_ = a; b_ = b; update(); }
    void setChapters(std::vector<double> starts) { chapters_ = std::move(starts); update(); }
    int chapterMarks() const { return int(chapters_.size()); }
    QSize sizeHint() const override { return {400, 18}; }

signals:
    void seekRequested(double seconds);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    double toSeconds(int x) const;
    double dur_ = 0, pos_ = 0, a_ = -1, b_ = -1;
    bool dragging_ = false;
    std::vector<bool> cached_, missing_;
    std::vector<double> chapters_;  // chapter starts, seconds
};
