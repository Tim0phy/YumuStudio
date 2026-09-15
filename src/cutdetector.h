#pragma once
#include <QObject>
#include <QVector>

class QProcess;

class CutDetector : public QObject {
    Q_OBJECT
public:
    explicit CutDetector(QObject *parent = nullptr);

    void run(const QString &videoPath, const QString &ffmpegPath,
             double sceneThreshold, qint64 durationMs);
    void cancel();

signals:
    void progress(int percent);
    void finished(bool ok, const QVector<qint64> &cutPointsMs, const QString &error);

private:
    QProcess *m_process = nullptr;
    QVector<qint64> m_cuts;
    qint64 m_durationMs = 0;
    qint64 m_lastReportedMs = 0;
};
