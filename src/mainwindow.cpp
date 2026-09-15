#pragma warning(disable: 4996)
 #include "mainwindow.h"
 #include "videopreview.h"
 #include "waveformwidget.h"
 #include "timefmt.h"
 #include "subtitleeditor.h"
#include "subtitlestylecard.h"
#include "glossarycard.h"
#include "promptcard.h"
 #include "exportdialog.h"
 #include "simpleexportdialog.h"
 #include "settingsdialog.h"
 #include "translationpreviewdialog.h"
#include "hardwareprobe.h"
#include "backendcatalog.h"
 #include "appconfig.h"
 #include "localmodelmanager.h"
#include "glossary.h"
#include "subtitletools.h"
#include "subtitlerefiner.h"
#include "subtitlecorrectionengine.h"
#include "learnedterms.h"
#include "appupdatechecker.h"
 #include <QCheckBox>
#include <QApplication>
#include <QIcon>
#include <QStatusBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QLabel>
#include <QAction>
#include <QComboBox>
#include <QToolButton>
#include <QMenu>
#include <QThread>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QPushButton>
#include <QDesktopServices>
#include <QShortcut>
#include <QSpacerItem>
#include <QAbstractItemView>
#include <QSignalBlocker>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QFont>
#include "stackanimator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Yumu Studio"));
    setWindowIcon(QIcon(":/app.ico"));
    setMinimumSize(1200, 760);
    resize(1440, 900);
    setAcceptDrops(true);

    AppConfig::instance().load();

    m_model      = new SubtitleModel(this);
    m_whisper    = new WhisperEngine(this);
    m_translator = new TranslationEngine(this);
    m_corrector  = new SubtitleCorrectionEngine(this);
    m_appUpdate  = new AppUpdateChecker(this);
    m_exporter   = new FFmpegExporter(this);

    buildUI();
    connectSignals();
    refreshLanguageChoices();

    // Promote frequently corrected terms to the creator glossary on startup.
    {
        const auto promoted = LearnedTerms::autoPromote(AppConfig::instance().system.mistakeThreshold);
        if (!promoted.isEmpty()) {
            updateStatusBar(tr("Auto-added %1 learned term(s) to glossary").arg(promoted.size()));
        }
    }

    // Check for a newer Yumu Studio release shortly after startup.
    if (AppConfig::instance().update.enabled) {
        QTimer::singleShot(2000, this, [this] {
            if (!m_appUpdate) return;
            const auto &cfg = AppConfig::instance().update;
            m_appUpdate->check(cfg.repoOwner, cfg.repoName);
        });
    }

    if (AppConfig::instance().system.checkUpdates) {
        m_updateManager = new LocalModelManager(this);
        connect(m_updateManager, &LocalModelManager::engineUpdateFinished, this,
                [this](const QString &engine, bool ok, const QString &, const QString &, bool update, const QString &) {
            if (ok && update) updateStatusBar(tr("Update available for %1").arg(engine));
        });
        QTimer::singleShot(0, m_updateManager, &LocalModelManager::checkEngineUpdates);
    }
}

MainWindow::~MainWindow() {}

QWidget *MainWindow::buildTopBar() {
    // Pastel mockup: rounded pink bar with hamburger | centered title | Ready pill.
    auto *wrap = new QWidget(this);
    auto *wrapLay = new QVBoxLayout(wrap);
    wrapLay->setContentsMargins(12, 12, 12, 0);
    wrapLay->setSpacing(0);
    auto *topBar = new QFrame(wrap);
    topBar->setObjectName("yumuHeader");
    topBar->setFixedHeight(48);
    auto *lay = new QHBoxLayout(topBar);
    lay->setContentsMargins(12, 4, 14, 4);
    lay->setSpacing(12);

    m_hamburgerBtn = new QToolButton(topBar);
    m_hamburgerBtn->setObjectName("hamburgerBtn");
    m_hamburgerBtn->setText(QString::fromUtf8("\xe2\x98\xb0"));
    m_hamburgerBtn->setPopupMode(QToolButton::InstantPopup);
    m_hamburgerBtn->setCursor(Qt::PointingHandCursor);
    m_hamburgerBtn->setFixedSize(32, 32);
    m_navMenu = new QMenu(m_hamburgerBtn);
    m_navMenu->setObjectName("yumuMenu");
    m_navWorkspaceAction = m_navMenu->addAction(tr("工作區"), this, &MainWindow::showWorkspace);
    m_navWorkspaceAction->setCheckable(true);
    m_navSettingsAction = m_navMenu->addAction(tr("設定"), this, &MainWindow::openSettings);
    m_navSettingsAction->setCheckable(true);
    m_hamburgerBtn->setMenu(m_navMenu);
    lay->addWidget(m_hamburgerBtn, 0, Qt::AlignVCenter);

    lay->addStretch();
    m_titleLabel = new QLabel(tr("Yumu Studio"), topBar);
    m_titleLabel->setObjectName("yumuTitle");
    m_titleLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
    // Keep legacy pointers null-safe for retranslateUi remnants.
    m_brandTitle = m_titleLabel;
    m_statusText = nullptr;
    lay->addStretch();

    m_readyPill = new QFrame(topBar);
    m_readyPill->setObjectName("readyPill");
    auto *pillLay = new QHBoxLayout(m_readyPill);
    pillLay->setContentsMargins(10, 4, 12, 4);
    pillLay->setSpacing(8);
    m_readyDot = new QFrame(m_readyPill);
    m_readyDot->setObjectName("readyDot");
    m_readyDot->setFixedSize(12, 12);
    m_readyText = new QLabel(tr("Ready"), m_readyPill);
    m_readyText->setObjectName("readyText");
    m_statusText = m_readyText;
    pillLay->addWidget(m_readyDot);
    pillLay->addWidget(m_readyText);
    lay->addWidget(m_readyPill, 0, Qt::AlignVCenter);

    wrapLay->addWidget(topBar);
    return wrap;
}

