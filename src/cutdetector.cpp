#include "cutdetector.h"
#include "securitypolicy.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>

CutDetector::CutDetector(QObject *parent) : QObject(parent) {}

void CutDetector::run(const QString &videoPath, const QString &ffmpegPath,
                      double sceneThreshold, qint64 durationMs) {
    if (m_process) return;
    if (!SecurityPolicy::isSafeMediaInputPath(videoPath)) {
        emit finished(false, {}, tr("Invalid or unsafe media input path: %1").arg(videoPath));
        return;
    }
    m_durationMs = durationMs;
    m_lastReportedMs = 0;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    QStringList args;
    args << "-hide_banner" << "-nostats"
         << "-i" << videoPath
         << "-an" << "-sn" << "-dn"
         << "-vf" << QString("select='gt(scene,%1)',metadata=print").arg(sceneThreshold, 0, 'f', 2)
         << "-f" << "null" << "-";

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        static const QRegularExpression timeRe(QStringLiteral("pts_time:(\\d+(?:\\.\\d+)?)"));
        const QString chunk = QString::fromLocal8Bit(m_process->readAllStandardOutput());
        auto it = timeRe.globalMatch(chunk);
        while (it.hasNext()) {
            const double seconds = it.next().captured(1).toDouble();
            const qint64 ms = qRound64(seconds * 1000.0);
            m_lastReportedMs = qMax(m_lastReportedMs, ms);
            if (m_cuts.isEmpty() || m_cuts.last() + 120 < ms)
                m_cuts << ms;
        }
        if (m_durationMs > 0)
            emit progress(qBound(0, int(m_lastReportedMs * 100 / m_durationMs), 99));
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        const QVector<qint64> cuts = m_cuts;
        m_process->deleteLater();
        m_process = nullptr;
        if (status == QProcess::CrashExit) {
            emit finished(false, {}, tr("Could not start FFmpeg for scene detection."));
            return;
        }
        if (exitCode != 0)
            emit finished(false, cuts, QStringLiteral("FFmpeg exited with code %1").arg(exitCode));
        else
            emit finished(true, cuts, {});
    });

    m_process->start(ffmpegPath.isEmpty() ? QStringLiteral("ffmpeg") : ffmpegPath, args);
}

void CutDetector::cancel() {
    if (!m_process) return;
    m_process->kill();
}
