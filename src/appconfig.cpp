#include "appconfig.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QNetworkProxy>
#include "hardwareprobe.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")
#endif

namespace {
QString protectSecret(const QString &value) {
#ifdef Q_OS_WIN
    if (value.isEmpty()) return value;
    const QByteArray plain = value.toUtf8();
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Yumu Studio", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        qWarning("DPAPI encryption failed; secret will not be persisted.");
        return {};
    }
    const QByteArray encrypted(reinterpret_cast<const char *>(output.pbData),
                               static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return QStringLiteral("dpapi:") + QString::fromLatin1(encrypted.toBase64());
#else
    return value;
#endif
}

QString unprotectSecret(const QString &value) {
#ifdef Q_OS_WIN
    if (!value.startsWith(QStringLiteral("dpapi:"))) return value;
    const QByteArray encrypted = QByteArray::fromBase64(value.mid(6).toLatin1());
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        qWarning("DPAPI decryption failed; existing encrypted secret will be cleared on next save.");
        return {};
    }
    const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(output.pbData),
                                             static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return plain;
#else
    return value;
#endif
}
}

AppConfig &AppConfig::instance() {
    static AppConfig inst;
    return inst;
}

bool AppConfig::isPortableMode() {
    const QString root = QCoreApplication::applicationDirPath();
    return QFileInfo::exists(QDir(root).filePath("portable.flag"))
        || QFileInfo::exists(QDir(root).filePath("portable.ini"));
}

QString AppConfig::portableRoot() {
    return QCoreApplication::applicationDirPath();
}

QSettings *AppConfig::settings() {
    if (!m_settings) {
        QString dir = isPortableMode()
            ? QDir(portableRoot()).filePath("config")
            : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        m_settings = new QSettings(dir + "/settings.ini", QSettings::IniFormat);
    }
    return m_settings;
}