QWidget *MainWindow::buildWorkspace() {
    auto *central = new QWidget;
    central->setObjectName("workspace");
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(12);

    auto glyphIcon = [](const char *utf8bytes) {
        QPixmap pm(40, 40);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f(QStringLiteral("Segoe UI Emoji"), 18);
        p.setFont(f);
        p.setPen(QColor(0x7B, 0x61, 0xB8));
        p.drawText(pm.rect(), Qt::AlignCenter, QString::fromUtf8(utf8bytes));
        p.end();
        return QIcon(pm);
    };

    // ── Top row: video card + right column in a user-resizable splitter ──
    // (3:2 default; sizes persisted to AppConfig workspace group)
    auto *topSplit = new QSplitter(Qt::Horizontal, central);
    topSplit->setObjectName("topSplit");
    topSplit->setHandleWidth(12);
    topSplit->setChildrenCollapsible(false);
    m_topSplit = topSplit;

    // Left: video card
    auto *videoCard = new QFrame(central);
    videoCard->setObjectName("yumuCard");
    auto *videoLay = new QVBoxLayout(videoCard);
    videoLay->setContentsMargins(16, 12, 16, 12);
    videoLay->setSpacing(8);

    // Black file strip: open / import / transcribe / translate / export + tools
    auto *fileRow = new QHBoxLayout;
    fileRow->setSpacing(0);
    m_fileBar = new QFrame(videoCard);
    m_fileBar->setObjectName("fileBar");
    auto *fileBarLay = new QHBoxLayout(m_fileBar);
    fileBarLay->setContentsMargins(2, 2, 2, 2);
    fileBarLay->setSpacing(2);
    auto mkFile = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, m_fileBar);
        b->setObjectName("fileDarkBtn");
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setProperty("active", false);
        fileBarLay->addWidget(b);
        return b;
    };
    m_fileOpenBtn = mkFile(tr("開啟"), tr("Open video (Ctrl+O)"));
    m_fileImportBtn = mkFile(tr("匯入SRT"), tr("Import SRT subtitles"));
    m_fileTranscribeBtn = mkFile(tr("轉寫"), tr("AI transcription (Ctrl+G)"));
    m_fileTranslateBtn = mkFile(tr("翻譯"), tr("Translate subtitles (Ctrl+T)"));
    m_fileExportBtn = mkFile(tr("匯出"), tr("Export (Ctrl+E)"));
    m_toolSplitBtn = mkFile(tr("智慧斷句"), tr("Smart segmentation"));
    m_toolCutBtn = mkFile(tr("切點對齊"), tr("Cut detection & magnetic alignment"));
    // Legacy aliases so retranslate/count logic keeps working.
    m_openQuickButton = m_fileOpenBtn;
    m_importQuickButton = m_fileImportBtn;
    m_transcribeQuickButton = m_fileTranscribeBtn;
    m_translateQuickButton = m_fileTranslateBtn;
    m_exportQuickButton = m_fileExportBtn;
    m_fileOpenBtn->setProperty("active", true);
    // Clicking a file-bar button marks it active (new pastel selected state).
    auto markFileActive = [this](QPushButton *btn) {
        for (auto *b : {m_fileOpenBtn, m_fileImportBtn, m_fileTranscribeBtn,
                        m_fileTranslateBtn, m_fileExportBtn, m_toolSplitBtn, m_toolCutBtn}) {
            if (!b) continue;
            b->setProperty("active", b == btn);
            b->style()->unpolish(b);
            b->style()->polish(b);
        }
    };
    connect(m_fileOpenBtn, &QPushButton::clicked, this, &MainWindow::openVideo);
    connect(m_fileImportBtn, &QPushButton::clicked, this, &MainWindow::importSRT);
    connect(m_fileTranscribeBtn, &QPushButton::clicked, this, &MainWindow::startTranscription);
    connect(m_fileTranslateBtn, &QPushButton::clicked, this, &MainWindow::startTranslation);
    connect(m_fileExportBtn, &QPushButton::clicked, this, &MainWindow::startExport);
    connect(m_toolSplitBtn, &QPushButton::clicked, this, &MainWindow::openSmartSplit);
    connect(m_toolCutBtn, &QPushButton::clicked, this, &MainWindow::openCutAlign);
    for (auto *b : {m_fileOpenBtn, m_fileImportBtn, m_fileTranscribeBtn,
                    m_fileTranslateBtn, m_fileExportBtn, m_toolSplitBtn, m_toolCutBtn}) {
        connect(b, &QPushButton::clicked, this, [markFileActive, b] { markFileActive(b); });
    }
    m_stopTranscribeButton = new QPushButton(tr("停止"), m_fileBar);
    m_stopTranscribeButton->setObjectName("fileDarkBtn");
    m_stopTranscribeButton->setVisible(false);
    m_stopTranscribeButton->setCursor(Qt::PointingHandCursor);
    connect(m_stopTranscribeButton, &QPushButton::clicked, this, &MainWindow::stopTranscription);
    fileBarLay->addWidget(m_stopTranscribeButton);
    fileRow->addWidget(m_fileBar);
    fileRow->addStretch();
    videoLay->addLayout(fileRow);

    // Preview perf row: low-res + mint pills + hw decode + language
    auto *perfRow = new QHBoxLayout;
    perfRow->setSpacing(6);
    m_lowResCheck = new QCheckBox(tr("低清預覽"), videoCard);
    m_lowResCheck->setToolTip(tr("只影響預覽流暢度，不影響最終匯出畫質"));
    m_lowResCheck->setChecked(AppConfig::instance().preview.lowResPreview);
    perfRow->addWidget(m_lowResCheck);
    m_previewQualityCombo = new QComboBox(videoCard);
    m_previewQualityCombo->setObjectName("mintCombo");
    m_previewQualityCombo->setToolTip(tr("預覽解析度（越低越流暢）"));
    m_previewQualityCombo->addItem("360p", 360);
    m_previewQualityCombo->addItem("480p", 480);
    m_previewQualityCombo->addItem("640p", 640);
    m_previewQualityCombo->addItem("720p", 720);
    m_previewQualityCombo->addItem("960p", 960);
    m_previewQualityCombo->addItem("1024p", 1024);
    m_previewQualityCombo->addItem(tr("原始"), 0);
    {
        int w = AppConfig::instance().preview.previewWidth;
        int idx = m_previewQualityCombo->findData(w);
        if (idx < 0 && !AppConfig::instance().preview.lowResPreview) idx = m_previewQualityCombo->findData(0);
        if (idx < 0) idx = 2; // 640
        m_previewQualityCombo->setCurrentIndex(idx);
        m_previewQualityCombo->setEnabled(AppConfig::instance().preview.lowResPreview);
    }
    perfRow->addWidget(m_previewQualityCombo);
    m_fpsLabel = new QLabel(tr("幀率"), videoCard);
    m_fpsLabel->setToolTip(tr("預覽幀率上限"));
    perfRow->addWidget(m_fpsLabel);
    m_previewFpsCombo = new QComboBox(videoCard);
    m_previewFpsCombo->setObjectName("mintCombo");
    m_previewFpsCombo->addItem("15", 15);
    m_previewFpsCombo->addItem("24", 24);
    m_previewFpsCombo->addItem("30", 30);
    {
        int fps = AppConfig::instance().preview.previewFpsCap;
        int idx = m_previewFpsCombo->findData(fps);
        if (idx < 0) idx = 1;
        m_previewFpsCombo->setCurrentIndex(idx);
    }
    perfRow->addWidget(m_previewFpsCombo);
    m_hardwareCheck = new QCheckBox(tr("硬體解碼"), videoCard);
    m_hardwareCheck->setToolTip(tr("開啟用 GPU 硬解（d3d11va/dxva2/cuda），關閉則強制 CPU 軟解，需重新載入影片"));
    m_hardwareCheck->setChecked(AppConfig::instance().preview.hardwarePreview);
    perfRow->addWidget(m_hardwareCheck);
    perfRow->addStretch();
    m_languageLabel = new QLabel(tr("語言"), videoCard);
    perfRow->addWidget(m_languageLabel);
    m_languageCombo = new QComboBox(videoCard);
    m_languageCombo->setObjectName("mintCombo");
    m_languageCombo->setFixedWidth(130);
    m_languageCombo->setMaxVisibleItems(10);
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        auto &cfg = AppConfig::instance();
        cfg.whisper.language = m_languageCombo->itemData(index).toString();
        cfg.save();
    });
    perfRow->addWidget(m_languageCombo);
    videoLay->addLayout(perfRow);

    m_videoPreview = new VideoPreview(videoCard);
    m_videoPreview->setMinimumWidth(360);
    m_videoPreview->setMinimumHeight(240);
    m_videoPreview->setModel(m_model);
    m_videoPreview->setSubtitleStyle(m_model->style());
    connect(m_lowResCheck, &QCheckBox::toggled, this, [this](bool on){
        if (!m_videoPreview) return;
        m_videoPreview->setLowResPreview(on);
        if (m_previewQualityCombo) m_previewQualityCombo->setEnabled(on);
        if (m_settingsPage) m_settingsPage->syncPreviewControls();
        updateStatusBar(on ? tr("已開啟低清預覽（更流暢）") : tr("已關閉低清預覽"));
    });
    connect(m_previewQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (!m_videoPreview || idx < 0) return;
        int w = m_previewQualityCombo->itemData(idx).toInt();
        if (w == 0) {
            m_videoPreview->setLowResPreview(false);
            if (m_lowResCheck) m_lowResCheck->setChecked(false);
        } else {
            if (m_lowResCheck && !m_lowResCheck->isChecked()) m_lowResCheck->setChecked(true);
            m_videoPreview->setLowResPreview(true);
            m_videoPreview->setPreviewWidth(w);
        }
        if (m_settingsPage) m_settingsPage->syncPreviewControls();
        updateStatusBar(tr("預覽解析度：%1").arg(w == 0 ? tr("原始") : QString::number(w) + QStringLiteral("p")));
    });
    connect(m_previewFpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (!m_videoPreview || idx < 0) return;
        int fps = m_previewFpsCombo->itemData(idx).toInt();
        m_videoPreview->setPreviewFpsCap(fps);
        if (m_settingsPage) m_settingsPage->syncPreviewControls();
        updateStatusBar(tr("預覽幀率上限：%1 fps").arg(fps));
    });
    connect(m_hardwareCheck, &QCheckBox::toggled, this, [this](bool on){
        if (!m_videoPreview) return;
        m_videoPreview->setHardwarePreview(on);
        if (m_settingsPage) m_settingsPage->syncPreviewControls();
        updateStatusBar(on ? tr("已開啟 GPU 硬體解碼") : tr("已切換為 CPU 軟解"));
    });
    connect(m_videoPreview, &VideoPreview::subtitlePositionEdited, this,
            &MainWindow::onSubtitlePositionEdited);
    connect(m_videoPreview, &VideoPreview::hardwareDecodeFailed, this,
            [this](const QString &reason){
                updateStatusBar(tr("硬解失敗，已切回 CPU 軟解：") + reason.left(80));
                if (m_hardwareCheck) {
                    const QSignalBlocker b(m_hardwareCheck);
                    m_hardwareCheck->setChecked(false);
                }
                if (m_settingsPage) m_settingsPage->syncPreviewControls();
            });
    // Preview toolbar directly above the picture: title + safe-frame picker
    // so creators can check platform overlays without scrolling down.
    auto *previewBar = new QHBoxLayout;
    previewBar->setSpacing(8);
    m_previewLabel = new QLabel(tr("預覽"), videoCard);
    m_previewLabel->setObjectName("yumuCardTitle");
    previewBar->addWidget(m_previewLabel);
    previewBar->addStretch();
    m_safeFrameLabel = new QLabel(tr("安全框"), videoCard);
    previewBar->addWidget(m_safeFrameLabel);
    m_safeFrameCombo = new QComboBox(videoCard);
    m_safeFrameCombo->setObjectName("mintCombo");
    m_safeFrameCombo->setFixedHeight(26);
    m_safeFrameCombo->setMinimumWidth(130);
    m_safeFrameCombo->addItem(tr("關閉"), int(VideoPreview::SafeFrameNone));
    m_safeFrameCombo->addItem(tr("通用"), int(VideoPreview::SafeFrameGeneral));
    m_safeFrameCombo->addItem(tr("YouTube"), int(VideoPreview::SafeFrameYouTube));
    m_safeFrameCombo->addItem(tr("YT Shorts"), int(VideoPreview::SafeFrameYouTubeShorts));
    m_safeFrameCombo->addItem(tr("Facebook"), int(VideoPreview::SafeFrameFacebook));
    m_safeFrameCombo->addItem(tr("Reels"), int(VideoPreview::SafeFrameReels));
    m_safeFrameCombo->addItem(tr("TikTok"), int(VideoPreview::SafeFrameTikTok));
    m_safeFrameCombo->addItem(tr("IG Story"), int(VideoPreview::SafeFrameIGStory));
    connect(m_safeFrameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0 && m_videoPreview) {
            int preset = m_safeFrameCombo->itemData(index).toInt();
            m_videoPreview->setSafeFrame(preset);
        }
    });
    previewBar->addWidget(m_safeFrameCombo);
    videoLay->addLayout(previewBar);
    videoLay->addWidget(m_videoPreview, 1);
    topSplit->addWidget(videoCard);

    // Right column: 3-icon toolbar + 3-page card
    auto *rightCol = new QWidget(central);
    auto *rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(12);
    auto *toolbar = new QFrame(rightCol);
    toolbar->setObjectName("yumuToolbar");
    auto *toolLay = new QHBoxLayout(toolbar);
    toolLay->setContentsMargins(12, 8, 12, 8);
    toolLay->setSpacing(8);
    toolLay->addStretch();
    auto mkTool = [&](const char *emoji, const QString &text, const QString &tip) {
        auto *b = new QToolButton(toolbar);
        b->setObjectName("iconBtn");
        b->setIcon(glyphIcon(emoji));
        b->setIconSize(QSize(26, 26));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        toolLay->addWidget(b);
        return b;
    };
    m_rightTranscriptBtn = mkTool("\xf0\x9f\x93\x9d", tr("轉寫文字"), tr("轉寫文字預覽"));
    m_rightStyleBtn = mkTool("\xf0\x9f\x8e\xa8", tr("字幕樣式"), tr("字幕樣式"));
    m_rightGlossBtn = mkTool("\xf0\x9f\x93\x96", tr("專屬詞庫"), tr("專屬詞庫"));
    m_rightPromptBtn = mkTool("\xf0\x9f\x92\xac", tr("提示詞"), tr("轉寫提示詞（whisper.cpp / faster-whisper）"));
    m_rightTranscriptBtn->setChecked(true);
    connect(m_rightTranscriptBtn, &QToolButton::clicked, this, [this] { onRightToolbarPage(0); });
    connect(m_rightStyleBtn, &QToolButton::clicked, this, [this] { onRightToolbarPage(1); });
    connect(m_rightGlossBtn, &QToolButton::clicked, this, [this] { onRightToolbarPage(2); });
    connect(m_rightPromptBtn, &QToolButton::clicked, this, [this] { onRightToolbarPage(3); });
    toolLay->addStretch();
    rightLay->addWidget(toolbar);

    auto *rightCard = new QFrame(rightCol);
    rightCard->setObjectName("yumuCard");
    auto *rightCardLay = new QVBoxLayout(rightCard);
    // Pads 12H/8V exactly like the toolbar above; the stack pages keep
    // zero own margins so all content left edges align on one line.
    rightCardLay->setContentsMargins(12, 8, 12, 8);
    rightCardLay->setSpacing(0);
    m_rightStack = new QStackedWidget(rightCard);
    m_editor = new SubtitleEditor(m_rightStack);
    m_editor->setModel(m_model);
    m_styleCard = new SubtitleStyleCard(m_rightStack);
    m_styleCard->setStyle(m_model->style());
    connect(m_styleCard, &SubtitleStyleCard::styleChanged, this, &MainWindow::onStyleCardChanged);
    m_glossCard = new GlossaryCard(m_rightStack);
    connect(m_glossCard, &GlossaryCard::glossaryChanged, this, &MainWindow::onGlossaryCardChanged);
    m_promptCard = new PromptCard(m_rightStack);
    connect(m_promptCard, &PromptCard::promptChanged, this, &MainWindow::onPromptCardChanged);
    m_rightStack->addWidget(m_editor);
    m_rightStack->addWidget(m_styleCard);
    m_rightStack->addWidget(m_glossCard);
    m_rightStack->addWidget(m_promptCard);
    m_rightStack->setCurrentIndex(0);
    rightCardLay->addWidget(m_rightStack, 1);
    rightLay->addWidget(rightCard, 1);
    videoCard->setMinimumWidth(380);
    rightCol->setMinimumWidth(300);
    topSplit->addWidget(rightCol);
    topSplit->setStretchFactor(0, 3);
    topSplit->setStretchFactor(1, 2);
    {
        // Restore the user's last video/right split, else a 3:2 default.
        const QList<int> saved = AppConfig::instance().workspace.topSplitSizes;
        if (saved.size() == 2 && saved.at(0) > 100 && saved.at(1) > 100)
            topSplit->setSizes(saved);
        else
            topSplit->setSizes({ 900, 600 });
    }
    connect(topSplit, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!m_topSplit) return;
        AppConfig::instance().workspace.topSplitSizes = m_topSplit->sizes();
        AppConfig::instance().save();
    });
    rootLayout->addWidget(topSplit, 1);

    // Bottom: timeline card
    auto *timelinePanel = new QFrame(central);
    timelinePanel->setObjectName("yumuCard");
    auto *timelineLay = new QVBoxLayout(timelinePanel);
    timelineLay->setContentsMargins(16, 10, 16, 10);
    timelineLay->setSpacing(8);
    auto *timelineHeader = new QHBoxLayout;
    timelineHeader->setSpacing(8);
    m_timelineLabel = new QLabel(tr("時間軸"), timelinePanel);
    m_timelineLabel->setObjectName("yumuCardTitle");
    timelineHeader->addWidget(m_timelineLabel);
    timelineHeader->addStretch();
    m_previewCount = nullptr;
    m_editorCount = nullptr;
    timelineLay->addLayout(timelineHeader);

    m_waveform = new WaveformWidget(timelinePanel);
    m_waveform->setModel(m_model);
    m_waveform->setMinimumHeight(120);
    m_waveform->setMaximumHeight(180);
    timelineLay->addWidget(m_waveform);

    rootLayout->addWidget(timelinePanel);
    return central;
}

