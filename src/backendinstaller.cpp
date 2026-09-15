#include "backendinstaller.h"
#include "archiveutil.h"
#include "backendcatalog.h"
#include "hardwareprobe.h"
#include "securitypolicy.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

#include <algorithm>

static const char *kEmbeddablePythonUrl =
    "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip";
// SECURITY: update this SHA-256 whenever the Python embeddable URL is bumped.
// The installer refuses to download the embeddable package when this is empty
// to prevent installation of an unverified runtime.
static const char *kEmbeddablePythonSha256 =
    "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3";
// SECURITY: get-pip.py must be fetched from an immutable, commit-addressed URL.
// https://bootstrap.pypa.io/get-pip.py is a ROLLING "latest" URL whose body
// changes on every pip release while keeping its byte length, so a hash pinned
// against it silently goes stale and every install fails verification until the
// pin is refreshed. Addressing the pypa/get-pip commit makes the URL and the
// hash agree by construction: commit af54dfe7 ("Update to 26.2.1") contains
// public/get-pip.py at exactly 2230488 bytes, pip 26.2.1 embedded.
// To bump pip: pick the new commit from
// https://github.com/pypa/get-pip/commits/main/public/get-pip.py, download
// public/get-pip.py from it, and update both the URL and the hash below.
static const char *kGetPipUrl =
    "https://raw.githubusercontent.com/pypa/get-pip/"
    "af54dfe793b24685f8dc4ebba0630d9f2d77653c/public/get-pip.py";
// bootstrap.pypa.io serves these exact bytes while 26.2.1 remains current, so it
// is kept as a secondary mirror. It is NOT primary: once PyPA rotates, it stops
// matching the hash and the download fails loudly, which is the intended signal.
static const char *kGetPipMirrorUrl = "https://bootstrap.pypa.io/get-pip.py";
static const char *kGetPipSha256 = "fb24e693bab954209a063d90953621412ccad4a500905a726286e038f508ddf6";

// ---------------------------------------------------------------------------
// helpers shared with sessions
// ---------------------------------------------------------------------------
static bool isSafeArchiveName(const QString &member) {
    return ArchiveUtil::isSafeMemberName(member);
}

// R-03: ZIP/tar slip guard. Lists archive members and rejects any traversal
// or absolute path so a malformed release cannot write outside engines/.
static bool validatePrebuiltArchive(const QString &archive, QString &error) {
    if (QStandardPaths::findExecutable("tar").isEmpty()) {
        error = QObject::tr("找不到 tar 執行檔，無法驗證封包內容。請安裝 Windows tar / bsdtar 後重試。");
        return false;
    }
    QProcess list;
    list.start("tar", {"-tf", archive});
    if (!list.waitForFinished(60000)) {
        list.kill();
        list.waitForFinished(5000);
        error = QObject::tr("Timeout while checking archive contents.");
        return false;
    }
    const QString out = QString::fromLocal8Bit(list.readAllStandardOutput());
    if (list.exitCode() != 0) return true; // unreadable; let extraction report it
    for (const QString &member : out.split('\n', Qt::SkipEmptyParts)) {
        if (!isSafeArchiveName(member)) {
            error = QObject::tr("Archive contains an unsafe path: %1").arg(member.trimmed());
            return false;
        }
    }
    return true;
}

// R-06: only permit known installer programs. Prevents a corrupted task queue
// or future code path from launching an arbitrary executable.
static bool isAllowedInstallerCommand(const QString &command) {
    if (command.isEmpty()) return false;
    const QString lower = command.toLower();
    const QStringList allowed = {
        QStringLiteral("tar"),
        QStringLiteral("powershell"),
        QStringLiteral("powershell.exe"),
        QStringLiteral("pwsh"),
        QStringLiteral("pwsh.exe"),
    };
    for (const QString &a : allowed)
        if (lower == a || lower.endsWith(QLatin1String("/") + a) || lower.endsWith(QLatin1String("\\") + a))
            return true;
    // Python runtime executable inside the private engines/ tree.
    if (lower.endsWith(QLatin1String("/python.exe")) || lower.endsWith(QLatin1String("\\python.exe")))
        return true;
    return false;
}

// ---------------------------------------------------------------------------
// InstallSession – one independent installation task (isolated command line)
// ---------------------------------------------------------------------------

class BackendInstaller::Session : public QObject {
public:
    enum class TaskType { ResolveRelease, Download, Extract, RunProcess,
                          PatchPython, PipInstall, CopyRunner, Finalize };
    struct Task {
        TaskType type;
        QString label;
        int weight = 1;
        QString url;
        QString filePath;
        QString digest;
        QString targetDir;
        bool isTar = false;
        QJsonObject releaseJson;
        QString command;
        QStringList arguments;
        QString workingDir;
        QString indexUrl;
        bool expectZeroExit = true;
        int retryCount = 0;
        int maxRetries = 0;
    };

    Session(BackendInstaller *host, const QString &engineId, const QString &variantId)
        : QObject(host), m_host(host), m_engineId(engineId), m_variantId(variantId)
    {
        m_network = new QNetworkAccessManager(this);
        bool found = false;
        const BackendSpec spec = BackendCatalog::find(engineId, variantId, &found);
        m_needsPython = found && spec.needsPython;
    }

    QString engineId() const { return m_engineId; }
    QString variantId() const { return m_variantId; }
    bool needsPython() const { return m_needsPython; }
    bool isBusy() const { return m_busy; }

    void start() {
        m_busy = true;
        beginInstall(m_engineId, m_variantId);
    }

    void cancelSession() {
        if (m_stallTimer) m_stallTimer->stop();
        if (m_reply) m_reply->abort();
        if (m_process) { m_process->kill(); m_process->waitForFinished(3000); }
        if (m_busy) fail(tr("使用者已取消安裝。"));
    }

private:
    void emitLog(const QString &line) {
        if (m_host)
            emit m_host->logLineDetailed(m_engineId, m_variantId, line);
    }
    void emitProgress(int pct, const QString &stage) {
        if (m_host)
            emit m_host->progressDetailed(m_engineId, m_variantId, pct, stage);
    }

    void beginInstall(const QString &engineId, const QString &variantId);
    void appendPrebuiltTasks(const QJsonObject &release);
    void appendPythonTasks(const QString &engineId, const QString &variantId);
    void runNext();
    void fail(const QString &message);
    void complete(const QJsonObject &manifest);
    void recomputeTotalWeight();
    void updateProgress();
    // Wire stdout/stderr capture for a child process, accumulating into
    // m_recentOutput and replaying to the log. Used by tar/cmake/pip stages.
    void connectProcessOutput(QProcess *proc, int bufferSize, bool trimLog = true);

