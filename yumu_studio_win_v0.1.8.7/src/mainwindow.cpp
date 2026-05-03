#pragma warning(disable: 4996)
#include "mainwindow.h"
#include "videopreview.h"
#include "waveformwidget.h"
#include "subtitleeditor.h"
#include "exportdialog.h"
#include "settingsdialog.h"
#include "appconfig.h"
#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QLabel>
#include <QAction>
#include <QThread>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Yumu Studio"));
    setMinimumSize(1100, 700);
    resize(1280, 800);
    setAcceptDrops(true);   // v8.7: enable drag-and-drop

    AppConfig::instance().load();

    m_model      = new SubtitleModel(this);
    m_whisper    = new WhisperEngine(this);
    m_translator = new TranslationEngine(this);
    m_exporter   = new FFmpegExporter(this);

    buildUI();
    buildMenuBar();
    buildToolBar();
    connectSignals();
}

MainWindow::~MainWindow() {}

void MainWindow::buildUI() {
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(200);
    m_progressBar->hide();
    m_statusLabel = new QLabel(tr("Ready — Yumu Studio v8.7"));
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_progressBar);

    auto *central    = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(4,4,4,4);
    rootLayout->setSpacing(4);
    setCentralWidget(central);

    auto *topSplit  = new QSplitter(Qt::Horizontal);
    m_videoPreview  = new VideoPreview(this);
    m_videoPreview->setMinimumWidth(320);
    m_editor        = new SubtitleEditor(this);
    m_editor->setModel(m_model);
    topSplit->addWidget(m_videoPreview);
    topSplit->addWidget(m_editor);
    topSplit->setStretchFactor(0, 2);
    topSplit->setStretchFactor(1, 3);

    m_waveform = new WaveformWidget(this);
    m_waveform->setModel(m_model);
    m_waveform->setFixedHeight(150);

    rootLayout->addWidget(topSplit, 1);
    rootLayout->addWidget(m_waveform);
}

void MainWindow::buildMenuBar() {
    auto *fileMenu = menuBar()->addMenu(tr("File(&F)"));
    fileMenu->addAction(tr("Open Video..."),  this, &MainWindow::openVideo,  QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Import SRT..."),  this, &MainWindow::importSRT);
    fileMenu->addAction(tr("Export SRT..."),  this, &MainWindow::exportSRT,  QKeySequence("Ctrl+S"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), qApp, &QApplication::quit, QKeySequence::Quit);

    auto *toolMenu = menuBar()->addMenu(tr("Tools(&T)"));
    toolMenu->addAction(tr("Generate Subtitles (AI)"), this, &MainWindow::startTranscription, QKeySequence("Ctrl+G"));
    toolMenu->addAction(tr("Translate Subtitles..."),  this, &MainWindow::startTranslation,   QKeySequence("Ctrl+T"));
    toolMenu->addSeparator();
    toolMenu->addAction(tr("Export Burned Video..."),  this, &MainWindow::startExport,        QKeySequence("Ctrl+E"));
    toolMenu->addSeparator();
    toolMenu->addAction(tr("Settings..."), this, &MainWindow::openSettings);

    auto *helpMenu = menuBar()->addMenu(tr("Help(&H)"));
    helpMenu->addAction(tr("About"), this, [this]{
        QMessageBox::about(this, tr("About Yumu Studio"),
            tr("<b>Yumu Studio for Windows v0.1.8.7</b><br><br>"
               "<b>What's New in v0.1.8.7:</b><br>"
               "• Google Gemini translation backend<br>"
               "• Ctrl+Wheel waveform zoom (1×–32×)<br>"
               "• Shift-all timecodes (⏱ button)<br>"
               "• Live subtitle style preview in Export dialog<br>"
               "• Ctrl+F search / Ctrl+H replace<br>"
               "• Drag & drop video files onto window<br>"
               "• Fixed version numbers & misc bugs<br><br>"
               "Dependencies: Qt 6, whisper.cpp, FFmpeg<br>"
               "Settings: %APPDATA%\\YumuStudio\\settings.ini"));
    });
}

void MainWindow::buildToolBar() {
    auto *tb = addToolBar(tr("Main Toolbar"));
    tb->setMovable(false);
    tb->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                  tr("Open Video"), this, &MainWindow::openVideo);
    tb->addSeparator();
    tb->addAction(tr("🎙 Transcribe"), this, &MainWindow::startTranscription);
    tb->addAction(tr("🌐 Translate"),  this, &MainWindow::startTranslation);
    tb->addAction(tr("📤 Export"),     this, &MainWindow::startExport);
    tb->addSeparator();
    tb->addAction(tr("⚙ Settings"),   this, &MainWindow::openSettings);
}

