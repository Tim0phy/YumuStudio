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

static QString buildSubFilter(const QString &srtEscaped, const SubtitleStyle &st) {
    QStringList fs;
    if (!st.fontFamily.isEmpty()) fs << QString("Fontname=%1").arg(st.fontFamily);
    fs << QString("Fontsize=%1").arg(st.fontSize);
    if (st.bold)   fs << "Bold=1";
    if (st.italic) fs << "Italic=1";
    fs << QString("PrimaryColour=%1").arg(toAssColor(st.textColor));
    fs << QString("OutlineColour=%1").arg(toAssColor(st.outlineColor));
    fs << QString("Outline=%1").arg(st.outlineWidth);
    int alignment = (st.position==1)?6:(st.position==2)?10:2;
    fs << QString("Alignment=%1").arg(alignment);
    fs << QString("MarginV=%1").arg(st.marginV);
    return QString("subtitles=%1:force_style='%2'").arg(srtEscaped, fs.join(","));
}

class ExportWorker : public QObject {
    Q_OBJECT
public:
    ExportParams         params;
    QList<SubtitleEntry> entries;

signals:
    void progressChanged(int pct, const QString &msg);
    void finished(bool ok, const QString &error);

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

        emit progressChanged(20, "Burning subtitles with FFmpeg...");
        QString srtEsc = srtPath;
        srtEsc.replace("\\","/").replace(":","\\:");
        QString vf = buildSubFilter(srtEsc, params.style);

        QStringList args = {
            "-y", "-i", params.inputVideo,
            "-vf", vf,
            "-c:v", params.videoCodec,
            "-crf", QString::number(params.crf),
            "-preset", params.preset,
            "-c:a", "aac",
            "-b:a", QString::number(params.audioBitrate)+"k",
            params.outputPath
        };

        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(ff, args);
        int fakeP = 20;
        while (!proc.waitForFinished(1000)) {
            fakeP = qMin(fakeP+2, 95);
            emit progressChanged(fakeP, "Encoding video...");
        }
        QFile::remove(srtPath);

        if (proc.exitCode() != 0) {
            emit finished(false,
                QString("FFmpeg failed (exit %1):\n%2")
                    .arg(proc.exitCode())
                    .arg(QString::fromUtf8(proc.readAll()).left(600)));
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
