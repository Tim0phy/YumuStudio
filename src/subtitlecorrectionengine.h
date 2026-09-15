#pragma once
#include <QObject>
#include <QPointer>
#include <QList>
#include <QString>
#include "subtitlemodel.h"

enum class CorrectionBackend {
    OpenAI    = 0,
    Anthropic = 1,
    Ollama    = 2,
    Gemini    = 3
};

struct CorrectionParams {
    CorrectionBackend backend     = CorrectionBackend::OpenAI;
    QString           apiKey;
    QString           ollamaUrl   = "http://localhost:11434";
    QString           ollamaModel = "llama3";
    QString           geminiModel = "gemini-2.0-flash";
    int               batchSize   = 20;
    // Free-text instructions appended to the system prompt.
    QString           customInstruction;
};

class QThread;
class CorrectionWorker;

class SubtitleCorrectionEngine : public QObject {
    Q_OBJECT
public:
    explicit SubtitleCorrectionEngine(QObject *parent = nullptr);
    ~SubtitleCorrectionEngine();

    void correct(const QList<SubtitleEntry> &entries, const CorrectionParams &params);
    bool isBusy() const { return m_busy; }

signals:
    void progressChanged(int pct, const QString &msg);
    void entryCorrected(int index, QString text);
    void finished(bool ok, const QString &error);

private:
    QPointer<QThread>   m_thread;
    CorrectionWorker   *m_worker = nullptr;
    bool                m_busy   = false;
};