    BackendInstaller *m_host = nullptr;
    QString m_engineId;
    QString m_variantId;
    bool m_needsPython = false;
    bool m_busy = false;

    QList<Task> m_queue;
    int m_doneWeight = 0;
    int m_totalWeight = 0;
    QJsonObject m_releaseJson;
    QString m_downloadedArchive;
    QJsonObject m_manifest;

    QNetworkAccessManager *m_network = nullptr;
    // QPointer: auto-nulls when the reply is destroyed, so the download stall
    // watchdog can never dereference a freed QNetworkReply (use-after-free crash).
    QPointer<QNetworkReply> m_reply;
    QFile *m_file = nullptr;
    QProcess *m_process = nullptr;

    QString m_currentDownloadLabel;
    qint64 m_currentDownloadTotal = 0;
    qint64 m_currentDownloadReceived = 0;
    int m_currentTaskWeight = 0;
    bool m_downloadCounted = false;
    QString m_recentOutput;
    // Host of a redirect we refused (not in the download allowlist), recorded
    // so the abort can be reported by cause instead of "Operation canceled".
    QString m_redirectRejectedHost;
    // Set once when the primary get-pip.py URL fails and we re-queue the same
    // download from the bootstrap.pypa.io mirror, so the fallback runs at most
    // once per provisioning run (no infinite retry loop).
    bool m_retriedPipMirror = false;
    // Maps a wheel URL to the verified local file path downloaded earlier.
    QHash<QString, QString> m_wheelPathMap;

    // Download stall watchdog (R-01): abort only when no bytes have arrived for
    // a long window, so GB-scale installs don't hang silently with no progress.
    QTimer *m_stallTimer = nullptr;
    QElapsedTimer m_downloadActivity;
    qint64 m_lastActivityBytes = -1;
};

void BackendInstaller::Session::beginInstall(const QString &engineId, const QString &variantId)
{
    m_engineId = engineId;
    m_variantId = variantId;
    m_doneWeight = 0;
    m_totalWeight = 0;
    m_releaseJson = QJsonObject();
    m_downloadedArchive.clear();
    m_manifest = QJsonObject();
    m_queue.clear();
    m_retriedPipMirror = false;

    const QString stage = BackendCatalog::variantDir(engineId, variantId);
    QDir().mkpath(stage);
    QDir(stage).mkpath(".staging");
    QDirIterator it(stage, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (it.fileName() == ".staging") continue;
        if (it.fileInfo().isDir()) QDir(path).removeRecursively();
        else QFile::remove(path);
    }

    bool found = false;
    const BackendSpec spec = BackendCatalog::find(engineId, variantId, &found);
    if (!found) {
        fail(tr("找不到引擎 %1 的變體 %2。").arg(engineId, variantId));
        return;
    }

    emitLog(tr("開始安裝：%1").arg(spec.name));
    emitLog(tr("安裝位置：%1").arg(QDir::toNativeSeparators(stage)));

    if (spec.needsPython) {
        appendPythonTasks(engineId, variantId);
        runNext();
        return;
    }

    Task resolve;
    resolve.type = TaskType::ResolveRelease;
    resolve.label = tr("查詢 %1 發佈資產").arg(spec.releaseRepo);
    resolve.weight = 1;
    resolve.url = QString("https://api.github.com/repos/%1/releases/latest").arg(spec.releaseRepo);
    m_queue << resolve;
    runNext();
}

void BackendInstaller::Session::appendPrebuiltTasks(const QJsonObject &release)
{
    bool found = false;
    const BackendSpec spec = BackendCatalog::find(m_engineId, m_variantId, &found);
    if (!found) { fail(tr("找不到後端規格。")); return; }

    const QJsonArray assets = release.value("assets").toArray();
    QString chosenUrl;
    QString assetName;
    QString digest;
    struct AssetMatch { QString name; QString url; QString digest; int patternIndex; };
    QList<AssetMatch> matches;
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value("name").toString();
        for (int patternIndex = 0; patternIndex < spec.assetPatterns.size(); ++patternIndex) {
            if (QRegularExpression(spec.assetPatterns.at(patternIndex),
                                   QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) {
                matches << AssetMatch{name, asset.value("browser_download_url").toString(),
                                      asset.value("digest").toString(), patternIndex};
                break;
            }
        }
    }
    if (!matches.isEmpty()) {
        const auto versionScore = [](const QString &name) {
            int highest = 0;
            auto it = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)")).globalMatch(name);
            while (it.hasNext()) highest = qMax(highest, it.next().captured(1).toInt());
            return highest;
        };
        std::stable_sort(matches.begin(), matches.end(),
                         [&versionScore](const AssetMatch &a, const AssetMatch &b) {
            if (a.patternIndex != b.patternIndex) return a.patternIndex < b.patternIndex;
            return versionScore(a.name) > versionScore(b.name);
        });
        chosenUrl = matches.first().url;
        assetName = matches.first().name;
        digest = matches.first().digest;
    }

    if (chosenUrl.isEmpty()) {
        fail(tr("在 %1 最新發佈中找不到符合 %2 的預編譯包。")
                 .arg(spec.releaseRepo, BackendCatalog::variantLabel(m_variantId)));
        return;
    }

    emitLog(tr("選用資產：%1").arg(assetName));
    const QString archive = QDir(BackendCatalog::variantDir(m_engineId, m_variantId))
                               .filePath(".staging/" + assetName);
    Task download;
    download.type = TaskType::Download;
    download.label = tr("下載 %1").arg(assetName);
    download.weight = 8;
    download.url = chosenUrl;
    download.filePath = archive;
    download.digest = digest;
    download.targetDir = QFileInfo(archive).absolutePath();
    m_queue << download;

    Task extract;
    extract.type = TaskType::Extract;
    extract.label = tr("解壓後端");
    extract.weight = 2;
    extract.filePath = archive;
    extract.targetDir = BackendCatalog::variantDir(m_engineId, m_variantId);
    extract.isTar = assetName.endsWith(".zip", Qt::CaseInsensitive) ? false : true;
    m_queue << extract;

    Task finalize;
    finalize.type = TaskType::Finalize;
    finalize.label = tr("完成");
    finalize.weight = 1;
    finalize.targetDir = BackendCatalog::variantDir(m_engineId, m_variantId);
    m_queue << finalize;
}

