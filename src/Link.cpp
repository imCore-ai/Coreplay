#include "Link.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include "Playlist.h"
#include "seq/Sequence.h"

namespace {
const int kMaxSource = 2048;

bool hasControlChar(const QString& s) {
    for (const QChar c : s)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) return true;
    return false;
}

bool allowedHost(const QString& host, const QStringList& extra) {
    const QString h = host.toLower();
    if (h == "imcore.ai" || h.endsWith(".imcore.ai")) return true;
    for (const QString& e : extra)
        if (!e.trimmed().isEmpty() && h == e.trimmed().toLower()) return true;
    return false;
}

bool thisComputer(const QString& host) {
    const QString h = host.toLower();
    return h == "localhost" || h == "127.0.0.1" || h == "::1";
}

// Empty = accepted; else the reason it is refused.
QString checkSource(const QString& s, const QStringList& extraHosts) {
    if (s.isEmpty()) return "empty source";
    if (s.size() > kMaxSource) return "source too long";
    if (hasControlChar(s)) return "control character in source";
    if (s.startsWith('-') || s.startsWith('#')) return "source looks like a switch";

    // "name://": a web address. (A Windows drive, "C:\", has one letter and no "//".)
    static const QRegularExpression scheme("^([A-Za-z][A-Za-z0-9+.-]+):");
    const auto m = scheme.match(s);
    if (m.hasMatch()) {
        const QString name = m.captured(1).toLower();
        if (name != "https" && name != "http") return "scheme not allowed: " + name;
        const QUrl u(s, QUrl::StrictMode);
        if (!u.isValid() || u.host().isEmpty()) return "address not valid";
        if (!u.userInfo().isEmpty()) return "address with a user name";
        if (name == "http" && !thisComputer(u.host())) return "http is allowed only on this computer";
        if (name == "https" && !allowedHost(u.host(), extraHosts) && !thisComputer(u.host()))
            return "host not allowed: " + u.host();
        // A web source goes to the movie engine: movie types only (no image sequence over the web).
        if (!Playlist::isMediaFile(u.path()) || SequenceScanner::isImageFile(u.path())) return "not a movie file type";
        return {};
    }

    // A path. It must be absolute, so it cannot depend on the folder Coreplay started in.
    static const QRegularExpression drive("^[A-Za-z]:[\\\\/]");
    const bool unc = s.startsWith("\\\\") || s.startsWith("//");
    if (!drive.match(s).hasMatch() && !unc && !s.startsWith('/')) return "path is not absolute";
    if (!Playlist::isMediaFile(s) && !QFileInfo(s).isDir()) return "not a media file type";
    return {};
}
}  // namespace

namespace Link {

bool isLink(const QString& arg) { return arg.startsWith("coreplay:", Qt::CaseInsensitive); }

bool isRemote(const QString& source) {
    return source.startsWith("https://", Qt::CaseInsensitive) || source.startsWith("http://", Qt::CaseInsensitive);
}

QStringList hostsFromSettings() { return QSettings().value("link/hosts").toStringList(); }

LinkRequest parse(const QString& link, const QStringList& extraHosts) {
    LinkRequest r;
    auto refuse = [&r](const QString& why) {
        r = LinkRequest{};
        r.error = why;
        return r;
    };
    if (link.size() > 3 * kMaxSource) return refuse("link too long");
    const QUrl u(link, QUrl::StrictMode);
    if (!u.isValid() || u.scheme().toLower() != "coreplay") return refuse("not a coreplay link");
    if (u.host().toLower() != "open") return refuse("unknown action");
    if (!(u.path().isEmpty() || u.path() == "/") || u.hasFragment() || !u.userInfo().isEmpty() || u.port() != -1)
        return refuse("unexpected part in the link");

    const auto items = QUrlQuery(u).queryItems(QUrl::FullyDecoded);
    QStringList seen;
    for (const auto& [key, value] : items) {
        if (seen.contains(key)) return refuse("parameter given twice: " + key);
        seen << key;
        if (key == "src") r.src = value;
        else if (key == "b") r.b = value;
        else if (key == "add") {
            if (value != "0" && value != "1") return refuse("add must be 0 or 1");
            r.add = value == "1";
        } else if (key == "frame") {
            static const QRegularExpression digits("^[0-9]{1,9}$");
            if (!digits.match(value).hasMatch()) return refuse("frame must be a number");
            r.frame = value.toLongLong();
        } else {
            return refuse("unknown parameter: " + key);
        }
    }
    if (!seen.contains("src")) return refuse("no source");
    if (const QString why = checkSource(r.src, extraHosts); !why.isEmpty()) return refuse("src: " + why);
    if (seen.contains("b"))
        if (const QString why = checkSource(r.b, extraHosts); !why.isEmpty()) return refuse("b: " + why);
    r.ok = true;
    return r;
}

}  // namespace Link
