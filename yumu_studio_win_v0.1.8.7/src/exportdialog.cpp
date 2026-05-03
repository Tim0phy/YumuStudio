#include "exportdialog.h"
#include "stylepreviewwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QColorDialog>
#include <QFontDialog>
#include <QDialogButtonBox>

ExportDialog::ExportDialog(SubtitleStyle defaultStyle, QWidget *parent)
    : QDialog(parent), m_style(defaultStyle)
{
    setWindowTitle(tr("Export Settings"));
    setMinimumWidth(520);
    buildUI();
}

void ExportDialog::buildUI() {
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);

    // ── Output format ─────────────────────────────────────────────────────────
    auto *fmtGroup  = new QGroupBox(tr("Output Format"));
    auto *fmtLayout = new QVBoxLayout(fmtGroup);
    m_burnRadio   = new QRadioButton(tr("Burn subtitles into video (MP4)"), fmtGroup);
    m_srtRadio    = new QRadioButton(tr("Export SRT subtitle file only"),   fmtGroup);
    m_bilingualCb = new QCheckBox(tr("Include bilingual translation"),      fmtGroup);
    m_burnRadio->setChecked(true);
    fmtLayout->addWidget(m_burnRadio);
    fmtLayout->addWidget(m_srtRadio);
    fmtLayout->addWidget(m_bilingualCb);
    root->addWidget(fmtGroup);

    // ── Video encoding ────────────────────────────────────────────────────────
    auto *vidGroup = new QGroupBox(tr("Video Encoding (Burn mode)"));
    auto *vidForm  = new QFormLayout(vidGroup);
    m_codecCombo   = new QComboBox; m_codecCombo->addItems({"libx264","libx265","libvpx-vp9"});
    m_presetCombo  = new QComboBox; m_presetCombo->addItems({"ultrafast","fast","medium","slow","veryslow"});
    m_presetCombo->setCurrentText("fast");
    m_crfSpin      = new QSpinBox; m_crfSpin->setRange(0,51); m_crfSpin->setValue(23);
    m_audioBrSpin  = new QSpinBox; m_audioBrSpin->setRange(64,320); m_audioBrSpin->setValue(192);
    m_audioBrSpin->setSuffix(" kbps");
    vidForm->addRow(tr("Video codec:"),   m_codecCombo);
    vidForm->addRow(tr("CRF quality:"),   m_crfSpin);
    vidForm->addRow(tr("Speed preset:"),  m_presetCombo);
    vidForm->addRow(tr("Audio bitrate:"), m_audioBrSpin);
    root->addWidget(vidGroup);

    // ── Subtitle style ────────────────────────────────────────────────────────
    auto *styleGroup = new QGroupBox(tr("Subtitle Style"));
    auto *styleForm  = new QFormLayout(styleGroup);

    m_fontLabel   = new QLabel(m_style.fontFamily);
    auto *fontBtn = new QPushButton(tr("Choose Font..."));
    fontBtn->setFixedWidth(110);
    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(m_fontLabel, 1); fontRow->addWidget(fontBtn);
    auto *fontW   = new QWidget; fontW->setLayout(fontRow);

    m_fontSizeSpin     = new QSpinBox; m_fontSizeSpin->setRange(12,120); m_fontSizeSpin->setValue(m_style.fontSize);
    m_outlineWidthSpin = new QSpinBox; m_outlineWidthSpin->setRange(0,8); m_outlineWidthSpin->setValue(m_style.outlineWidth);
    m_boldCb   = new QCheckBox; m_boldCb->setChecked(m_style.bold);
    m_italicCb = new QCheckBox; m_italicCb->setChecked(m_style.italic);
    m_bgCb     = new QCheckBox(tr("Show background box")); m_bgCb->setChecked(m_style.showBg);
    m_positionCombo = new QComboBox;
    m_positionCombo->addItems({tr("Bottom center"), tr("Top center"), tr("Center")});
    m_positionCombo->setCurrentIndex(m_style.position);

    // Color buttons — use lambda, no extra slots needed
    auto makeColorBtn = [this](QColor &ref) -> QPushButton* {
        auto *btn = new QPushButton(this);
        btn->setFixedSize(64, 24);
        auto refresh = [btn, &ref]() {
            btn->setStyleSheet(QString("background:%1;border:1px solid #666;border-radius:3px;").arg(ref.name()));
        };
        refresh();
        connect(btn, &QPushButton::clicked, this, [this, btn, &ref, refresh]() mutable {
            QColor c = QColorDialog::getColor(ref, btn, tr("Choose Color"), QColorDialog::ShowAlphaChannel);
            if (c.isValid()) { ref = c; refresh(); updatePreview(); }
        });
        return btn;
    };

    m_textColorBtn    = makeColorBtn(m_style.textColor);
    m_outlineColorBtn = makeColorBtn(m_style.outlineColor);

    styleForm->addRow(tr("Font:"),          fontW);
    styleForm->addRow(tr("Font size:"),     m_fontSizeSpin);
    styleForm->addRow(tr("Text color:"),    m_textColorBtn);
    styleForm->addRow(tr("Outline color:"), m_outlineColorBtn);
    styleForm->addRow(tr("Outline width:"), m_outlineWidthSpin);
    styleForm->addRow(tr("Bold:"),          m_boldCb);
    styleForm->addRow(tr("Italic:"),        m_italicCb);
    styleForm->addRow(tr("Position:"),      m_positionCombo);
    styleForm->addRow(QString{},            m_bgCb);
    root->addWidget(styleGroup);

    // ── Live Preview ──────────────────────────────────────────────────────────
    auto *previewGroup = new QGroupBox(tr("Live Preview"));
    auto *pvLayout     = new QVBoxLayout(previewGroup);
    m_preview = new StylePreviewWidget(this);
    m_preview->setStyle(m_style);
    pvLayout->addWidget(m_preview);
    root->addWidget(previewGroup);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(fontBtn,            &QPushButton::clicked,                              this, &ExportDialog::chooseFont);
    connect(m_fontSizeSpin,     QOverload<int>::of(&QSpinBox::valueChanged),        this, &ExportDialog::updatePreview);
    connect(m_outlineWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged),        this, &ExportDialog::updatePreview);
    connect(m_boldCb,           &QCheckBox::toggled,                               this, &ExportDialog::updatePreview);
    connect(m_italicCb,         &QCheckBox::toggled,                               this, &ExportDialog::updatePreview);
    connect(m_bgCb,             &QCheckBox::toggled,                               this, &ExportDialog::updatePreview);
    connect(m_positionCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),this, &ExportDialog::updatePreview);
    connect(m_bilingualCb,      &QCheckBox::toggled,                               this, &ExportDialog::updatePreview);
    connect(m_srtRadio,         &QRadioButton::toggled, vidGroup,    &QGroupBox::setDisabled);
    connect(m_srtRadio,         &QRadioButton::toggled, styleGroup,  &QGroupBox::setDisabled);
    connect(m_srtRadio,         &QRadioButton::toggled, previewGroup,&QGroupBox::setDisabled);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Start Export"));
    root->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, [this](){
        m_result.accepted      = true;
        m_result.burnSubtitles = m_burnRadio->isChecked();
        m_result.srtOnly       = m_srtRadio->isChecked();
        m_result.bilingualBurn = m_bilingualCb->isChecked();
        m_result.videoCodec    = m_codecCombo->currentText();
        m_result.crf           = m_crfSpin->value();
        m_result.preset        = m_presetCombo->currentText();
        m_result.audioBitrate  = m_audioBrSpin->value();
        m_style.fontSize       = m_fontSizeSpin->value();
        m_style.outlineWidth   = m_outlineWidthSpin->value();
        m_style.bold           = m_boldCb->isChecked();
        m_style.italic         = m_italicCb->isChecked();
        m_style.showBg         = m_bgCb->isChecked();
        m_style.position       = m_positionCombo->currentIndex();
        m_result.style         = m_style;
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExportDialog::chooseFont() {
    bool ok;
    QFont f(m_style.fontFamily, m_style.fontSize);
    QFont chosen = QFontDialog::getFont(&ok, f, this, tr("Choose Font"));
    if (ok) {
        m_style.fontFamily = chosen.family();
        m_style.fontSize   = chosen.pointSize();
        m_fontLabel->setText(chosen.family());
        m_fontSizeSpin->setValue(chosen.pointSize());
        updatePreview();
    }
}

void ExportDialog::updatePreview() {
    m_style.fontSize         = m_fontSizeSpin->value();
    m_style.outlineWidth     = m_outlineWidthSpin->value();
    m_style.bold             = m_boldCb->isChecked();
    m_style.italic           = m_italicCb->isChecked();
    m_style.showBg           = m_bgCb->isChecked();
    m_style.position         = m_positionCombo->currentIndex();
    m_style.bilingualEnabled = m_bilingualCb->isChecked();
    m_preview->setStyle(m_style);
}