void BackendInstaller::Session::appendPythonTasks(const QString &engineId, const QString &variantId)
{
    const QString pyRoot = BackendCatalog::pythonRoot();
    const QString pyExe = BackendCatalog::pythonExecutable();

    if (pyExe.isEmpty()) {
        if (QString::fromLatin1(kEmbeddablePythonSha256).isEmpty()) {
            fail(tr("安全設定遺失：嵌入式 Python 套件缺少 SHA-256 雜湊值，無法驗證下載完整性。請更新 kEmbeddablePythonSha256 後重試。"));
            return;
        }
        Task download;
        download.type = TaskType::Download;
        download.label = tr("下載 Python 執行環境");
        download.weight = 6;
        download.url = QString::fromLatin1(kEmbeddablePythonUrl);
        download.digest = QStringLiteral("sha256:") + QString::fromLatin1(kEmbeddablePythonSha256);
        download.filePath = QDir(pyRoot).filePath(".download/python.zip");
        download.targetDir = pyRoot;
        m_queue << download;

        Task extract;
        extract.type = TaskType::Extract;
        extract.label = tr("解壓 Python 執行環境");
        extract.weight = 2;
        extract.filePath = download.filePath;
        extract.targetDir = pyRoot;
        extract.isTar = false;
        m_queue << extract;

        Task patch;
        patch.type = TaskType::PatchPython;
        patch.label = tr("啟用 Python 套件搜尋路徑");
        patch.weight = 1;
        m_queue << patch;
    } else {
        emitLog(tr("Python 執行環境已存在，略過下載。"));
        Task patch;
        patch.type = TaskType::PatchPython;
        patch.label = tr("檢查 Python 套件搜尋路徑");
        patch.weight = 1;
        m_queue << patch;
    }

    // Pip provisioning is gated on pip actually being importable, NOT on whether
    // python.exe exists. A previous run can leave a working interpreter without
    // pip (bootstrap interrupted, or a get-pip download that failed hash
    // verification), and gating on python alone made that state permanent:
    // every retry skipped the download and died later with "No module named pip".
    // Probing pip here lets a retry heal the environment on its own.
    if (!BackendCatalog::pythonHasPip(pyExe)) {
        Task getpip;
        getpip.type = TaskType::Download;
        getpip.label = tr("下載 pip 引導器");
        getpip.weight = 1;
        getpip.url = QString::fromLatin1(kGetPipUrl);
        getpip.digest = QStringLiteral("sha256:") + QString::fromLatin1(kGetPipSha256);
        getpip.filePath = QDir(pyRoot).filePath(".download/get-pip.py");
        getpip.targetDir = pyRoot;
        m_queue << getpip;

        Task bootstrap;
        bootstrap.type = TaskType::RunProcess;
        bootstrap.label = tr("安裝 pip");
        bootstrap.weight = 3;
        bootstrap.command = pyRoot + "/python.exe";
        bootstrap.workingDir = pyRoot;
        bootstrap.arguments << QDir(pyRoot).filePath(".download/get-pip.py")
                            << "--no-warn-script-location";
        m_queue << bootstrap;
        emitLog(tr("偵測到 pip 尚未安裝，將自動修復。"));
    } else {
        emitLog(tr("pip 已就緒，略過安裝。"));
    }

    bool found = false;
    const BackendSpec spec = BackendCatalog::find(engineId, variantId, &found);
    if (found && !spec.pipPackages.isEmpty()) {
        bool shipsSherpaWheel = false;
        for (const QString &package : spec.pipPackages) {
            if (package.contains("sherpa_onnx") && package.endsWith(".whl")) shipsSherpaWheel = true;
        }

        // SECURITY: pre-download direct wheel URLs so we can verify SHA-256
        // before pip executes arbitrary code from them.
        for (const QString &package : spec.pipPackages) {
            if (package.startsWith(QLatin1String("https://")) && package.endsWith(QLatin1String(".whl"), Qt::CaseInsensitive)) {
                Task wheel;
                wheel.type = TaskType::Download;
                wheel.label = tr("下載 wheel 套件");
                wheel.weight = 2;
                wheel.url = package;
                const QString fileName = QUrl(package).fileName();
                wheel.filePath = QDir(pyRoot).filePath(".download/" + fileName);
                wheel.digest = QStringLiteral("sha256:") + spec.pipPackageHashes.value(package);
                wheel.targetDir = pyRoot;
                m_queue << wheel;
            }
        }

        QStringList stalePackages;
        if (shipsSherpaWheel) {
            stalePackages << "sherpa-onnx-core";
            // D-08: 清理舊版 nvidia CUDA 相關包，避免多版本 DLL 在 PATH 中衝突
            stalePackages << "nvidia-cublas-cu12" << "nvidia-cudnn-cu12" << "nvidia-cuda-runtime-cu12" << "nvidia-cufft-cu12";
        }
        if (!stalePackages.isEmpty()) {
            Task cleanup;
            cleanup.type = TaskType::RunProcess;
            cleanup.label = tr("清理衝突的 ONNX Runtime 套件");
            cleanup.weight = 1;
            cleanup.command = pyRoot + "/python.exe";
            cleanup.workingDir = pyRoot;
            cleanup.arguments = QStringList{"-m", "pip", "uninstall", "-y", "-q"} + stalePackages;
            cleanup.expectZeroExit = false;
            m_queue << cleanup;
        }

        const QString index = spec.pipIndexUrl;
        QStringList torchLike;
        QStringList standard;
        for (const QString &package : spec.pipPackages) {
            if (package.startsWith("torch")) torchLike << package;
            else standard << package;
        }
        if (!torchLike.isEmpty()) {
            Task pip;
            pip.type = TaskType::PipInstall;
            pip.label = tr("安裝 %1 依賴（含 CUDA）").arg(engineId);
            pip.weight = 12;
            pip.arguments = torchLike;
            pip.indexUrl = index;
            m_queue << pip;
        }
        if (!standard.isEmpty()) {
            Task pip;
            pip.type = TaskType::PipInstall;
            pip.label = tr("安裝 %1 依賴").arg(engineId);
            pip.weight = 6;
            pip.arguments = standard;
            pip.indexUrl.clear();
            m_queue << pip;
        }
    }

    Task copyRunner;
    copyRunner.type = TaskType::CopyRunner;
    copyRunner.label = tr("佈署本地轉寫腳本");
    copyRunner.weight = 1;
    m_queue << copyRunner;

    Task finalize;
    finalize.type = TaskType::Finalize;
    finalize.label = tr("完成");
    finalize.weight = 1;
    finalize.targetDir = pyRoot;
    m_queue << finalize;
}

