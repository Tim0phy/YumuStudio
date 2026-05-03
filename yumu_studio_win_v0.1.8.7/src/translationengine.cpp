#include "translationengine.h"
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QUrlQuery>
#include <QUrl>

// ─────────────────────────────────────────────────────────────────────────────
//  Worker
// ─────────────────────────────────────────────────────────────────────────────
class TranslationWorker : public QObject {
    Q_OBJECT
public:
    QList<SubtitleEntry> entries;
    TranslationParams    params;

signals:
    void progressChanged(int pct, const QString &msg);
    void batchTranslated(int startIdx, QStringList translations);
    void finished(bool ok, const QString &error);

public slots:
    void run() {
        int total = entries.size();
        int batch = qMax(1, params.batchSize);

        for (int i = 0; i < total; i += batch) {
            // Collect batch texts
            QStringList texts;
            for (int j = i; j < qMin(i+batch, total); ++j)
                texts << entries[j].text;

            int pct = int(100.0 * i / total);
            emit progressChanged(pct, QString("Translating %1/%2...").arg(i).arg(total));

            QString err;
            QStringList results = translateBatch(texts, err);

            if (!err.isEmpty()) { emit finished(false, err); return; }

            // Pad if needed
            while (results.size() < texts.size())
                results << texts[results.size()];

            emit batchTranslated(i, results);
        }
        emit progressChanged(100, "Translation complete");
        emit finished(true, {});
    }

private:
    QStringList translateBatch(const QStringList &texts, QString &outErr) {
        switch (params.backend) {
            case TranslationBackend::OpenAI:    return callOpenAI(texts, outErr);
            case TranslationBackend::Anthropic: return callAnthropic(texts, outErr);
            case TranslationBackend::Ollama:    return callOllama(texts, outErr);
            case TranslationBackend::Gemini:    return callGemini(texts, outErr);   // v8.7
        }
        return texts;
    }