void AppConfig::load() {
    auto *s = settings();

    s->beginGroup("System");
    system.language      = s->value("language", "zh").toString();
    system.theme         = s->value("theme", "light").toString();
    system.checkUpdates  = s->value("checkUpdates", true).toBool();
    system.preventSleep  = s->value("preventSleep", true).toBool();
    system.useCustomTemp = s->value("useCustomTemp", false).toBool();
    system.tempPath      = s->value("tempPath", QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("yumu_studio")).toString();
    system.proxyMode     = s->value("proxyMode", 0).toInt();
    system.proxyHost     = s->value("proxyHost", "").toString();
    system.proxyPort     = s->value("proxyPort", 0).toInt();
    system.onboardingDone = s->value("onboardingDone", false).toBool();
    system.learnMistakes  = s->value("learnMistakes", true).toBool();
    system.mistakeThreshold = s->value("mistakeThreshold", 3).toInt();
    s->endGroup();

    s->beginGroup("Paths");
    const QString portableBin = QDir(portableRoot()).filePath("bin");
    const QString portableFfmpeg = QDir(portableBin).filePath("ffmpeg.exe");
    const QString portableCli = QDir(portableBin).filePath("whisper-cli.exe");
    const QString configuredFfmpeg = s->value("ffmpeg", "").toString();
    const QString configuredCli = s->value("whisperCli", "").toString();
    paths.ffmpegPath     = QFileInfo::exists(portableFfmpeg) ? portableFfmpeg
                         : (configuredFfmpeg.isEmpty() ? "ffmpeg" : configuredFfmpeg);
    paths.whisperCliPath = QFileInfo::exists(portableCli) ? portableCli : configuredCli;
    paths.modelPath      = s->value("model",      "").toString();
    paths.modelRoot      = s->value("modelRoot", isPortableMode() ? QDir(portableRoot()).filePath("models") : "").toString();
    const QString portablePython = QDir(portableRoot()).filePath("engines/python/python.exe");
    const QString legacyPortablePython = QDir(portableRoot()).filePath("runtime/asr-venv/Scripts/python.exe");
    const QString configuredRunner = s->value("runner", "").toString();
    if (QFileInfo::exists(portablePython))
        paths.runnerPath = portablePython;
    else if (QFileInfo::exists(legacyPortablePython))
        paths.runnerPath = legacyPortablePython;
    else
        paths.runnerPath = configuredRunner;
    paths.engineRoot     = s->value("engineRoot", "").toString();
    s->endGroup();

    s->beginGroup("Whisper");
    whisper.modelPath = paths.modelPath;
    whisper.engine    = s->value("engine", "whisper.cpp").toString();
    whisper.modelId   = s->value("modelId", "").toString();
    whisper.cliPath   = paths.whisperCliPath;
    whisper.runnerPath = paths.runnerPath;
    whisper.language  = s->value("language",  "auto").toString();
    whisper.computeDevice = s->value("computeDevice", "auto").toString();
    whisper.precision = s->value("precision", "auto").toString();
    whisper.threads   = s->value("threads",   4).toInt();
    whisper.translate = s->value("translate", false).toBool();
    whisper.useGlossary = s->value("useGlossary", true).toBool();
    whisper.customPrompt = s->value("customPrompt", "").toString();
    s->endGroup();

    s->beginGroup("Workspace");
    workspace.topSplitSizes.clear();
    for (const QVariant &v : s->value("topSplitSizes").toList())
        workspace.topSplitSizes << v.toInt();
    s->endGroup();

    s->beginGroup("Preview");
    preview.lowResPreview  = s->value("lowResPreview", true).toBool();
    preview.previewWidth   = s->value("previewWidth", 640).toInt();
    preview.previewFpsCap  = s->value("previewFpsCap", 24).toInt();
    if (s->contains("hardwarePreview")) {
        preview.hardwarePreview = s->value("hardwarePreview").toBool();
    } else {
        // 首次啟動：若快速檢測到 GPU (NVIDIA 或 Vulkan 運行庫) 則自動開啟硬解
        bool hasGpu = HardwareProbe::hasNvidiaGpuQuick() || HardwareProbe::vulkanRuntimeQuick();
        preview.hardwarePreview = hasGpu;
        qDebug("[AppConfig] first run hardwarePreview auto=%d hasNvidia=%d vulkan=%d",
               hasGpu, HardwareProbe::hasNvidiaGpuQuick(), HardwareProbe::vulkanRuntimeQuick());
        s->setValue("hardwarePreview", preview.hardwarePreview);
        s->sync();
    }
    // clamp to sane ranges
    preview.previewWidth = qBound(360, preview.previewWidth, 1920);
    preview.previewFpsCap = qBound(10, preview.previewFpsCap, 60);
    preview.volume = qBound(0.0f, s->value("volume", 1.0f).toFloat(), 1.0f);
    preview.muted  = s->value("muted", false).toBool();
    s->endGroup();

    s->beginGroup("Translation");
    translation.backend     = static_cast<TranslationBackend>(s->value("backend", 0).toInt());
    const QString storedApiKey = s->value("apiKey", "").toString();
    translation.apiKey      = unprotectSecret(storedApiKey);
    translation.targetLang  = s->value("targetLang",  "zh-TW").toString();
    translation.ollamaUrl   = s->value("ollamaUrl",   "http://localhost:11434").toString();
    translation.ollamaModel = s->value("ollamaModel", "llama3").toString();
    translation.geminiModel = s->value("geminiModel", "gemini-2.0-flash").toString();
    s->endGroup();

    s->beginGroup("Correction");
    correction.backend     = static_cast<CorrectionBackend>(s->value("backend", static_cast<int>(CorrectionBackend::OpenAI)).toInt());
    correction.apiKey      = unprotectSecret(s->value("apiKey", "").toString());
    correction.ollamaUrl   = s->value("ollamaUrl",   "http://localhost:11434").toString();
    correction.ollamaModel = s->value("ollamaModel", "llama3").toString();
    correction.geminiModel = s->value("geminiModel", "gemini-2.0-flash").toString();
    correction.batchSize   = s->value("batchSize", 20).toInt();
    correction.customInstruction = s->value("customInstruction", "").toString();
    s->endGroup();

    s->beginGroup("Update");
    update.enabled        = s->value("enabled", true).toBool();
    update.repoOwner      = s->value("repoOwner", "Tim0phy").toString();
    update.repoName       = s->value("repoName",  "YumuStudio").toString();
    update.skippedVersion = s->value("skippedVersion", "").toString();
    s->endGroup();

#ifdef Q_OS_WIN
    if (!storedApiKey.isEmpty() && !storedApiKey.startsWith(QStringLiteral("dpapi:"))) {
        const QString encrypted = protectSecret(translation.apiKey);
        if (!encrypted.isEmpty()) {
            s->beginGroup("Translation");
            s->setValue("apiKey", encrypted);
            s->endGroup();
            s->sync();
        }
    }
#endif
    applyNetworkSettings();
}