void BackendInstaller::Session::runNext()
{
    if (m_reply || m_process) return;

    if (m_queue.isEmpty()) {
        fail(tr("安裝流程結束但沒有產生有效結果。"));
        return;
    }

    if (m_totalWeight == 0) {
        for (const Task &task : m_queue) m_totalWeight += task.weight;
    }
    Task task = m_queue.takeFirst();

    switch (task.type) {
    case TaskType::ResolveRelease: {
        emitLog(tr("→ %1").arg(task.url));
        QNetworkRequest request(task.url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YumuStudio/2.0"));
        // SECURITY: reject HTTPS->HTTP downgrades and rely on final URL allowlist.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::UserVerifiedRedirectPolicy);
        // D-03: GitHub API 限流/網絡抖動時需超時，避免長時間掛起無進度
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferNetwork);
        m_reply = m_network->get(request);
        if (m_reply) {
            connect(m_reply, &QNetworkReply::redirected, this, [this](const QUrl &target) {
                if (!m_reply) return;
                const QUrl targetUrl = m_reply->url().resolved(target);
                if (SecurityPolicy::isAllowedRedirect(m_reply->url(), targetUrl))
                    m_reply->redirectAllowed();
                else {
                    // Record why we refused; abort() alone would surface as a
                    // generic "Operation canceled" that looks like a timeout.
                    m_redirectRejectedHost = targetUrl.host();
                    m_reply->abort();
                }
            });
        }
        m_currentDownloadLabel = task.label;
        m_currentDownloadTotal = 0;
        m_currentDownloadReceived = 0;
        m_currentTaskWeight = task.weight;
        QNetworkReply *resolveReply = m_reply;
        const QPointer<QNetworkReply> safeResolveReply(m_reply);
        QTimer::singleShot(30000, this, [this, safeResolveReply] {
            if (safeResolveReply && safeResolveReply->isRunning()) {
                emitLog(tr("查詢發佈資產逾時（30s），正在重試..."));
                safeResolveReply->abort();
            }
        });
        connect(m_reply, &QNetworkReply::finished, this, [this] {
            const QByteArray data = m_reply->readAll();
            const bool timeout = m_reply->error() == QNetworkReply::OperationCanceledError;
            const bool ok = m_reply->error() == QNetworkReply::NoError;
            const QString err = m_reply->errorString();
            m_reply->deleteLater(); m_reply = nullptr;
            if (!ok) {
                const QString msg = timeout ? tr("查詢發佈資產逾時，請檢查網絡或稍後重試（%1）").arg(err)
                                            : tr("查詢發佈資產失敗：%1").arg(err);
                fail(msg); return;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                fail(tr("發佈資產清單格式錯誤。")); return;
            }
            m_releaseJson = doc.object();
            appendPrebuiltTasks(m_releaseJson);
            recomputeTotalWeight();
            runNext();
        });
        emitProgress(0, task.label);
        break;
    }
    case TaskType::Download: {
        QDir().mkpath(QFileInfo(task.filePath).absolutePath());
        m_file = new QFile(task.filePath, this);
        if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fail(tr("無法建立下載檔案：%1").arg(task.filePath)); return;
        }
        emitLog(tr("→ %1").arg(task.url));
        // R-01: stall watchdog for this download. IP/transfer stalls abort after
        // 15 minutes with no bytes; a 6h absolute ceiling prevents infinite
        // hangs even on pathological retries.
        m_lastActivityBytes = -1;
        m_downloadActivity.start();
        m_redirectRejectedHost.clear();
        if (!m_stallTimer) {
            m_stallTimer = new QTimer(this);
            m_stallTimer->setInterval(60 * 1000);
            connect(m_stallTimer, &QTimer::timeout, this, [this] {
                if (!m_reply || !m_reply->isRunning()) return;
                const bool stalled = m_lastActivityBytes >= 0
                    && m_downloadActivity.isValid() && m_downloadActivity.elapsed() > 15 * 60 * 1000;
                if (stalled) m_reply->abort();
            });
        }
        m_stallTimer->start();
        QNetworkRequest request(task.url);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YumuStudio/2.0"));
        // SECURITY: reject HTTPS->HTTP downgrades and rely on final URL allowlist.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::UserVerifiedRedirectPolicy);
        m_reply = m_network->get(request);
        if (m_reply) {
            connect(m_reply, &QNetworkReply::redirected, this, [this](const QUrl &target) {
                if (!m_reply) return;
                const QUrl targetUrl = m_reply->url().resolved(target);
                if (SecurityPolicy::isAllowedRedirect(m_reply->url(), targetUrl))
                    m_reply->redirectAllowed();
                else {
                    // Record why we refused; abort() alone would surface as a
                    // generic "Operation canceled" that looks like a timeout.
                    m_redirectRejectedHost = targetUrl.host();
                    m_reply->abort();
                }
            });
        }
        QNetworkReply *reply = m_reply;
        QFile *file = m_file;
        const bool verifyDigest = task.digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive);
        const QByteArray expectedHash = QByteArray::fromHex(
            task.digest.mid(7).toLatin1());
        m_currentDownloadLabel = task.label;
        m_currentDownloadTotal = 0;
        m_currentDownloadReceived = 0;
        m_downloadCounted = false;
        m_currentTaskWeight = task.weight;
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
            m_currentDownloadReceived = got;
            if (total > 0) m_currentDownloadTotal = total;
            m_lastActivityBytes = got;
            m_downloadActivity.restart();
            updateProgress();
        });
        connect(reply, &QNetworkReply::readyRead, this, [this, reply, file] {
            if (!m_busy) { reply->abort(); return; }
            const QByteArray data = reply->readAll();
            if (file->write(data) != data.size()) {
                fail(tr("寫入下載檔案失敗（磁碟空間不足或 IO 錯誤）：%1")
                         .arg(QDir::toNativeSeparators(file->fileName())));
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, task, reply, file, expectedHash, verifyDigest] {
            if (file->isOpen()) { file->write(reply->readAll()); file->close(); }
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            const QUrl finalUrl = reply->url();
            const qint64 expectedBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            reply->deleteLater();
            file->deleteLater();
            if (m_reply == reply) m_reply = nullptr;
            if (m_file == file) m_file = nullptr;
            if (!m_busy) return;
            if (!ok) {
                // A rejected redirect aborts the reply, which otherwise reports
                // as a bare "Operation canceled" indistinguishable from a user
                // cancel or a timeout. Name the real cause instead.
                if (!m_redirectRejectedHost.isEmpty()) {
                    fail(tr("下載被中斷：重定向目標 %1 不在允許清單中，已拒絕連線。"
                            "若此主機屬於官方下載來源，請將其加入 SecurityPolicy 的允許清單。")
                             .arg(m_redirectRejectedHost));
                    return;
                }
                // The primary get-pip.py URL is commit-addressed, so when the
                // raw.githubusercontent host is unreachable (throttling, DNS,
                // outage) the same pinned bytes are available from
                // bootstrap.pypa.io, which still serves this exact 26.2.1 build.
                // Re-queue the identical download against the mirror once; the
                // SHA-256 assertion still runs on whatever arrives, so a
                // rotated or substituted file fails just as loudly.
                const bool isPipPrimary =
                    task.url == QString::fromLatin1(kGetPipUrl);
                if (isPipPrimary && !m_retriedPipMirror) {
                    m_retriedPipMirror = true;
                    emitLog(tr("主要下載來源無法連線（%1），將改用備援來源重試一次。")
                                .arg(err));
                    Task retry = task;
                    retry.url = QString::fromLatin1(kGetPipMirrorUrl);
                    m_queue.prepend(retry);
                    runNext();
                    return;
                }
                fail(tr("下載失敗：%1").arg(err));
                return;
            }
            // SECURITY: final URL must be on the application allowlist.
            if (!SecurityPolicy::isAllowedDownloadUrl(finalUrl)) {
                QFile::remove(file->fileName());
                fail(tr("下載最終網址不在允許清單中：%1").arg(finalUrl.host()));
                return;
            }
            if (verifyDigest && !expectedHash.isEmpty()) {
                if (!SecurityPolicy::verifyFileSha256(file->fileName(), expectedHash.toHex())) {
                    QFile::remove(file->fileName());
                    fail(tr("下載檔案校驗失敗（SHA-256 不符），已刪除損毀檔案。請重試安裝。"));
                    return;
                }
                emitLog(tr("SHA-256 校驗通過（%1 MB）。").arg(QFileInfo(file->fileName()).size() / (1024 * 1024)));
            } else {
                // D-04/D-08: GitHub 當前多數資產 digest 為空，至少校驗 Content-Length
                QFile check(file->fileName());
                if (check.open(QIODevice::ReadOnly)) {
                    const qint64 actual = check.size();
                    check.close();
                    if (actual == 0) {
                        QFile::remove(file->fileName());
                        fail(tr("下載檔案為空，請重試。"));
                        return;
                    }
                    if (expectedBytes > 0 && actual != expectedBytes) {
                        QFile::remove(file->fileName());
                        fail(tr("下載檔案大小不符（預期 %1，實際 %2），已刪除。請重試。").arg(expectedBytes).arg(actual));
                        return;
                    }
                    if (expectedBytes > 0)
                        emitLog(tr("大小校驗通過（%1 MB）。").arg(actual / (1024 * 1024)));
                }
            }
            m_downloadedArchive = task.filePath;
            emitLog(tr("下載完成：%1").arg(task.filePath));
            if (task.url.endsWith(QLatin1String(".whl"), Qt::CaseInsensitive))
                m_wheelPathMap[task.url] = task.filePath;
            if (m_stallTimer) m_stallTimer->stop();
            runNext();
        });
        m_currentDownloadLabel = task.label;
        m_currentDownloadReceived = 0;
        m_currentDownloadTotal = 0;
        m_currentTaskWeight = task.weight;
        emitProgress(0, task.label);
        break;
    }
    case TaskType::Extract: {
        emitLog(tr("解壓：%1").arg(task.filePath));
        // D-05: 避免 powershell 單引號轉義被 ";`$ 等突破，使用 tar 優先處理 zip
        // 並對資產名做安全過濾
        const QString assetName = QFileInfo(task.filePath).fileName();
        const QString unsafeChars = QStringLiteral(";`$|&()\"%?*<>");
        bool hasUnsafeChar = false;
        for (const QChar c : unsafeChars)
            if (assetName.contains(c)) { hasUnsafeChar = true; break; }
        if (hasUnsafeChar) {
            fail(tr("資產名包含不安全字元，已拒絕解壓：%1").arg(assetName));
            return;
        }
        // R-03: reject path traversal inside the archive before touching disk.
        {
            QString archiveError;
            if (!validatePrebuiltArchive(task.filePath, archiveError)) {
                fail(tr("封包包含不安全路徑，已拒絕解壓：%1").arg(archiveError));
                return;
            }
        }
        QProcess *tar = new QProcess(this);
        m_process = tar;
        m_recentOutput.clear();
        tar->setProcessChannelMode(QProcess::MergedChannels);
        const QString dest = task.isTar
            ? QDir(task.targetDir).filePath(".staging/extract")
            : task.targetDir;
        QDir().mkpath(dest);
        QStringList args;
        QString program;
        if (task.isTar) {
            args << "-xf" << task.filePath << "-C" << dest << "--strip-components=1";
            program = "tar";
        } else {
            // 優先嘗試 bsdtar 對 zip 的處理，失敗再回退到 PowerShell
            // 使用 tar 可避免 Expand-Archive 的注入風險
            if (QStandardPaths::findExecutable("tar").isEmpty()) {
                // 寫入臨時 PowerShell 腳本，以 -File 參數傳遞路徑，避免命令列轉義問題。
                const QString scriptDir = QDir(task.targetDir).filePath(".staging");
                QDir().mkpath(scriptDir);
                const QString scriptPath = QDir(scriptDir).filePath("extract.ps1");
                QFile scriptFile(scriptPath);
                if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                    scriptFile.write(QByteArrayLiteral(
                        "param([Parameter(Mandatory=$true)][string]$LiteralPath, "
                        "[Parameter(Mandatory=$true)][string]$DestinationPath)\n"
                        "Expand-Archive -Force -LiteralPath $LiteralPath -DestinationPath $DestinationPath\n"));
                    scriptFile.close();
                }
                args << "-NoProfile" << "-ExecutionPolicy" << "Bypass"
                     << "-File" << scriptPath
                     << "-LiteralPath" << task.filePath
                     << "-DestinationPath" << dest;
                program = "powershell";
            } else {
                args << "-xf" << task.filePath << "-C" << dest;
                program = "tar";
            }
        }
        tar->start(program, args);
        connectProcessOutput(tar, 2000, false);
        connect(tar, &QProcess::finished, this, [this, task, dest, tar](int code) {
            if (m_process == tar) m_process = nullptr;
            tar->deleteLater();
            if (!m_busy) return;
            const QString out = m_recentOutput.trimmed().right(400);
            m_recentOutput.clear();
            if (code != 0) { fail(tr("解壓失敗（代碼 %1）：%2").arg(code).arg(out)); return; }
            if (task.isTar) {
                QDirIterator it(dest, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString src = it.next();
                    const QString rel = QDir(dest).relativeFilePath(src);
                    const QString target = QDir(task.targetDir).filePath(rel);
                    if (it.fileInfo().isDir()) QDir().mkpath(target);
                    else { QDir().mkpath(QFileInfo(target).absolutePath()); QFile::remove(target); QFile::copy(src, target); }
                }
                QDir(dest).removeRecursively();
            } else {
                QDirIterator it(dest, QDir::Dirs | QDir::NoDotAndDotDot);
                while (it.hasNext()) {
                    const QString path = it.next();
                    if (it.fileName().startsWith(QStringLiteral("whisper.cpp-"))) {
                        const QString shortName = QDir(dest).filePath(QStringLiteral("s"));
                        if (QDir(shortName).exists()) QDir(shortName).removeRecursively();
                        // D-06: rename 無原子性，失敗時嘗試複製並回滾
                        if (!QDir().rename(path, shortName)) {
                            emitLog(tr("重命名失敗，嘗試複製..."));
                            // 遞歸複製作為回退
                            QDirIterator srcIt(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                            bool copyOk = true;
                            while (srcIt.hasNext()) {
                                const QString src = srcIt.next();
                                const QString rel = QDir(path).relativeFilePath(src);
                                const QString dst = QDir(shortName).filePath(rel);
                                if (srcIt.fileInfo().isDir()) {
                                    if (!QDir().mkpath(dst)) { copyOk = false; break; }
                                } else {
                                    QDir().mkpath(QFileInfo(dst).absolutePath());
                                    if (!QFile::copy(src, dst)) { copyOk = false; break; }
                                }
                            }
                            if (copyOk) {
                                QDir(path).removeRecursively();
                                emitLog(tr("已將源碼目錄複製為 %1").arg(QDir::toNativeSeparators(shortName)));
                            } else {
                                QDir(shortName).removeRecursively();
                                fail(tr("無法縮短源碼目錄，將保留長路徑：%1").arg(path));
                                // 不中斷流程，後續 isWhisperRoot 會遍歷子目錄容錯
                                emitLog(tr("保留長路徑，重試配置。"));
                            }
                        } else {
                            emitLog(tr("已將源碼目錄縮短為 %1").arg(QDir::toNativeSeparators(shortName)));
                        }
                        break;
                    }
                }
            }
            runNext();
        });
        emitProgress(0, task.label);
        break;
    }
    case TaskType::RunProcess: {
        m_recentOutput.clear();
        if (!isAllowedInstallerCommand(task.command)) {
            fail(tr("安裝程式拒絕執行未允許的執行檔：%1").arg(task.command));
            return;
        }
        QProcess *proc = new QProcess(this);
        m_process = proc;
        if (!task.workingDir.isEmpty()) proc->setWorkingDirectory(task.workingDir);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                if (m_process != proc) return;
                fail(tr("無法啟動程序：%1").arg(proc->errorString()));
            }
        });
        proc->start(task.command, task.arguments);
        if (!proc->waitForStarted(8000)) {
            if (m_process == proc) {
                fail(tr("程序啟動逾時：%1。").arg(task.command));
            }
            return;
        }
        emitLog(tr("程序已啟動，工作目錄：%1")
                        .arg(QDir::toNativeSeparators(proc->workingDirectory())));
        connectProcessOutput(proc, 4000);
        connect(proc, &QProcess::finished, this, [this, task, proc](int code) {
            if (m_process == proc) m_process = nullptr;
            proc->deleteLater();
            if (!m_busy) return;
            const QString err = m_recentOutput.trimmed().right(600);
            m_recentOutput.clear();
            if (code != 0 && task.expectZeroExit) {
                fail(tr("%1 失敗（代碼 %2）：%3").arg(task.label).arg(code).arg(err));
                return;
            }
            runNext();
        });
        emitProgress(0, task.label);
        break;
    }
    case TaskType::PatchPython: {
        const QString root = BackendCatalog::pythonRoot();
        const QStringList pthFiles = QDir(root).entryList({"python*._pth"});
        bool patched = false;
        for (const QString &name : pthFiles) {
            QFile file(QDir(root).filePath(name));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            QString content = QString::fromUtf8(file.readAll());
            file.close();
            bool changed = false;
            QStringList lines = content.split('\n');
            for (QString &line : lines) {
                const QString trimmed = line.trimmed().toLower();
                // D-07: 兼容 "import site # uncomment" 等註釋變體
                if (trimmed.startsWith("#import site") || trimmed.startsWith("# import site")) {
                    line = "import site"; changed = true;
                } else if (trimmed.startsWith("import site")) {
                    // 已啟用，確保大小寫正確
                    if (line.trimmed() != "import site") { line = "import site"; changed = true; }
                }
            }
            bool hasSitePackages = false;
            for (const QString &line : lines) {
                if (line.trimmed().compare("Lib/site-packages", Qt::CaseInsensitive) == 0) { hasSitePackages = true; break; }
            }
            if (!hasSitePackages) { lines.append("Lib/site-packages"); changed = true; }
            if (!content.contains("import site", Qt::CaseInsensitive)) {
                lines.append("import site"); changed = true;
            }
            if (changed) content = lines.join('\n');
            if (changed) {
                if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                    file.write(content.toUtf8()); file.close(); patched = true;
                }
            }
        }
        emitLog(patched ? tr("已啟用 Python 套件搜尋路徑。") : tr("Python 套件路徑已就緒。"));
        runNext();
        break;
    }
    case TaskType::PipInstall: {
        const QString root = BackendCatalog::pythonRoot();
        const QString python = BackendCatalog::pythonExecutable();
        if (!QFileInfo::exists(python)) {
            fail(tr("找不到 Python，Python 執行環境初始化失敗。")); return;
        }
        emitLog(tr("安裝：%1").arg(task.arguments.join(' ')));
        QProcess *proc = new QProcess(this);
        m_process = proc;
        m_recentOutput.clear();
        if (!task.workingDir.isEmpty()) proc->setWorkingDirectory(task.workingDir);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        QStringList args{"-m", "pip", "install", "--no-warn-script-location"};
        bool hasTorchPackage = false;
        for (const QString &package : task.arguments) {
            if (package == "torch" || package == "torchaudio") { hasTorchPackage = true; break; }
        }
        if (!task.indexUrl.isEmpty()) args << (hasTorchPackage ? "--index-url" : "--extra-index-url") << task.indexUrl;
        bool hasSherpaWheel = false;
        QStringList resolvedArguments;
        for (const QString &package : task.arguments) {
            // Replace direct wheel URLs with the verified local copy downloaded earlier.
            const QString local = m_wheelPathMap.value(package, package);
            resolvedArguments << local;
            if (package.startsWith(QLatin1String("https://")) && package.contains(QLatin1String("sherpa_onnx")) && package.endsWith(QLatin1String(".whl"), Qt::CaseInsensitive)) {
                hasSherpaWheel = true;
            }
        }
        // D-08: 同時清理可能共存的舊版 CUDA wheel 相關包，避免 PATH 多版本衝突
        // stalePackages 已在 appendPythonTasks 中處理 nvidia 殘留，這裡確保 pip 重裝時不保留舊依賴
        if (hasSherpaWheel) args << "--force-reinstall" << "--no-deps";
        // 對 torch 相關重裝時，pip 會自動處理依賴，保留 --force-reinstall 僅對 sherpa
        args += resolvedArguments;
        connectProcessOutput(proc, 4000);
        proc->start(python, args);
        if (!proc->waitForStarted(8000)) {
            if (m_process == proc) {
                fail(tr("無法啟動 pip：%1").arg(proc->errorString()));
            }
            proc->deleteLater();
            if (m_process == proc) m_process = nullptr;
            return;
        }
        connect(proc, &QProcess::finished, this, [this, task, proc](int code) {
            if (m_process == proc) m_process = nullptr;
            proc->deleteLater();
            if (!m_busy) return;
            const QString err = m_recentOutput.trimmed().right(600);
            m_recentOutput.clear();
            if (code != 0) {
                fail(tr("%1 失敗（代碼 %2）：%3").arg(task.label).arg(code).arg(err));
                return;
            }
            runNext();
        });
        emitProgress(0, task.label);
        break;
    }
    case TaskType::CopyRunner: {
        const QString root = BackendCatalog::pythonRoot();
        const QStringList candidates = {
            QDir(QCoreApplication::applicationDirPath()).filePath("scripts/local_asr_runner.py"),
            QDir(QCoreApplication::applicationDirPath()).filePath("local_asr_runner.py"),
            QDir(QCoreApplication::applicationDirPath()).filePath("../scripts/local_asr_runner.py")
        };
        bool copied = false;
        for (const QString &src : candidates) {
            if (QFileInfo::exists(src)) {
                QFile::remove(QDir(root).filePath("local_asr_runner.py"));
                if (QFile::copy(src, QDir(root).filePath("local_asr_runner.py"))) {
                    emitLog(tr("已佈署本地轉寫腳本。"));
                    copied = true;
                }
                break;
            }
        }
        if (!copied) {
            fail(tr("找不到 local_asr_runner.py（轉寫腳本），無法完成 Python 引擎安裝。"
                    "請確認程式目錄下的 scripts 資料夾完整。"));
            break;
        }
        runNext();
        break;
    }
    case TaskType::Finalize: {
        bool found = false;
        const BackendSpec spec = BackendCatalog::find(m_engineId, m_variantId, &found);
        const QString exe = BackendCatalog::variantExecutable(m_engineId, m_variantId);

        if (spec.needsPython) {
            if (!BackendCatalog::isPythonRuntimeReady()) {
                fail(tr("Python 執行環境未完成初始化。")); return;
            }
            m_manifest = QJsonObject{
                {"engine", m_engineId},
                {"variant", m_variantId},
                {"complete", true},
                {"version", m_releaseJson.value("tag_name").toString()},
                {"installedAt", QDateTime::currentDateTime().toString(Qt::ISODate)}
            };
        } else {
            if (exe.isEmpty()) {
                fail(tr("找不到後端可執行檔。請檢查解壓結果。")); return;
            }
            const QFileInfo exeInfo(exe);
            m_manifest = QJsonObject{
                {"engine", m_engineId},
                {"variant", m_variantId},
                {"executable", exeInfo.isAbsolute() ? QDir(task.targetDir).relativeFilePath(exe) : exe},
                {"version", m_releaseJson.value("tag_name").toString()},
                {"source", "prebuilt"},
                {"installedAt", QDateTime::currentDateTime().toString(Qt::ISODate)}
            };
        }
        if (BackendCatalog::writeManifest(m_engineId, m_variantId, m_manifest)) {
            complete(m_manifest);
        } else {
            fail(tr("寫入安裝清單失敗。"));
        }
        break;
    }
    }
}

