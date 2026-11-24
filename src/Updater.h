#pragma once
// In-app updates from the signed feed (https://auth.imcore.ai/coreplay-updates/<channel>/:
// windows, mac-arm64, mac-x64, linux-x86_64 for the AppImage).
// latest.json is trusted only when latest.json.sig verifies against the
// public key built into Coreplay (ECDSA P-256 / SHA-256), and the downloaded installer
// only when its size and SHA-256 match that signed manifest.
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

class Updater : public QObject {
    Q_OBJECT
public:
    struct Release {
        QString version, file, sha256, notes;
        qint64 size = 0;
    };

    explicit Updater(QObject* parent = nullptr);
    static QString feedUrl();                // COREPLAY_UPDATE_URL, else the imCore feed
    static QString currentVersion();
    static bool newer(const QString& a, const QString& b);  // a > b
    // True when this system has an update channel (Linux: the AppImage only, not the .deb).
    static bool supported();
    // True when this copy is the installed one (it can update itself).
    static bool installedCopy();
    // Starts the verified update (installer, .dmg or AppImage); Coreplay closes after true.
    static bool install(const QString& file, QString* error);

    void check();                            // -> checked()
    void download();                         // after check(): -> progress(), ready() / failed()
    void cancel();
    const Release& latest() const { return latest_; }
    bool updateAvailable() const { return !latest_.version.isEmpty() && newer(latest_.version, currentVersion()); }
    QString state() const { return state_; }  // idle / checking / current / available / downloading / ready / failed

signals:
    void checked(bool ok, const QString& error);
    void progress(qint64 received, qint64 total);
    void ready(const QString& installerPath);
    void failed(const QString& error);

private:
    void fail(const QString& msg);
    static bool verify(const QByteArray& message, const QByteArray& signatureBase64);
    QNetworkAccessManager* net_;
    QNetworkReply* reply_ = nullptr;
    QFile* part_ = nullptr;
    Release latest_;
    QString state_ = "idle";
};
