#pragma once
#include <QObject>
#include <QPointer>
#include <QThread>
#include <atomic>
#include "subtitlemodel.h"

struct WhisperParams {
    QString engine = "whisper.cpp";
    QString modelId;
    QString modelPath;
    QString cliPath;
    QString runnerPath;
    QString language  = "auto";
    QString computeDevice = "auto";
    QString precision = "auto";
    int     threads   = 4;
    bool    translate = false;
    bool    useGlossary = true;
    // Free-text transcription prompt (whisper.cpp --prompt,
    // faster-whisper initial_prompt). Empty = previous behaviour.
    QString customPrompt;
};

class WhisperWorker;

class WhisperEngine : public QObject {
    Q_OBJECT
public:
    explicit WhisperEngine(QObject *parent = nullptr);
    ~WhisperEngine();

    void transcribe(const QString &videoPath, const WhisperParams &params);
    void stop();
    bool isBusy() const { return m_busy; }

signals:
    void progressChanged(int pct, const QString &msg);
    void segmentReady(SubtitleEntry entry);
    void warning(const QString &msg);
    void finished(bool ok, const QString &error);

private:
    bool           m_busy   = false;
    QPointer<QThread> m_thread;
    WhisperWorker *m_worker = nullptr;
    std::atomic<qint64> m_childProcessId{0};
    bool m_stopRequested = false;
    void killChildProcess();
};
