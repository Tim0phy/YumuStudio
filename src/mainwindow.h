#pragma once
#include <QMainWindow>
#include <QProgressBar>
#include <QLabel>
#include "subtitlemodel.h"
#include "whisperengine.h"
#include "translationengine.h"
#include "ffmpegexporter.h"
#include "translationpreviewdialog.h"

class VideoPreview;
class WaveformWidget;
class SubtitleEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // v8.7: drag-and-drop video file support
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e)           override;

private slots:
    void openVideo();
    void importSRT();
    void exportSRT();
    void startTranscription();
    void startTranslation();
    void showTranslationPreview();
    void startExport();
    void openSettings();
    void onSegmentReady(SubtitleEntry entry);
    void onTranscriptionFinished(bool ok, const QString &err);
    void onTranslationBatch(int startIdx, QStringList translations);
    void onExportFinished(bool ok, const QString &err);

private:
    void buildUI();
    void buildMenuBar();
    void buildToolBar();
    void connectSignals();
    void updateStatusBar(const QString &msg);
    void setProgress(int pct, const QString &msg);
    void loadVideoFile(const QString &path);   // v8.7 shared helper

    SubtitleModel     *m_model         = nullptr;
    WhisperEngine     *m_whisper       = nullptr;
    TranslationEngine *m_translator    = nullptr;
    FFmpegExporter    *m_exporter      = nullptr;
    QString            m_currentVideoPath;

    VideoPreview   *m_videoPreview  = nullptr;
    WaveformWidget *m_waveform      = nullptr;
    SubtitleEditor *m_editor        = nullptr;
    QProgressBar   *m_progressBar   = nullptr;
    QLabel         *m_statusLabel   = nullptr;
};
