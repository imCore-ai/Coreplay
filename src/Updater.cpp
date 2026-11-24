#include "Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <memory>

#include "UpdateKey.h"

#include <QProcess>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#elif defined(Q_OS_LINUX)
#include <dlfcn.h>
#endif

namespace {
// One channel per download: the Windows installer, each Mac chip's .dmg, the Linux AppImage
// and the Linux .deb (the last two are chosen when Coreplay runs: see feedUrl()).
#if defined(Q_OS_WIN)
constexpr char kDefaultFeed[] = "https://auth.imcore.ai/coreplay-updates/windows/";
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
constexpr char kDefaultFeed[] = "https://auth.imcore.ai/coreplay-updates/mac-arm64/";
#elif defined(Q_OS_MACOS)
constexpr char kDefaultFeed[] = "https://auth.imcore.ai/coreplay-updates/mac-x64/";
#else
constexpr char kDefaultFeed[] = "https://auth.imcore.ai/coreplay-updates/linux-x86_64/";
constexpr char kDebFeed[] = "https://auth.imcore.ai/coreplay-updates/linux-deb-amd64/";
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
// The built-in public key as an uncompressed point: 04 || X || Y.
QByteArray publicKeyPoint() {
    QByteArray p(1, '\x04');
    p.append(reinterpret_cast<const char*>(kUpdateKeyX), 32);
    p.append(reinterpret_cast<const char*>(kUpdateKeyY), 32);
    return p;
}
// raw r || s (the feed's format) -> DER SEQUENCE { INTEGER r, INTEGER s } (what Apple and OpenSSL take).
QByteArray derSignature(const QByteArray& raw) {
    auto integer = [](QByteArray v) {
        while (v.size() > 1 && v[0] == '\0' && !(uchar(v[1]) & 0x80)) v.remove(0, 1);
        if (uchar(v[0]) & 0x80) v.prepend('\0');
        return QByteArray(1, '\x02') + char(v.size()) + v;
    };
    const QByteArray body = integer(raw.left(32)) + integer(raw.mid(32, 32));
    return QByteArray(1, '\x30') + char(body.size()) + body;
}
#endif
#ifdef Q_OS_LINUX
// The running AppImage (its runtime sets APPIMAGE); empty for the .deb.
QString appImagePath() { return qEnvironmentVariable("APPIMAGE"); }
// Installed from the .deb (tools/linux/make_deb.sh puts Coreplay in /opt/coreplay, and dpkg
// records the package).
bool debInstall() {
    return appImagePath().isEmpty() && QCoreApplication::applicationFilePath().startsWith("/opt/coreplay/") &&
           QFile::exists("/var/lib/dpkg/info/coreplay.list");
}
#endif
#ifdef Q_OS_MACOS
// /Applications/Coreplay.app (from .../Coreplay.app/Contents/MacOS).
QString bundlePath() {
    QDir d(QCoreApplication::applicationDirPath());
    d.cdUp();
    d.cdUp();
    return d.absolutePath();
}
#endif
}  // namespace

bool Updater::supported() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_LINUX)
    return !appImagePath().isEmpty() || debInstall();
#else
    return false;
#endif
}

