#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include "subtitlemodel.h"

enum class TranslationBackend {
    OpenAI    = 0,
    Anthropic = 1,
    Ollama    = 2,
    Gemini    = 3   // v8.7 NEW
};

struct TranslationParams {
    TranslationBackend backend     = TranslationBackend::OpenAI;
    QString            apiKey;
    QString            targetLang  = "zh-TW";
    QString            ollamaUrl   = "http://localhost:11434";
    QString            ollamaModel = "llama3";
    QString            geminiModel = "gemini-2.0-flash";  // v8.7
    int                batchSize   = 20;
};

class QThread;
class TranslationWorker;

class TranslationEngine : public QObject {
    Q_OBJECT
public:
    explicit TranslationEngine(QObject *parent = nullptr);
    ~TranslationEngine();

    void translate(const QList<SubtitleEntry> &entries, const TranslationParams &params);
    bool isBusy() const { return m_busy; }

signals:
    void progressChanged(int pct, const QString &msg);
    void batchTranslated(int startIndex, QStringList translations);
    void finished(bool ok, const QString &error);

private:
    QThread            *m_thread = nullptr;
    TranslationWorker  *m_worker = nullptr;
    bool                m_busy   = false;
};
