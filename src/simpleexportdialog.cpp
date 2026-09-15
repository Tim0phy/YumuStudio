#include "simpleexportdialog.h"
#include "stylepreviewwidget.h"
#include "uitheme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFileInfo>
#include <QApplication>

SimpleExportDialog::SimpleExportDialog(SubtitleModel *model,
                                       const QString &videoPath,
                                       qint64 durationMs,
                                       QWidget *parent)
    : QDialog(parent), m_model(model), m_videoPath(videoPath), m_durationMs(durationMs)
{
    setWindowTitle(tr("Export"));
    setMinimumWidth(560);
    setMinimumHeight(620);
    resize(600, 700);
    m_editStyle = model ? model->style() : SubtitleStyle();
    buildUI();
}

QString SimpleExportDialog::estimateTime() const {
    if (m_durationMs <= 0) return tr("Estimated ~2–5 min");
    double mins = m_durationMs / 60000.0;
    // Rough: encode ~ 1x realtime on medium, faster on ultrafast
    int est = qMax(1, int(mins * 0.45));
    if (est < 2) return tr("Estimated ~1 min");
    return tr("Estimated ~%1 min").arg(est);
}

void SimpleExportDialog::buildUI() {
    // All chrome comes from the application-wide stylesheet via objectName
    // ("yumuDialogHeader"/"yumuCard"/"yumuCardTitle", "pillBtn", "yumuBtn"…),
    // so the export window shares the exact design language of the new
    // workspace and follows the light/dark theme automatically.
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(0);

    // Title bar — pastel dialog header shared with the new design family
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName("yumuDialogHeader");
    titleBar->setFixedHeight(48);
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(18, 8, 12, 8);
    auto *titleLabel = new QLabel(tr("Export"), titleBar);
    titleLabel->setObjectName("yumuDialogTitle");
    titleLay->addWidget(titleLabel);
    titleLay->addStretch();
    auto *closeBtn = new QPushButton(QStringLiteral("×"), titleBar);
    closeBtn->setFixedSize(28,28);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(tr("Close"));
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none; font-size:18px; color:%1; background:transparent; border-radius:14px;}"
        "QPushButton:hover{color:%2; background:%3;}")
        .arg(UiTheme::hex(UiTheme::textSecond()),
             UiTheme::hex(UiTheme::text()),
             UiTheme::hex(UiTheme::panelHover())));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    titleLay->addWidget(closeBtn);
    root->addWidget(titleBar);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(16, 16, 16, 16);
    contentLay->setSpacing(14);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto makeCard = [&](const QString &title, const QString &subtitle) -> QFrame* {
        auto *card = new QFrame(content);
        card->setObjectName("yumuCard");
        auto *lay = new QVBoxLayout(card);
        lay->setContentsMargins(14, 14, 14, 14);
        lay->setSpacing(8);
        auto *t = new QLabel(title, card);
        t->setObjectName("yumuCardTitle");
        lay->addWidget(t);
        if (!subtitle.isEmpty()) {
            auto *sub = new QLabel(subtitle, card);
            sub->setWordWrap(true);
            sub->setStyleSheet(QStringLiteral("font-size: 11px; color: %1; background: transparent;")
                                   .arg(UiTheme::hex(UiTheme::textSecond())));
            lay->addWidget(sub);
        }
        return card;
    };
    auto makeBadge = [&](const QString &text, bool green) -> QLabel* {
        auto *badge = new QLabel(text, nullptr);
        badge->setFixedHeight(20);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(green
            ? QStringLiteral("background: rgba(52,199,119,0.14); color:%1; border:1px solid %2;"
                             "border-radius:4px; padding:2px 8px; font-size:10px; font-weight:700;")
                .arg(UiTheme::hex(UiTheme::success()), UiTheme::hex(UiTheme::successBorder()))
            : QStringLiteral("background:%1; color:%2; border:1px solid %3;"
                             "border-radius:4px; padding:2px 8px; font-size:10px; font-weight:700;")
                .arg(UiTheme::hex(UiTheme::panelHover()),
                     UiTheme::hex(UiTheme::text()),
                     UiTheme::hex(UiTheme::inputBorder())));
        return badge;
    };

    // Card 1: 字幕稿
    {
        auto *card = makeCard(tr("Subtitle File"), tr("For subtitle platforms, editing apps, transcripts or translations."));
        auto *lay = static_cast<QVBoxLayout*>(card->layout());
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *srtBtn = new QPushButton(tr("Download SRT"), card);
        srtBtn->setObjectName("pillBtn");
        srtBtn->setCursor(Qt::PointingHandCursor);
        auto *vttBtn = new QPushButton(tr("VTT"), card);
        vttBtn->setObjectName("pillBtnSecondary");
        vttBtn->setCursor(Qt::PointingHandCursor);
        auto *txtBtn = new QPushButton(tr("Transcript"), card);
        txtBtn->setObjectName("pillBtnSecondary");
        txtBtn->setCursor(Qt::PointingHandCursor);
        row->addWidget(srtBtn);
        row->addWidget(vttBtn);
        row->addWidget(txtBtn);
        row->addStretch();
        lay->addLayout(row);
        connect(srtBtn, &QPushButton::clicked, this, &SimpleExportDialog::onDownloadSRT);
        connect(vttBtn, &QPushButton::clicked, this, &SimpleExportDialog::onDownloadVTT);
        connect(txtBtn, &QPushButton::clicked, this, &SimpleExportDialog::onDownloadTXT);
        contentLay->addWidget(card);
    }

    // Card 2: 成品影片
    {
        auto *card = makeCard(tr("Finished Video"), tr("Burns subtitles into the picture at the original resolution, ready to publish"));
        auto *lay = static_cast<QVBoxLayout*>(card->layout());
        // top row: 影片 · 在你電腦上運算、不上傳  + 估時
        auto *infoRow = new QHBoxLayout;
        auto *infoLabel = new QLabel(tr("Video · rendered locally on your computer, never uploaded"), card);
        infoLabel->setStyleSheet(QStringLiteral("font-size:11px; color:%1; background:transparent;")
                                      .arg(UiTheme::hex(UiTheme::textMuted())));
        m_estimateLabel = new QLabel(estimateTime(), card);
        m_estimateLabel->setStyleSheet(QStringLiteral("font-size:11px; color:%1; background:transparent;")
                                           .arg(UiTheme::hex(UiTheme::accent())));
        infoRow->addWidget(infoLabel);
        infoRow->addStretch();
        infoRow->addWidget(m_estimateLabel);
        lay->addLayout(infoRow);

        // MP4 badge row
        auto *badgeRow = new QHBoxLayout;
        badgeRow->setSpacing(8);
        badgeRow->addWidget(makeBadge(QStringLiteral("MP4"), false));
        badgeRow->addStretch();
        lay->addLayout(badgeRow);

        // Quality row
        auto *qRow = new QHBoxLayout;
        qRow->setSpacing(8);
        auto *qLabel = new QLabel(tr("Quality"), card);
        qLabel->setStyleSheet(QStringLiteral("color:%1; font-size:12px; background:transparent;")
                                  .arg(UiTheme::hex(UiTheme::textSecond())));
        m_qualityCombo = new QComboBox(card);
        m_qualityCombo->addItem(tr("High quality"), 0);
        m_qualityCombo->addItem(tr("Standard"), 1);
        m_qualityCombo->addItem(tr("Smooth"), 2);
        m_qualityCombo->setCurrentIndex(1);
        m_qualityCombo->setFixedWidth(120);
        qRow->addWidget(qLabel);
        qRow->addWidget(m_qualityCombo);
        qRow->addStretch();
        // bilingual checkbox
        m_bilingualCb = new QCheckBox(tr("Bilingual burn-in"), card);
        m_bilingualCb->setChecked(m_editStyle.bilingualEnabled);
        qRow->addWidget(m_bilingualCb);
        lay->addLayout(qRow);

        // Preview sample block (light bar; user chose all-light preview areas)
        auto *sampleFrame = new QFrame(card);
        sampleFrame->setStyleSheet(QStringLiteral(
            "QFrame{background:%1; border-radius:6px; border:1px solid %2;}")
            .arg(UiTheme::hex(UiTheme::timelineBg()), UiTheme::hex(UiTheme::inputBorder())));
        sampleFrame->setFixedHeight(42);
        auto *sampleLay = new QHBoxLayout(sampleFrame);
        sampleLay->setContentsMargins(10, 6, 10, 6);
        auto *sampleLabel = new QLabel(tr("Your video is almost done"), sampleFrame);
        sampleLabel->setStyleSheet(QStringLiteral(
            "color:%1; background:%2; padding:3px 8px; font-size:13px; border-radius:4px;")
            .arg(UiTheme::hex(UiTheme::text()), UiTheme::hex(UiTheme::panelHover())));
        sampleLabel->setAlignment(Qt::AlignCenter);
        sampleLay->addStretch();
        sampleLay->addWidget(sampleLabel);
        sampleLay->addStretch();
        lay->addWidget(sampleFrame);

        // Action row — subtitle style is configured in the main-window style
        // studio, so export always burns the model's current style.
        auto *actRow = new QHBoxLayout;
        actRow->setSpacing(8);
        actRow->addStretch();
        auto *exportBtn = new QPushButton(tr("Start export"), card);
        exportBtn->setObjectName("pillBtn");
        exportBtn->setCursor(Qt::PointingHandCursor);
        actRow->addWidget(exportBtn);
        lay->addLayout(actRow);
        connect(exportBtn, &QPushButton::clicked, this, &SimpleExportDialog::onExportMP4);

        contentLay->addWidget(card);
    }

    // Card 3: 透明字幕影片
    {
        auto *card = makeCard(tr("Transparent Subtitle Video"), tr("Subtitles only on a transparent background; overlay onto the original video at its resolution"));
        auto *lay = static_cast<QVBoxLayout*>(card->layout());
        auto *badgeRow = new QHBoxLayout;
        badgeRow->setSpacing(8);
        badgeRow->addWidget(makeBadge(tr("MOV · Transparent"), true));
        auto *advLabel = new QLabel(tr("Advanced"), card);
        advLabel->setStyleSheet(QStringLiteral(
            "color:%1; font-size:11px; border:1px solid %2; border-radius:4px;"
            "padding:2px 6px; background:transparent;")
            .arg(UiTheme::hex(UiTheme::textMuted()), UiTheme::hex(UiTheme::inputBorder())));
        badgeRow->addWidget(advLabel);
        badgeRow->addStretch();
        lay->addLayout(badgeRow);
        // sample transparent preview
        auto *sampleFrame = new QFrame(card);
        sampleFrame->setStyleSheet(QStringLiteral(
            "QFrame{background:%1; border-radius:6px; border:1px dashed %2;}")
            .arg(UiTheme::hex(UiTheme::timelineBg()), UiTheme::hex(UiTheme::inputBorder())));
        sampleFrame->setFixedHeight(42);
        auto *sLay = new QHBoxLayout(sampleFrame);
        sLay->setAlignment(Qt::AlignCenter);
        auto *sLabel = new QLabel(tr("Transparent background · subtitles only"), sampleFrame);
        sLabel->setStyleSheet(QStringLiteral(
            "color:%1; background:%2; padding:3px 10px; border-radius:4px; font-size:12px; border:1px solid %3;")
            .arg(UiTheme::hex(UiTheme::text()), UiTheme::hex(UiTheme::panelHover()),
                 UiTheme::hex(UiTheme::inputBorder())));
        sLay->addWidget(sLabel);
        lay->addWidget(sampleFrame);

        auto *actRow = new QHBoxLayout;
        actRow->addStretch();
        auto *btn = new QPushButton(tr("Export MOV (transparent)"), card);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:rgba(52,199,119,0.14); color:%1; border:1px solid %2;"
            "border-radius:7px; padding:7px 16px; font-weight:600; min-height:28px;}"
            "QPushButton:hover{background:rgba(52,199,119,0.24);}")
            .arg(UiTheme::hex(UiTheme::success()), UiTheme::hex(UiTheme::successBorder())));
        actRow->addWidget(btn);
        lay->addLayout(actRow);
        connect(btn, &QPushButton::clicked, this, &SimpleExportDialog::onExportTransparent);
        contentLay->addWidget(card);
    }

    contentLay->addStretch();

    // Bottom close bar
    auto *bottomBar = new QFrame(this);
    bottomBar->setObjectName("yumuDialogHeader");
    bottomBar->setFixedHeight(48);
    auto *bLay = new QHBoxLayout(bottomBar);
    bLay->setContentsMargins(14, 8, 14, 8);
    auto *hint = new QLabel(tr("Use the safe-frame selector next to the preview to check subtitle placement"), bottomBar);
    hint->setStyleSheet(QStringLiteral("color:%1; font-size:11px; background:transparent;")
                            .arg(UiTheme::hex(UiTheme::textSecond())));
    hint->setWordWrap(true);
    bLay->addWidget(hint, 1);
    auto *close2 = new QPushButton(tr("Close"), bottomBar);
    close2->setObjectName("pillBtnSecondary");
    close2->setCursor(Qt::PointingHandCursor);
    connect(close2, &QPushButton::clicked, this, &QDialog::reject);
    bLay->addWidget(close2);
    root->addWidget(bottomBar);
}

