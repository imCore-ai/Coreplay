#include "Export.h"
#include "MovFix.h"

#include <mpv/client.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <thread>

// ------------------------------------------------------------------ presets

bool ExportPreset::sequence() const { return QByteArray(of) == "image2"; }
bool ExportPreset::audioOnly() const { return !*ovc; }
bool ExportPreset::yuv() const { return QByteArray(pixfmt).startsWith("yuv"); }

namespace ExportPresets {

QString framePattern(const QString& output) {
    const QString norm = QDir::fromNativeSeparators(output);
    const int slash = norm.lastIndexOf('/');
    const QString dir = norm.left(slash + 1), name = norm.mid(slash + 1);
    static const QRegularExpression field(R"(%0?\d*d)"), hashes("#+");
    auto escape = [](QString s) { return s.replace('%', "%%"); };
    QString out;
    const QRegularExpressionMatch m = field.match(name);
    if (m.hasMatch()) {
        out = escape(name.left(m.capturedStart())) + m.captured() + escape(name.mid(m.capturedEnd()));
    } else {
        // the LAST run of # is the frame number (an earlier one is part of the name)
        QRegularExpressionMatch last;
        for (auto it = hashes.globalMatch(name); it.hasNext();) last = it.next();
        if (last.hasMatch()) {
            out = escape(name.left(last.capturedStart())) + "%0" + QString::number(last.capturedLength()) + "d"  /* not arg(): Qt reads "%0" as a place marker */ +
                  escape(name.mid(last.capturedEnd()));
        } else {
            const int dot = name.lastIndexOf('.');
            out = dot > 0 ? escape(name.left(dot)) + ".%04d" + escape(name.mid(dot)) : escape(name) + ".%04d";
        }
    }
    return escape(dir) + out;
}
const std::vector<ExportPreset>& all() {
    // Encoders all ship inside libmpv-2.dll (checked by name in the DLL).
    static const std::vector<ExportPreset> p = {
        // id, group, label, ext, of, ovc, ovcopts, pixfmt, oac, oacopts
        {"h264", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "H.264 MP4 (x264, high quality)"), "mp4", "mp4", "libx264", "crf=16,preset=medium", "yuv420p", "aac", "b=320k"},
        {"h264_nvenc", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "H.264 MP4 (NVIDIA GPU, fast)"), "mp4", "mp4", "h264_nvenc", "preset=p6,rc=vbr,cq=18,b=0", "yuv420p", "aac", "b=320k"},
        {"h265", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "H.265 / HEVC MP4 (x265)"), "mp4", "mp4", "libx265", "crf=18,preset=medium,x265-params=log-level=error", "yuv420p10le", "aac", "b=320k"},
        {"h265_nvenc", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "H.265 / HEVC MP4 (NVIDIA GPU, fast)"), "mp4", "mp4", "hevc_nvenc", "preset=p6,rc=vbr,cq=20,b=0", "p010le", "aac", "b=320k"},
        {"av1", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "AV1 MP4 (SVT-AV1)"), "mp4", "mp4", "libsvtav1", "crf=28,preset=8", "yuv420p10le", "aac", "b=320k"},
        {"av1_nvenc", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "AV1 MP4 (NVIDIA RTX 40+ GPU)"), "mp4", "mp4", "av1_nvenc", "preset=p6,rc=vbr,cq=24,b=0", "p010le", "aac", "b=320k"},
        {"vp9", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "VP9 WebM"), "webm", "webm", "libvpx-vp9", "crf=30,b=0,row-mt=1", "yuv420p", "libopus", "b=192k"},
        {"prores422hq", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "ProRes 422 HQ MOV"), "mov", "mov", "prores_ks", "profile=3,vendor=apl0", "yuv422p10le", "pcm_s24le", ""},
        {"prores422", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "ProRes 422 MOV"), "mov", "mov", "prores_ks", "profile=2,vendor=apl0", "yuv422p10le", "pcm_s24le", ""},
        {"prores422lt", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "ProRes 422 LT MOV"), "mov", "mov", "prores_ks", "profile=1,vendor=apl0", "yuv422p10le", "pcm_s24le", ""},
        {"prores422proxy", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "ProRes 422 Proxy MOV"), "mov", "mov", "prores_ks", "profile=0,vendor=apl0", "yuv422p10le", "pcm_s24le", ""},
        {"prores4444", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "ProRes 4444 MOV"), "mov", "mov", "prores_ks", "profile=4,vendor=apl0", "yuva444p10le", "pcm_s24le", ""},
        {"dnxhr_hq", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "DNxHR HQ MOV (8-bit)"), "mov", "mov", "dnxhd", "profile=dnxhr_hq", "yuv422p", "pcm_s24le", ""},
        {"dnxhr_hqx", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "DNxHR HQX MOV (10-bit)"), "mov", "mov", "dnxhd", "profile=dnxhr_hqx", "yuv422p10le", "pcm_s24le", ""},
        {"dnxhr_444", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "DNxHR 444 MOV (10-bit)"), "mov", "mov", "dnxhd", "profile=dnxhr_444", "yuv444p10le", "pcm_s24le", ""},
        {"cineform", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "CineForm MOV"), "mov", "mov", "cfhd", "quality=film1", "yuv422p10le", "pcm_s24le", ""},
        {"ffv1", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "FFV1 MKV (lossless)"), "mkv", "matroska", "ffv1", "level=3,slices=16,slicecrc=1", "yuv444p10le", "flac", ""},
        {"utvideo", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "Ut Video AVI (lossless)"), "avi", "avi", "utvideo", "", "gbrp", "pcm_s16le", ""},
        {"qtrle", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "QuickTime Animation MOV (lossless RGB)"), "mov", "mov", "qtrle", "", "rgb24", "pcm_s16le", ""},
        {"hap", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "HAP MOV"), "mov", "mov", "hap", "format=hap", "rgba", "pcm_s16le", ""},
        {"mjpeg", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "Motion JPEG AVI"), "avi", "avi", "mjpeg", "q=2", "yuvj422p", "pcm_s16le", ""},
        {"gif", QT_TRANSLATE_NOOP("ExportPresets", "Video"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "Animated GIF"), "gif", "gif", "gif", "", "pal8", "", ""},
        {"exr_half", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "OpenEXR half float (ZIP)"), "exr", "image2", "exr", "compression=zip16,format=half", "gbrpf32le", "", ""},
        {"exr_float", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "OpenEXR 32-bit float (ZIP)"), "exr", "image2", "exr", "compression=zip16,format=float", "gbrpf32le", "", ""},
        {"dpx10", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "DPX 10-bit"), "dpx", "image2", "dpx", "", "gbrp10le", "", ""},
        {"dpx16", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "DPX 16-bit"), "dpx", "image2", "dpx", "", "rgb48le", "", ""},
        {"tiff16", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "TIFF 16-bit"), "tif", "image2", "tiff", "compression_algo=deflate", "rgb48le", "", ""},
        {"tiff8", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "TIFF 8-bit"), "tif", "image2", "tiff", "compression_algo=deflate", "rgb24", "", ""},
        {"png16", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "PNG 16-bit"), "png", "image2", "png", "", "rgb48be", "", ""},
        {"png8", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "PNG 8-bit"), "png", "image2", "png", "", "rgb24", "", ""},
        {"jpeg", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "JPEG"), "jpg", "image2", "mjpeg", "q=2", "yuvj444p", "", ""},
        {"webp", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "WebP (lossless)"), "webp", "image2", "libwebp", "lossless=1", "bgra", "", ""},
        {"jxl", QT_TRANSLATE_NOOP("ExportPresets", "Image sequence"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "JPEG XL (lossless)"), "jxl", "image2", "libjxl", "distance=0", "rgb48le", "", ""},
        {"wav", QT_TRANSLATE_NOOP("ExportPresets", "Audio only"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "WAV 24-bit"), "wav", "wav", "", "", "", "pcm_s24le", ""},
        {"flac", QT_TRANSLATE_NOOP("ExportPresets", "Audio only"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "FLAC"), "flac", "flac", "", "", "", "flac", ""},
        {"mp3", QT_TRANSLATE_NOOP("ExportPresets", "Audio only"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "MP3 320 kb/s"), "mp3", "mp3", "", "", "", "libmp3lame", "b=320k"},
        {"m4a", QT_TRANSLATE_NOOP("ExportPresets", "Audio only"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "AAC M4A 320 kb/s"), "m4a", "ipod", "", "", "", "aac", "b=320k"},
        {"opus", QT_TRANSLATE_NOOP("ExportPresets", "Audio only"), /*: Keep format and codec names in English. */ QT_TRANSLATE_NOOP("ExportPresets", "Opus"), "opus", "ogg", "", "", "", "libopus", "b=192k"},
    };
    return p;
}