void MainWindow::connectSignals() {
    connect(m_whisper, &WhisperEngine::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_whisper, &WhisperEngine::segmentReady, this, &MainWindow::onSegmentReady);
    connect(m_whisper, &WhisperEngine::finished,     this, &MainWindow::onTranscriptionFinished);

    connect(m_translator, &TranslationEngine::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_translator, &TranslationEngine::batchTranslated,
            this, &MainWindow::onTranslationBatch);
    connect(m_translator, &TranslationEngine::finished, this, [this](bool ok, const QString &err){
        m_progressBar->hide();
        if (!ok) QMessageBox::warning(this, tr("Translation failed"), err);
        else     updateStatusBar(tr("Translation complete"));
    });

    connect(m_exporter, &FFmpegExporter::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_exporter, &FFmpegExporter::finished, this, &MainWindow::onExportFinished);

    connect(m_videoPreview, &VideoPreview::positionChanged, m_waveform, &WaveformWidget::setPositionMs);
    connect(m_videoPreview, &VideoPreview::positionChanged, m_editor,   &SubtitleEditor::highlightAtMs);
    connect(m_videoPreview, &VideoPreview::durationChanged, m_waveform, &WaveformWidget::setDurationMs);
    connect(m_waveform,     &WaveformWidget::seekRequested, m_videoPreview, &VideoPreview::seekTo);
    connect(m_editor,       &SubtitleEditor::jumpRequested, m_videoPreview, &VideoPreview::seekTo);

    connect(m_editor, &SubtitleEditor::splitRequested,  m_model,
            [this](int row, qint64 ms){ m_model->splitAt(row, ms); });
    connect(m_editor, &SubtitleEditor::mergeRequested,  m_model,
            [this](int r1, int r2){ m_model->mergeRows(r1, r2); });
    connect(m_editor, &SubtitleEditor::deleteRequested, m_model,
            [this](int row){ m_model->removeEntry(row); });
    connect(m_waveform, &WaveformWidget::subtitleMoved, this,
            [this]{ updateStatusBar(tr("Subtitle timing updated")); });
    connect(m_model, &SubtitleModel::entriesChanged, m_waveform,
            QOverload<>::of(&QWidget::update));
}