void MainWindow::buildUI() {
    // ── Status bar (built early so widgets exist) ─────────────────────────
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedWidth(180);
    m_progressBar->setFixedHeight(16);
    m_progressBar->hide();
    m_statusLabel = new QLabel(tr("Ready"));
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->setSizeGripEnabled(true);

    // ── Central stack: workspace page + settings center page ─────────────
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("pages");

    m_pages->addWidget(buildWorkspace());

    // Settings page is heavy (engine/model lists); build it lazily on first
    // open so startup and workspace switches stay instant.
    m_settingsPage = nullptr;
    m_pages->setCurrentIndex(0);

    // ── Shell layout: top bar on top, nav rail beside the stack ──────────
    auto *shell = new QWidget;
    shell->setObjectName("yumuShell");
    auto *shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    shellLayout->addWidget(buildTopBar());

    auto *bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_pages, 1);
    shellLayout->addLayout(bodyLayout, 1);

    setCentralWidget(shell);
    updateNavActive();
    QTimer::singleShot(600, this, &MainWindow::ensureRuntimeBackend);
}

void MainWindow::createSettingsPage() {
    const int idx = m_pages->indexOf(m_settingsPage);
    SettingsDialog *page = new SettingsDialog(m_pages);
    page->setWindowFlags(Qt::Widget);
    page->setObjectName("settingsPage");
    connect(page, &QDialog::accepted, this, &MainWindow::showWorkspace);
    connect(page, &QDialog::rejected, this, &MainWindow::showWorkspace);
    connect(page, &SettingsDialog::appearanceChanged, this, [this] {
        // Chrome stylesheets of custom-painted widgets are built from theme
        // tokens, so refresh them when the appearance changes.
        if (m_videoPreview) m_videoPreview->onThemeChanged();
        if (m_waveform)     m_waveform->onThemeChanged();
    });
    // The settings center holds many static texts created at construction
    // time; rebuild it entirely whenever the interface language changes so
    // no stale strings survive a live switch.
    connect(page, &SettingsDialog::languageChanged, this, [this] {
        retranslateUi();
        if (m_settingsPage) createSettingsPage();
    });
    // 預覽分辨率雙向同步：設定中心與主界面低清預覽保持一致
    connect(page, &SettingsDialog::previewConfigChanged, this, [this] {
        const auto &cfg = AppConfig::instance().preview;
        if (m_lowResCheck) {
            const QSignalBlocker b(m_lowResCheck);
            m_lowResCheck->setChecked(cfg.lowResPreview);
        }
        if (m_previewQualityCombo) {
            const QSignalBlocker b(m_previewQualityCombo);
            int idx = m_previewQualityCombo->findData(cfg.lowResPreview ? cfg.previewWidth : 0);
            if (idx < 0) idx = m_previewQualityCombo->findData(640);
            if (idx < 0) idx = m_previewQualityCombo->findData(0);
            m_previewQualityCombo->setCurrentIndex(qMax(0, idx));
            m_previewQualityCombo->setEnabled(cfg.lowResPreview);
        }
        if (m_previewFpsCombo) {
            const QSignalBlocker b(m_previewFpsCombo);
            int idx = m_previewFpsCombo->findData(cfg.previewFpsCap);
            if (idx >= 0) m_previewFpsCombo->setCurrentIndex(idx);
        }
        if (m_hardwareCheck) {
            const QSignalBlocker b(m_hardwareCheck);
            m_hardwareCheck->setChecked(cfg.hardwarePreview);
        }
        if (m_videoPreview) {
            // VideoPreview setters會寫回 AppConfig，無需重複 save
            const QSignalBlocker v1(m_videoPreview);
            m_videoPreview->setLowResPreview(cfg.lowResPreview);
            m_videoPreview->setPreviewWidth(cfg.previewWidth);
            m_videoPreview->setPreviewFpsCap(cfg.previewFpsCap);
            m_videoPreview->setHardwarePreview(cfg.hardwarePreview);
        }
    });

    SettingsDialog *old = m_settingsPage;
    m_settingsPage = page;
    if (idx >= 0) {
        m_pages->insertWidget(idx, page);
        if (old) {
            m_pages->removeWidget(old);
            old->deleteLater();
        }
    } else {
        m_pages->addWidget(page);
    }
}