const ExportPreset* find(const QString& id) {
    for (const auto& p : all())
        if (id == p.id) return &p;
    return nullptr;
}
}  // namespace ExportPresets

// ------------------------------------------------------------------ exporter

Exporter::Exporter(QObject* parent) : QObject(parent) {}

Exporter::~Exporter() {
    if (mpv_) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        mpv_handle* h = mpv_;
        mpv_ = nullptr;
        std::thread([h] { mpv_terminate_destroy(h); }).join();  // not on the GUI thread: COM
    }
    if (!listFile_.isEmpty()) QFile::remove(listFile_);
}

bool Exporter::start(const ExportJob& job, QString* error) {
    const ExportPreset* p = ExportPresets::find(job.presetId);
    if (!p) { *error = tr("Unknown export format"); return false; }
    job_ = job;
    if (p->sequence()) job_.output = ExportPresets::framePattern(job.output);  // whatever name the user typed
    cancelled_ = false;
    options_.clear();
    lastError_.clear();

    QString input = QFileInfo(job.input).absoluteFilePath();
    if (job.isSequence) {
        // A list of every frame in the range; a missing frame repeats the one before
        // it, so the timing never shifts.
        const Sequence& s = job.seq;
        int i0 = 0, i1 = s.count() - 1;
        if (job.start >= 0) i0 = std::clamp(int(std::llround(job.start * job.fps.value())), 0, i1);
        if (job.end >= 0) i1 = std::clamp(int(std::llround(job.end * job.fps.value())) - 1, i0, s.count() - 1);
        QTemporaryFile tf(QDir::tempPath() + "/coreplay_export_XXXXXX.txt");
        tf.setAutoRemove(false);
        if (!tf.open()) { *error = tr("Cannot write the frame list"); return false; }
        QString last;
        for (int i = i0; i <= i1; ++i) {
            if (s.present[size_t(i)]) last = s.pathFor(s.first + i);
            if (last.isEmpty()) {  // the range starts on missing frames: use the next real one
                for (int k = i; k <= i1 && last.isEmpty(); ++k)
                    if (s.present[size_t(k)]) last = s.pathFor(s.first + k);
            }
            tf.write((QDir::toNativeSeparators(last) + "\n").toUtf8());
        }
        listFile_ = tf.fileName();
        input = "mf://@" + QDir::fromNativeSeparators(listFile_);
        span_ = (i1 - i0 + 1) / job.fps.value();
    }

    mpv_ = mpv_create();
    auto opt = [this](const QByteArray& k, const QString& v) {
        const int rc = mpv_set_option_string(mpv_, k.constData(), v.toUtf8().constData());
        options_ << QString("%1=%2%3").arg(QString::fromUtf8(k), v, rc < 0 ? QString(" [REJECTED: %1]").arg(mpv_error_string(rc)) : QString());
    };
    opt("config", "no");
    opt("terminal", "no");
    opt("ytdl", "no");
    opt("load-scripts", "no");
    if (const QString log = qEnvironmentVariable("COREPLAY_MPV_LOG"); !log.isEmpty()) {
        opt("log-file", log + ".export");   // the export's own libmpv log, next to the player's
        opt("msg-level", "all=v");
    }
    opt("o", QDir::toNativeSeparators(job_.output));
    if (!job.muxer.isEmpty()) opt("of", job.muxer);
    else if (*p->of) opt("of", p->of);
    if (p->sequence()) opt("ofopts", QString("start_number=%1").arg(job.startNumber));
    if (*p->ovc) {
        opt("ovc", p->ovc);
    } else {
        opt("vid", "no");
    }
    if (*p->oac && job.includeAudio && !job.isSequence) {
        opt("oac", p->oac);
        if (*p->oacopts) opt("oacopts", p->oacopts);
        // Editorial deliveries (ProRes / DNxHR / CineForm MOV) use 48 kHz.
        if (QByteArray(p->oac).startsWith("pcm_") && QByteArray(p->of) != "wav" && QByteArray(p->of) != "avi")
            opt("audio-samplerate", "48000");
    } else {
        opt("aid", "no");
    }
    if (job.isSequence) opt("mf-fps", QString::number(job.fps.value(), 'g', 17));
    if (!job.isSequence && job.start >= 0) opt("start", QString::number(job.start, 'f', 6));
    if (!job.isSequence && job.end >= 0) opt("end", QString::number(job.end, 'f', 6));
    opt("hwdec", "auto-copy-safe");

    // Video filters: colour (baked .cube) -> RGB->YUV with an explicit matrix -> encoder format.
    QStringList graph;
    bool rgb = job.isSequence;  // sequences decode to RGB
    if (*p->ovc && job.colour.kind != LutChoice::None) {
        QString err;
        QString cube = Lut::bakeCube(job.colour, &err);
        if (cube.isEmpty()) { *error = tr("Cannot bake the colour transform: %1").arg(err); discard(); return false; }
        cube = QDir::fromNativeSeparators(cube);
        cube.replace(":", "\\:");
        graph << QString("lut3d=file='%1':interp=tetrahedral").arg(cube);
        rgb = true;
    }
    // The grade: on the picture as shown, so after the colour (a .cube of the same maths
    // as the viewer's shader, Grade::apply).
    if (*p->ovc && !job.grade.isIdentity()) {
        QString err;
        QString cube = job.grade.bakeCube(&err);
        if (cube.isEmpty()) { *error = tr("Cannot bake the grade: %1").arg(err); discard(); return false; }
        cube = QDir::fromNativeSeparators(cube);
        cube.replace(":", "\\:");
        graph << QString("lut3d=file='%1':interp=tetrahedral").arg(cube);
        rgb = true;
    }
    QString primaries = "bt709", trc = "bt709", matrix = "bt709";
    if (job.colourTarget == "Rec.2020") { primaries = "bt2020"; matrix = "bt2020nc"; trc = "bt709"; }
    else if (job.colourTarget == "sRGB") { trc = "iec61966-2-1"; }
    if (*p->ovc) {
        const QString pix = p->pixfmt;
        if (pix == "pal8") {
            graph << "split[a][b];[a]palettegen=stats_mode=full[p];[b][p]paletteuse=dither=sierra2_4a";
        } else {
            if (rgb && (p->yuv() || pix == "p010le"))
                graph << QString("scale=out_color_matrix=%1:out_range=%2")
                             .arg(matrix == "bt2020nc" ? "bt2020" : "bt709", pix.startsWith("yuvj") ? "pc" : "tv");
            graph << "format=" + pix;
        }
        opt("vf", "lavfi=[" + graph.join(',') + "]");
        QStringList vo = QString(p->ovcopts).split(',', Qt::SkipEmptyParts);
        if (rgb && (p->yuv() || pix == "p010le"))  // tag what the picture now is
            vo << "color_primaries=" + primaries << "color_trc=" + trc << "colorspace=" + matrix;
        // Encoder time base 1/120000: every common rate is a whole number of ticks
        // (23.976 = 5005, 25 = 4800, 29.97 = 4004, 59.94 = 2002, 119.88 = 1001) and it is
        // finer than any frame rate. mpv's default, 1/24000, cannot hold 29.97 exactly.
        vo << "time_base=1/120000";
        opt("ovcopts", vo.join(','));
    }

    // Subtitles: burned in by mpv's encoder output when shown, with the player's style.
    if (*p->ovc && job.burnSubtitles) {
        if (!job.subtitleFiles.isEmpty()) {
            QStringList files;
            for (const QString& f : job.subtitleFiles) files << QDir::toNativeSeparators(f);
#ifdef Q_OS_WIN
            opt("sub-files", files.join(';'));
#else
            opt("sub-files", files.join(':'));
#endif
        }
        if (!job.subtitleFiles.isEmpty()) {
            // Track numbers differ between instances: pick the file by name once it is
            // loaded, while the encoder waits paused.
            opt("pause", "yes");
            pickSubtitle_ = true;
        } else {
            opt("sid", job.subtitleTrack.isEmpty() ? "auto" : job.subtitleTrack);
        }
        opt("sub-visibility", "yes");
        for (const auto& [k, v] : job.subStyle.mpvOptions()) opt(k, v);
    } else {
        opt("sid", "no");
        opt("sub-auto", "no");
    }
    opt("msg-level", "all=warn");
    // Diagnostics: COREPLAY_EXPORT_LOG=<file> writes mpv's full log of the export.
    if (qEnvironmentVariableIsSet("COREPLAY_EXPORT_LOG")) {
        opt("log-file", qEnvironmentVariable("COREPLAY_EXPORT_LOG"));
        opt("msg-level", "all=v");
    }

    if (mpv_initialize(mpv_) < 0) {
        *error = tr("The encoder did not start: %1").arg(options_.join(' '));
        discard();
        return false;
    }
    mpv_request_log_messages(mpv_, "warn");
    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_set_wakeup_callback(mpv_, [](void* ctx) {
        auto* self = static_cast<Exporter*>(ctx);
        QMetaObject::invokeMethod(self, [self] { self->drain(); }, Qt::QueuedConnection);
    }, this);
    if (!job.isSequence) span_ = 0;
    const QByteArray in = input.toUtf8();
    const char* cmd[] = {"loadfile", in.constData(), nullptr};
    mpv_command(mpv_, cmd);
    return true;
}