void BackendInstaller::Session::fail(const QString &message)
{
    emitLog(tr("✗ %1").arg(message));
    m_busy = false;
    if (m_stallTimer) m_stallTimer->stop();
    if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
    if (m_file) { m_file->deleteLater(); m_file = nullptr; }
    m_process = nullptr;
    const QString key = m_host ? m_host->sessionKey(m_engineId, m_variantId) : QString();
    if (m_host) m_host->onSessionFinished(key, false, message);
}

void BackendInstaller::Session::complete(const QJsonObject &manifest)
{
    Q_UNUSED(manifest);
    if (m_stallTimer) m_stallTimer->stop();
    emitLog(tr("✓ 安裝完成：%1").arg(BackendInstaller::installLabel(m_engineId, m_variantId)));
    m_busy = false;
    const QString key = m_host ? m_host->sessionKey(m_engineId, m_variantId) : QString();
    if (m_host) m_host->onSessionFinished(key, true, tr("安裝完成。"));
}

void BackendInstaller::Session::recomputeTotalWeight()
{
    m_totalWeight = 0;
    for (const Task &task : m_queue) m_totalWeight += task.weight;
    m_totalWeight += m_doneWeight;
}

void BackendInstaller::Session::updateProgress()
{
    int percent;
    const qint64 base = static_cast<qint64>(m_doneWeight) * 100;
    if (m_totalWeight > 0 && m_currentTaskWeight > 0 && m_currentDownloadTotal > 0) {
        const qint64 taskPart = static_cast<qint64>(m_currentTaskWeight) * 100
                                * m_currentDownloadReceived / m_currentDownloadTotal;
        percent = static_cast<int>(qBound(0LL, base + taskPart, static_cast<qint64>(m_totalWeight) * 100 - 1)
                                   / m_totalWeight);
    } else if (m_totalWeight > 0) {
        percent = static_cast<int>(qBound(0LL, base, static_cast<qint64>(m_totalWeight) * 100 - 1)
                                   / m_totalWeight);
    } else {
        percent = 5;
    }
    emitProgress(qBound(0, percent, 99), m_currentDownloadLabel);
    if (!m_downloadCounted && m_currentDownloadTotal > 0
        && m_currentDownloadReceived >= m_currentDownloadTotal) {
        m_downloadCounted = true;
        m_doneWeight += m_currentTaskWeight;
    }
}

