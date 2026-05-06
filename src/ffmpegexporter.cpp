#include "ffmpegexporter.h"
#include <QThread>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>

static QString msToSrtTime(qint64 ms) {
    int h = int(ms/3600000); ms %= 3600000;
    int m = int(ms/60000);   ms %= 60000;
    int s = int(ms/1000);    ms %= 1000;
    return QString("%1:%2:%3,%4")
        .arg(h,2,10,QChar('0')).arg(m,2,10,QChar('0'))
        .arg(s,2,10,QChar('0')).arg(ms,3,10,QChar('0'));
}

static QString toAssColor(const QColor &c) {
    return QString("&H%1%2%3%4")
        .arg(255-c.alpha(),2,16,QChar('0'))
        .arg(c.blue(),     2,16,QChar('0'))
        .arg(c.green(),    2,16,QChar('0'))
        .arg(c.red(),      2,16,QChar('0'))
        .toUpper();
}

class ExportWorker : public QObject {
    Q_OBJECT
public:
    ExportParams         params;
    QList<SubtitleEntry> entries;

signals:
    void progressChanged(int pct, const QString &msg);
    void finished(bool ok, const QString &error);

private:
    static QString escapeSrtPath(const QString &path) {
        QString p = QDir::fromNativeSeparators(path);
        p.replace("'", "\\'");
        p.replace(":", "\\:");
        return p;
    }

    static QStringList parseVideoInfo(const QString &ffmpeg, const QString &input) {
        QStringList result;
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, {"-i", input});
        p.waitForFinished(8000);
        result.append(QString::fromUtf8(p.readAll()));
        return result;
    }

    static QString extractError(const QString &output) {
        QStringList lines = output.split("\n", Qt::SkipEmptyParts);
        for (int i = lines.size() - 1; i >= 0; --i) {
            QString l = lines[i].toLower();
            if (l.contains("error") || l.contains("invalid") || l.contains("no such") || l.contains("failed"))
                return lines[i].trimmed();
        }
        return lines.isEmpty() ? QString() : lines.last().trimmed();
    }

public slots:
    void run() {
        const QString ff = params.ffmpegPath.isEmpty() ? "ffmpeg" : params.ffmpegPath;

        if (params.srtOnly) {
            emit progressChanged(50, "Exporting SRT...");
            if (!writeSRT(params.outputPath))
                emit finished(false, "Failed to write SRT: " + params.outputPath);
            else { emit progressChanged(100, "Done"); emit finished(true, {}); }
            return;
        }

        emit progressChanged(10, "Writing subtitle file...");
        QString tmpDir = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation) + "/yumu_studio";
        QDir().mkpath(tmpDir);
        QString srtPath = tmpDir + "/burn.srt";
        if (!writeSRT(srtPath)) {
            emit finished(false, "Failed to write temp SRT"); return;
        }

        QString escapedSrt = escapeSrtPath(srtPath);
        emit progressChanged(20, "Detecting streams...");

        bool hasAudio = false;
        QString videoInfo = parseVideoInfo(ff, params.inputVideo).first();
        if (videoInfo.contains("Audio:") || (videoInfo.contains("Stream #") && videoInfo.contains("Audio")))
            hasAudio = true;

        emit progressChanged(25, "Starting FFmpeg encode...");

        struct Config { QString vf; QString codec; QString crf; QString preset; };
        QList<Config> configs = {
            {
                "scale=trunc(iw/2)*2:trunc(ih/2)*2,subtitles='" + escapedSrt + "',format=yuv420p",
                "libx264", "23", "fast"
            },
            {
                "scale=trunc(iw/2)*2:trunc(ih/2)*2,subtitles=filename='" + escapedSrt + "'",
                "libx264", "23", "fast"
            },
            {
                "subtitles=" + escapedSrt,
                "libx264", "23", "veryfast"
            }
        };

        bool success = false;
        QString lastError;
        QString lastCommand;

        for (int i = 0; i < configs.size() && !success; ++i) {
            const Config &c = configs[i];
            QString vf = c.vf;

            QStringList args;
            args << "-y";
            args << "-i" << params.inputVideo;
            args << "-vf" << vf;
            args << "-c:v" << c.codec;
            args << "-crf" << c.crf;
            args << "-preset" << c.preset;
            if (hasAudio) {
                args << "-c:a" << "copy";
            } else {
                args << "-an";
            }
            args << params.outputPath;

            lastCommand = ff + " " + args.join(" ");
            emit progressChanged(30 + i * 15, QString("Attempt %1/%2...").arg(i+1).arg(configs.size()));

            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);

            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            QString ffDir = QFileInfo(ff).path();
            if (!ffDir.isEmpty())
                env.insert("PATH", env.value("PATH") + ";" + ffDir);
            p.setProcessEnvironment(env);

            p.start(ff, args);
            if (!p.waitForStarted(5000)) {
                lastError = "Failed to start FFmpeg";
                continue;
            }

            int pct = 30;
            while (!p.waitForFinished(2000)) {
                pct = qMin(pct + 1, 90);
                emit progressChanged(pct, "Encoding...");
                p.readAll();
            }

            QByteArray allOut = p.readAll();
            if (p.exitCode() == 0) {
                success = true;
            } else {
                lastError = extractError(QString::fromUtf8(allOut));
                if (lastError.isEmpty()) lastError = QString::fromUtf8(allOut).left(500);
                QFile::remove(params.outputPath);
            }
        }

        QFile::remove(srtPath);

        if (!success) {
            emit finished(false,
                QString("FFmpeg failed\n\nCommand:\n%1\n\nError:\n%2")
                    .arg(lastCommand)
                    .arg(lastError.left(800)));
            return;
        }

        emit progressChanged(100, "Done");
        emit finished(true, {});
    }

private:
    bool writeSRT(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly|QIODevice::Text)) return false;
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        for (const auto &e : entries) {
            out << e.index << "\n"
                << msToSrtTime(e.startMs) << " --> " << msToSrtTime(e.endMs) << "\n"
                << e.text;
            if (!e.translation.isEmpty() && params.bilingualBurn)
                out << "\n" << e.translation;
            out << "\n\n";
        }
        return true;
    }
};

#include "ffmpegexporter.moc"

FFmpegExporter::FFmpegExporter(QObject *parent) : QObject(parent) {}
FFmpegExporter::~FFmpegExporter() {
    if (m_thread) { m_thread->quit(); m_thread->wait(); }
}

void FFmpegExporter::startExport(const ExportParams &params,
                                  const QList<SubtitleEntry> &entries)
{
    if (m_busy) return;
    m_busy   = true;
    m_thread = new QThread(this);
    m_worker = new ExportWorker();
    m_worker->params  = params;
    m_worker->entries = entries;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started,              m_worker, &ExportWorker::run);
    connect(m_worker, &ExportWorker::progressChanged,  this,    &FFmpegExporter::progressChanged);
    connect(m_worker, &ExportWorker::finished, this, [this](bool ok, const QString &err){
        m_busy = false; m_thread->quit(); emit finished(ok, err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
