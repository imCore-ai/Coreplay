#pragma once
// Finds the image sequence a file belongs to: shot_v01.1001.exr -> shot_v01.####.exr,
// with its frame range and the frames missing from disk.
#include <QString>
#include <QStringList>
#include <vector>

struct Sequence {
    QString dir;
    QString prefix;      // text before the frame number
    QString suffix;      // text after the frame number, with the extension
    int padding = 0;     // digit count of the frame number (0 = single image)
    int first = 0;
    int last = 0;
    std::vector<bool> present;   // index = frame - first

    int count() const { return last - first + 1; }
    int missingCount() const;
    QString pathFor(int frame) const;    // full path of one frame
    QString pattern() const;             // dir/prefix####suffix
    QString displayName() const;         // prefix####suffix [first-last]
};

namespace SequenceScanner {
bool isImageFile(const QString& path);
QStringList imageExtensions();
// Builds the sequence that contains `file`. A file with no frame number is a
// one-frame sequence.
Sequence scan(const QString& file);
// Every sequence and every single image in a folder (one entry per sequence:
// the path of its first frame).
QStringList sequencesInFolder(const QString& dir);
}  // namespace SequenceScanner
