#pragma once
// F1: every command that has a key, over the whole window on a translucent black,
// in groups and columns. Reads the current keys (hotkeys.json included) each time it
// opens. F1, Esc or a click closes it. Drawn by hand, as the Osd (no style sheet).
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

class HotkeyOverlay : public QWidget {
    Q_OBJECT
public:
    explicit HotkeyOverlay(QWidget* window);
    void toggle();
    void open();
    void close();
    int rows() const;           // shown commands (the tests)
    QString text() const;       // "label = keys" lines (the tests)
    int fontPx() const { return fontPx_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool event(QEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    struct Row { QString label, keys; };
    struct Group { QString title; std::vector<Row> rows; };
    void fill();
    void fit();                 // the largest font where every group fits on the screen
    std::vector<Group> groups_;
    int fontPx_ = 14;
    QWidget* focusBefore_ = nullptr;
};
