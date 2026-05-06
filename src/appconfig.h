#pragma once
#include <QString>
#include <QSettings>
#include "whisperengine.h"
#include "translationengine.h"

struct AppPaths {
    QString ffmpegPath;
    QString whisperCliPath;
    QString modelPath;
};

class AppConfig {
public:
    static AppConfig &instance();
    void load();
    void save();

    AppPaths          paths;
    WhisperParams     whisper;
    TranslationParams translation;

private:
    AppConfig() = default;
    QSettings *settings();
    QSettings *m_settings = nullptr;
};