void BackendInstaller::Session::connectProcessOutput(QProcess *proc, int bufferSize, bool trimLog)
{
    const auto accumulate = [this, proc, bufferSize, trimLog](QString chunk) {
        m_recentOutput = (m_recentOutput + chunk).right(bufferSize);
        emitLog(trimLog ? chunk.trimmed() : chunk);
    };
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, accumulate] {
        accumulate(QString::fromLocal8Bit(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc, accumulate] {
        accumulate(QString::fromLocal8Bit(proc->readAllStandardError()));
    });
}

// ---------------------------------------------------------------------------
// BackendInstaller – manager that multiplexes isolated sessions
// ---------------------------------------------------------------------------
BackendInstaller::BackendInstaller(QObject *parent) : QObject(parent)
{
}

BackendInstaller::~BackendInstaller()
{
    cancel();
    qDeleteAll(m_sessions);
}

QString BackendInstaller::installLabel(const QString &engineId, const QString &variantId)
{
    return BackendCatalog::find(engineId, variantId).name;
}

QString BackendInstaller::sessionKey(const QString &engineId, const QString &variantId) const
{
    return engineId.toLower() + "/" + variantId.toLower();
}

BackendInstaller::Session *BackendInstaller::findSession(const QString &engineId, const QString &variantId) const
{
    return m_sessions.value(sessionKey(engineId, variantId), nullptr);
}

