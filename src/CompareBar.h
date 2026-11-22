#pragma once
// The A and B slots of A/B compare, above the seek bar while compare is on. Drop a clip
// on a slot (from Explorer or the playlist) or click it to choose a file; the same clip
// may go in both. Each slot has its own LUT button.
#include <QFrame>
#include <QString>

class QLabel;
class QToolButton;

class CompareSlot : public QFrame {
    Q_OBJECT
public:
    CompareSlot(const QString& letter, QWidget* parent = nullptr);
    void setFile(const QString& name);       // "" = empty slot
    void setLutLabel(const QString& label);
    QToolButton* lutButton() const { return lut_; }
    QString fileText() const;

signals:
    void dropped(const QString& path);
    void clicked();

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void paintEvent(QPaintEvent*) override;

private:
    QLabel* name_;
    QToolButton* lut_;
    bool empty_ = true;
    bool hover_ = false;
};

class CompareBar : public QWidget {
    Q_OBJECT
public:
    explicit CompareBar(QWidget* parent = nullptr);
    CompareSlot* slotA() const { return a_; }
    CompareSlot* slotB() const { return b_; }

private:
    CompareSlot *a_, *b_;
};