void MainWindow::updateNavActive() {
    const int settingsIdx = (m_pages && m_settingsPage) ? m_pages->indexOf(m_settingsPage) : 1;
    const bool onSettings = m_pages && settingsIdx >= 0 && m_pages->currentIndex() == settingsIdx;
    if (m_navWorkspaceAction) m_navWorkspaceAction->setChecked(!onSettings);
    if (m_navSettingsAction) m_navSettingsAction->setChecked(onSettings);
}

void MainWindow::retranslateUi() {
    setWindowTitle(m_currentVideoPath.isEmpty()
                       ? tr("Yumu Studio")
                       : tr("Yumu Studio — ") + QFileInfo(m_currentVideoPath).fileName());
    if (m_titleLabel) m_titleLabel->setText(tr("Yumu Studio"));
    if (m_brandTitle && m_brandTitle != m_titleLabel) m_brandTitle->setText(tr("Yumu Studio"));
    if (m_navWorkspaceAction) m_navWorkspaceAction->setText(tr("工作區"));
    if (m_navSettingsAction) m_navSettingsAction->setText(tr("設定"));
    if (m_fileOpenBtn) { m_fileOpenBtn->setText(tr("開啟")); m_fileOpenBtn->setToolTip(tr("Open video (Ctrl+O)")); }
    if (m_fileImportBtn) { m_fileImportBtn->setText(tr("匯入SRT")); m_fileImportBtn->setToolTip(tr("Import SRT subtitles")); }
    if (m_fileTranscribeBtn) { m_fileTranscribeBtn->setText(tr("轉寫")); m_fileTranscribeBtn->setToolTip(tr("AI transcription (Ctrl+G)")); }
    if (m_fileTranslateBtn) { m_fileTranslateBtn->setText(tr("翻譯")); m_fileTranslateBtn->setToolTip(tr("Translate subtitles (Ctrl+T)")); }
    if (m_fileExportBtn) { m_fileExportBtn->setText(tr("匯出")); m_fileExportBtn->setToolTip(tr("Export (Ctrl+E)")); }
    if (m_toolSplitBtn) { m_toolSplitBtn->setText(tr("智慧斷句")); m_toolSplitBtn->setToolTip(tr("Smart segmentation")); }
    if (m_toolCutBtn) { m_toolCutBtn->setText(tr("切點對齊")); m_toolCutBtn->setToolTip(tr("Cut detection & magnetic alignment")); }
    if (m_stopTranscribeButton) m_stopTranscribeButton->setText(tr("停止"));
    if (m_openQuickButton && m_openQuickButton != m_fileOpenBtn) m_openQuickButton->setText(tr("開啟"));
    if (m_importQuickButton && m_importQuickButton != m_fileImportBtn) m_importQuickButton->setText(tr("匯入SRT"));
    if (m_transcribeQuickButton && m_transcribeQuickButton != m_fileTranscribeBtn) m_transcribeQuickButton->setText(tr("轉寫"));
    if (m_translateQuickButton && m_translateQuickButton != m_fileTranslateBtn) m_translateQuickButton->setText(tr("翻譯"));
    if (m_exportQuickButton && m_exportQuickButton != m_fileExportBtn) m_exportQuickButton->setText(tr("匯出"));
    if (m_lowResCheck) { m_lowResCheck->setText(tr("低清預覽")); m_lowResCheck->setToolTip(tr("只影響預覽流暢度，不影響最終匯出畫質")); }
    if (m_fpsLabel) { m_fpsLabel->setText(tr("幀率")); m_fpsLabel->setToolTip(tr("預覽幀率上限")); }
    if (m_hardwareCheck) { m_hardwareCheck->setText(tr("硬體解碼")); m_hardwareCheck->setToolTip(tr("開啟用 GPU 硬解，關閉則強制 CPU 軟解，需重新載入影片")); }
    if (m_languageLabel) m_languageLabel->setText(tr("語言"));
    if (m_rightTranscriptBtn) m_rightTranscriptBtn->setText(tr("轉寫文字"));
    if (m_rightStyleBtn) m_rightStyleBtn->setText(tr("字幕樣式"));
    if (m_rightGlossBtn) m_rightGlossBtn->setText(tr("專屬詞庫"));
    if (m_rightPromptBtn) {
        m_rightPromptBtn->setText(tr("提示詞"));
        m_rightPromptBtn->setToolTip(tr("轉寫提示詞（whisper.cpp / faster-whisper）"));
    }
    if (m_timelineLabel) m_timelineLabel->setText(tr("時間軸"));
    if (m_previewLabel) m_previewLabel->setText(tr("預覽"));
    if (m_safeFrameLabel) m_safeFrameLabel->setText(tr("安全框"));
    if (m_previewQualityCombo) {
        const QSignalBlocker b(m_previewQualityCombo);
        const int cur = m_previewQualityCombo->currentIndex();
        m_previewQualityCombo->clear();
        m_previewQualityCombo->addItem("360p", 360);
        m_previewQualityCombo->addItem("480p", 480);
        m_previewQualityCombo->addItem("640p", 640);
        m_previewQualityCombo->addItem("720p", 720);
        m_previewQualityCombo->addItem("960p", 960);
        m_previewQualityCombo->addItem("1024p", 1024);
        m_previewQualityCombo->addItem(tr("原始"), 0);
        m_previewQualityCombo->setCurrentIndex(cur);
    }
    if (m_safeFrameCombo) {
        const QSignalBlocker blocker(m_safeFrameCombo);
        const int cur = m_safeFrameCombo->currentIndex();
        m_safeFrameCombo->clear();
        m_safeFrameCombo->addItem(tr("關閉"), int(VideoPreview::SafeFrameNone));
        m_safeFrameCombo->addItem(tr("通用"), int(VideoPreview::SafeFrameGeneral));
        m_safeFrameCombo->addItem(tr("YouTube"), int(VideoPreview::SafeFrameYouTube));
        m_safeFrameCombo->addItem(tr("YT Shorts"), int(VideoPreview::SafeFrameYouTubeShorts));
        m_safeFrameCombo->addItem(tr("Facebook"), int(VideoPreview::SafeFrameFacebook));
        m_safeFrameCombo->addItem(tr("Reels"), int(VideoPreview::SafeFrameReels));
        m_safeFrameCombo->addItem(tr("TikTok"), int(VideoPreview::SafeFrameTikTok));
        m_safeFrameCombo->addItem(tr("IG Story"), int(VideoPreview::SafeFrameIGStory));
        m_safeFrameCombo->setCurrentIndex(cur);
    }
    if (m_editor) m_editor->retranslateUi();
    if (m_styleCard) m_styleCard->retranslateUi();
    if (m_glossCard) m_glossCard->retranslateUi();
    if (m_promptCard) m_promptCard->retranslateUi();
    refreshLanguageChoices();
    // Painted (non-widget) labels must repaint so the new language takes effect.
    if (m_videoPreview) m_videoPreview->update();
    if (m_waveform)     m_waveform->update();
    if (!m_progressBar->isVisible()) updateStatusBar(tr("Ready"));
}

