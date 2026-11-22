#pragma once
// Subtitle look, applied to libmpv (playback and export burn-in alike).
#include <QColor>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

struct SubStyle {
    QString font = "Arial";
    int size = 46;               // mpv sub-font-size: pixels at a 720-pixel-high picture
    bool bold = true;
    bool italic = false;
    QColor text = Qt::white;
    int outline = 3;             // outline width (0 = none)
    QColor outlineColor = Qt::black;
    int shadow = 0;              // shadow offset
    QColor shadowColor = QColor(0, 0, 0, 128);
    bool box = false;            // draw a background box behind the text
    QColor boxColor = Qt::black;
    int boxOpacity = 60;         // percent: 0 = clear, 100 = solid
    int position = 100;          // vertical: 0 top .. 100 bottom
    int margin = 34;             // distance from the picture edge, in mpv's units
    bool overrideAss = false;    // force this look onto styled .ass/.ssa subtitles too

    static SubStyle load();      // from the settings
    void save() const;
    // mpv option name -> value. The same list is set on the player and on the export.
    QList<QPair<QByteArray, QString>> mpvOptions() const;
};

namespace Subtitles {
QStringList extensions();        // srt, ass, ssa, vtt, sub, ...
bool isSubtitleFile(const QString& path);
}

// Live subtitle look editor: every change is applied at once.
class SubtitleDialog : public QDialog {
    Q_OBJECT
public:
    explicit SubtitleDialog(const SubStyle& start, QWidget* parent = nullptr);
    SubStyle style() const { return s_; }

signals:
    void changed(const SubStyle& s);

private:
    SubStyle s_;
};
