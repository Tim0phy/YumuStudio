#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QList>
#include "subtitlemodel.h"

struct ExportParams {
    QString       inputVideo;
    QString       outputPath;
    QString       ffmpegPath;
    bool          burnSubtitles = true;
    bool          bilingualBurn = false;
    bool          srtOnly       = false;
    bool          transparentProres = false; // 透明字幕影片 (ProRes 4444)
    QString       videoCodec    = "libx264";
    int           crf           = 23;
    QString       preset        = "fast";
    int           audioBitrate  = 192;
    SubtitleStyle style;
};

class QThread;
class ExportWorker;

class FFmpegExporter : public QObject {
    Q_OBJECT
public:
    explicit FFmpegExporter(QObject *parent = nullptr);
    ~FFmpegExporter();
    void startExport(const ExportParams &params,
                     const QList<SubtitleEntry> &entries);
    bool isBusy() const { return m_busy; }

signals:
    void progressChanged(int pct, const QString &msg);
    void finished(bool ok, const QString &error);

private:
    ExportWorker *m_worker = nullptr;
    QPointer<QThread> m_thread;
    bool          m_busy   = false;
};