void Exporter::cancel() {
    if (!mpv_) return;
    cancelled_ = true;
    const char* cmd[] = {"stop", nullptr};
    mpv_command_async(mpv_, 0, cmd);
}

void Exporter::drain() {
    while (mpv_) {
        mpv_event* e = mpv_wait_event(mpv_, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto* pr = static_cast<mpv_event_property*>(e->data);
            if (pr->format != MPV_FORMAT_DOUBLE || !pr->data) continue;
            const double v = *static_cast<double*>(pr->data);
            if (QByteArray(pr->name) == "duration" && span_ <= 0) {
                const double a = job_.start >= 0 ? job_.start : 0.0;
                const double b = job_.end >= 0 ? std::min(job_.end, v) : v;
                span_ = std::max(1e-6, b - a);
            } else if (QByteArray(pr->name) == "time-pos" && span_ > 0) {
                const double a = job_.isSequence ? 0.0 : std::max(0.0, job_.start);
                emit progress(std::clamp((v - a) / span_, 0.0, 1.0));
            }
        } else if (e->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* m = static_cast<mpv_event_log_message*>(e->data);
            // Keep the last few warnings: mpv's own end reason is often just "something happened".
            const QString line = QString("%1: %2").arg(QString::fromUtf8(m->prefix), QString::fromUtf8(m->text).trimmed());
            QStringList recent = lastError_.split(" | ", Qt::SkipEmptyParts);
            recent << line;
            while (recent.size() > 3) recent.removeFirst();
            lastError_ = recent.join(" | ");
            // An encoder that cannot start (e.g. NVENC without an NVIDIA driver). libmpv 0.41
            // then ends the file, but 0.39 drops the video and waits for ever: fail it here.
            if (m->log_level == MPV_LOG_LEVEL_FATAL && !cancelled_) {
                finish(false, tr("Export failed: %1").arg(lastError_));
                return;
            }
        } else if (e->event_id == MPV_EVENT_FILE_LOADED && pickSubtitle_) {
            pickSubtitle_ = false;
            int64_t n = 0;
            mpv_get_property(mpv_, "track-list/count", MPV_FORMAT_INT64, &n);
            const QString want = QFileInfo(job_.subtitleFiles.first()).absoluteFilePath();
            for (int64_t i = 0; i < n; ++i) {
                auto prop = [&](const char* f) {
                    char* s = mpv_get_property_string(mpv_, QString("track-list/%1/%2").arg(i).arg(f).toUtf8().constData());
                    const QString v = s ? QString::fromUtf8(s) : QString();
                    mpv_free(s);
                    return v;
                };
                if (prop("type") != "sub") continue;
                const QString ext = prop("external-filename");
                if (!ext.isEmpty() && QFileInfo(ext).absoluteFilePath().compare(want, Qt::CaseInsensitive) == 0) {
                    mpv_set_property_string(mpv_, "sid", prop("id").toUtf8().constData());
                    subtitleId_ = prop("id");
                    break;
                }
            }
            mpv_set_property_string(mpv_, "pause", "no");
        } else if (e->event_id == MPV_EVENT_END_FILE) {
            auto* ef = static_cast<mpv_event_end_file*>(e->data);
            if (cancelled_) finish(false, tr("Export cancelled"));
            else if (ef->reason == MPV_END_FILE_REASON_EOF) finish(true, tr("Exported %1").arg(QDir::toNativeSeparators(job_.output)));
            else finish(false, tr("Export failed: %1").arg(lastError_.isEmpty() ? QString(mpv_error_string(ef->error)) : lastError_));
            return;
        }
    }
}

