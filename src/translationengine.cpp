#include "translationengine.h"
#include "securitypolicy.h"
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
#include <QRegularExpression>
#include <QTimer>
#include <QJsonParseError>

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
        if (total == 0) {
            emit finished(true, {});
            return;
        }
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

            emit batchTranslated(i, results);
        }
        emit progressChanged(100, "Translation complete");
        emit finished(true, {});
    }

private:
    QStringList translateBatch(const QStringList &texts, QString &outErr) {
        if ((params.backend == TranslationBackend::OpenAI
             || params.backend == TranslationBackend::Anthropic
             || params.backend == TranslationBackend::Gemini)
            && params.apiKey.trimmed().isEmpty()) {
            outErr = "An API key is required for the selected translation service.";
            return {};
        }
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
        // SECURITY: reject HTTPS->HTTP downgrades and unexpected redirects.
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
        for (auto it = headers.begin(); it != headers.end(); ++it)
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        bool timedOut = false;
        auto *reply = nam.post(req, body);
        connect(reply, &QNetworkReply::redirected, &loop, [reply](const QUrl &target) {
            if (!reply) return;
            const QUrl targetUrl = reply->url().resolved(target);
            if (SecurityPolicy::isAllowedRedirect(reply->url(), targetUrl))
                reply->redirectAllowed();
            else
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, [&] {
            timedOut = true;
            reply->abort();
            loop.quit();
        });
        timeout.start(120000);
        loop.exec();

        if (timedOut) {
            outErr = "Network request timed out after 120 seconds.";
            reply->deleteLater();
            return {};
        }
        if (reply->error() != QNetworkReply::NoError) {
            // SECURITY: do not echo response bodies that may contain secrets.
            const QString sanitized = SecurityPolicy::redactSecrets(reply->errorString());
            outErr = "Network request failed: " + sanitized;
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

    QStringList parseNumberedLines(const QString &raw, int expected, QString &outErr) {
        QStringList result;
        static QRegularExpression re(R"(^\d+\.\s*(.+)$)");
        for (const QString &line : raw.split('\n')) {
            auto m = re.match(line.trimmed());
            if (m.hasMatch()) result << m.captured(1).trimmed();
        }
        // Some providers omit the numbering even when instructed to keep it.
        if (result.isEmpty())
            result = raw.split('\n', Qt::SkipEmptyParts);
        if (result.size() < expected) {
            outErr = QString("Translation response contained %1 lines; expected %2.")
                .arg(result.size()).arg(expected);
            return {};
        }
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

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(resp, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outErr = "OpenAI returned invalid JSON.";
            return {};
        }
        const auto choices = doc.object().value("choices").toArray();
        if (choices.isEmpty() || !choices.first().isObject()) {
            outErr = "OpenAI response did not contain a translation choice.";
            return {};
        }
        const auto message = choices.first().toObject().value("message").toObject();
        const QString raw = message.value("content").toString().trimmed();
        if (raw.isEmpty()) { outErr = "OpenAI returned an empty translation."; return {}; }
        return parseNumberedLines(raw, texts.size(), outErr);
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

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(resp, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outErr = "Anthropic returned invalid JSON.";
            return {};
        }
        const auto content = doc.object().value("content").toArray();
        if (content.isEmpty() || !content.first().isObject()) {
            outErr = "Anthropic response did not contain translated content.";
            return {};
        }
        const QString raw = content.first().toObject().value("text").toString().trimmed();
        if (raw.isEmpty()) { outErr = "Anthropic returned an empty translation."; return {}; }
        return parseNumberedLines(raw, texts.size(), outErr);
    }

    // ── Ollama ────────────────────────────────────────────────────────────────
    QStringList callOllama(const QStringList &texts, QString &outErr) {
        QJsonObject body;
        body["model"]  = params.ollamaModel;
        body["prompt"] = buildPrompt(texts);
        body["stream"] = false;

        QUrl base(params.ollamaUrl.trimmed());
        if (!base.isValid() || (base.scheme() != "http" && base.scheme() != "https") || base.host().isEmpty()) {
            outErr = "Ollama URL must be a valid http or https URL.";
            return {};
        }
        QString path = base.path();
        if (!path.endsWith('/')) path += '/';
        base.setPath(path + "api/generate");
        const QByteArray resp = postJson(base, QJsonDocument(body).toJson(QJsonDocument::Compact), {}, outErr);
        if (!outErr.isEmpty()) return {};
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(resp, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outErr = "Ollama returned invalid JSON.";
            return {};
        }
        const QString raw = doc.object().value("response").toString().trimmed();
        if (raw.isEmpty()) { outErr = "Ollama returned an empty translation."; return {}; }
        return parseNumberedLines(raw, texts.size(), outErr);
    }

    // ── v8.7: Google Gemini ───────────────────────────────────────────────────
    QStringList callGemini(const QStringList &texts, QString &outErr) {
        QJsonObject part;
        part["text"] = buildPrompt(texts);
        QJsonObject content;
        content["parts"] = QJsonArray{part};
        QJsonObject body;
        body["contents"] = QJsonArray{content};

        QMap<QString,QString> hdrs;
        hdrs["x-goog-api-key"] = params.apiKey;

        QString urlStr = QString(
            "https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
            .arg(params.geminiModel);

        QByteArray resp = postJson(
            QUrl(urlStr),
            QJsonDocument(body).toJson(QJsonDocument::Compact), hdrs, outErr);
        if (!outErr.isEmpty()) return {};

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(resp, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outErr = "Gemini returned invalid JSON.";
            return {};
        }
        const auto candidates = doc.object().value("candidates").toArray();
        QString raw;
        if (!candidates.isEmpty()) {
            const QJsonObject candidate = candidates.first().toObject();
            const QJsonObject content = candidate.value("content").toObject();
            const QJsonArray parts = content.value("parts").toArray();
            if (!parts.isEmpty())
                raw = parts.at(0).toObject().value("text").toString().trimmed();
        }
        if (raw.isEmpty()) {
            outErr = "Gemini returned empty response.";
            return {};
        }
        return parseNumberedLines(raw, texts.size(), outErr);
    }
};

#include "translationengine.moc"

// ─────────────────────────────────────────────────────────────────────────────
//  TranslationEngine
// ─────────────────────────────────────────────────────────────────────────────
TranslationEngine::TranslationEngine(QObject *parent) : QObject(parent) {}
TranslationEngine::~TranslationEngine() {
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
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
        m_busy = false;
        if (m_thread) m_thread->quit();
        emit finished(ok, err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