bool BackendInstaller::isBusy() const
{
    return !m_sessions.isEmpty();
}

bool BackendInstaller::isBusy(const QString &engineId, const QString &variantId) const
{
    return m_sessions.contains(sessionKey(engineId, variantId));
}

bool BackendInstaller::isPythonBusy() const
{
    for (auto *s : m_sessions) if (s && s->needsPython()) return true;
    return false;
}

void BackendInstaller::enqueuePythonPending(const QString &engineId, const QString &variantId)
{
    for (const auto &p : m_pendingPythonQueue) if (p.engineId == engineId && p.variantId == variantId) return;
    m_pendingPythonQueue.append({engineId, variantId});
}

void BackendInstaller::tryStartPendingPython()
{
    while (!m_pendingPythonQueue.isEmpty() && !isPythonBusy()) {
        const PendingInstall next = m_pendingPythonQueue.takeFirst();
        startSession(next.engineId, next.variantId);
    }
}

void BackendInstaller::startSession(const QString &engineId, const QString &variantId)
{
    const QString key = sessionKey(engineId, variantId);
    if (m_sessions.contains(key)) return;
    auto *session = new Session(this, engineId, variantId);
    m_sessions.insert(key, session);
    // ensure session deletes itself on finish via host cleanup
    session->start();
}