void MainWindow::refreshLanguageChoices() {
    if (!m_languageCombo) return;
    const auto &cfg = AppConfig::instance();
    const QStringList codes = LocalModelManager::supportedLanguageCodes(cfg.whisper.engine, cfg.whisper.modelId);
    const QString wanted = cfg.whisper.language.isEmpty() ? "auto" : cfg.whisper.language;
    const QSignalBlocker blocker(m_languageCombo);
    m_languageCombo->clear();
    m_languageCombo->addItem(tr(LocalModelManager::languageDisplayName("auto").toUtf8().constData()), "auto");
    for (const auto &code : codes) {
        if (code == "auto") continue;
        m_languageCombo->addItem(tr(LocalModelManager::languageDisplayName(code).toUtf8().constData()), code);
    }
    int index = m_languageCombo->findData(wanted);
    if (index < 0) {
        index = 0;
        auto &mutableConfig = AppConfig::instance();
        mutableConfig.whisper.language = "auto";
        mutableConfig.save();
    }
    m_languageCombo->setCurrentIndex(index);
}

void MainWindow::connectSignals() {
    connect(m_whisper, &WhisperEngine::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_whisper, &WhisperEngine::segmentReady, this, &MainWindow::onSegmentReady);
    connect(m_whisper, &WhisperEngine::finished,     this, &MainWindow::onTranscriptionFinished);
    connect(m_whisper, &WhisperEngine::warning, this, [this](const QString &msg){
        QMessageBox::warning(this, tr("Warning"), msg);
    });

    connect(m_translator, &TranslationEngine::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_translator, &TranslationEngine::batchTranslated,
            this, &MainWindow::onTranslationBatch);
    connect(m_translator, &TranslationEngine::finished, this, [this](bool ok, const QString &err){
        endTask();
        if (!ok) QMessageBox::warning(this, tr("Translation failed"), err);
        else     updateStatusBar(tr("Translation complete"));
    });

    connect(m_corrector, &SubtitleCorrectionEngine::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_corrector, &SubtitleCorrectionEngine::entryCorrected,
            this, &MainWindow::onSubtitleCorrected);
    connect(m_corrector, &SubtitleCorrectionEngine::finished, this, &MainWindow::onCorrectionFinished);

    connect(m_editor, &SubtitleEditor::correctionRequested, this, &MainWindow::startSubtitleCorrection);

    connect(m_appUpdate, &AppUpdateChecker::checkFinished,
            this, &MainWindow::onAppUpdateCheckFinished);

    connect(m_exporter, &FFmpegExporter::progressChanged,
            this, [this](int p, const QString &m){ setProgress(p,m); });
    connect(m_exporter, &FFmpegExporter::finished, this, &MainWindow::onExportFinished);

    connect(m_videoPreview, &VideoPreview::positionChanged, m_waveform, &WaveformWidget::setPositionMs, Qt::QueuedConnection);
    connect(m_videoPreview, &VideoPreview::positionChanged, m_editor,   &SubtitleEditor::highlightAtMs, Qt::QueuedConnection);
    connect(m_videoPreview, &VideoPreview::durationChanged, m_waveform, &WaveformWidget::setDurationMs);
    connect(m_videoPreview, &VideoPreview::durationChanged, this, [this](qint64 durationMs) {
        m_videoDurationMs = durationMs;
        if (!m_currentVideoPath.isEmpty())
            m_waveform->setMediaFile(m_currentVideoPath);
    });
    connect(m_waveform,     &WaveformWidget::seekRequested, m_videoPreview, &VideoPreview::seekTo);
    connect(m_editor,       &SubtitleEditor::jumpRequested, m_videoPreview, &VideoPreview::seekTo);
    // Keep the preview's draggable subtitle block in sync with the editor
    // selection so users can position cues even when the playhead sits
    // between two subtitles.
    connect(m_editor,       &SubtitleEditor::selectionChanged, m_videoPreview, &VideoPreview::setSelectedRow);
    connect(m_waveform,     &WaveformWidget::selectionChanged, m_videoPreview, &VideoPreview::setSelectedRow);

    connect(m_editor, &SubtitleEditor::splitRequested,  m_model,
            [this](int row, qint64 ms){ m_model->splitAt(row, ms); });
    connect(m_editor, &SubtitleEditor::mergeRequested,  m_model,
            [this](int r1, int r2){ m_model->mergeRows(r1, r2); });
    connect(m_editor, qOverload<int>(&SubtitleEditor::deleteRequested), m_model,
            [this](int row){ m_model->removeEntry(row); });
    connect(m_editor, qOverload<const QList<int>&>(&SubtitleEditor::deleteRequested), m_model,
            [this](const QList<int> &rows){ m_model->removeEntries(rows); });
    connect(m_waveform, &WaveformWidget::subtitleMoved, this,
            [this]{ updateStatusBar(tr("Subtitle timing updated")); });
    connect(m_waveform, &WaveformWidget::selectionChanged, this,
            [this](int row) {
                if (row < 0) updateStatusBar(tr("Ready"));
                else {
                    const auto &e = m_model->entryAt(row);
                    updateStatusBar(tr("Selected subtitle #%1: %2 → %3 (Alt+←/→ nudge, Delete removes)")
                        .arg(row + 1)
                        .arg(TimeFmt::formatClock(e.startMs, true),
                             TimeFmt::formatClock(e.endMs, true)));
                }
            });
    // Undo / redo for every subtitle edit (timeline drags, split, merge,
    // delete, smart segmentation, magnetic alignment…).
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(undoShortcut, &QShortcut::activated, m_model, &SubtitleModel::undo);
    connect(redoShortcut, &QShortcut::activated, m_model, &SubtitleModel::redo);
    connect(m_model, &SubtitleModel::undoStateChanged, this, [this] {
        updateStatusBar(tr("Undo: %1 / Redo: %2")
            .arg(m_model->canUndo() ? tr("Available") : tr("Unavailable"),
                 m_model->canRedo() ? tr("Available") : tr("Unavailable")));
    });
    // Pastel redesign dropped the FILE/TOOLS menus; keep their shortcuts alive.
    auto *openShortcut = new QShortcut(QKeySequence::Open, this);
    connect(openShortcut, &QShortcut::activated, this, &MainWindow::openVideo);
    auto *saveShortcut = new QShortcut(QKeySequence("Ctrl+S"), this);
    connect(saveShortcut, &QShortcut::activated, this, &MainWindow::exportSRT);
    auto *transcribeShortcut = new QShortcut(QKeySequence("Ctrl+G"), this);
    connect(transcribeShortcut, &QShortcut::activated, this, &MainWindow::startTranscription);
    auto *translateShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(translateShortcut, &QShortcut::activated, this, &MainWindow::startTranslation);
    auto *exportShortcut = new QShortcut(QKeySequence("Ctrl+E"), this);
    connect(exportShortcut, &QShortcut::activated, this, &MainWindow::startExport);
    auto *previewShortcut = new QShortcut(QKeySequence("Ctrl+Shift+T"), this);
    connect(previewShortcut, &QShortcut::activated, this, &MainWindow::showTranslationPreview);    connect(m_model, &SubtitleModel::entriesChanged, m_waveform,
            QOverload<>::of(&QWidget::update));
    // Editorial folio: keep manuscript count in sync (folio ledger)
    connect(m_model, &SubtitleModel::entriesChanged, this, [this] {
        if (m_editorCount) m_editorCount->setText(tr("%1 ENTRIES").arg(m_model->rowCount()));
        if (m_previewCount) {
            m_previewCount->setText(m_model->rowCount() > 0 ? tr("%1 CUES").arg(m_model->rowCount()) : QStringLiteral("—"));
            m_previewCount->setVisible(m_model->rowCount() > 0);
        }
    });
}