Updater::Updater(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

QString Updater::feedUrl() {
#ifdef Q_OS_LINUX
    const char* feed = debInstall() ? kDebFeed : kDefaultFeed;
#else
    const char* feed = kDefaultFeed;
#endif
    QString u = qEnvironmentVariable("COREPLAY_UPDATE_URL", feed);
    if (!u.endsWith('/')) u += '/';
    return u;
}

QString Updater::currentVersion() { return QCoreApplication::applicationVersion(); }

bool Updater::newer(const QString& a, const QString& b) {
    const QStringList x = a.split('.'), y = b.split('.');
    for (int i = 0; i < std::max(x.size(), y.size()); ++i) {
        const int p = i < x.size() ? x[i].toInt() : 0, q = i < y.size() ? y[i].toInt() : 0;
        if (p != q) return p > q;
    }
    return false;
}

bool Updater::installedCopy() {
#ifdef Q_OS_WIN
    const QString dir = QSettings("HKEY_CURRENT_USER\\Software\\Coreplay", QSettings::NativeFormat).value("InstallDir").toString();
    return !dir.isEmpty() &&
           QFileInfo(dir).canonicalFilePath().compare(QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath(),
                                                      Qt::CaseInsensitive) == 0;
#elif defined(Q_OS_MACOS)
    // Not from the .dmg (read-only) or a translocated copy (macOS runs a quarantined app from a
    // read-only place): only an app the user moved to a folder they can write to.
    const QString app = bundlePath();
    if (!app.endsWith(".app") || app.startsWith("/Volumes/") || app.contains("/AppTranslocation/")) return false;
    return QFileInfo(QFileInfo(app).absolutePath()).isWritable() && QFileInfo(app).isWritable();
#elif defined(Q_OS_LINUX)
    // The .deb is installed by apt as root: pkexec asks for the password in the system's dialog.
    if (debInstall())
        return !QStandardPaths::findExecutable("pkexec").isEmpty() && QFile::exists("/usr/bin/apt-get");
    const QString ai = appImagePath();
    return !ai.isEmpty() && QFileInfo(ai).isWritable() && QFileInfo(QFileInfo(ai).absolutePath()).isWritable();
#else
    return false;
#endif
}

// ECDSA P-256 over SHA-256 with the built-in key; the feed's signature is raw r||s, base64.
// Windows: its own CNG. macOS: Apple's Security framework. Linux: the system's OpenSSL
// (libcrypto, loaded only for this check: the same library Qt uses for https).
bool Updater::verify(const QByteArray& message, const QByteArray& signatureBase64) {
    const QByteArray sig = QByteArray::fromBase64(signatureBase64.trimmed());
    if (sig.size() != 64) return false;
#ifdef Q_OS_WIN
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) return false;
    QByteArray blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64, 0);
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    hdr->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), kUpdateKeyX, 32);
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + 32, kUpdateKeyY, 32);
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, reinterpret_cast<PUCHAR>(blob.data()),
                            ULONG(blob.size()), 0) == 0) {
        QByteArray digest = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
        ok = BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()),
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(sig.data())), ULONG(sig.size()), 0) == 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#elif defined(Q_OS_MACOS)
    const QByteArray point = publicKeyPoint(), der = derSignature(sig);
    CFDataRef keyData = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(point.data()), point.size());
    int bits = 256;
    CFNumberRef size = CFNumberCreate(nullptr, kCFNumberIntType, &bits);
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits};
    const void* values[] = {kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPublic, size};
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    SecKeyRef key = SecKeyCreateWithData(keyData, attrs, nullptr);
    CFDataRef msg = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(message.data()), message.size());
    CFDataRef signature = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(der.data()), der.size());
    const bool ok = key && SecKeyVerifySignature(key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, msg, signature, nullptr);
    for (CFTypeRef r : {CFTypeRef(keyData), CFTypeRef(size), CFTypeRef(attrs), CFTypeRef(key), CFTypeRef(msg), CFTypeRef(signature)})
        if (r) CFRelease(r);
    return ok;
#elif defined(Q_OS_LINUX)
    void* lib = dlopen("libcrypto.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libcrypto.so.1.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    // The EC_KEY calls, not d2i_PUBKEY + EVP_DigestVerify: inside Coreplay (the AppImage and the
    // .deb on Ubuntu 22.04) OpenSSL 3's key decoder refuses the key (EVP error 0x3000072), so
    // every release was refused. These calls need no decoder and are in OpenSSL 1.1 and 3.
    using KeyNew = void* (*)(int);
    using Oct2Key = int (*)(void*, const unsigned char*, size_t, void*);
    using Verify = int (*)(int, const unsigned char*, int, const unsigned char*, int, void*);
    using Free = void (*)(void*);
    auto keyNew = reinterpret_cast<KeyNew>(dlsym(lib, "EC_KEY_new_by_curve_name"));
    auto oct2key = reinterpret_cast<Oct2Key>(dlsym(lib, "EC_KEY_oct2key"));
    auto verifyFn = reinterpret_cast<Verify>(dlsym(lib, "ECDSA_verify"));
    auto keyFree = reinterpret_cast<Free>(dlsym(lib, "EC_KEY_free"));
    bool ok = false;
    if (keyNew && oct2key && verifyFn && keyFree) {
        constexpr int kPrime256v1 = 415;  // NID_X9_62_prime256v1
        void* key = keyNew(kPrime256v1);
        const QByteArray point = publicKeyPoint(), der = derSignature(sig);
        const QByteArray digest = QCryptographicHash::hash(message, QCryptographicHash::Sha256);
        if (key && oct2key(key, reinterpret_cast<const unsigned char*>(point.data()), size_t(point.size()), nullptr) == 1)
            ok = verifyFn(0, reinterpret_cast<const unsigned char*>(digest.data()), int(digest.size()),
                          reinterpret_cast<const unsigned char*>(der.data()), int(der.size()), key) == 1;
        if (key) keyFree(key);
    }
    dlclose(lib);
    return ok;
#else
    Q_UNUSED(message);
    return false;
#endif
}

