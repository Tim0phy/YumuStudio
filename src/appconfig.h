#pragma once
#include <QString>
#include <QSettings>
#include "whisperengine.h"
#include "translationengine.h"
#include "subtitlecorrectionengine.h"

struct AppPaths {
    QString ffmpegPath;
    QString whisperCliPath;
    QString modelPath;
    QString modelRoot;
    QString runnerPath;
    QString engineRoot;
};

struct PreviewParams {
    // Preview ≠ Export: preview may sacrifice resolution for smoothness.
    bool lowResPreview = true;      // 低清預覽開關
    int previewWidth = 640;         // 低清寬度 (480/640/720/960/1024)
    int previewFpsCap = 24;         // 幀率上限 (15/24/30)
    bool hardwarePreview = false;   // GPU 硬件解碼：true=QT_FFMPEG_DECODING_HW_DEVICE_TYPES=d3d11va...  false=強制 CPU 軟解
    float volume = 1.0f;            // 預覽音量 (0.0 - 1.0)
    bool muted = false;             // 預覽靜音
};

struct SystemParams {
    QString language = "zh";
    QString theme = "light";   // "dark" | "light"
    bool checkUpdates = true;
    bool preventSleep = true;
    bool useCustomTemp = false;
    QString tempPath;
    int proxyMode = 0;
    QString proxyHost;
    int proxyPort = 0;
    // First-run onboarding: the Engine Center is opened automatically only
    // once so later launches never interrupt the user with a settings page.
    bool onboardingDone = false;
    bool learnMistakes = true;       // auto-learn frequent corrections
    int  mistakeThreshold = 3;       // auto-promote to glossary after N observations
};

struct AppUpdateParams {
    bool    enabled   = true;
    QString repoOwner = "Tim0phy";
    QString repoName  = "YumuStudio";
    QString skippedVersion; // user chose "Skip this version"; don't prompt again until a newer release
};

struct WorkspaceParams {
    QList<int> topSplitSizes;   // video card | right column widths
};

class AppConfig {
public:
    static AppConfig &instance();
    static bool isPortableMode();
    static QString portableRoot();
    void load();
    void save();
    void applyNetworkSettings() const;
    QSettings *settings();

    AppPaths          paths;
    SystemParams      system;
    WorkspaceParams   workspace;
    PreviewParams     preview;
    WhisperParams     whisper;
    TranslationParams translation;
    CorrectionParams  correction;
    AppUpdateParams   update;

private:
    AppConfig() = default;
    QSettings *m_settings = nullptr;
};