// ── Drag-and-drop ────────────────────────────────────────────────────────────
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
    // The waveform is decoded once the media duration is known (see connectSignals).
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
    if (hasActiveTask()) return;

    auto &cfg = AppConfig::instance();
    if (cfg.whisper.engine.compare("whisper.cpp", Qt::CaseInsensitive) == 0) {
        QString installedVariant;
        const QString installedCli = BackendCatalog::resolveWhisperCli(cfg.whisper.computeDevice, &installedVariant);
        if (!installedCli.isEmpty()) {
            cfg.paths.engineRoot = BackendCatalog::installRoot();
            cfg.paths.whisperCliPath = installedCli;
            cfg.whisper.cliPath = installedCli;
            cfg.whisper.computeDevice = installedVariant;
            cfg.save();
        }
    }
    if (cfg.whisper.engine == "whisper.cpp"
        && BackendCatalog::installedVariants("whisper.cpp").isEmpty()
        && cfg.whisper.cliPath.isEmpty()) {
        QMessageBox::information(this, tr("Backend required"),
            tr("The whisper.cpp backend is not installed. Open Settings → Audio transcription engines, pick a variant and install it."));
        openSettings();
        return;
    }
    if (cfg.whisper.engine != "whisper.cpp"
        && !BackendCatalog::isPythonRuntimeReady()
        && (cfg.whisper.runnerPath.isEmpty() || !QFileInfo(cfg.whisper.runnerPath).isFile())) {
        QMessageBox::information(this, tr("Engine runner required"),
            tr("This local engine needs a runner executable. Set it in Settings -> Paths."));
        openSettings();
        return;
    }
    // Python engines run through sherpa-onnx / CTranslate2 / PyTorch — none of
    // them provide a Vulkan backend. Fail fast here instead of mid-run inside
    // the runner with an English traceback.
    if (cfg.whisper.engine != "whisper.cpp"
        && cfg.whisper.computeDevice.trimmed().compare("vulkan", Qt::CaseInsensitive) == 0) {
        QMessageBox::information(this, tr("Device not supported"),
            tr("This engine does not support Vulkan. Switch to CPU under Settings → Device, or (NVIDIA GPUs) install the CUDA build and select CUDA."));
        openSettings();
        return;
    }
    if (!cfg.whisper.modelId.isEmpty()) {
        for (const auto &spec : LocalModelManager::catalog()) {
            const bool installedInRoot = LocalModelManager::isInstalled(spec, cfg.paths.modelRoot);
            const bool configuredFileExists = cfg.whisper.engine.compare("whisper.cpp", Qt::CaseInsensitive) == 0
                && !cfg.whisper.modelPath.isEmpty()
                && QFileInfo::exists(cfg.whisper.modelPath)
                && QFileInfo(cfg.whisper.modelPath).isFile();
            if (spec.engine.compare(cfg.whisper.engine, Qt::CaseInsensitive) == 0 && spec.id == cfg.whisper.modelId &&
                !installedInRoot && !configuredFileExists) {
                QMessageBox::information(this, tr("Model not ready"),
                    tr("The selected model is incomplete. Open Settings and finish the download first."));
                openSettings();
                return;
            }
        }
    }
    const QFileInfo modelInfo(cfg.whisper.modelPath);
    const bool nativeEngine = cfg.whisper.engine.compare("whisper.cpp", Qt::CaseInsensitive) == 0;
    const bool validModelPath = nativeEngine ? modelInfo.isFile() : (modelInfo.isFile() || modelInfo.isDir());
    if (cfg.whisper.modelPath.isEmpty() || !validModelPath) {
        auto r = QMessageBox::question(this, tr("Settings required"),
            tr("A valid model file or model folder is required before transcription.\nOpen Settings now?"),
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
    m_stopTranscribeButton->setVisible(true);
    updateTaskPowerRequest(true);
    setProgress(0, tr("Preparing transcription..."));
    m_progressBar->show();
}

void MainWindow::onSegmentReady(SubtitleEntry entry) {
    if (AppConfig::instance().whisper.useGlossary) {
        const QList<GlossaryEntry> glossary = Glossary::load();
        if (!glossary.isEmpty())
            entry.text = Glossary::applyToText(glossary, entry.text);
    }
    m_model->appendEntry(entry);
}

void MainWindow::onTranscriptionFinished(bool ok, const QString &err) {
    if (m_stopTranscribeButton) m_stopTranscribeButton->setVisible(false);
    endTask();
    m_model->reIndex();
    if (!ok) QMessageBox::warning(this, tr("Transcription failed"), err);
    else     updateStatusBar(tr("Done: %1 subtitles generated").arg(m_model->rowCount()));
}

void MainWindow::stopTranscription() {
    if (!m_whisper || !m_whisper->isBusy()) return;
    m_statusLabel->setText(tr("Stopping transcription..."));
    m_whisper->stop();
}

// ── Translation ───────────────────────────────────────────────────────────────
void MainWindow::startTranslation() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to translate"));
        return;
    }
    if (hasActiveTask()) return;
    const auto &cfg = AppConfig::instance();
    m_translator->translate(m_model->entries(), cfg.translation);
    updateTaskPowerRequest(true);
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

