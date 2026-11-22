#pragma once
// libmpv's encoder writes the last video frame of an MP4 / MOV with a duration of 0
// (it never learns the next frame's time), so players and editors drop or flash it.
// This gives that last sample the frame duration of the one before it and updates
// the media, track, movie and edit-list durations to match. It edits fixed-size
// fields in place: no box grows, no data moves.
#include <QString>

namespace MovFix {
// Returns an empty string on success, else what went wrong. A file whose last sample
// already has a duration is left untouched.
QString fixLastVideoSampleDuration(const QString& path);
}
