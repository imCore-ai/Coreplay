#pragma once
#include <QString>
#include <QStringList>

// The coreplay:// link: lets a web page (Core Watch) ask Coreplay to open a clip.
//
//   coreplay://open?src=<source>[&add=0|1][&frame=<n>][&b=<source>]
//
//   src    the clip: an absolute path (local or network), or an https address
//   add    1 = add to the playlist; the clip that plays stays
//   frame  go to this frame and pause there (0 = the first frame of the clip)
//   b      a second clip for slot B of Compare
//
// Any web page can send this link, so it is treated as untrusted input:
//   - Only these four parameters, each at most once. Anything else: refused.
//   - A source is never a command-line switch, and holds no control character
//     (the hand-over between windows is line-based).
//   - A path must be absolute, and a media file type or a folder.
//   - A web source must be https on an allowed imCore host (http only on this
//     computer), and a movie file type.
//   - The link only opens something for viewing. It never runs, writes or
//     deletes a file, and when a process is started from a link, every other
//     command-line argument is ignored.
struct LinkRequest {
    bool ok = false;
    QString error;        // why it was refused (English, for the log and the tests)
    QString src;
    QString b;
    bool add = false;
    long long frame = -1;  // -1: not given
};

namespace Link {
// True for an argument that is a coreplay: link (valid or not).
bool isLink(const QString& arg);
// True for a source that is a web address, which the engines must not turn into a file path.
bool isRemote(const QString& source);
// `extraHosts`: more allowed https hosts (Settings key link/hosts), for a site on its own domain.
LinkRequest parse(const QString& link, const QStringList& extraHosts = {});
// The allowed extra hosts from the user's settings.
QStringList hostsFromSettings();
}  // namespace Link