void AppConfig::save() {
    auto *s = settings();

    s->beginGroup("System");
    s->setValue("language", system.language);
    s->setValue("theme", system.theme);
    s->setValue("checkUpdates", system.checkUpdates);
    s->setValue("preventSleep", system.preventSleep);
    s->setValue("useCustomTemp", system.useCustomTemp);
    s->setValue("tempPath", system.tempPath);
    s->setValue("proxyMode", system.proxyMode);
    s->setValue("proxyHost", system.proxyHost);
    s->setValue("proxyPort", system.proxyPort);
    s->setValue("onboardingDone", system.onboardingDone);
    s->setValue("learnMistakes",  system.learnMistakes);
    s->setValue("mistakeThreshold", system.mistakeThreshold);
    s->endGroup();

    paths.modelPath      = whisper.modelPath;
    paths.whisperCliPath = whisper.cliPath;
    paths.runnerPath     = whisper.runnerPath;

    s->beginGroup("Paths");
    s->setValue("ffmpeg",     paths.ffmpegPath);
    s->setValue("whisperCli", paths.whisperCliPath);
    s->setValue("model",      paths.modelPath);
    s->setValue("modelRoot",  paths.modelRoot);
    s->setValue("runner",    paths.runnerPath);
    s->setValue("engineRoot", paths.engineRoot);
    s->endGroup();

    s->beginGroup("Whisper");
    s->setValue("language",  whisper.language);
    s->setValue("engine",    whisper.engine);
    s->setValue("modelId",   whisper.modelId);
    s->setValue("computeDevice", whisper.computeDevice);
    s->setValue("precision", whisper.precision);
    s->setValue("threads",   whisper.threads);
    s->setValue("translate", whisper.translate);
    s->setValue("useGlossary", whisper.useGlossary);
    s->setValue("customPrompt", whisper.customPrompt);
    s->endGroup();

    s->beginGroup("Workspace");
    QVariantList sz;
    for (int v : workspace.topSplitSizes) sz << v;
    s->setValue("topSplitSizes", sz);
    s->endGroup();

    s->beginGroup("Preview");
    s->setValue("lowResPreview", preview.lowResPreview);
    s->setValue("previewWidth", preview.previewWidth);
    s->setValue("previewFpsCap", preview.previewFpsCap);
    s->setValue("hardwarePreview", preview.hardwarePreview);
    s->setValue("volume", preview.volume);
    s->setValue("muted", preview.muted);
    s->endGroup();

    s->beginGroup("Translation");
    s->setValue("backend",     static_cast<int>(translation.backend));
    {
        const QString encrypted = protectSecret(translation.apiKey);
        if (encrypted.isEmpty() && !translation.apiKey.isEmpty()) {
            qWarning("DPAPI encryption failed; apiKey not persisted to settings.");
        } else {
            s->setValue("apiKey", encrypted);
        }
    }
    s->setValue("targetLang",  translation.targetLang);
    s->setValue("ollamaUrl",   translation.ollamaUrl);
    s->setValue("ollamaModel", translation.ollamaModel);
    s->setValue("geminiModel", translation.geminiModel);
    s->endGroup();

    s->beginGroup("Correction");
    s->setValue("backend", static_cast<int>(correction.backend));
    {
        const QString encrypted = protectSecret(correction.apiKey);
        if (encrypted.isEmpty() && !correction.apiKey.isEmpty()) {
            qWarning("DPAPI encryption failed; correction apiKey not persisted to settings.");
        } else {
            s->setValue("apiKey", encrypted);
        }
    }
    s->setValue("ollamaUrl",        correction.ollamaUrl);
    s->setValue("ollamaModel",      correction.ollamaModel);
    s->setValue("geminiModel",      correction.geminiModel);
    s->setValue("batchSize",        correction.batchSize);
    s->setValue("customInstruction", correction.customInstruction);
    s->endGroup();

    s->beginGroup("Update");
    s->setValue("enabled",        update.enabled);
    s->setValue("repoOwner",      update.repoOwner);
    s->setValue("repoName",       update.repoName);
    s->setValue("skippedVersion", update.skippedVersion);
    s->endGroup();

    s->sync();
    applyNetworkSettings();
}

void AppConfig::applyNetworkSettings() const {
    if (system.proxyMode == 0) {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return;
    }
    if (system.proxyMode == 2 && !system.proxyHost.trimmed().isEmpty() && system.proxyPort > 0) {
        QNetworkProxy proxy(QNetworkProxy::HttpProxy, system.proxyHost.trimmed(),
                            static_cast<quint16>(qBound(1, system.proxyPort, 65535)));
        QNetworkProxy::setApplicationProxy(proxy);
        return;
    }
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
}