void Updater::fail(const QString& msg) {
    state_ = "failed";
    emit failed(msg);
}

void Updater::check() {
    if (state_ == "checking" || state_ == "downloading") return;
    state_ = "checking";
    const QString base = feedUrl();
    const QUrl url(base);
    // Plain http only for a local test feed; the manifest is signed either way.
    if (url.scheme() != "https" && !(url.scheme() == "http" && (url.host() == "127.0.0.1" || url.host() == "localhost"))) {
        state_ = "failed";
        emit checked(false, tr("The update address must be https."));
        return;
    }
    auto get = [this](const QString& u) {
        QNetworkRequest rq{QUrl(u)};
        rq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        rq.setTransferTimeout(20000);
        return net_->get(rq);
    };
    QNetworkReply* manifest = get(base + "latest.json");
    QNetworkReply* signature = get(base + "latest.json.sig");
    auto pending = std::make_shared<int>(2);
    auto done = [this, manifest, signature, pending] {
        if (--*pending) return;
        manifest->deleteLater();
        signature->deleteLater();
        QSettings().setValue("updates/lastCheck", QDateTime::currentDateTimeUtc());
        if (manifest->error() || signature->error()) {
            state_ = "failed";
            emit checked(false, tr("Cannot reach the update server: %1")
                                    .arg(manifest->error() ? manifest->errorString() : signature->errorString()));
            return;
        }
        const QByteArray body = manifest->readAll();
        if (!verify(body, signature->readAll())) {
            state_ = "failed";
            //: Keep the product name in English.
            emit checked(false, tr("The update information is not signed by imcore. Nothing was downloaded."));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        latest_ = {o.value("version").toString(), o.value("file").toString(), o.value("sha256").toString().toLower(),
                   o.value("notes").toString(), qint64(o.value("size").toDouble())};
        // The file name comes from the signed manifest; still keep it a plain name.
        if (latest_.version.isEmpty() || latest_.file.isEmpty() || latest_.file.contains('/') || latest_.file.contains('\\')) {
            state_ = "failed";
            emit checked(false, tr("The update information is incomplete."));
            return;
        }
        state_ = updateAvailable() ? "available" : "current";
        emit checked(true, {});
    };
    connect(manifest, &QNetworkReply::finished, this, done);
    connect(signature, &QNetworkReply::finished, this, done);
}

void Updater::download() {
    if (!updateAvailable() || state_ == "downloading") return;
    state_ = "downloading";
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/Coreplay-update";
    QDir().mkpath(dir);
    part_ = new QFile(dir + "/" + latest_.file + ".part", this);
    if (!part_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Cannot write %1").arg(part_->fileName()));
        return;
    }
    QNetworkRequest rq{QUrl(feedUrl() + QUrl::toPercentEncoding(latest_.file))};
    rq.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    reply_ = net_->get(rq);
    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    connect(reply_, &QNetworkReply::readyRead, this, [this, hash] {
        const QByteArray chunk = reply_->readAll();
        hash->addData(chunk);
        part_->write(chunk);
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        emit progress(got, total > 0 ? total : latest_.size);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, hash] {
        const QByteArray rest = reply_->readAll();
        hash->addData(rest);
        part_->write(rest);
        part_->close();
        const bool netError = reply_->error() != QNetworkReply::NoError;
        const QString netMsg = reply_->errorString();
        reply_->deleteLater();
        reply_ = nullptr;
        const QString partName = part_->fileName();
        const qint64 size = part_->size();
        part_->deleteLater();
        part_ = nullptr;
        if (state_ != "downloading") { QFile::remove(partName); return; }  // cancelled
        if (netError) { QFile::remove(partName); fail(tr("Download failed: %1").arg(netMsg)); return; }
        if (size != latest_.size || QString(hash->result().toHex()) != latest_.sha256) {
            QFile::remove(partName);
            fail(tr("The downloaded file does not match the signed release. It was deleted."));
            return;
        }
        QString final = partName.chopped(5);  // drop ".part"
        QFile::remove(final);
        QFile::rename(partName, final);
        state_ = "ready";
        emit ready(final);
    });
}

void Updater::cancel() {
    if (state_ != "downloading") return;
    state_ = "idle";
    if (reply_) reply_->abort();
}

// Starts the verified update. The caller closes Coreplay when this returns true:
// Windows: the installer runs silently, waits for Coreplay to close and starts it again.
// macOS: a shell step waits for this process to end, puts the .dmg's Coreplay.app in place of
// this one (the old app comes back if the copy fails) and starts it.
// Linux: the new AppImage is put next to this one now; after Coreplay ends it takes its name
// and starts. The .deb: after Coreplay ends, apt installs it (pkexec asks for the password)
// and Coreplay starts again, the new version or, when the password was refused, the old one.
bool Updater::install(const QString& file, QString* error) {
#if defined(Q_OS_WIN)
    if (QProcess::startDetached(file, {"/S", "/RUN"})) return true;
    if (error) *error = tr("The installer could not start: %1").arg(file);
    return false;
#elif defined(Q_OS_MACOS)
    static const char* script = R"SH(pid="$1"; dmg="$2"; app="$3"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
mnt="$(mktemp -d)" || exit 1
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$mnt" "$dmg" || exit 1
new="$app.update.$$"; old="$app.old.$$"
if ditto "$mnt/Coreplay.app" "$new"; then
  mv "$app" "$old" && { mv "$new" "$app" || mv "$old" "$app"; }
fi
rm -rf "$new" "$old"
hdiutil detach -quiet "$mnt"; rmdir "$mnt"; rm -f "$dmg"
open "$app")SH";
    const QStringList args{"-c", script, "coreplay-update", QString::number(QCoreApplication::applicationPid()), file,
                           bundlePath()};
    if (QProcess::startDetached("/bin/sh", args)) return true;
    if (error) *error = tr("The update could not start.");
    return false;
#elif defined(Q_OS_LINUX)
    if (debInstall()) {
        static const char* debScript = R"SH(pid="$1"; deb="$2"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
pkexec /usr/bin/apt-get install -y -q -o DPkg::Lock::Timeout=120 "$deb"
rm -f "$deb"
exec setsid /usr/bin/coreplay)SH";
        const QStringList args{"-c", debScript, "coreplay-update", QString::number(QCoreApplication::applicationPid()),
                               QFileInfo(file).absoluteFilePath()};
        if (QProcess::startDetached("/bin/sh", args)) return true;
        if (error) *error = tr("The update could not start.");
        return false;
    }
    const QString target = appImagePath(), next = target + ".update";
    QFile::remove(next);
    if (!QFile::copy(file, next) ||
        !QFile::setPermissions(next, QFile::permissions(target) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther)) {
        if (error) *error = tr("Cannot write %1").arg(next);
        return false;
    }
    QFile::remove(file);
    static const char* script = R"SH(pid="$1"; next="$2"; target="$3"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
mv -f "$next" "$target" && exec setsid "$target")SH";
    const QStringList args{"-c", script, "coreplay-update", QString::number(QCoreApplication::applicationPid()), next,
                           target};
    if (QProcess::startDetached("/bin/sh", args)) return true;
    if (error) *error = tr("The update could not start.");
    return false;
#else
    Q_UNUSED(file);
    if (error) *error = tr("Updates are not available on this system.");
    return false;
#endif
}
