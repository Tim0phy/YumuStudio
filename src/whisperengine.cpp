#include "whisperengine.h"
#include "appconfig.h"
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QCoreApplication>
#include <QFileInfo>
#include <vector>
#include <cstdint>

#ifdef HAVE_WHISPER_LIB
  #include "whisper.h"
#endif

static bool extractWav(const QString &videoPath,
                       const QString &wavPath,
                       const QString &ffmpegExe,
                       QString       &errOut)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ffmpegExe, {"-y","-i",videoPath,
                        "-ar","16000","-ac","1","-sample_fmt","s16",
                        "-f","wav", wavPath});
    if (!p.waitForFinished(300000)) { errOut="FFmpeg timed out."; return false; }
    errOut = QString::fromUtf8(p.readAll());
    return QFile::exists(wavPath);
}

static qint64 srtTimeToMs(const QString &t) {
    auto c = t.trimmed().split(':');
    if (c.size()<3) return 0;
    auto s = c[2].split(',');
    return qint64(c[0].toInt())*3600000+c[1].toInt()*60000
          +s[0].toInt()*1000+(s.size()>1?s[1].toInt():0);
}

// ─── WhisperWorker ────────────────────────────────────────────────────────────
class WhisperWorker : public QObject {
    Q_OBJECT
public:
    QString       videoPath;
    WhisperParams params;
    QString       ffmpegPath;

signals:
    void progressChanged(int pct, const QString &msg);
    void segmentReady(SubtitleEntry entry);
    void finished(bool ok, const QString &error);

public slots:
    void run() {
        const QString ff = ffmpegPath.isEmpty() ? "ffmpeg" : ffmpegPath;
        QString tmpDir = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation) + "/yumu_studio";
        QDir().mkpath(tmpDir);
        const QString wavPath = tmpDir + "/audio.wav";
        const QString srtBase = tmpDir + "/output";

        emit progressChanged(5, tr("Extracting audio..."));
        QString ffErr;
        if (!extractWav(videoPath, wavPath, ff, ffErr)) {
            emit finished(false,
                tr("FFmpeg failed to extract audio.\n\n"
                   "Tip: Set FFmpeg path in Settings → Paths.\n\n") + ffErr.left(400));
            return;
        }

#ifdef HAVE_WHISPER_LIB
        emit progressChanged(12, tr("Loading Whisper model..."));
        if (params.modelPath.isEmpty() || !QFile::exists(params.modelPath)) {
            QFile::remove(wavPath);
            emit finished(false, tr("Whisper model not found: ") + params.modelPath);
            return;
        }
        whisper_context_params cparams = whisper_context_default_params();
        whisper_context *ctx = whisper_init_from_file_with_params(
            params.modelPath.toUtf8().constData(), cparams);
        if (!ctx) {
            QFile::remove(wavPath);
            emit finished(false, tr("Failed to load model: ") + params.modelPath);
            return;
        }
        emit progressChanged(22, tr("Loading audio..."));
        QFile wav(wavPath);
        if (!wav.open(QIODevice::ReadOnly)) {
            whisper_free(ctx); QFile::remove(wavPath);
            emit finished(false, tr("Cannot open WAV: ") + wavPath); return;
        }
        wav.seek(44);
        const QByteArray pcmBytes = wav.readAll(); wav.close();
        QFile::remove(wavPath);
        const int numSamples = pcmBytes.size() / 2;
        std::vector<float> pcm(numSamples);
        const int16_t *raw = reinterpret_cast<const int16_t*>(pcmBytes.constData());
        for (int i=0;i<numSamples;++i) pcm[i]=raw[i]/32768.0f;

        whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        wparams.n_threads   = std::max(1, params.threads);
        wparams.translate   = params.translate;
        wparams.print_realtime = false;
        wparams.print_progress = false;
        if (params.language!="auto" && !params.language.isEmpty()) {
            static QByteArray langBuf; langBuf = params.language.toUtf8();
            wparams.language = langBuf.constData();
        }
        struct PC { WhisperWorker *w; };
        static PC pctx; pctx.w = this;
        wparams.progress_callback = [](whisper_context*,whisper_state*,int prog,void*ud){
            auto *c=static_cast<PC*>(ud);
            emit c->w->progressChanged(25+prog*65/100,
                QString("Recognizing... %1%").arg(prog));
        };
        wparams.progress_callback_user_data = &pctx;
        emit progressChanged(25, tr("Starting recognition..."));
        if (whisper_full(ctx, wparams, pcm.data(), numSamples)!=0) {
            whisper_free(ctx);
            emit finished(false, tr("whisper_full() returned error.")); return;
        }
        emit progressChanged(92, tr("Parsing segments..."));
        const int nSeg = whisper_full_n_segments(ctx);
        for (int i=0;i<nSeg;++i) {
            SubtitleEntry e;
            e.index   = i+1;
            e.startMs = whisper_full_get_segment_t0(ctx,i)*10;
            e.endMs   = whisper_full_get_segment_t1(ctx,i)*10;
            e.text    = QString::fromUtf8(whisper_full_get_segment_text(ctx,i)).trimmed();
            if (!e.text.isEmpty()) emit segmentReady(e);
        }
        whisper_free(ctx);

#else
        // ── CLI mode ──────────────────────────────────────────────────────────
        emit progressChanged(15, tr("Checking whisper-cli..."));
        const QString cli = resolveCli();
        if (cli.isEmpty()) {
            QFile::remove(wavPath);
            emit finished(false,
                tr("whisper-cli.exe not found.\n\n"
                   "Set it in Settings → Paths → whisper-cli.exe\n"
                   "Or rebuild with -DWHISPER_ROOT=<path> for static lib mode."));
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

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(cli, args);
        int fakeP=20;
        while (!proc.waitForFinished(800)) {
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
#endif
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

#ifndef HAVE_WHISPER_LIB
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
#endif
};

#include "whisperengine.moc"

WhisperEngine::WhisperEngine(QObject *parent) : QObject(parent) {}
WhisperEngine::~WhisperEngine() { if (m_thread) { m_thread->quit(); m_thread->wait(); } }

void WhisperEngine::transcribe(const QString &videoPath, const WhisperParams &params) {
    if (m_busy) return;
    m_busy   = true;
    m_thread = new QThread(this);
    m_worker = new WhisperWorker();
    m_worker->videoPath  = videoPath;
    m_worker->params     = params;
    m_worker->ffmpegPath = AppConfig::instance().paths.ffmpegPath;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started,              m_worker, &WhisperWorker::run);
    connect(m_worker, &WhisperWorker::progressChanged, this,    &WhisperEngine::progressChanged);
    connect(m_worker, &WhisperWorker::segmentReady,    this,    &WhisperEngine::segmentReady);
    connect(m_worker, &WhisperWorker::finished, this, [this](bool ok, const QString &err){
        m_busy=false; m_thread->quit(); emit finished(ok,err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