// A start that failed: free everything, report nothing (start() returns the error).
void Exporter::discard() {
    if (mpv_) {
        mpv_handle* h = mpv_;
        mpv_ = nullptr;
        std::thread([h] { mpv_terminate_destroy(h); }).join();
    }
    if (!listFile_.isEmpty()) {
        QFile::remove(listFile_);
        listFile_.clear();
    }
}

void Exporter::finish(bool ok, const QString& msg) {
    if (mpv_) {
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        mpv_handle* h = mpv_;
        mpv_ = nullptr;
        // Terminating flushes and closes the output file; off the GUI thread (COM).
        std::thread([h] { mpv_terminate_destroy(h); }).join();
    }
    if (!listFile_.isEmpty()) {
        QFile::remove(listFile_);
        listFile_.clear();
    }
    if (!ok && cancelled_ && !ExportPresets::find(job_.presetId)->sequence()) QFile::remove(job_.output);
    if (ok) {
        const QString ext = QFileInfo(job_.output).suffix().toLower();
        if (ext == "mp4" || ext == "mov" || ext == "m4v") {
            const QString err = MovFix::fixLastVideoSampleDuration(job_.output);
            if (!err.isEmpty()) {
                ok = false;
                emit progress(1.0);
                emit finished(false, tr("Exported, but the last frame's duration could not be set: %1").arg(err));
                return;
            }
        }
    }
    if (ok) emit progress(1.0);
    emit finished(ok, msg);
}

