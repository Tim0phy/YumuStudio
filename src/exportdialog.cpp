#include "exportdialog.h"
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
    m_bilingualCb = new QCheckBox(tr("Include translation (bilingual subtitles)"), fmtGroup);
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

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_srtRadio,         &QRadioButton::toggled, vidGroup,   &QGroupBox::setDisabled);
    connect(m_srtRadio,         &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) m_bilingualCb->setChecked(true);
    });
    m_bilingualCb->setChecked(true);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Start Export"));
    bb->button(QDialogButtonBox::Ok)->setObjectName("yumuBtn");
    bb->button(QDialogButtonBox::Cancel)->setObjectName("yumuBtnSecondary");
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
        // Subtitle style is configured in the main-window style studio; the
        // export dialog never edits it, so the model style passes through.
        m_result.style         = m_style;
        accept();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}