#include "ffmpegexporter.h"
#include "appconfig.h"
#include "securitypolicy.h"
#include <QThread>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QRegularExpression>

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

static QString msToAssTime(qint64 ms) {
    ms = qMax<qint64>(0, ms);
    const int h = int(ms / 3600000);
    ms %= 3600000;
    const int m = int(ms / 60000);
    ms %= 60000;
    const int s = int(ms / 1000);
    const int cs = int((ms % 1000) / 10);
    return QString("%1:%2:%3.%4")
        .arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        .arg(cs, 2, 10, QChar('0'));
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
        p.replace("\\", "\\\\");
        p.replace("'", "\\'");
        p.replace(":", "\\:");
        p.replace("[", "\\[");
        p.replace("]", "\\]");
        p.replace(",", "\\,");
        p.replace(";", "\\;");
        return p;
    }

    // Build the subtitles= filter argument. fontsdir is passed explicitly so
    // libass can resolve the user's chosen font from the system font folder
    // (some Windows FFmpeg builds don't scan it by themselves, which made the
    // burned output fall back to a default font instead of the preview font).
    static QString subtitleFilterArg(const QString &escapedSubtitlePath) {
        const QString fontsDir = QDir::fromNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::FontsLocation));
        if (fontsDir.isEmpty() || !QFileInfo(fontsDir).isDir())
            return QStringLiteral("subtitles='%1'").arg(escapedSubtitlePath);
        return QStringLiteral("subtitles='%1':fontsdir='%2'")
            .arg(escapedSubtitlePath, escapeSrtPath(fontsDir));
    }

    static QStringList parseVideoInfo(const QString &ffmpeg, const QString &input) {
        QStringList result;
        if (!SecurityPolicy::isSafeMediaInputPath(input)) {
            result.append("Invalid or unsafe media input path: " + input);
            return result;
        }
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, {"-i", input});
        if (!p.waitForStarted(10000)) {
            result.append("Could not start FFmpeg: " + p.errorString());
            return result;
        }
        if (!p.waitForFinished(8000)) {
            p.kill();
            p.waitForFinished(5000);
        }
        result.append(QString::fromUtf8(p.readAll()));
        return result;
    }

    static bool hasEncoder(const QString &ffmpeg, const QString &codec, QString &error) {
        QProcess p;
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ffmpeg, {"-hide_banner", "-encoders"});
        if (!p.waitForStarted(10000)) {
            error = "Could not start FFmpeg: " + p.errorString();
            return false;
        }
        if (!p.waitForFinished(30000)) {
            p.kill();
            p.waitForFinished(5000);
            error = "FFmpeg timed out while listing encoders.";
            return false;
        }
        const QString output = QString::fromUtf8(p.readAll());
        if (p.exitCode() != 0) {
            error = output.left(500);
            return false;
        }
        if (!output.contains(QRegularExpression(QStringLiteral("\\b") + QRegularExpression::escape(codec) + QStringLiteral("\\b")))) {
            error = "FFmpeg does not provide the selected video encoder: " + codec;
            return false;
        }
        return true;
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

        if (!params.inputVideo.isEmpty() && !SecurityPolicy::isSafeMediaInputPath(params.inputVideo)) {
            emit finished(false, "Invalid or unsafe media input path: " + params.inputVideo);
            return;
        }

        if (params.transparentProres) {
            // ── Transparent subtitle track (ProRes 4444 + alpha) ──────────────
            emit progressChanged(10, tr("Writing subtitle file..."));
            const auto &system = AppConfig::instance().system;
            QString tempRoot = system.useCustomTemp && !system.tempPath.trimmed().isEmpty()
                ? system.tempPath.trimmed()
                : QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
            if (system.useCustomTemp && !SecurityPolicy::isSafeTempRoot(tempRoot)) {
                tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
            }
            QDir().mkpath(tempRoot);
            QTemporaryDir jobDir(QDir(tempRoot).filePath("export-XXXXXX"));
            if (!jobDir.isValid()) { emit finished(false, tr("Cannot create temporary directory.")); return; }

            // Detect input video dimensions / duration for canvas FIRST — the
            // ASS PlayRes must match the real frame size so burned positions
            // land exactly where the preview showed them.
            int canvasW = 1920, canvasH = 1080;
            double durationSec = 10.0;
            if (!params.inputVideo.isEmpty() && QFileInfo(params.inputVideo).isFile()) {
                QString info = parseVideoInfo(ff, params.inputVideo).value(0);
                QRegularExpression reWH(R"((\d{2,5})x(\d{2,5}))");
                auto mWH = reWH.match(info);
                if (mWH.hasMatch()) {
                    int w = mWH.captured(1).toInt(), h = mWH.captured(2).toInt();
                    if (w >= 160 && h >= 90 && w <= 8192 && h <= 4320) { canvasW = w; canvasH = h; }
                }
                QRegularExpression reDur(R"(Duration:\s*(\d+):(\d+):(\d+)\.(\d+))");
                auto mDur = reDur.match(info);
                if (mDur.hasMatch()) {
                    int hh = mDur.captured(1).toInt(), mm = mDur.captured(2).toInt(),
                        ss = mDur.captured(3).toInt(), cs = mDur.captured(4).toInt();
                    durationSec = hh*3600 + mm*60 + ss + cs/100.0;
                } else if (!entries.isEmpty()) {
                    durationSec = (entries.last().endMs + 800) / 1000.0;
                }
                durationSec = qBound(1.0, durationSec, 60*60*3.0);
            } else if (!entries.isEmpty()) {
                durationSec = (entries.last().endMs + 800) / 1000.0;
            }

            const QString assPath = QDir(jobDir.path()).filePath("subtitles.ass");
            if (!writeASS(assPath, canvasW, canvasH)) { emit finished(false, tr("Failed to write ASS.")); return; }
            QString escapedAss = escapeSrtPath(assPath);

            // Verify encoder
            QString encErr;
            const QString proresCodec = hasEncoder(ff, "prores_ks", encErr) ? "prores_ks"
                                      : (hasEncoder(ff, "prores", encErr) ? "prores" : QString());
            if (proresCodec.isEmpty()) {
                emit finished(false, tr("FFmpeg 不支援 ProRes 編碼器，無法匯出透明字幕影片。請更新 FFmpeg。\n") + encErr);
                return;
            }

            const QString canvasSpec = QString("%1x%2").arg(canvasW).arg(canvasH);
            const QString durStr = QString::number(durationSec, 'f', 2);
            const QString vf = subtitleFilterArg(escapedAss) + ",format=yuva444p10le";

            QStringList args;
            args << "-y"
                 << "-f" << "lavfi" << "-i" << QString("color=c=black@0:s=%1:r=30:d=%2").arg(canvasSpec, durStr)
                 << "-vf" << vf
                 << "-c:v" << proresCodec << "-profile:v" << "4444" << "-pix_fmt" << "yuva444p10le"
                 << "-t" << durStr
                 << params.outputPath;

            emit progressChanged(30, tr("Encoding transparent ProRes..."));
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(ff, args);
            if (!p.waitForStarted(8000)) { emit finished(false, tr("無法啟動 FFmpeg：") + p.errorString()); return; }
            QElapsedTimer t; t.start();
            while (!p.waitForFinished(2000)) {
                if (t.elapsed() > 60*60*1000) { p.kill(); p.waitForFinished(5000);
                    emit finished(false, tr("FFmpeg 逾時（60 分鐘）。")); return; }
                emit progressChanged(60, tr("Encoding..."));
                p.readAll();
            }
            QByteArray out = p.readAll();
            if (p.exitCode() != 0) {
                QFile::remove(params.outputPath);
                QString err = extractError(QString::fromUtf8(out));
                if (err.isEmpty()) err = QString::fromUtf8(out).left(800);
                emit finished(false, tr("FFmpeg 失敗\n\n%1").arg(err));
                return;
            }
            emit progressChanged(100, tr("Done"));
            emit finished(true, {});
            return;
        }

        if (!QFileInfo(params.inputVideo).isFile()) {
            emit finished(false, "Input video does not exist: " + params.inputVideo);
            return;
        }

        if (!params.burnSubtitles) {
            QProcess p;
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(ff, {"-y", "-i", params.inputVideo, "-c", "copy", params.outputPath});
            if (!p.waitForStarted(10000)) {
                emit finished(false, "Could not start FFmpeg: " + p.errorString());
                return;
            }
            if (!p.waitForFinished(60 * 60 * 1000)) {
                p.kill(); p.waitForFinished(5000);
                emit finished(false, "FFmpeg timed out while copying the video.");
                return;
            }
            if (p.exitCode() != 0) {
                emit finished(false, "FFmpeg failed:\n\n" + QString::fromUtf8(p.readAll()).left(800));
                return;
            }
            emit progressChanged(100, "Done");
            emit finished(true, {});
            return;
        }

        emit progressChanged(10, "Writing subtitle file...");
        const auto &system = AppConfig::instance().system;
        QString tempRoot = system.useCustomTemp && !system.tempPath.trimmed().isEmpty()
            ? system.tempPath.trimmed()
            : QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
        if (system.useCustomTemp && !SecurityPolicy::isSafeTempRoot(tempRoot)) {
            tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/yumu_studio";
        }
        if (!QDir().mkpath(tempRoot)) {
            emit finished(false, "Cannot create temporary directory: " + tempRoot);
            return;
        }
        QTemporaryDir jobDir(QDir(tempRoot).filePath("export-XXXXXX"));
        if (!jobDir.isValid()) {
            emit finished(false, "Cannot create a private temporary export directory.");
            return;
        }

        emit progressChanged(20, "Detecting streams...");
        bool hasAudio = false;
        int canvasW = 1920, canvasH = 1080;
        QString videoInfo = parseVideoInfo(ff, params.inputVideo).first();
        if (videoInfo.contains("Audio:") || (videoInfo.contains("Stream #") && videoInfo.contains("Audio")))
            hasAudio = true;
        {
            // Use the real frame size as the ASS PlayRes canvas so \pos
            // coordinates are exact pixels — the burned block then sits
            // precisely where the preview drag placed it on any aspect ratio.
            QRegularExpression reWH(R"((\d{2,5})x(\d{2,5}))");
            auto mWH = reWH.match(videoInfo);
            if (mWH.hasMatch()) {
                int w = mWH.captured(1).toInt(), h = mWH.captured(2).toInt();
                if (w >= 160 && h >= 90 && w <= 8192 && h <= 4320) { canvasW = w; canvasH = h; }
            }
        }

        const QString subtitlePath = QDir(jobDir.path()).filePath("subtitles.ass");
        if (!writeASS(subtitlePath, canvasW, canvasH)) {
            emit finished(false, "Failed to write temporary ASS subtitles"); return;
        }

        QString escapedSubtitle = escapeSrtPath(subtitlePath);
        emit progressChanged(25, "Starting FFmpeg encode...");

        QString codec = params.videoCodec;
        if (codec != "libx264" && codec != "libx265" && codec != "libvpx-vp9") codec = "libx264";
        QString encoderError;
        if (!hasEncoder(ff, codec, encoderError)) {
            emit finished(false, encoderError);
            return;
        }
        QString preset = params.preset;
        if (!QStringList{"ultrafast", "fast", "medium", "slow", "veryslow"}.contains(preset)) preset = "fast";
        const QString crf = QString::number(qBound(0, params.crf, 51));
        struct Config { QString vf; };
        QList<Config> configs = {
            {"scale=trunc(iw/2)*2:trunc(ih/2)*2," + subtitleFilterArg(escapedSubtitle) + ",format=yuv420p"},
            {"scale=trunc(iw/2)*2:trunc(ih/2)*2," + subtitleFilterArg(escapedSubtitle)},
            {"subtitles=" + escapedSubtitle}
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
            args << "-c:v" << codec;
            args << "-crf" << crf;
            args << "-preset" << preset;
            if (hasAudio) {
                args << "-c:a" << "aac" << "-b:a" << QString::number(qBound(64, params.audioBitrate, 320)) + "k";
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

            QElapsedTimer encodeTimer;
            encodeTimer.start();
            int pct = 30;
            while (!p.waitForFinished(2000)) {
                if (encodeTimer.elapsed() > 60 * 60 * 1000) {
                    p.kill(); p.waitForFinished(5000);
                    lastError = "FFmpeg timed out after 60 minutes.";
                    break;
                }
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
        // Never let ASS override tags leak into SRT text — players would
        // render them literally.
        static const QRegularExpression overrideTag(
            QStringLiteral("\\{\\\\[^}]*\\}"));
        for (const auto &e : entries) {
            out << e.index << "\n"
                << msToSrtTime(e.startMs) << " --> " << msToSrtTime(e.endMs) << "\n"
                << QString(e.text).remove(overrideTag);
            if (!e.translation.isEmpty() && params.bilingualBurn)
                out << "\n" << QString(e.translation).remove(overrideTag);
            out << "\n\n";
        }
        return true;
    }

    bool writeASS(const QString &path, int canvasW, int canvasH) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        const SubtitleStyle &s = params.style;
        const QString font = s.fontFamily.isEmpty() ? QStringLiteral("Arial")
                                                      : s.fontFamily;
        // Everything is expressed on the real frame size (PlayRes = video
        // resolution), so \pos coordinates are exact pixels and the burned
        // block lands exactly where the preview drag placed it — regardless
        // of the video's aspect ratio. Font sizes / margins / outlines scale
        // from the 1080p reference by the same height factor as the preview.
        const qreal sy = qreal(qMax(1, canvasH)) / 1080.0;
        // Free placement (dragged in the preview) overrides the preset:
        // anchor both styles centre-middle and position every event with \pos.
        const bool freePos = s.posY >= 0.0 && s.posY <= 1.0;
        const int alignment = freePos ? 5
                            : (s.position == 1 ? 8 : (s.position == 2 ? 5 : 2));
        const int borderStyle = s.showBg ? 3 : 1;
        const int bold = s.bold ? -1 : 0;
        const int italic = s.italic ? -1 : 0;
        // Shadow (Scheme A true rendering): Back colour carries the shadow
        // colour when there is no background box; with a box it stays the box.
        QColor shadowBase = s.shadowColor.isValid() ? s.shadowColor : QColor(0, 0, 0);
        const int shadowAlpha = 255 - qBound(0, s.shadowOpacity, 100) * 255 / 100;
        shadowBase.setAlpha(shadowAlpha);
        const QString backColor = s.showBg ? toAssColor(s.bgColor) : toAssColor(shadowBase);
        const QString outlineColor = toAssColor(s.outlineColor);
        const QString textColor = toAssColor(s.textColor);
        const QString translationColor = toAssColor(s.bilingualColor);
        const int mainFontPx = qMax(1, qRound(s.fontSize * sy));
        const int transFontPx = qMax(1, qRound(s.bilingualFontSize * sy));
        const int outlinePx = qMax(0, qRound(s.outlineWidth * sy));
        const int shadowPx = s.shadowOpacity > 0 ? qMax(0, qRound(s.shadowWidth * sy)) : 0;
        const int mainMargin = qMax(0, qRound(s.marginV * sy));
        const int translationMargin = mainMargin + transFontPx + qMax(1, qRound(4.0 * sy));

        // Shared block layout in canvas pixels so burned output matches the
        // on-screen preview exactly.
        const bool hasBilingual = params.bilingualBurn;
        const double mainH = mainFontPx * 1.25 + outlinePx * 2.0 + 8.0 * sy;
        const double transH = hasBilingual ? transFontPx * 1.25 + 6.0 * sy : 0.0;
        const double blockH = mainH + transH;

        // Strip any ASS override tags that somehow ended up inside the
        // subtitle text (e.g. "{\pos(960.0,79.3)}") — escaping would make
        // libass render them literally on screen.
        auto assText = [](QString text) {
            static const QRegularExpression overrideTag(
                QStringLiteral("\\{\\\\[^}]*\\}"));
            text.remove(overrideTag);
            text.replace("\\", "\\\\");
            text.replace("{", "\\{");
            text.replace("}", "\\}");
            text.replace("\r\n", "\\N");
            text.replace('\n', "\\N");
            return text;
        };
        auto writeStyle = [&](const QString &name, int size, const QString &primary,
                              int margin) {
            out << "Style: " << name << "," << font << "," << size << ","
                << primary << ",&H000000FF," << outlineColor << "," << backColor << ","
                << bold << "," << italic << "," << borderStyle << ","
                << outlinePx << "," << shadowPx << "," << alignment << ","
                << margin << "," << margin << "," << margin << ",1\n";
        };

        out << "[Script Info]\nScriptType: v4.00+\nPlayResX: " << canvasW
            << "\nPlayResY: " << canvasH << "\n"
               "WrapStyle: 2\nScaledBorderAndShadow: yes\n\n"
               "[V4+ Styles]\n"
               "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
               "OutlineColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, "
               "Alignment, MarginL, MarginR, MarginV, Encoding\n";
        writeStyle("Main", mainFontPx, textColor, mainMargin);
        writeStyle("Translation", transFontPx, translationColor, translationMargin);
        out << "\n[Events]\n"
               "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";

        // Block centre in real canvas pixels for free placement.
        const qreal posX = qBound(0.02, s.posX, 0.98) * qreal(canvasW);
        const qreal centerY = qBound(0.02, s.posY, 0.98) * qreal(canvasH);
        const qreal mainCentreY = centerY - blockH / 2.0 + mainH / 2.0;
        const qreal transCentreY = centerY + blockH / 2.0 - transH / 2.0;

        for (const auto &e : entries) {
            const QString start = msToAssTime(e.startMs);
            const QString end = msToAssTime(e.endMs);
            if (freePos) {
                out << "Dialogue: 0," << start << "," << end
                    << ",Main,,0,0,0,,{\\pos(" << QString::number(posX, 'f', 1)
                    << "," << QString::number(mainCentreY, 'f', 1) << ")}"
                    << assText(e.text) << "\n";
            } else {
                out << "Dialogue: 0," << start << "," << end
                    << ",Main,,0,0,0,," << assText(e.text) << "\n";
            }
            if (hasBilingual && !e.translation.isEmpty()) {
                if (freePos) {
                    out << "Dialogue: 0," << start << "," << end
                        << ",Translation,,0,0,0,,{\\pos(" << QString::number(posX, 'f', 1)
                        << "," << QString::number(transCentreY, 'f', 1) << ")}"
                        << assText(e.translation) << "\n";
                } else {
                    out << "Dialogue: 0," << start << "," << end
                        << ",Translation,,0,0,0,," << assText(e.translation) << "\n";
                }
            }
        }
        return true;
    }
};

#include "ffmpegexporter.moc"

FFmpegExporter::FFmpegExporter(QObject *parent) : QObject(parent) {}
FFmpegExporter::~FFmpegExporter() {
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
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
        m_busy = false;
        if (m_thread) m_thread->quit();
        emit finished(ok, err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
