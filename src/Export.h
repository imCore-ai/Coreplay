#pragma once
// Export / transcode the current clip with a second, windowless libmpv in encoding
// mode: decode -> (LUT or colour view) -> (burned-in subtitles) -> encoder.
#include <QDialog>
#include <QObject>
#include <QStringList>

#include "Engine.h"
#include "Grade.h"
#include "Lut.h"
#include "Subtitles.h"
#include "seq/Sequence.h"

struct mpv_handle;

struct ExportPreset {
    const char* id;
    const char* group;     // "Video", "Image sequence", "Audio only"
    const char* label;
    const char* ext;
    const char* of;        // container / muxer ("" = from the extension)
    const char* ovc;       // video encoder ("" = none)
    const char* ovcopts;
    const char* pixfmt;    // format the encoder gets
    const char* oac;       // audio encoder ("" = none)
    const char* oacopts;
    bool sequence() const;
    bool audioOnly() const;
    bool yuv() const;
};

namespace ExportPresets {
const std::vector<ExportPreset>& all();
const ExportPreset* find(const QString& id);
}

struct ExportJob {
    QString input;              // movie path, or the first frame of a sequence
    bool isSequence = false;
    Sequence seq;
    Rational fps;
    double start = -1, end = -1;  // seconds; -1 = from the start / to the end
    QString presetId;
    QString output;             // file, or for sequences a pattern with %04d
    int startNumber = 1001;     // first output frame number (sequences)
    LutChoice colour;           // baked into the picture (None = untouched)
    QString colourTarget;       // "sRGB", "Rec.709", "Rec.2020": tags of the result
    Grade grade;                // baked after the colour (identity = none)
    QString muxer;              // container override ("" = the preset's); Save keeps the file's own
    bool includeAudio = true;
    bool burnSubtitles = false;
    QStringList subtitleFiles;  // the external subtitle file to burn (empty: the movie's own track)
    QString subtitleTrack;      // mpv sid of the movie's own subtitle track
    SubStyle subStyle;
};

class Exporter : public QObject {
    Q_OBJECT
public:
    explicit Exporter(QObject* parent = nullptr);
    ~Exporter() override;
    bool start(const ExportJob& job, QString* error);
    void cancel();
    bool running() const { return mpv_ != nullptr; }
    QString lastOptions() const { return options_.join(' '); }  // for the log / tests
    QString burnedSubtitleId() const { return subtitleId_; }

signals:
    void progress(double fraction);
    void finished(bool ok, const QString& message);

private:
    void drain();
    void finish(bool ok, const QString& msg);
    void discard();
    mpv_handle* mpv_ = nullptr;
    ExportJob job_;
    QString listFile_;
    double span_ = 0;
    bool cancelled_ = false;
    QStringList options_;
    QString lastError_;
    bool pickSubtitle_ = false;
    QString subtitleId_;
};

// The Export window: format, file, range, colour and subtitles.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    struct Source {
        QString path;
        bool isSequence = false;
        Sequence seq;
        Rational fps;
        double duration = 0;
        double abA = -1, abB = -1;
        LutChoice lut;              // active LUT
        LutChoice view;             // sequences: the current colour view
        Grade grade;                // the clip's grade (identity = none)
        bool hasAudio = false;
        QStringList subtitleFiles;
        QString subtitleTrack;      // currently shown subtitle track id ("" = none)
        SubStyle subStyle;
    };
    ExportDialog(const Source& src, QWidget* parent = nullptr);
    ExportJob job() const;

private:
    void presetChanged();
    void updateOutput();
    Source src_;
    class QComboBox* format_ = nullptr;
    class QLineEdit* path_ = nullptr;
    class QCheckBox* colour_ = nullptr;
    class QCheckBox* grade_ = nullptr;
    class QCheckBox* audio_ = nullptr;
    class QCheckBox* subs_ = nullptr;
    class QRadioButton* whole_ = nullptr;
    class QRadioButton* ab_ = nullptr;
    class QSpinBox* startNum_ = nullptr;
    class QLabel* note_ = nullptr;
    QString lastAuto_;
};
