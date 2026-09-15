#include "subtitlecorrectionengine.h"
#include "securitypolicy.h"
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QUrl>
#include <QRegularExpression>
#include <QTimer>
#include <QJsonParseError>

// ─────────────────────────────────────────────────────────────────────────────
//  Worker
// ─────────────────────────────────────────────────────────────────────────────
class CorrectionWorker : public QObject {
    Q_OBJECT
public:
    QList<SubtitleEntry> entries;
    CorrectionParams     params;

signals:
    void progressChanged(int pct, const QString &msg);
    void entryCorrected(int index, QString text);
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
            QStringList texts;
            int end = qMin(i + batch, total);
            for (int j = i; j < end; ++j)
                texts << entries[j].text;

            int pct = int(100.0 * i / total);
            emit progressChanged(pct, tr("Correcting %1/%2...").arg(i).arg(total));

            QString err;
            QStringList results = correctBatch(texts, err);
            if (!err.isEmpty()) { emit finished(false, err); return; }

            for (int k = 0; k < results.size() && (i + k) < total; ++k)
                emit entryCorrected(i + k, results[k]);
        }
        emit progressChanged(100, tr("Correction complete"));
        emit finished(true, {});
    }

private:
    QStringList correctBatch(const QStringList &texts, QString &outErr) {
        if ((params.backend == CorrectionBackend::OpenAI
             || params.backend == CorrectionBackend::Anthropic
             || params.backend == CorrectionBackend::Gemini)
            && params.apiKey.trimmed().isEmpty()) {
            outErr = tr("An API key is required for the selected correction service.");
            return {};
        }
        switch (params.backend) {
            case CorrectionBackend::OpenAI:    return callOpenAI(texts, outErr);
            case CorrectionBackend::Anthropic: return callAnthropic(texts, outErr);
            case CorrectionBackend::Ollama:    return callOllama(texts, outErr);
            case CorrectionBackend::Gemini:    return callGemini(texts, outErr);
        }
        return texts;
    }

    QByteArray postJson(const QUrl &url,
                        const QByteArray &body,
                        const QMap<QString,QString> &headers,
                        QString &outErr)
    {
        QNetworkAccessManager nam;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
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
            outErr = tr("Network request timed out after 120 seconds.");
            reply->deleteLater();
            return {};
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QString sanitized = SecurityPolicy::redactSecrets(reply->errorString());
            outErr = tr("Network request failed: %1").arg(sanitized);
            reply->deleteLater();
            return {};
        }
        QByteArray data = reply->readAll();
        reply->deleteLater();
        return data;
    }

    QString buildPrompt(const QStringList &texts) {
        QString numbered;
        for (int i = 0; i < texts.size(); ++i)
            numbered += QString("%1. %2\n").arg(i + 1).arg(texts[i]);
        QString instruction = params.customInstruction.trimmed();
        if (instruction.isEmpty()) {
            instruction = tr("Fix obvious speech-recognition errors, punctuation and segmentation. "
                             "Keep the original meaning and spoken style. "
                             "Do not change names unless they are clearly wrong.");
        }
        return tr("You are a professional subtitle proofreader. %1\n\n"
                  "Correct the following numbered subtitles. "
                  "Return ONLY the corrected lines in the same order, one per line, "
                  "prefixed with the same number. Do NOT add explanations.\n\n%2")
            .arg(instruction, numbered.trimmed());
    }

    QStringList parseNumberedLines(const QString &raw, int expected, QString &outErr) {
        QStringList result;
        static QRegularExpression re("^\\d+\\.\\s*(.+)$");

        for (const QString &line : raw.split('\n')) {
            auto m = re.match(line.trimmed());
            if (m.hasMatch()) result << m.captured(1).trimmed();
        }
        if (result.isEmpty())
            result = raw.split('\n', Qt::SkipEmptyParts);
        if (result.size() < expected) {
            outErr = tr("Correction response contained %1 lines; expected %2.").arg(result.size()).arg(expected);
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
            outErr = tr("OpenAI returned invalid JSON.");
            return {};
        }
        const auto choices = doc.object().value("choices").toArray();
        if (choices.isEmpty() || !choices.first().isObject()) {
            outErr = tr("OpenAI response did not contain a correction choice.");
            return {};
        }
        const auto message = choices.first().toObject().value("message").toObject();
        const QString raw = message.value("content").toString().trimmed();
        if (raw.isEmpty()) { outErr = tr("OpenAI returned an empty correction."); return {}; }
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
            outErr = tr("Anthropic returned invalid JSON.");
            return {};
        }
        const auto content = doc.object().value("content").toArray();
        if (content.isEmpty() || !content.first().isObject()) {
            outErr = tr("Anthropic response did not contain corrected content.");
            return {};
        }
        const QString raw = content.first().toObject().value("text").toString().trimmed();
        if (raw.isEmpty()) { outErr = tr("Anthropic returned an empty correction."); return {}; }
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
            outErr = tr("Ollama URL must be a valid http or https URL.");
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
            outErr = tr("Ollama returned invalid JSON.");
            return {};
        }
        const QString raw = doc.object().value("response").toString().trimmed();
        if (raw.isEmpty()) { outErr = tr("Ollama returned an empty correction."); return {}; }
        return parseNumberedLines(raw, texts.size(), outErr);
    }

    // ── Gemini ─────────────────────────────────────────────────────────────────
    QStringList callGemini(const QStringList &texts, QString &outErr) {
        QJsonObject part;
        part["text"] = buildPrompt(texts);
        QJsonObject content;
        content["parts"] = QJsonArray{part};
        QJsonObject body;
        body["contents"] = QJsonArray{content};

        QMap<QString,QString> hdrs;
        hdrs["x-goog-api-key"] = params.apiKey;

        QString urlStr = QString("https://generativelanguage.googleapis.com/v1beta/models/%1:generateContent")
            .arg(params.geminiModel);

        QByteArray resp = postJson(QUrl(urlStr), QJsonDocument(body).toJson(QJsonDocument::Compact), hdrs, outErr);
        if (!outErr.isEmpty()) return {};

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(resp, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            outErr = tr("Gemini returned invalid JSON.");
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
            outErr = tr("Gemini returned empty response.");
            return {};
        }
        return parseNumberedLines(raw, texts.size(), outErr);
    }
};

#include "subtitlecorrectionengine.moc"

// ─────────────────────────────────────────────────────────────────────────────
//  SubtitleCorrectionEngine
// ─────────────────────────────────────────────────────────────────────────────
SubtitleCorrectionEngine::SubtitleCorrectionEngine(QObject *parent) : QObject(parent) {}

SubtitleCorrectionEngine::~SubtitleCorrectionEngine() {
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

void SubtitleCorrectionEngine::correct(const QList<SubtitleEntry> &entries,
                                       const CorrectionParams &params)
{
    if (m_busy) return;
    m_busy   = true;
    m_thread = new QThread(this);
    m_worker = new CorrectionWorker();
    m_worker->entries = entries;
    m_worker->params  = params;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,                 m_worker, &CorrectionWorker::run);
    connect(m_worker, &CorrectionWorker::progressChanged, this, &SubtitleCorrectionEngine::progressChanged);
    connect(m_worker, &CorrectionWorker::entryCorrected,  this, &SubtitleCorrectionEngine::entryCorrected);
    connect(m_worker, &CorrectionWorker::finished, this, [this](bool ok, const QString &err){
        m_busy = false;
        if (m_thread) m_thread->quit();
        emit finished(ok, err);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}
