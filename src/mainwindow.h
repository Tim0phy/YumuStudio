#pragma once
#include <QMainWindow>
#include <QProgressBar>
#include <QLabel>
#include <QStackedWidget>
#include "subtitlemodel.h"
#include "whisperengine.h"
#include "translationengine.h"
#include "subtitlecorrectionengine.h"
#include "appupdatechecker.h"
#include "ffmpegexporter.h"
#include "translationpreviewdialog.h"

class VideoPreview;
class WaveformWidget;
class SubtitleEditor;
class SubtitleStyleCard;
class GlossaryCard;
class PromptCard;
class SettingsDialog;
class QAction;
class QMenu;
class QToolButton;
class QPushButton;
class QComboBox;
class QCheckBox;
class QFrame;
class QSplitter;
class LocalModelManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // v2.0: runtime backend provisioning
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e)           override;

private slots:
    void openVideo();
    void importSRT();
    void exportSRT();
    void startTranscription();
    void stopTranscription();
    void startTranslation();
    void showTranslationPreview();
    void startSubtitleCorrection();
    void onSubtitleCorrected(int index, QString text);
    void onCorrectionFinished(bool ok, const QString &err);
    void onAppUpdateCheckFinished(bool ok, bool updateAvailable,
                                  const QString &latestVersion,
                                  const QString &releaseUrl,
                                  const QString &error);
    void startExport();
    void startAdvancedExport();
    void openSettings();
    void openSmartSplit();
    void openCutAlign();
    void ensureRuntimeBackend();
    void onSegmentReady(SubtitleEntry entry);
    void onTranscriptionFinished(bool ok, const QString &err);
    void onTranslationBatch(int startIdx, QStringList translations);
    void onExportFinished(bool ok, const QString &err);

private:
    void buildUI();
    void createSettingsPage();
    QWidget *buildTopBar();
    QWidget *buildWorkspace();
    void connectSignals();
    void retranslateUi();
    void refreshLanguageChoices();
    void updateStatusBar(const QString &msg);
    void setProgress(int pct, const QString &msg);
    void updateTaskPowerRequest(bool active);
    void beginTask();
    void endTask();
    bool hasActiveTask() const;
    void loadVideoFile(const QString &path);   // v8.7 shared helper
    void showWorkspace();
    void updateNavActive();
    void onRightToolbarPage(int page);
    void onStyleCardChanged(const SubtitleStyle &s);
    void onSubtitlePositionEdited();
    void onGlossaryCardChanged();
    void onPromptCardChanged();
    void syncFileBarActive();

    SubtitleModel            *m_model         = nullptr;
    WhisperEngine            *m_whisper       = nullptr;
    TranslationEngine        *m_translator    = nullptr;
    SubtitleCorrectionEngine *m_corrector     = nullptr;
    AppUpdateChecker         *m_appUpdate     = nullptr;
    FFmpegExporter           *m_exporter      = nullptr;
    QString            m_currentVideoPath;
    qint64             m_videoDurationMs = 0;

    VideoPreview   *m_videoPreview  = nullptr;
    WaveformWidget *m_waveform      = nullptr;
    SubtitleEditor *m_editor        = nullptr;
    SubtitleStyleCard *m_styleCard  = nullptr;
    GlossaryCard   *m_glossCard    = nullptr;
    PromptCard     *m_promptCard   = nullptr;
    QStackedWidget *m_rightStack   = nullptr;
    QSplitter      *m_topSplit     = nullptr;
    QToolButton    *m_rightTranscriptBtn = nullptr;
    QToolButton    *m_rightStyleBtn = nullptr;
    QToolButton    *m_rightGlossBtn = nullptr;
    QToolButton    *m_rightPromptBtn = nullptr;
    // Pastel header (mockup): hamburger + centered title + Ready pill.
    QToolButton    *m_hamburgerBtn = nullptr;
    QMenu          *m_navMenu      = nullptr;
    QAction        *m_navWorkspaceAction = nullptr;
    QAction        *m_navSettingsAction  = nullptr;
    QLabel         *m_titleLabel   = nullptr;
    QFrame         *m_readyPill    = nullptr;
    QFrame         *m_readyDot     = nullptr;
    QLabel         *m_readyText    = nullptr;
    // Video-card file bar (mockup black strip, 7 buttons).
    QFrame         *m_fileBar      = nullptr;
    QPushButton    *m_fileOpenBtn  = nullptr;
    QPushButton    *m_fileImportBtn = nullptr;
    QPushButton    *m_fileTranscribeBtn = nullptr;
    QPushButton    *m_fileTranslateBtn = nullptr;
    QPushButton    *m_fileExportBtn = nullptr;
    QPushButton    *m_toolSplitBtn = nullptr;
    QPushButton    *m_toolCutBtn   = nullptr;
    QProgressBar   *m_progressBar   = nullptr;
    QLabel         *m_statusLabel   = nullptr;
    QLabel         *m_brandTitle    = nullptr;
    QLabel         *m_statusText    = nullptr;
    QLabel         *m_languageLabel = nullptr;
    QLabel         *m_safeFrameLabel = nullptr;
    QLabel         *m_previewLabel  = nullptr;
    QLabel         *m_previewCount  = nullptr;
    QLabel         *m_editorCount   = nullptr;
    QLabel         *m_timelineLabel = nullptr;
    QPushButton    *m_openQuickButton = nullptr;
    QPushButton    *m_importQuickButton = nullptr;
    QPushButton    *m_transcribeQuickButton = nullptr;
    QPushButton    *m_stopTranscribeButton = nullptr;
    QPushButton    *m_translateQuickButton = nullptr;
    QPushButton    *m_exportQuickButton = nullptr;
    QComboBox      *m_languageCombo = nullptr;
    QComboBox      *m_safeFrameCombo = nullptr;
    QCheckBox      *m_lowResCheck = nullptr;
    QComboBox      *m_previewQualityCombo = nullptr;
    QComboBox      *m_previewFpsCombo = nullptr;
    QCheckBox      *m_hardwareCheck = nullptr;
    QLabel         *m_fpsLabel = nullptr;
    QStackedWidget  *m_pages         = nullptr;
    SettingsDialog *m_settingsPage  = nullptr;
    LocalModelManager *m_updateManager = nullptr;
    int              m_activeTasks  = 0;
};
