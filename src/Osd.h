#pragma once
#include <QColor>
#include <QLabel>
#include <QTimer>

// MPC-HC style on-screen text on a translucent box. Drawn by hand, not with a
// style sheet: the first style sheet costs ~300 ms of start-up.
class Osd : public QLabel {
    Q_OBJECT
public:
    explicit Osd(QWidget* parent, QColor text = Qt::white, int pixelSize = 15, bool monospace = false);
    void showMessage(const QString& text, int ms = 1500);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QTimer hide_;
};
