#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QUrl>
#include <QList>
#include <QStringList>
#include <QHash>

struct LocalModelFile {
    QString relativePath;
    QUrl url;
    QString sha256; // expected SHA-256 hex digest; empty = not verified
};

struct LocalModelSpec {
    QString engine;
    QString id;
    QString name;
    QString description;
    QList<LocalModelFile> files;
    QString archiveName;
    QStringList requiredFiles;
    bool archive = false;
    QString category;
    QString sizeLabel;
    int speedScore = 3;
    int accuracyScore = 3;
    QStringList tags;
};

struct LocalEngineSpec {
    QString id;
    QString name;
    QString description;
    QString installedVersion;
    QUrl releaseApi;
    QUrl releaseUrl;
};

class LocalModelManager : public QObject {
    Q_OBJECT
public:
    explicit LocalModelManager(QObject *parent = nullptr);
    static QList<LocalModelSpec> catalog();
    static QList<LocalEngineSpec> engines();
    static QStringList supportedLanguageCodes(const QString &engine, const QString &modelId);
    static QString languageDisplayName(const QString &code);
    static QString defaultRoot();
    static QString modelDir(const QString &engine, const QString &id, const QString &root = {});
    static bool isInstalled(const LocalModelSpec &spec, const QString &root = {});
    QStringList installed(const QString &engine, const QString &root = {});
    void download(const LocalModelSpec &spec, const QString &root = {});
    void cancel();
    bool remove(const LocalModelSpec &spec, const QString &root = {});
    bool importFolder(const LocalModelSpec &spec, const QString &source, const QString &root, QString *error = nullptr);
    void checkEngineUpdates();
signals:
    void progress(const QString &engine, const QString &id, qint64 received, qint64 total);
    void finished(const QString &engine, const QString &id, bool ok, const QString &message);
    void engineUpdateFinished(const QString &engine, bool ok, const QString &installedVersion,
                              const QString &latestVersion, bool updateAvailable, const QString &message);
private:
    void downloadNext();
    void finish(bool ok, const QString &message);
    void fail(const QString &message);
    class QNetworkAccessManager *m_network = nullptr;
    class QNetworkReply *m_reply = nullptr;
    class QFile *m_file = nullptr;
    LocalModelSpec m_current;
    QString m_root;
    int m_fileIndex = -1;
    qint64 m_doneBytes = 0;
    qint64 m_totalBytes = 0;
    class QProcess *m_extractProcess = nullptr;
    QHash<QString, class QNetworkReply *> m_updateReplies;
    // Install cache: key = engine+sep+root, val = list of installed model ids.
    // invalidated on download/remove/importFolder/finish/fail.
    QHash<QString, QStringList> m_installedCache;
    QString installedCacheKey(const QString &engine, const QString &root) const;
    // Download watchdog: aborts when no bytes have arrived for a long period
    // (slow links used to be killed by a fixed 30-minute wall-clock cap).
    class QTimer *m_stallTimer = nullptr;
    QElapsedTimer m_activityTimer;
    QElapsedTimer m_overallTimer;
    qint64 m_lastActivityBytes = -1;
    bool m_userCancelled = false;
    // Host of a redirect we refused (not in the download allowlist), recorded
    // so the abort can be reported by cause instead of "Operation canceled".
    QString m_redirectRejectedHost;
    void startStallWatchdog();
    void stopStallWatchdog();
};