// ── v8.7: Drag-and-drop ───────────────────────────────────────────────────────
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) {
        for (const auto &url : e->mimeData()->urls()) {
            QString ext = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (QStringList{"mp4","mov","mkv","avi","m4v","webm"}.contains(ext)) {
                e->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *e) {
    for (const auto &url : e->mimeData()->urls()) {
        QString path = url.toLocalFile();
        QString ext  = QFileInfo(path).suffix().toLower();
        if (QStringList{"mp4","mov","mkv","avi","m4v","webm"}.contains(ext)) {
            loadVideoFile(path);
            e->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::loadVideoFile(const QString &path) {
    m_currentVideoPath = path;
    m_videoPreview->loadMedia(path);
    setWindowTitle(tr("Yumu Studio — ") + QFileInfo(path).fileName());
    updateStatusBar(tr("Loaded: ") + QFileInfo(path).fileName());
}

// ── File actions ──────────────────────────────────────────────────────────────
void MainWindow::openVideo() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open Video"), {},
        tr("Video Files (*.mp4 *.mov *.mkv *.avi *.m4v *.webm);;All Files (*.*)"));
    if (!path.isEmpty()) loadVideoFile(path);
}

void MainWindow::importSRT() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Import SRT"), {}, tr("SRT Subtitles (*.srt);;All Files (*.*)"));
    if (path.isEmpty()) return;
    if (!m_model->importSRT(path))
        QMessageBox::warning(this, tr("Error"), tr("Failed to parse SRT file"));
    else
        updateStatusBar(tr("Imported %1 subtitles").arg(m_model->rowCount()));
}

void MainWindow::exportSRT() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export SRT"), {}, tr("SRT Subtitles (*.srt)"));
    if (path.isEmpty()) return;
    bool ok = m_model->style().bilingualEnabled
            ? m_model->exportBilingualSRT(path)
            : m_model->exportSRT(path);
    if (!ok) QMessageBox::warning(this, tr("Error"), tr("Export failed"));
    else     updateStatusBar(tr("SRT exported to ") + path);
}

// ── AI Transcription ──────────────────────────────────────────────────────────
void MainWindow::startTranscription() {
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("Please open a video file first"));
        return;
    }
    if (m_whisper->isBusy()) return;

    const auto &cfg = AppConfig::instance();
    if (cfg.paths.modelPath.isEmpty() && cfg.paths.whisperCliPath.isEmpty()) {
        auto r = QMessageBox::question(this, tr("Settings required"),
            tr("Whisper model path is not set.\nOpen Settings now?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::Yes) openSettings();
        return;
    }

    if (m_model->rowCount() > 0) {
        auto r = QMessageBox::question(this, tr("Confirm"),
            tr("Existing subtitles will be cleared. Continue?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }
    m_model->clear();
    m_whisper->transcribe(m_currentVideoPath, cfg.whisper);
    setProgress(0, tr("Preparing transcription..."));
    m_progressBar->show();
}

void MainWindow::onSegmentReady(SubtitleEntry entry) {
    m_model->appendEntry(entry);
}

void MainWindow::onTranscriptionFinished(bool ok, const QString &err) {
    m_progressBar->hide();
    m_model->reIndex();
    if (!ok) QMessageBox::warning(this, tr("Transcription failed"), err);
    else     updateStatusBar(tr("Done: %1 subtitles generated").arg(m_model->rowCount()));
}

// ── Translation ───────────────────────────────────────────────────────────────
void MainWindow::startTranslation() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to translate"));
        return;
    }
    if (m_translator->isBusy()) return;
    const auto &cfg = AppConfig::instance();
    m_translator->translate(m_model->entries(), cfg.translation);
    m_model->style().bilingualEnabled = true;
    m_progressBar->show();
}

void MainWindow::onTranslationBatch(int startIdx, QStringList translations) {
    for (int i = 0; i < translations.size(); ++i) {
        int row = startIdx + i;
        if (row < m_model->rowCount())
            m_model->entryAt(row).translation = translations[i];
    }
    emit m_model->entriesChanged();
    m_waveform->update();
}

// ── Export ────────────────────────────────────────────────────────────────────
void MainWindow::startExport() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to export"));
        return;
    }
    ExportDialog dlg(m_model->style(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto res = dlg.result();
    QString filter = res.srtOnly ? tr("SRT (*.srt)") : tr("MP4 Video (*.mp4)");
    QString outPath = QFileDialog::getSaveFileName(this, tr("Save output"), {}, filter);
    if (outPath.isEmpty()) return;

    ExportParams ep;
    ep.inputVideo    = m_currentVideoPath;
    ep.outputPath    = outPath;
    ep.burnSubtitles = res.burnSubtitles;
    ep.bilingualBurn = res.bilingualBurn;
    ep.srtOnly       = res.srtOnly;
    ep.videoCodec    = res.videoCodec;
    ep.crf           = res.crf;
    ep.preset        = res.preset;
    ep.audioBitrate  = res.audioBitrate;
    ep.style         = res.style;
    ep.ffmpegPath    = AppConfig::instance().paths.ffmpegPath;
    m_exporter->startExport(ep, m_model->entries());
    m_progressBar->show();
}

void MainWindow::onExportFinished(bool ok, const QString &err) {
    m_progressBar->hide();
    if (!ok) QMessageBox::critical(this, tr("Export failed"), err);
    else {
        QMessageBox::information(this, tr("Done"), tr("Export successful!"));
        updateStatusBar(tr("Export complete"));
    }
}

// ── Settings ──────────────────────────────────────────────────────────────────
void MainWindow::openSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::updateStatusBar(const QString &msg) { m_statusLabel->setText(msg); }

void MainWindow::setProgress(int pct, const QString &msg) {
    m_progressBar->setValue(pct);
    m_progressBar->show();
    m_statusLabel->setText(msg);
}
