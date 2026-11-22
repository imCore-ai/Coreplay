#pragma once
// Alt+G: the colour grading window, BunnyLab's controls (Primary sliders, then a wheel
// and a tone slider for shadows, midtones and highlights). A translucent tool window
// over the player: the clip keeps playing while you grade. Its controls never take the
// keyboard, so the player's hotkeys (Space, arrows...) keep working while it is open.
#include <QWidget>

#include "Grade.h"

class QLabel;
class QSlider;
class QToolButton;
class QPushButton;
class GradeWheel;

class GradeWindow : public QWidget {
    Q_OBJECT
public:
    explicit GradeWindow(QWidget* player);
    void setGrade(const Grade& g);          // shows the clip's grade (no signal)
    const Grade& grade() const { return g_; }
    void setClipName(const QString& name, bool modified);
    void placeNear(QWidget* player);        // the first time: last position, else right of the player
    void testDrag(int dx, int dy);

    // Test hooks: the same steps as the controls.
    bool setControl(const QString& name, double value);   // "contrast", "shadowsTint.angle", ...

signals:
    void gradeChanged(const Grade& g);      // every control move
    void saveRequested();
    void saveAsRequested();
    void exportRequested();
    void keyForPlayer(QKeyEvent* e);        // hotkeys typed while this window is active

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool event(QEvent*) override;

private:
    struct Row {
        QSlider* slider;
        QLabel* value;
        QToolButton* reset;
        double scale;     // slider int = value * scale
        double def;
        int decimals;
        QString suffix;
    };
    Row addRow(class QGridLayout* grid, int r, const QString& label, double min, double max, double step, double def,
               int decimals, const QString& suffix);
    void showValues();
    void emitChange();

    Grade g_;
    bool updating_ = false;
    QLabel* clip_;
    Row bright_, contrast_, gamma_, sat_, hue_;
    struct Tone {
        GradeWheel* wheel;
        QToolButton* wheelReset;
        Row tone;
    } tones_[3];
    QPoint keepOnScreen(QPoint p) const;
    QWidget* bar_ = nullptr;                // title bar: the drag handle
    QPoint drag_;
    bool dragging_ = false;
    bool placed_ = false;
};

// One colour wheel: hue around (red at 12 o'clock), strength out from the centre.
class GradeWheel : public QWidget {
    Q_OBJECT
public:
    explicit GradeWheel(QWidget* parent = nullptr);
    void setTint(const GradeTint& t);
    GradeTint tint() const { return t_; }
    QSize sizeHint() const override { return {124, 124}; }

signals:
    void tintChanged(const GradeTint& t);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    void pick(QPointF p);
    GradeTint t_;
    QImage disc_;
};