// ------------------------------------------------------------------ dialog

ExportDialog::ExportDialog(const Source& src, QWidget* parent) : QDialog(parent), src_(src) {
    setWindowTitle(tr("Export"));
    setMinimumWidth(620);
    auto* form = new QFormLayout;

    format_ = new QComboBox(this);
    auto* model = qobject_cast<QStandardItemModel*>(format_->model());
    QString group;
    for (const auto& p : ExportPresets::all()) {
        if (src.isSequence && p.audioOnly()) continue;  // a sequence has no sound
        if (group != p.group) {
            group = p.group;
            format_->addItem(tr("— %1 —").arg(QCoreApplication::translate("ExportPresets", p.group)));
            model->item(format_->count() - 1)->setEnabled(false);
        }
        format_->addItem(QCoreApplication::translate("ExportPresets", p.label), QString(p.id));
    }
    const QString last = QSettings().value("export/preset", "h264").toString();
    const int li = format_->findData(last);
    format_->setCurrentIndex(li >= 0 ? li : 1);
    form->addRow(tr("Format"), format_);

    auto* pathRow = new QHBoxLayout;
    path_ = new QLineEdit(this);
    auto* browse = new QPushButton(tr("Browse..."), this);
    pathRow->addWidget(path_, 1);
    pathRow->addWidget(browse);
    form->addRow(tr("Save to"), pathRow);

    auto* rangeRow = new QHBoxLayout;
    whole_ = new QRadioButton(tr("Whole clip"), this);
    ab_ = new QRadioButton(tr("A-B marks"), this);
    whole_->setChecked(true);
    ab_->setEnabled(src.abA >= 0 || src.abB >= 0);
    if (ab_->isEnabled()) ab_->setChecked(true);
    rangeRow->addWidget(whole_);
    rangeRow->addWidget(ab_);
    rangeRow->addStretch(1);
    form->addRow(tr("Range"), rangeRow);

    startNum_ = new QSpinBox(this);
    startNum_->setRange(0, 9999999);
    startNum_->setValue(src.isSequence ? src.seq.first : 1001);
    form->addRow(tr("First frame number"), startNum_);

    QString colourText = tr("Apply the colour view");
    if (src.lut.kind != LutChoice::None) colourText = tr("Apply the LUT: %1").arg(src.lut.label());
    else if (src.isSequence) colourText = tr("Apply the colour view: %1").arg(src.view.label());
    colour_ = new QCheckBox(colourText, this);
    colour_->setEnabled(src.lut.kind != LutChoice::None || src.isSequence);
    colour_->setChecked(colour_->isEnabled());
    form->addRow(tr("Colour"), colour_);

    grade_ = new QCheckBox(src.grade.isIdentity() ? tr("No grade") : tr("Bake the grade: %1").arg(src.grade.summary()), this);
    grade_->setEnabled(!src.grade.isIdentity());
    grade_->setChecked(grade_->isEnabled());
    form->addRow(tr("Grade"), grade_);
    // A sequence's grade sits on the colour view: baking it needs the view baked too.
    auto lockColour = [this] {
        if (!src_.isSequence) return;
        const bool need = grade_->isChecked();
        if (need) colour_->setChecked(true);
        colour_->setEnabled(!need);
    };
    connect(grade_, &QCheckBox::toggled, this, lockColour);
    lockColour();

    audio_ = new QCheckBox(tr("Include audio"), this);
    audio_->setEnabled(!src.isSequence && src.hasAudio);
    audio_->setChecked(audio_->isEnabled());
    form->addRow(tr("Audio"), audio_);

    subs_ = new QCheckBox(tr("Burn in subtitles (with the current subtitle style)"), this);
    subs_->setEnabled(!src.isSequence && (!src.subtitleFiles.isEmpty() || !src.subtitleTrack.isEmpty()));
    subs_->setChecked(false);
    form->addRow(tr("Subtitles"), subs_);

    note_ = new QLabel(this);
    note_->setWordWrap(true);
    form->addRow("", note_);

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings().setValue("export/preset", format_->currentData().toString());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::presetChanged);
    connect(browse, &QPushButton::clicked, this, [this] {
        const ExportPreset* p = ExportPresets::find(format_->currentData().toString());
        if (!p) return;
        if (p->sequence()) {
            const QString d = QFileDialog::getExistingDirectory(this, tr("Folder for the frames"), QFileInfo(path_->text()).absolutePath());
            if (!d.isEmpty()) path_->setText(QDir::toNativeSeparators(d + "/" + QFileInfo(path_->text()).fileName()));
        } else {
            const QString f = QFileDialog::getSaveFileName(this, tr("Export"), path_->text(), QString("*.%1").arg(p->ext));
            if (!f.isEmpty()) path_->setText(QDir::toNativeSeparators(f));
        }
    });
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(buttons);
    presetChanged();
}