void SimpleExportDialog::onDownloadSRT() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Notice"), tr("No subtitles to export."));
        return;
    }
    if (m_bilingualCb) m_editStyle.bilingualEnabled = m_bilingualCb->isChecked();
    QString path = QFileDialog::getSaveFileName(this, tr("Save SRT"), QString(), tr("SRT (*.srt)"));
    if (path.isEmpty()) return;
    bool ok = m_editStyle.bilingualEnabled ? m_model->exportBilingualSRT(path) : m_model->exportSRT(path);
    if (!ok) QMessageBox::warning(this, tr("Failed"), tr("Failed to write file: ") + path);
    else QMessageBox::information(this, tr("Done"), tr("SRT saved:\n") + path);
}

void SimpleExportDialog::onDownloadVTT() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Notice"), tr("No subtitles to export."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save VTT"), QString(), tr("WebVTT (*.vtt)"));
    if (path.isEmpty()) return;
    if (!m_model->exportVTT(path))
        QMessageBox::warning(this, tr("Failed"), tr("Failed to write file: ") + path);
    else QMessageBox::information(this, tr("Done"), tr("VTT saved:\n") + path);
}

void SimpleExportDialog::onDownloadTXT() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Notice"), tr("No subtitles to export."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Save transcript"), QString(), tr("Text files (*.txt)"));
    if (path.isEmpty()) return;
    // Ask whether to include timestamps
    auto ret = QMessageBox::question(this, tr("Transcript format"),
        tr("Add a timecode before each line?\n(Yes = with timecodes, No = plain text)"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (ret == QMessageBox::Cancel) return;
    bool withTs = (ret == QMessageBox::Yes);
    if (!m_model->exportTXT(path, withTs))
        QMessageBox::warning(this, tr("Failed"), tr("Failed to write file: ") + path);
    else QMessageBox::information(this, tr("Done"), tr("Transcript saved:\n") + path);
}

void SimpleExportDialog::onExportMP4() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Notice"), tr("No subtitles to burn."));
        return;
    }
    if (m_videoPath.isEmpty() || !QFileInfo(m_videoPath).isFile()) {
        QMessageBox::information(this, tr("Video required"), tr("Load a video before exporting a finished video."));
        return;
    }
    if (m_bilingualCb) m_editStyle.bilingualEnabled = m_bilingualCb->isChecked();
    m_result.action = Action::ExportMP4;
    m_result.style = m_editStyle;
    m_result.qualityIndex = m_qualityCombo ? m_qualityCombo->currentIndex() : 1;
    m_result.bilingual = m_bilingualCb ? m_bilingualCb->isChecked() : false;
    accept();
}

void SimpleExportDialog::onExportTransparent() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Notice"), tr("No subtitles to export."));
        return;
    }
    if (m_bilingualCb) m_editStyle.bilingualEnabled = m_bilingualCb->isChecked();
    m_result.action = Action::ExportTransparent;
    m_result.style = m_editStyle;
    m_result.bilingual = m_bilingualCb ? m_bilingualCb->isChecked() : false;
    accept();
}
