#include "whisperengine.h"
#include "appconfig.h"
#include "backendcatalog.h"
#include "glossary.h"
#include "securitypolicy.h"
#include <QProcess>
#include <atomic>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QProcessEnvironment>
#include <atomic>

static bool extractWav(const QString &videoPath,
                       const QString &wavPath,
                       const QString &ffmpegExe,
                       QString       &errOut)
{
    if (!SecurityPolicy::isSafeMediaInputPath(videoPath)) {
        errOut = QObject::tr("Invalid or unsafe media input path: %1").arg(videoPath);
        return false;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ffmpegExe, {"-y","-i",videoPath,
                        "-ar","16000","-ac","1","-sample_fmt","s16",
                        "-f","wav", wavPath});
    if (!p.waitForStarted(10000)) {
        errOut = "Could not start FFmpeg: " + p.errorString();
        return false;
    }
    if (!p.waitForFinished(300000)) {
        p.kill();
        p.waitForFinished(5000);
        errOut="FFmpeg timed out while extracting audio.";
        return false;
    }
    errOut = QString::fromUtf8(p.readAll());
    if (p.exitCode() != 0) {
        if (errOut.isEmpty()) errOut = "FFmpeg exited with code " + QString::number(p.exitCode());
        return false;
    }
    if (!QFileInfo(wavPath).isFile() || QFileInfo(wavPath).size() <= 44) {
        if (errOut.isEmpty()) errOut = "FFmpeg did not create a valid WAV file.";
        return false;
    }
    return true;
}

static qint64 srtTimeToMs(const QString &t) {
    auto c = t.trimmed().split(':');
    if (c.size()<3) return 0;
    auto s = c[2].split(',');
    return qint64(c[0].toInt())*3600000+c[1].toInt()*60000
          +s[0].toInt()*1000+(s.size()>1?s[1].toInt():0);
}

// Custom free-text prompt first, glossary natural sentence (rare words last)
// appended: matches OpenAI initial-prompt guidance either way. Empty when
// neither is set (previous behaviour, no --prompt flag at all).
static QString combinedPrompt(const WhisperParams &params) {
    QStringList parts;
    const QString custom = params.customPrompt.trimmed();
    if (!custom.isEmpty()) parts << custom;
    const QList<GlossaryEntry> glossary = params.useGlossary ? Glossary::load() : QList<GlossaryEntry>{};
    if (!glossary.isEmpty()) {
        // 自然句提示詞：符合 OpenAI 建議，稀有詞置尾，輕量 H-PRM 已做 top-30 截斷（224 token 限制）
        const QString gp = Glossary::hotwordPromptNatural(glossary, 30);
        if (!gp.isEmpty()) parts << gp;
    }
    return parts.join(QStringLiteral(" "));
}

// Locate the local ASR runner script bundled beside the application, falling
// back to the provisioned Python runtime directory last.
static QString findAsrScript(const bool pythonReady)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("scripts/local_asr_runner.py"),
        QDir(appDir).filePath("local_asr_runner.py"),
        QDir(appDir).filePath("../scripts/local_asr_runner.py"),
    };
    for (const QString &c : candidates)
        if (QFile::exists(c)) return c;
    if (pythonReady) {
        const QString inRuntime = QDir(BackendCatalog::pythonRoot()).filePath("local_asr_runner.py");
        if (QFile::exists(inRuntime)) return inRuntime;
    }
    return {};
}

