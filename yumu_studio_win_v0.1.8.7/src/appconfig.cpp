#include "appconfig.h"
#include <QDir>
#include <QStandardPaths>

AppConfig &AppConfig::instance() {
    static AppConfig inst;
    return inst;
}

QSettings *AppConfig::settings() {
    if (!m_settings) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        m_settings = new QSettings(dir + "/settings.ini", QSettings::IniFormat);
    }
    return m_settings;
}

void AppConfig::load() {
    auto *s = settings();

    s->beginGroup("Paths");
    paths.ffmpegPath     = s->value("ffmpeg",     "ffmpeg").toString();
    paths.whisperCliPath = s->value("whisperCli", "").toString();
    paths.modelPath      = s->value("model",      "").toString();
    s->endGroup();

    s->beginGroup("Whisper");
    whisper.modelPath = paths.modelPath;
    whisper.cliPath   = paths.whisperCliPath;
    whisper.language  = s->value("language",  "auto").toString();
    whisper.threads   = s->value("threads",   4).toInt();
    whisper.translate = s->value("translate", false).toBool();
    s->endGroup();

    s->beginGroup("Translation");
    translation.backend     = static_cast<TranslationBackend>(s->value("backend", 0).toInt());
    translation.apiKey      = s->value("apiKey",      "").toString();
    translation.targetLang  = s->value("targetLang",  "zh-TW").toString();
    translation.ollamaUrl   = s->value("ollamaUrl",   "http://localhost:11434").toString();
    translation.ollamaModel = s->value("ollamaModel", "llama3").toString();
    translation.geminiModel = s->value("geminiModel", "gemini-2.0-flash").toString();
    s->endGroup();
}

void AppConfig::save() {
    auto *s = settings();

    paths.modelPath      = whisper.modelPath;
    paths.whisperCliPath = whisper.cliPath;

    s->beginGroup("Paths");
    s->setValue("ffmpeg",     paths.ffmpegPath);
    s->setValue("whisperCli", paths.whisperCliPath);
    s->setValue("model",      paths.modelPath);
    s->endGroup();

    s->beginGroup("Whisper");
    s->setValue("language",  whisper.language);
    s->setValue("threads",   whisper.threads);
    s->setValue("translate", whisper.translate);
    s->endGroup();

    s->beginGroup("Translation");
    s->setValue("backend",     static_cast<int>(translation.backend));
    s->setValue("apiKey",      translation.apiKey);
    s->setValue("targetLang",  translation.targetLang);
    s->setValue("ollamaUrl",   translation.ollamaUrl);
    s->setValue("ollamaModel", translation.ollamaModel);
    s->setValue("geminiModel", translation.geminiModel);
    s->endGroup();

    s->sync();
}