void ExportDialog::presetChanged() {
    const ExportPreset* p = ExportPresets::find(format_->currentData().toString());
    if (!p) return;
    startNum_->setEnabled(p->sequence());
    // Keep raw values by default when the result is OpenEXR (scene-linear stays linear).
    if (colour_->isEnabled()) colour_->setChecked(QByteArray(p->ext) != "exr" || src_.lut.kind != LutChoice::None);
    if (src_.isSequence && grade_->isChecked()) colour_->setChecked(true);
    subs_->setEnabled(!src_.isSequence && !p->audioOnly() && (!src_.subtitleFiles.isEmpty() || !src_.subtitleTrack.isEmpty()));
    audio_->setEnabled(!src_.isSequence && src_.hasAudio && *p->oac && !p->sequence());
    note_->setText(p->sequence() ? tr("Frames are written as name.####.ext, starting at the first frame number.")
                                 : QString());
    updateOutput();
}

// <dir>/<base>_<preset>.<ext>, or for a sequence <dir>/<base>_<preset>/<base>.%04d.<ext>.
// The frame-number pattern is added AFTER arg(): inside an arg() string Qt reads "%04" as
// place marker 4, and the old "%%04d.%4" came out as "%pngd" (every frame went to one file,
// and the export failed with "Writing packet failed").
QString ExportDialog::suggestedOutput(const Source& src, const ExportPreset* p, const QString& dir) {
    const QFileInfo fi(src.path);
    const QString base = src.isSequence ? QString(src.seq.prefix).remove(QRegularExpression("[._-]+$")) : fi.completeBaseName();
    if (p->sequence())
        return QString("%1/%2_%3/%2.").arg(dir, base, p->id) + "%04d." + p->ext;
    return QString("%1/%2_%3.%4").arg(dir, base, p->id, p->ext);
}