void MainWindow::showTranslationPreview() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to preview"));
        return;
    }
    TranslationPreviewDialog dlg(m_model->entries(), this);
    if (dlg.exec() == QDialog::Accepted) {
        auto updated = dlg.updatedEntries();
        for (int i = 0; i < updated.size() && i < m_model->rowCount(); ++i) {
            m_model->entryAt(i).translation = updated[i].translation;
        }
        emit m_model->entriesChanged();
        m_waveform->update();
    }
}

// ── AI Subtitle Correction ──────────────────────────────────────────────────
void MainWindow::startSubtitleCorrection() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to correct"));
        return;
    }
    if (hasActiveTask()) return;

    const auto &cfg = AppConfig::instance();
    CorrectionParams params = cfg.correction;
    // Fall back to translation settings when correction fields are left blank.
    if (params.apiKey.trimmed().isEmpty())
        params.apiKey = cfg.translation.apiKey;
    if (params.ollamaUrl.trimmed().isEmpty())
        params.ollamaUrl = cfg.translation.ollamaUrl;
    if (params.ollamaModel.trimmed().isEmpty())
        params.ollamaModel = cfg.translation.ollamaModel;
    if (params.geminiModel.trimmed().isEmpty())
        params.geminiModel = cfg.translation.geminiModel;

    if ((params.backend == CorrectionBackend::OpenAI
         || params.backend == CorrectionBackend::Anthropic
         || params.backend == CorrectionBackend::Gemini)
        && params.apiKey.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("API key required"),
            tr("Please configure an API key for AI subtitle correction in Settings → Translation Services."));
        openSettings();
        return;
    }

    m_model->beginTimelineEdit(); // record undo snapshot before batch correction
    m_corrector->correct(m_model->entries(), params);
    beginTask();
    setProgress(0, tr("Preparing AI correction..."));
}

void MainWindow::onSubtitleCorrected(int index, QString text) {
    if (!m_model || index < 0 || index >= m_model->rowCount()) return;
    m_model->entryAt(index).text = text;
}

void MainWindow::onCorrectionFinished(bool ok, const QString &err) {
    endTask();
    if (!ok) {
        QMessageBox::warning(this, tr("Correction failed"), err);
    } else {
        emit m_model->entriesChanged();
        m_waveform->update();
        updateStatusBar(tr("AI correction complete"));
    }
}

void MainWindow::onAppUpdateCheckFinished(bool ok, bool updateAvailable,
                                          const QString &latestVersion,
                                          const QString &releaseUrl,
                                          const QString &error)
{
    if (!ok) {
        // Silent failure by default; network errors should not interrupt the user.
        if (!error.isEmpty())
            updateStatusBar(tr("Update check failed: %1").arg(error.left(80)));
        return;
    }
    if (!updateAvailable) return;

    auto &cfg = AppConfig::instance();
    if (latestVersion == cfg.update.skippedVersion) return;

    auto *box = new QMessageBox(this);
    box->setWindowTitle(tr("Update Available"));
    box->setText(tr("Yumu Studio %1 is available.\n\nYou are currently running %2.")
                 .arg(latestVersion, QStringLiteral(APP_VERSION)));
    box->setInformativeText(tr("Would you like to open the release page and download it now?"));
    box->setIcon(QMessageBox::Information);
    auto *downloadBtn = box->addButton(tr("Go to Download"), QMessageBox::YesRole);
    auto *skipBtn     = box->addButton(tr("Skip this version"), QMessageBox::RejectRole);
    auto *laterBtn    = box->addButton(tr("Later"), QMessageBox::NoRole);
    box->setDefaultButton(downloadBtn);
    connect(box, &QMessageBox::finished, this, [box, releaseUrl, latestVersion, downloadBtn, skipBtn](int) {
        auto *clicked = box->clickedButton();
        if (clicked == downloadBtn) {
            QDesktopServices::openUrl(QUrl(releaseUrl));
        } else if (clicked == skipBtn) {
            auto &cfg = AppConfig::instance();
            cfg.update.skippedVersion = latestVersion;
            cfg.save();
        }
        box->deleteLater();
    });
    box->show();
}

// ── Export (Simplified + Advanced) ──────────────────────────────────────────
void MainWindow::startExport() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to export"));
        return;
    }
    if (hasActiveTask()) return;

    SimpleExportDialog dlg(m_model, m_currentVideoPath, m_videoDurationMs, this);
    if (dlg.exec() != QDialog::Accepted) return;
    auto res = dlg.result();

    // The dialog no longer edits subtitle style — exports always burn the
    // model's live style from the style studio. Keep the bilingual toggle in
    // sync so the preview and future exports agree with this export.
    m_model->style().bilingualEnabled = res.bilingual;

    if (res.action == SimpleExportDialog::Action::ExportMP4) {
        if (m_currentVideoPath.isEmpty() || !QFileInfo(m_currentVideoPath).isFile()) {
            QMessageBox::information(this, tr("Video required"),
                tr("Load a video before exporting a finished video."));
            return;
        }
        QString outPath = QFileDialog::getSaveFileName(this, tr("Save finished video"), {}, tr("MP4 Video (*.mp4)"));
        if (outPath.isEmpty()) return;
        if (QFileInfo(m_currentVideoPath).absoluteFilePath().compare(
                QFileInfo(outPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, tr("Invalid output"),
                tr("The output file cannot be the same as the input video."));
            return;
        }
        ExportParams ep;
        ep.inputVideo    = m_currentVideoPath;
        ep.outputPath    = outPath;
        ep.burnSubtitles = true;
        ep.bilingualBurn = res.bilingual;
        ep.srtOnly       = false;
        ep.transparentProres = false;
        // Quality mapping: 高畫質/標準/流暢
        if (res.qualityIndex == 0) { ep.crf = 18; ep.preset = "slow"; }
        else if (res.qualityIndex == 2) { ep.crf = 28; ep.preset = "fast"; }
        else { ep.crf = 23; ep.preset = "medium"; }
        ep.videoCodec    = "libx264";
        ep.audioBitrate  = 192;
        ep.style         = m_model->style();
        ep.ffmpegPath    = AppConfig::instance().paths.ffmpegPath;
        m_exporter->startExport(ep, m_model->entries());
        beginTask();
    } else if (res.action == SimpleExportDialog::Action::ExportTransparent) {
        // Transparent ProRes export may use video duration as canvas length;
        // still requires at least a reference size (fallback 1920x1080 if no video).
        QString outPath = QFileDialog::getSaveFileName(this, tr("Save transparent subtitle video"), {}, tr("MOV (*.mov)"));
        if (outPath.isEmpty()) return;
        ExportParams ep;
        ep.inputVideo    = m_currentVideoPath;
        ep.outputPath    = outPath;
        ep.burnSubtitles = true;
        ep.bilingualBurn = res.bilingual;
        ep.srtOnly       = false;
        ep.transparentProres = true;
        ep.style         = m_model->style();
        ep.ffmpegPath    = AppConfig::instance().paths.ffmpegPath;
        m_exporter->startExport(ep, m_model->entries());
        beginTask();
    }
    // SRT/VTT/TXT downloads are handled directly inside SimpleExportDialog
}