// ─── WhisperWorker ────────────────────────────────────────────────────────────
class WhisperWorker : public QObject {
    Q_OBJECT
public:
    QString       videoPath;
    WhisperParams params;
    QString       ffmpegPath;
    std::atomic<qint64> *childProcessId = nullptr;

signals:
    void progressChanged(int pct, const QString &msg);
    void segmentReady(SubtitleEntry entry);
    void warning(const QString &msg);
    void finished(bool ok, const QString &error);

public slots:
    void run() {
        params.customPrompt = SecurityPolicy::sanitizePrompt(params.customPrompt);
        const QFileInfo modelInfo(params.modelPath);
        const bool pythonReady = BackendCatalog::isPythonRuntimeReady();
        // Resolve the whisper.cpp backend at runtime: prefer an installed
        // variant matched to the requested compute device, then the legacy
        // user-configured cli path, and only then the built-in library.
        QString installedVariant;
        const QString resolvedCli = BackendCatalog::resolveWhisperCli(params.computeDevice, &installedVariant);
        const QString cliForRun = resolvedCli.isEmpty() ? params.cliPath : resolvedCli;
        const QString variantForRun = installedVariant;

        const bool nativeEngine = params.engine.compare("whisper.cpp", Qt::CaseInsensitive) == 0;
        const bool validModelPath = nativeEngine ? modelInfo.isFile() : (modelInfo.isFile() || modelInfo.isDir());
        if (params.modelPath.trimmed().isEmpty() || !validModelPath) {
            emit finished(false, tr("Model file or folder is missing or invalid: %1").arg(params.modelPath));
            return;
        }
        const QString ff = ffmpegPath.isEmpty() ? "ffmpeg" : ffmpegPath;
        const auto &system = AppConfig::instance().system;
        QString tempRoot = system.useCustomTemp && !system.tempPath.trimmed().isEmpty()
            ? system.tempPath.trimmed()
            : QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
        // SECURITY: a user-controlled temp root must be in a known-safe location;
        // otherwise fall back to the system temp directory.
        if (system.useCustomTemp && !SecurityPolicy::isSafeTempRoot(tempRoot)) {
            emit warning(tr("Custom temp path is not in a safe location; using system temp instead."));
            tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
        }
        if (!QDir().mkpath(tempRoot)) {
            emit finished(false, tr("Cannot create temporary directory: %1").arg(tempRoot));
            return;
        }
        QTemporaryDir jobDir(QDir(tempRoot).filePath("job-XXXXXX"));
        if (!jobDir.isValid()) {
            emit finished(false, tr("Cannot create a private temporary job directory under: %1").arg(tempRoot));
            return;
        }
        const QString tmpDir = jobDir.path();
        const QString wavPath = QDir(tmpDir).filePath("audio.wav");
        const QString srtBase = QDir(tmpDir).filePath("output");

        emit progressChanged(5, tr("Extracting audio..."));
        QString ffErr;
        if (!extractWav(videoPath, wavPath, ff, ffErr)) {
            emit finished(false,
                tr("FFmpeg failed to extract audio.\n\n"
                   "Tip: Set FFmpeg path in Settings → Paths.\n\n") + ffErr.left(400));
            return;
        }

        // Non-whisper engines use a small external runner contract so the Qt app
        // stays independent from Python, ONNX and CUDA package versions.
        if (params.engine != "whisper.cpp") {
            emit progressChanged(18, tr("Running %1...").arg(params.engine));
            if (params.runnerPath.isEmpty() && !pythonReady) {
                QFile::remove(wavPath);
                emit finished(false, tr("Runner not found. Configure it in Settings -> Paths."));
                return;
            }
            const QFileInfo modelInfo(params.modelPath);
            const QString modelDir = modelInfo.isDir() ? modelInfo.absoluteFilePath() : modelInfo.absolutePath();
            const QString outPath = srtBase + ".srt";
            // Prefer the shared Python runtime provisioned by the Engine Center so
            // the user does not need a separate venv built by the developer.
            const QString runnerExec = pythonReady
                ? BackendCatalog::pythonExecutable()
                : (params.runnerPath.isEmpty() ? QString("python") : params.runnerPath);
            QProcess runner; runner.setProcessChannelMode(QProcess::MergedChannels);
            QStringList runnerArgs;
            const QString runnerName = QFileInfo(runnerExec).fileName().toLower();
            if (runnerName == "python.exe" || runnerName == "python3.exe" || runnerName == "python") {
                QProcess probe;
                probe.setProcessChannelMode(QProcess::MergedChannels);
                probe.start(runnerExec, {"-c", "import sys; print(sys.version)"});
                if (!probe.waitForStarted(10000) || !probe.waitForFinished(10000)
                    || probe.exitCode() != 0) {
                    QFile::remove(wavPath);
                    emit finished(false, tr("The configured Python runner cannot start. Rebuild the local ASR runtime or select a working Python executable in Settings."));
                    return;
                }
                const QString script = findAsrScript(pythonReady);
                if (script.isEmpty()) {
                    QFile::remove(wavPath);
                    emit finished(false, tr("Local ASR runner script is missing. Reinstall Yumu Studio or copy scripts/local_asr_runner.py beside the application."));
                    return;
                }
                runnerArgs << script;
            }
            runnerArgs << params.engine << modelDir << wavPath << outPath << params.language
                       << (params.computeDevice.isEmpty() ? QStringLiteral("auto") : params.computeDevice)
                       << (params.precision.isEmpty() ? QStringLiteral("auto") : params.precision);
        const QList<GlossaryEntry> glossary = params.useGlossary ? Glossary::load() : QList<GlossaryEntry>{};
        if (!glossary.isEmpty())
            runnerArgs << Glossary::filePath();
        if (pythonReady)
            BackendCatalog::applyProcessEnvironment(runner, params.engine, params.computeDevice);
        {
            // Custom prompt travels via environment (never argv): older
            // runner scripts simply ignore the unknown variable.
            QProcessEnvironment procEnv = runner.processEnvironment();
            if (procEnv.isEmpty())
                procEnv = QProcessEnvironment::systemEnvironment();
            const QString customPrompt = params.customPrompt.trimmed();
            if (!customPrompt.isEmpty())
                procEnv.insert(QStringLiteral("YUMU_ASR_PROMPT"), customPrompt);
            runner.setProcessEnvironment(procEnv);
        }
            runner.start(runnerExec, runnerArgs);
            if (!runner.waitForStarted(10000)) {
                QFile::remove(wavPath);
                emit finished(false, tr("Could not start local ASR runner.\nExecutable: %1\nError: %2")
                    .arg(runnerExec, runner.errorString()));
                return;
            }
            if (childProcessId) childProcessId->store(static_cast<qint64>(runner.processId()), std::memory_order_relaxed);
            // Long lectures / films on slow machines legitimately exceed half
            // an hour; cap at two hours instead of killing a healthy job.
            if (!runner.waitForFinished(120 * 60 * 1000)) {
                runner.kill();
                runner.waitForFinished(5000);
                QFile::remove(wavPath);
                emit finished(false, tr("%1 runner timed out after 120 minutes.").arg(params.engine));
                return;
            }
            if (childProcessId) childProcessId->store(0, std::memory_order_relaxed);
            const QString runnerOutput = QString::fromUtf8(runner.readAll());
            if (runner.exitCode() != 0) {
                QFile::remove(wavPath);
                const QString detail = runnerOutput.isEmpty() ? runner.errorString() : runnerOutput;
                emit finished(false, tr("%1 runner failed (exit code %2).\n\n%3\n\nModel directory: %4")
                    .arg(params.engine).arg(runner.exitCode()).arg(detail.left(4000)).arg(modelDir));
                return;
            }
            // Surface non-fatal warnings, e.g. the runner silently falling
            // back to CPU when the requested CUDA provider cannot start.
            for (const QString &line : runnerOutput.split('\n')) {
                const QString trimmed = line.trimmed();
                if (trimmed.startsWith(QLatin1String("YUMU_WARNING:"))) {
                    QString msg = trimmed.mid(QStringLiteral("YUMU_WARNING:").size()).trimmed();
                    if (!msg.isEmpty()) emit warning(msg.left(1000));
                    break;
                }
            }
            QFile::remove(wavPath);
            if (!QFile::exists(outPath)) {
                emit finished(false, tr("Runner completed but did not create an SRT file."));
                return;
            }
            QFile out(outPath);
            if (out.open(QIODevice::ReadOnly | QIODevice::Text)) { parseSRT(QString::fromUtf8(out.readAll())); out.close(); }
            QFile::remove(outPath);
            emit progressChanged(100, tr("Recognition complete"));
            emit finished(true, {});
            return;
        }

        // ── CLI mode ──────────────────────────────────────────────────────────
        emit progressChanged(15, tr("Checking whisper-cli..."));
        // Prefer the variant resolved from the backend catalog (Vulkan / CUDA /
        // CPU) so the selected GPU backend is actually launched. Fall back to
        // the legacy configured path and the built-in copy for compatibility.
        const QString cli = cliForRun.isEmpty() ? resolveCli() : cliForRun;
        if (cli.isEmpty()) {
            QFile::remove(wavPath);
            emit finished(false,
                tr("找不到 whisper.cpp 後端。請到「設定 → 音訊轉寫引擎」安裝 whisper.cpp（CPU / Vulkan / CUDA）後端，\n"
                   "或在設定中手動指定 whisper-cli.exe 路徑。"));
            return;
        }
        emit progressChanged(20, tr("Running AI recognition..."));
        QStringList args;
        args << "-m" << params.modelPath
             << "-f" << wavPath
             << "-of" << srtBase
             << "-osrt";
        if (params.language!="auto" && !params.language.isEmpty()) args << "-l" << params.language;
        if (params.threads>0) args << "-t" << QString::number(params.threads);
        if (params.translate) args << "--translate";
        const QString fullPrompt = combinedPrompt(params);
        if (!fullPrompt.isEmpty())
            args << "--prompt" << fullPrompt;
        if (params.computeDevice.trimmed().compare("cpu", Qt::CaseInsensitive) == 0)
            args << "--no-gpu";

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        // The environment must be set before start(); setting it afterwards
        // has no effect on the already-running child process.
        if (!resolvedCli.isEmpty())
            BackendCatalog::applyProcessEnvironment(proc, "whisper.cpp", variantForRun);
        proc.start(cli, args);
        if (!proc.waitForStarted(10000)) {
            QFile::remove(wavPath);
            emit finished(false, tr("Could not start whisper-cli.\nExecutable: %1\nError: %2")
                .arg(cli, proc.errorString()));
            return;
        }
        int fakeP=20;
        QElapsedTimer cliTimer;
        cliTimer.start();
        while (!proc.waitForFinished(800)) {
            if (cliTimer.elapsed() > 120 * 60 * 1000) {
                proc.kill();
                proc.waitForFinished(5000);
                QFile::remove(wavPath);
                emit finished(false, tr("whisper-cli timed out after 120 minutes."));
                return;
            }
            fakeP=qMin(fakeP+2,88);
            emit progressChanged(fakeP, tr("AI recognition in progress..."));
        }
        const QString cliOutput = QString::fromUtf8(proc.readAll());
        const int exitCode = proc.exitCode();
        QFile::remove(wavPath);
        if (exitCode!=0) {
            emit finished(false,
                QString("whisper-cli exited %1\n\n%2").arg(exitCode).arg(cliOutput.left(800)));
            return;
        }
        emit progressChanged(92, tr("Parsing subtitles..."));
        // find SRT
        QString srtPath;
        for (const auto &name : {srtBase+".srt", srtBase+".en.srt", wavPath+".srt"}) {
            if (QFile::exists(name)) { srtPath=name; break; }
        }
        QDir tmpD(tmpDir);
        if (srtPath.isEmpty()) {
            for (const auto &f : tmpD.entryList({"*.srt"},QDir::Files)) {
                srtPath=tmpDir+"/"+f; break;
            }
        }
        if (srtPath.isEmpty()) {
            emit finished(false, tr("whisper-cli succeeded but no SRT found.\nSearched: ")+tmpDir
                +"\n\nCLI output:\n"+cliOutput.left(400)); return;
        }
        QFile srtFile(srtPath);
        if (!srtFile.open(QIODevice::ReadOnly|QIODevice::Text)) {
            emit finished(false, tr("Cannot read SRT: ")+srtPath); return;
        }
        parseSRT(QString::fromUtf8(srtFile.readAll()));
        srtFile.close(); QFile::remove(srtPath);
        emit progressChanged(100, tr("Recognition complete"));
        emit finished(true, {});
    }

private:
    QString resolveCli() const {
        if (!params.cliPath.isEmpty() && QFile::exists(params.cliPath))
            return params.cliPath;
        // search common locations
        QStringList candidates = {
            params.cliPath,
            "whisper-cli",
            QCoreApplication::applicationDirPath() + "/whisper-cli.exe",
        };
        for (const auto &c : candidates)
            if (!c.isEmpty() && QFile::exists(c)) return c;
        return {};
    }

