#include "seq/Sequence.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <map>
#include <set>

namespace {
// The frame number is the LAST run of digits in the file name before the extension.
// Returns false when the name holds no digits.
bool split(const QString& name, QString& prefix, QString& digits, QString& suffix) {
    static const QRegularExpression re(R"(^(.*?)(\d+)(\D*)$)");
    const int dot = name.lastIndexOf('.');
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString ext = dot > 0 ? name.mid(dot) : QString();
    const auto m = re.match(stem);
    if (!m.hasMatch()) return false;
    prefix = m.captured(1);
    digits = m.captured(2);
    suffix = m.captured(3) + ext;
    return true;
}
}  // namespace

int Sequence::missingCount() const {
    return int(std::count(present.begin(), present.end(), false));
}

QString Sequence::pathFor(int frame) const {
    if (padding == 0) return dir + "/" + prefix + suffix;
    QString n = QString::number(std::abs(frame)).rightJustified(padding, '0');
    if (frame < 0) n.prepend('-');
    return dir + "/" + prefix + n + suffix;
}

QString Sequence::pattern() const {
    return dir + "/" + prefix + QString(padding, '#') + suffix;
}

QString Sequence::displayName() const {
    if (padding == 0) return prefix + suffix;
    return QString("%1%2%3 [%4-%5]").arg(prefix, QString(padding, '#'), suffix).arg(first).arg(last);
}

namespace SequenceScanner {

QStringList imageExtensions() {
    return {"exr", "tif", "tiff", "png", "jpg", "jpeg", "dpx", "cin", "tga", "bmp",
            "hdr", "psd", "webp", "jp2", "j2k", "sgi", "rgb", "iff", "tx"};
}

bool isImageFile(const QString& path) {
    return imageExtensions().contains(QFileInfo(path).suffix().toLower());
}

Sequence scan(const QString& file) {
    const QFileInfo fi(file);
    Sequence s;
    s.dir = fi.absolutePath();
    QString digits;
    if (!split(fi.fileName(), s.prefix, digits, s.suffix)) {
        s.prefix = fi.completeBaseName();
        s.suffix = fi.fileName().mid(s.prefix.size());
        s.padding = 0;
        s.present = {true};
        return s;
    }
    // Collect every file in the folder with the same prefix and suffix.
    std::set<int> frames;
    int minDigits = 1 << 30;
    const QDir d(s.dir);
    const QStringList names = d.entryList({s.prefix + "*" + s.suffix}, QDir::Files);
    for (const QString& n : names) {
        QString p, dg, sf;
        if (!split(n, p, dg, sf) || p != s.prefix || sf != s.suffix) continue;
        frames.insert(dg.toInt());
        minDigits = std::min(minDigits, int(dg.size()));
    }
    if (frames.empty()) {  // the file itself must exist; keep it as one frame
        frames.insert(digits.toInt());
        minDigits = digits.size();
    }
    // Padding: unpadded numbers (1, 2, ..., 10) have differing lengths; padded ones
    // all share the shortest length.
    s.padding = (minDigits == int(digits.size())) ? minDigits : 1;
    s.first = *frames.begin();
    s.last = *frames.rbegin();
    s.present.assign(size_t(s.count()), false);
    for (int f : frames) s.present[size_t(f - s.first)] = true;
    return s;
}

QStringList sequencesInFolder(const QString& dirPath) {
    QStringList out;
    const QDir d(dirPath);
    QStringList filters;
    for (const QString& e : imageExtensions()) filters << "*." + e;
    std::map<QString, std::pair<int, QString>> firstOf;  // key -> (frame, path)
    for (const QString& n : d.entryList(filters, QDir::Files, QDir::Name)) {
        QString p, dg, sf;
        if (!split(n, p, dg, sf)) {
            out << d.absoluteFilePath(n);
            continue;
        }
        const QString key = p + "\x01" + sf;
        const int fr = dg.toInt();
        auto it = firstOf.find(key);
        if (it == firstOf.end() || fr < it->second.first)
            firstOf[key] = {fr, d.absoluteFilePath(n)};
    }
    for (const auto& [key, v] : firstOf) out << v.second;
    out.sort(Qt::CaseInsensitive);
    return out;
}

}  // namespace SequenceScanner