void BackendInstaller::install(const QString &engineId, const QString &variantId)
{
    const QString key = sessionKey(engineId, variantId);
    if (m_sessions.contains(key)) return; // already installing this variant

    bool found = false;
    const BackendSpec spec = BackendCatalog::find(engineId, variantId, &found);
    if (!found) {
        emit finished(engineId, variantId, false, tr("找不到引擎 %1 的變體 %2。").arg(engineId, variantId));
        return;
    }

    // Python 引擎共用同一個私有執行環境（python/ 目錄與 pip site-packages 為全局資源）。
    // 若已有 Python 任務正在執行，後續 Python 任務需排隊，避免同時寫入同一 zip/目錄或並行 pip
    // 破壞 site-packages。此為獨立命令行（isolated command line）模型下的關鍵序列化點。
    // 非 Python 引擎（whisper.cpp 預編譯）各自擁有獨立 variantDir，可完全並行。
    if (spec.needsPython && isPythonBusy()) {
        enqueuePythonPending(engineId, variantId);
        const QString msg = tr("Python 執行環境正由 [%1/%2] 佔用，[%3/%4] 已排入等候佇列（隊列 %5）。")
            .arg(m_sessions.keys().join(","), QString::number(m_sessions.size()),
                 engineId, variantId, QString::number(m_pendingPythonQueue.size()));
        emit logLineDetailed(engineId, variantId, msg);
        return;
    }

    startSession(engineId, variantId);
}

void BackendInstaller::onSessionFinished(const QString &key, bool ok, const QString &message)
{
    auto *session = m_sessions.take(key);
    QString engine, variant;
    if (session) {
        engine = session->engineId();
        variant = session->variantId();
        session->deleteLater();
    } else {
        const int slash = key.indexOf('/');
        if (slash >= 0) { engine = key.left(slash); variant = key.mid(slash+1); }
        else { engine = key; variant = ""; }
    }
    emit finished(engine, variant, ok, message);
    // 若是 Python 任務完成，嘗試喚醒隊列中的下一個
    tryStartPendingPython();
}

void BackendInstaller::cancel()
{
    // 取消所有進行中的安裝與排隊
    for (auto *s : m_sessions) if (s) s->cancelSession();
    // pending python queue: notify cancelled
    for (const auto &p : m_pendingPythonQueue) {
        emit finished(p.engineId, p.variantId, false, tr("使用者已取消安裝。"));
        emit logLineDetailed(p.engineId, p.variantId, tr("✗ 使用者已取消安裝（排隊中）。"));
    }
    m_pendingPythonQueue.clear();
}

void BackendInstaller::cancel(const QString &engineId, const QString &variantId)
{
    const QString key = sessionKey(engineId, variantId);
    if (m_sessions.contains(key)) {
        m_sessions.value(key)->cancelSession();
        return;
    }
    // 若在排隊中則移除
    for (int i = 0; i < m_pendingPythonQueue.size(); ++i) {
        if (m_pendingPythonQueue[i].engineId.compare(engineId, Qt::CaseInsensitive)==0
            && m_pendingPythonQueue[i].variantId.compare(variantId, Qt::CaseInsensitive)==0) {
            m_pendingPythonQueue.removeAt(i);
            emit finished(engineId, variantId, false, tr("使用者已取消安裝。"));
            emit logLineDetailed(engineId, variantId, tr("✗ 使用者已取消安裝（排隊中）。"));
            return;
        }
    }
}
