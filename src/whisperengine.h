#pragma once
#include <QObject>
#include <QThread>
#include "subtitlemodel.h"

struct WhisperParams {
    QString modelPath;
    QString cliPath;
    QString language  = "auto";
    int     threads   = 4;
    bool    translate = false;
};

class WhisperWorker;

class WhisperEngine : public QObject {
    Q_OBJECT
public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine();

    void transcribe(const QString &videoPath, const WhisperParams &params);
    bool isBusy() const { return m_busy; }

signals:
    void progressChanged(int pct, const QString &msg);
    void segmentReady(SubtitleEntry entry);
    void finished(bool ok, const QString &error);

private:
    bool           m_busy   = false;
    QThread       *m_thread = nullptr;
    WhisperWorker *m_worker = nullptr;
};