    // ── Shared helpers ────────────────────────────────────────────────────────
    QByteArray postJson(const QUrl &url,
                        const QByteArray &body,
                        const QMap<QString,QString> &headers,
                        QString &outErr)
    {
        QNetworkAccessManager nam;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        for (auto it = headers.begin(); it != headers.end(); ++it)
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

        QEventLoop loop;
        auto *reply = nam.post(req, body);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            outErr = reply->errorString() + "\n" + reply->readAll().left(400);
            reply->deleteLater();
            return {};
        }
        QByteArray data = reply->readAll();
        reply->deleteLater();
        return data;
    }

    QString buildPrompt(const QStringList &texts) {
        return QString(
            "You are a professional subtitle translator. "
            "Translate the following numbered subtitles into %1. "
            "Return ONLY the translated lines in the same order, one per line, "
            "prefixed with the same number. Do NOT add explanations.\n\n%2")
            .arg(params.targetLang,
                 [&](){
                     QString r;
                     for (int i=0; i<texts.size(); ++i)
                         r += QString("%1. %2\n").arg(i+1).arg(texts[i]);
                     return r.trimmed();
                 }());
    }

    QStringList parseNumberedLines(const QString &raw, int expected) {
        QStringList result;
        static QRegularExpression re(R"(^\d+\.\s*(.+)$)");
        for (const QString &line : raw.split('\n')) {
            auto m = re.match(line.trimmed());
            if (m.hasMatch()) result << m.captured(1).trimmed();
        }
        // fallback: just split by newlines if regex yields nothing
        if (result.isEmpty())
            result = raw.split('\n', Qt::SkipEmptyParts);
        while (result.size() < expected) result << "—";
        return result.mid(0, expected);
    }

    // ── OpenAI ────────────────────────────────────────────────────────────────
    QStringList callOpenAI(const QStringList &texts, QString &outErr) {
        QJsonObject body;
        body["model"] = "gpt-4o-mini";
        QJsonArray msgs;
        msgs.append(QJsonObject{{"role","user"},{"content",buildPrompt(texts)}});
        body["messages"] = msgs;

        QMap<QString,QString> hdrs;
        hdrs["Authorization"] = "Bearer " + params.apiKey;

        QByteArray resp = postJson(
            QUrl("https://api.openai.com/v1/chat/completions"),
            QJsonDocument(body).toJson(QJsonDocument::Compact), hdrs, outErr);
        if (!outErr.isEmpty()) return {};

        auto doc = QJsonDocument::fromJson(resp);
        QString raw = doc["choices"][0]["message"]["content"].toString();
        return parseNumberedLines(raw, texts.size());
    }

    // ── Anthropic ─────────────────────────────────────────────────────────────
    QStringList callAnthropic(const QStringList &texts, QString &outErr) {
        QJsonObject body;
        body["model"]      = "claude-3-5-haiku-20241022";
        body["max_tokens"] = 4096;
        QJsonArray msgs;
        msgs.append(QJsonObject{{"role","user"},{"content",buildPrompt(texts)}});
        body["messages"] = msgs;

        QMap<QString,QString> hdrs;
        hdrs["x-api-key"]         = params.apiKey;
        hdrs["anthropic-version"] = "2023-06-01";

        QByteArray resp = postJson(
            QUrl("https://api.anthropic.com/v1/messages"),
            QJsonDocument(body).toJson(QJsonDocument::Compact), hdrs, outErr);
        if (!outErr.isEmpty()) return {};

        auto doc = QJsonDocument::fromJson(resp);
        QString raw = doc["content"][0]["text"].toString();
        return parseNumberedLines(raw, texts.size());
    }

    // ── Ollama ────────────────────────────────────────────────────────────────
    QStringList callOllama(const QStringList &texts, QString &outErr) {
        QJsonObject body;
        body["model"]  = params.ollamaModel;
        body["prompt"] = buildPrompt(texts);
        body["stream"] = false;

        QNetworkAccessManager nam;
        QNetworkRequest req(QUrl(params.ollamaUrl + "/api/generate"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QEventLoop loop;
        auto *reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            outErr = reply->errorString();
            reply->deleteLater();
            return {};
        }
        auto doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        QString raw = doc["response"].toString();
        return parseNumberedLines(raw, texts.size());
    }

    // ── v8.7: Google Gemini ───────────────────────────────────────────────────
    QStringList callGemini(const QStringList &texts, QString &outErr) {
        QJsonObject part;
        part["text"] = buildPrompt(texts);
        QJsonObject content;
        content["parts"] = QJsonArray{part};
        QJsonObject body;
        body["contents"] = QJsonArray{content};

        QMap<QString,QString> hdrs;  // No extra headers needed; key goes in URL

        QString urlStr = QString(
            "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent?key=%2")
            .arg(params.geminiModel, params.apiKey);

        QByteArray resp = postJson(
            QUrl(urlStr),
            QJsonDocument(body).toJson(QJsonDocument::Compact), hdrs, outErr);
        if (!outErr.isEmpty()) return {};

        auto doc = QJsonDocument::fromJson(resp);
        // Gemini response path: candidates[0].content.parts[0].text
        QString raw = doc["candidates"][0]["content"]["parts"][0]["text"].toString();
        if (raw.isEmpty()) {
            outErr = "Gemini returned empty response: " + QString::fromUtf8(resp).left(400);
            return {};
        }
        return parseNumberedLines(raw, texts.size());
    }
};

#include "translationengine.moc"

// ─────────────────────────────────────────────────────────────────────────────
//  TranslationEngine
// ─────────────────────────────────────────────────────────────────────────────
TranslationEngine::TranslationEngine(QObject *parent) : QObject(parent) {}
TranslationEngine::~TranslationEngine() {
    if (m_thread) { m_thread->quit(); m_thread->wait(); }
}

void TranslationEngine::translate(const QList<SubtitleEntry> &entries,
                                   const TranslationParams    &params)
{
    if (m_busy) return;
    m_busy   = true;
    m_thread = new QThread(this);
    m_worker = new TranslationWorker();
    m_worker->entries = entries;
    m_worker->params  = params;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,                m_worker, &TranslationWorker::run);
    connect(m_worker, &TranslationWorker::progressChanged, this, &TranslationEngine::progressChanged);
    connect(m_worker, &TranslationWorker::batchTranslated, this, &TranslationEngine::batchTranslated);
    connect(m_worker, &TranslationWorker::finished, this, [this](bool ok, const QString &err){
        m_busy = false; m_thread->quit(); emit finished(ok, err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
