#pragma once
#include <QDialog>
#include <QHash>
#include <QVector>
#include "appconfig.h"
#include "backendcatalog.h"

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class AppUpdateChecker;
class LocalModelManager;
class BackendInstaller;
struct LocalModelSpec;
struct LocalEngineSpec;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // Sidebar section order inside the unified Settings Center.
    enum PageIndex {
        GeneralPageIndex = 0,
        EnginePageIndex,
        TranscriptionPageIndex,
        TranslationPageIndex,
        StorageNetworkPageIndex,
        AboutPageIndex
    };

    explicit SettingsDialog(QWidget *parent = nullptr);

    AppPaths paths() const;
    WhisperParams whisperParams() const;
    TranslationParams translationParams() const;
    CorrectionParams correctionParams() const;

    void showPage(int index);
    void syncPreviewControls(); // 主界面與設定中心低清預覽分辨率雙向同步

signals:
    void languageChanged();
    void appearanceChanged();
    void previewConfigChanged();

private:
    void buildUI();
    QWidget *buildGeneralPage();
    QWidget *buildEnginePage();
    QWidget *buildTranscriptionPage();
    QWidget *buildTranslationPage();
    QWidget *buildStorageNetworkPage();
    QWidget *buildAboutPage();
    QWidget *makePathRow(QLineEdit *edit, const QString &title, bool isExe);
    QWidget *makeFolderRow(QLineEdit *edit, const QString &title);
    QFrame *makeCard(const QString &title, const QString &description, QVBoxLayout **body);
    void loadFromConfig();
    void saveAndAccept();
    void refreshModelChoices();
    void refreshLanguageChoices();
    void refreshEngineDetails();
    void refreshBackendChoices();
    void installSelectedBackend();
    void rebuildModelList();
    void checkEngineUpdates();
    void importModelFolder(const LocalModelSpec &spec);
    void downloadSelectedModel();
    void updateHardwareStatusText(bool hwOn);

    QStackedWidget *m_pages = nullptr;
    QVector<QPushButton *> m_navButtons;

    // Shared paths and local engine controls.
    QLineEdit *m_ffmpegPath = nullptr;
    QLineEdit *m_modelPath = nullptr;
    QLineEdit *m_cliPath = nullptr;
    QLineEdit *m_modelRoot = nullptr;
    QLineEdit *m_storageRoot = nullptr;
    QLineEdit *m_runnerPath = nullptr;
    QListWidget *m_engineList = nullptr;
    QString m_selectedEngine;
    QLabel *m_engineTitle = nullptr;
    QLabel *m_engineDescription = nullptr;
    QLabel *m_engineStatus = nullptr;
    QLabel *m_engineVersion = nullptr;
    QLabel *m_engineUpdateStatus = nullptr;
    QPushButton *m_checkUpdatesButton = nullptr;
    QPushButton *m_installBackend = nullptr;
    QPlainTextEdit *m_backendLog = nullptr;
    BackendInstaller *m_backendInstaller = nullptr;
    // Per-variant 卡片（無下拉、無綠色進度條、各變體獨立命令行）
    QVBoxLayout *m_backendVariantsLayout = nullptr;
    QHash<QString, QLabel*> m_backendVariantStatus;
    QHash<QString, QPushButton*> m_backendVariantButtons;
    QHash<QString, QFrame*> m_backendVariantRows;
    // 方案 A：按引擎隔離的命令行日誌（切換引擎時不串台）
    QHash<QString, QString> m_backendLogBuffers;
    QPushButton *m_upgradeButton = nullptr;
    QComboBox *m_computeDevice = nullptr;
    QComboBox *m_precision = nullptr;
    QLineEdit *m_modelSearch = nullptr;
    QCheckBox *m_onlyInstalled = nullptr;
    QVBoxLayout *m_modelListLayout = nullptr;
    QTimer *m_searchDebounce = nullptr;
    QComboBox *m_engine = nullptr;
    QComboBox *m_model = nullptr;
    QLabel *m_modelStatus = nullptr;
    QPushButton *m_downloadModel = nullptr;
    QPushButton *m_cancelDownload = nullptr;
    QString m_downloadingId;
    QString m_activeEngine;
    QString m_activeModelId;
    LocalModelManager *m_models = nullptr;
    QHash<QString, LocalEngineSpec> m_engineInfo;
    QHash<QString, QString> m_latestEngineVersions;
    QHash<QString, bool> m_engineUpdates;
    int m_pendingEngineChecks = 0;

    // Recognition settings.
    QSpinBox *m_threads = nullptr;
    QComboBox *m_language = nullptr;
    QCheckBox *m_translate = nullptr;
    QCheckBox *m_useGlossary = nullptr;

    // System settings.
    QComboBox *m_uiLanguage = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QCheckBox *m_checkUpdates = nullptr;
    QCheckBox *m_preventSleep = nullptr;
    QCheckBox *m_customTemp = nullptr;
    QLineEdit *m_tempPath = nullptr;
    QCheckBox *m_learnMistakes = nullptr;
    QSpinBox  *m_mistakeThreshold = nullptr;
    QCheckBox *m_checkAppUpdates = nullptr;
    QLabel    *m_skippedVersionLabel = nullptr;
    QPushButton *m_clearSkippedBtn = nullptr;
    AppUpdateChecker *m_appUpdate = nullptr;
    QComboBox *m_proxyMode = nullptr;
    QLineEdit *m_proxyHost = nullptr;
    QSpinBox *m_proxyPort = nullptr;

    // Preview settings (A+B方案).
    QCheckBox *m_previewLowRes = nullptr;
    QComboBox *m_previewWidthCombo = nullptr;
    QComboBox *m_previewFpsCombo = nullptr;
    QCheckBox *m_previewHardware = nullptr;
    QLabel    *m_previewHardwareStatus = nullptr;

    // Translation settings.
    QComboBox *m_backend = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_targetLang = nullptr;
    QLineEdit *m_ollamaUrl = nullptr;
    QComboBox *m_ollamaModel = nullptr;
    QComboBox *m_geminiModel = nullptr;

    // AI subtitle correction settings.
    QComboBox *m_correctionBackend = nullptr;
    QLineEdit *m_correctionApiKey = nullptr;
    QLineEdit *m_correctionOllamaUrl = nullptr;
    QComboBox *m_correctionOllamaModel = nullptr;
    QComboBox *m_correctionGeminiModel = nullptr;
    QSpinBox  *m_correctionBatchSize = nullptr;
    QLineEdit *m_correctionInstruction = nullptr;
};