void ExportDialog::updateOutput() {
    const ExportPreset* p = ExportPresets::find(format_->currentData().toString());
    if (!p) return;
    // Suggest a name next to the source; keep a name the user typed.
    const QString suggestion = QDir::toNativeSeparators(suggestedOutput(src_, p, QFileInfo(src_.path).absolutePath()));
    if (path_->text().isEmpty() || path_->text() == lastAuto_) path_->setText(suggestion);
    else {  // the user's name with the new extension
        QString t = path_->text();
        const int dot = t.lastIndexOf('.');
        if (dot > t.lastIndexOf(QDir::separator())) t = t.left(dot);
        if (p->sequence() && !t.contains("%")) t += ".%04d";
        if (!p->sequence()) t.remove(QRegularExpression("\\.%0\\dd$"));
        path_->setText(t + "." + p->ext);
    }
    lastAuto_ = suggestion;
}

ExportJob ExportDialog::job() const {
    ExportJob j;
    j.input = src_.path;
    j.isSequence = src_.isSequence;
    j.seq = src_.seq;
    j.fps = src_.fps;
    if (ab_->isChecked()) {
        j.start = src_.abA;
        j.end = src_.abB;
    }
    j.presetId = format_->currentData().toString();
    j.output = QDir::fromNativeSeparators(path_->text());
    j.startNumber = startNum_->value();
    if (colour_->isChecked() && (colour_->isEnabled() || grade_->isChecked())) {
        j.colour = src_.lut.kind != LutChoice::None ? src_.lut : src_.view;
        j.colourTarget = j.colour.kind == LutChoice::Preset ? j.colour.target
                         : j.colour.display.contains("Rec.709") ? "Rec.709"
                         : j.colour.display.contains("2020") ? "Rec.2020" : "sRGB";
    }
    if (grade_->isEnabled() && grade_->isChecked()) j.grade = src_.grade;
    j.includeAudio = audio_->isEnabled() && audio_->isChecked();
    j.burnSubtitles = subs_->isEnabled() && subs_->isChecked();
    j.subtitleFiles = src_.subtitleFiles;
    j.subtitleTrack = src_.subtitleTrack;
    j.subStyle = src_.subStyle;
    return j;
}