    void parseSRT(const QString &text) {
        static QRegularExpression timeLine(
            R"((\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3}))");
        SubtitleEntry cur; bool inBlock=false; int idx=0;
        for (const auto &raw : text.split('\n')) {
            QString line=raw.trimmed();
            if (line.isEmpty()) {
                if (inBlock && !cur.text.isEmpty()) { emit segmentReady(cur); cur={}; }
                inBlock=false; continue;
            }
            auto m=timeLine.match(line);
            if (m.hasMatch()) {
                cur.startMs=srtTimeToMs(m.captured(1));
                cur.endMs  =srtTimeToMs(m.captured(2));
                cur.index  =++idx; inBlock=true; continue;
            }
            bool ok; line.toInt(&ok);
            if (ok && !inBlock) continue;
            if (inBlock) { if (!cur.text.isEmpty()) cur.text+="\n"; cur.text+=line; }
        }
        if (inBlock && !cur.text.isEmpty()) emit segmentReady(cur);
    }
};

#include "whisperengine.moc"

WhisperEngine::WhisperEngine(QObject *parent) : QObject(parent) {}

void WhisperEngine::killChildProcess() {
#ifdef Q_OS_WIN
    if (m_childProcessId.load(std::memory_order_relaxed) <= 0) return;
    QProcess killer;
    killer.start("taskkill", {"/PID", QString::number(m_childProcessId.load(std::memory_order_relaxed)), "/T", "/F"});
    killer.waitForFinished(10000);
    m_childProcessId.store(0, std::memory_order_relaxed);
#endif
}

void WhisperEngine::stop() {
    m_stopRequested = true;
    killChildProcess();
}

WhisperEngine::~WhisperEngine() {
    killChildProcess();
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

void WhisperEngine::transcribe(const QString &videoPath, const WhisperParams &params) {
    if (m_busy) return;
    m_busy   = true;
    m_stopRequested = false;
    m_thread = new QThread(this);
    m_worker = new WhisperWorker();
    m_worker->videoPath  = videoPath;
    m_worker->params     = params;
    m_worker->ffmpegPath = AppConfig::instance().paths.ffmpegPath;
    m_worker->childProcessId = &m_childProcessId;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started,              m_worker, &WhisperWorker::run);
    connect(m_worker, &WhisperWorker::progressChanged, this,    &WhisperEngine::progressChanged);
    connect(m_worker, &WhisperWorker::segmentReady,    this,    &WhisperEngine::segmentReady);
    connect(m_worker, &WhisperWorker::warning,         this,    &WhisperEngine::warning);
    connect(m_worker, &WhisperWorker::finished, this, [this](bool ok, const QString &err){
        m_busy = false;
        if (m_thread) m_thread->quit();
        emit finished(ok,err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
