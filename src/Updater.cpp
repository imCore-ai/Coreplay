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

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
constexpr char kDefaultFeed[] = "https://auth.imcore.ai/coreplay-updates/windows/";
}

Updater::Updater(QObject* parent) : QObject(parent), net_(new QNetworkAccessManager(this)) {}

QString Updater::feedUrl() {
    QString u = qEnvironmentVariable("COREPLAY_UPDATE_URL", kDefaultFeed);
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
#else
    return false;
#endif
}

// ECDSA P-256 over SHA-256, raw r||s, checked by Windows' own CNG with the built-in key.
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
#else
    Q_UNUSED(message);
    return false;  // macOS / Linux: verification to be added with those ports
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