void MainWindow::startAdvancedExport() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to export"));
        return;
    }
    if (hasActiveTask()) return;
    ExportDialog dlg(m_model->style(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    auto res = dlg.result();
    // Export dialog no longer edits style — burn the model's live style and
    // keep the bilingual toggle in sync with the model.
    m_model->style().bilingualEnabled = res.bilingualBurn;

    if (!res.srtOnly && (m_currentVideoPath.isEmpty() || !QFileInfo(m_currentVideoPath).isFile())) {
        QMessageBox::information(this, tr("Video required"),
            tr("Open a valid video before exporting a burned video."));
        return;
    }
    QString filter = res.srtOnly ? tr("SRT (*.srt)") : tr("MP4 Video (*.mp4)");
    QString outPath = QFileDialog::getSaveFileName(this, tr("Save output"), {}, filter);
    if (outPath.isEmpty()) return;
    if (QFileInfo(m_currentVideoPath).absoluteFilePath().compare(
            QFileInfo(outPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        QMessageBox::warning(this, tr("Invalid output"),
            tr("The output file must be different from the input video."));
        return;
    }

    ExportParams ep;
    ep.inputVideo    = m_currentVideoPath;
    ep.outputPath    = outPath;
    ep.burnSubtitles = res.burnSubtitles;
    ep.bilingualBurn = res.bilingualBurn;
    ep.srtOnly       = res.srtOnly;
    ep.transparentProres = false;
    ep.videoCodec    = res.videoCodec;
    ep.crf           = res.crf;
    ep.preset        = res.preset;
    ep.audioBitrate  = res.audioBitrate;
    ep.style         = m_model->style();
    ep.ffmpegPath    = AppConfig::instance().paths.ffmpegPath;
    m_exporter->startExport(ep, m_model->entries());
    beginTask();
}

void MainWindow::onExportFinished(bool ok, const QString &err) {
    endTask();
    if (!ok) QMessageBox::critical(this, tr("Export failed"), err);
    else {
        QMessageBox::information(this, tr("Done"), tr("Export successful!"));
        updateStatusBar(tr("Export complete"));
    }
}

void MainWindow::openSmartSplit() {
    if (m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to refine yet. Transcribe or import a subtitle file first."));
        return;
    }
    SmartSplitDialog dlg(m_model->rowCount(), this);
    connect(&dlg, &SmartSplitDialog::applied, this, [this](const RefineParams &params) {
        const QList<SubtitleEntry> previous = m_model->entries();
        const QList<SubtitleEntry> refined = SubtitleRefiner::refine(previous, params);
        if (refined.isEmpty()) return;
        m_model->setEntries(refined);
        updateStatusBar(tr("Smart segmentation: %1 → %2 subtitles")
                            .arg(previous.size()).arg(refined.size()));
    });
    dlg.exec();
}

void MainWindow::openCutAlign() {
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, tr("Info"), tr("Please open a video file first"));
        return;
    }
    CutAlignDialog dlg(m_currentVideoPath, AppConfig::instance().paths.ffmpegPath,
                       m_videoDurationMs, m_model->rowCount(), this);
    connect(&dlg, &CutAlignDialog::requestAlign, this,
            [this](const QVector<qint64> &cuts, qint64 toleranceMs) {
        QList<SubtitleEntry> entries = m_model->entries();
        SubtitleRefiner::snapToCuts(entries, cuts, toleranceMs);
        m_model->setEntries(entries);
        // Keep the cut markers on the timeline: they stay visible and act as
        // live magnetic snap targets for further manual adjustments.
        m_waveform->setCuts(cuts);
        updateStatusBar(tr("Magnetic alignment applied: %1 scene cuts, tolerance %2 ms").arg(cuts.size()).arg(toleranceMs));
    });
    dlg.exec();
}

// ── Settings ──────────────────────────────────────────────────────────────────
void MainWindow::ensureRuntimeBackend() {
    // Never steal focus at startup: the workspace is always the landing page.
    // When no backend is installed we just leave a hint in the status bar so
    // the user can open the Engine Center whenever it suits them (starting a
    // transcription without a backend also guides them there).
    if (!BackendCatalog::installedVariants("whisper.cpp").isEmpty())
        return;
    auto &config = AppConfig::instance();
    config.system.onboardingDone = true;
    config.save();
    updateStatusBar(tr("whisper.cpp backend is not installed yet. Open Settings to install it."));
}

void MainWindow::openSettings() {
    if (!m_settingsPage) createSettingsPage();
    const int idx = m_pages ? m_pages->indexOf(m_settingsPage) : -1;
    if (idx < 0 || (m_pages && m_pages->currentIndex() == idx)) return;
    updateStatusBar(tr("設定"));
    // Instant switch + short fade: first paint is immediate, no double-grab
    // wait like the old cross-fade.
    if (!StackAnimator::fadeIn(m_pages, idx, 140))
        m_pages->setCurrentIndex(idx);
    updateNavActive();
}

void MainWindow::showWorkspace() {
    if (m_pages && m_pages->currentIndex() == 0) return;
    updateStatusBar(tr("Ready"));
    if (!StackAnimator::fadeIn(m_pages, 0, 140))
        m_pages->setCurrentIndex(0);
    updateNavActive();
}

void MainWindow::updateStatusBar(const QString &msg) {
    if (m_statusLabel) m_statusLabel->setText(msg);
    if (m_readyText) {
        if (hasActiveTask()) m_readyText->setText(tr("Working…"));
        else if (msg.isEmpty()) m_readyText->setText(tr("Ready"));
        else if (msg == tr("Ready") || msg == tr("設定")) m_readyText->setText(msg);
        else m_readyText->setText(tr("Ready"));
    }
    if (m_statusText && m_statusText != m_readyText) m_statusText->setText(msg);
}

void MainWindow::setProgress(int pct, const QString &msg) {
    if (m_progressBar) { m_progressBar->setValue(pct); m_progressBar->show(); }
    if (m_statusLabel) m_statusLabel->setText(msg);
    if (m_readyText) m_readyText->setText(tr("Working…"));
}

void MainWindow::onRightToolbarPage(int page) {
    if (m_rightStack) m_rightStack->setCurrentIndex(page);
    if (m_rightTranscriptBtn) m_rightTranscriptBtn->setChecked(page == 0);
    if (m_rightStyleBtn) m_rightStyleBtn->setChecked(page == 1);
    if (m_rightGlossBtn) m_rightGlossBtn->setChecked(page == 2);
    if (m_rightPromptBtn) m_rightPromptBtn->setChecked(page == 3);
    if (page == 1 && m_styleCard && m_model) m_styleCard->setStyle(m_model->style());
    if (page == 2 && m_glossCard) m_glossCard->reload();
    if (page == 3 && m_promptCard) m_promptCard->reload();
}

void MainWindow::onStyleCardChanged(const SubtitleStyle &s) {
    if (!m_model) return;
    m_model->style() = s;
    if (m_videoPreview) m_videoPreview->setSubtitleStyle(s);
}

void MainWindow::onSubtitlePositionEdited() {
    updateStatusBar(tr("字幕位置已更新"));
    // Pull the dragged position back into the style card so later font /
    // colour edits emit WITH the new coordinates instead of stale ones
    // silently resetting the position. A free (dragged) position also
    // clears the 9-grid highlight; a double-click reset (preset position)
    // re-checks its cell via the normal syncUi path.
    if (!m_model || !m_styleCard) return;
    m_styleCard->setStyle(m_model->style());
    if (m_model->style().posY >= 0.0)
        m_styleCard->markPositionCustom();
}

void MainWindow::onPromptCardChanged() {
    updateStatusBar(tr("提示詞已儲存，下次轉寫生效"));
}

void MainWindow::onGlossaryCardChanged() {
    if (!AppConfig::instance().whisper.useGlossary || !m_model || m_model->rowCount() == 0) {
        updateStatusBar(tr("詞庫已儲存"));
        return;
    }
    const QList<GlossaryEntry> glossary = Glossary::load();
    if (glossary.isEmpty()) return;
    for (int i = 0; i < m_model->rowCount(); ++i)
        m_model->entryAt(i).text = Glossary::applyToText(glossary, m_model->entryAt(i).text);
    emit m_model->entriesChanged();
    updateStatusBar(tr("已套用新詞庫到現有字幕"));
}

void MainWindow::syncFileBarActive() {
    if (m_fileOpenBtn) {
        m_fileOpenBtn->setProperty("active", true);
        m_fileOpenBtn->style()->unpolish(m_fileOpenBtn);
        m_fileOpenBtn->style()->polish(m_fileOpenBtn);
    }
}

bool MainWindow::hasActiveTask() const {
    return m_whisper->isBusy() || m_translator->isBusy() || m_corrector->isBusy() || m_exporter->isBusy();
}

void MainWindow::updateTaskPowerRequest(bool active) {
    if (active) {
        ++m_activeTasks;
    } else {
        m_activeTasks = qMax(0, m_activeTasks - 1);
    }
    if (!AppConfig::instance().system.preventSleep) return;
#ifdef Q_OS_WIN
    if (m_activeTasks > 0)
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    else
        SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

void MainWindow::beginTask() {
    updateTaskPowerRequest(true);
    m_progressBar->show();
}

void MainWindow::endTask() {
    updateTaskPowerRequest(false);
    m_progressBar->hide();
}
