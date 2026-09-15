#include "subtitletools.h"
#include "cutdetector.h"
#include "subtitlerefiner.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SmartSplitDialog::SmartSplitDialog(int subtitleCount, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Smart Segmentation"));
    auto *root = new QVBoxLayout(this);

    root->addWidget(new QLabel(
        tr("Re-split subtitles by reading rhythm: characters per line, lines per cue and maximum dwell time. Long cues are divided at sentence boundaries.")));
    if (subtitleCount <= 0)
        root->addWidget(new QLabel(tr("There are no subtitles to refine yet.")));

    auto *form = new QFormLayout;
    m_charsPerLine = new QSpinBox;
    m_charsPerLine->setRange(6, 60);
    m_charsPerLine->setValue(16);
    form->addRow(tr("Characters per line"), m_charsPerLine);
    m_maxLines = new QSpinBox;
    m_maxLines->setRange(1, 3);
    m_maxLines->setValue(2);
    form->addRow(tr("Lines per subtitle"), m_maxLines);
    m_maxSeconds = new QSpinBox;
    m_maxSeconds->setRange(1, 30);
    m_maxSeconds->setValue(6);
    m_maxSeconds->setSuffix(tr(" s"));
    form->addRow(tr("Maximum time on screen"), m_maxSeconds);
    root->addLayout(form);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(tr("Apply segmentation"));
    box->button(QDialogButtonBox::Ok)->setObjectName("yumuBtn");
    box->button(QDialogButtonBox::Cancel)->setObjectName("yumuBtnSecondary");
    connect(box, &QDialogButtonBox::accepted, this, &SmartSplitDialog::apply);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);
}

void SmartSplitDialog::apply() {
    RefineParams params;
    params.maxCharsPerLine = m_charsPerLine->value();
    params.maxLines = m_maxLines->value();
    params.maxCueMs = qint64(m_maxSeconds->value()) * 1000;
    emit applied(params);
    accept();
}

CutAlignDialog::CutAlignDialog(const QString &videoPath, const QString &ffmpegPath,
                               qint64 durationMs, int subtitleCount, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath), m_ffmpegPath(ffmpegPath), m_durationMs(durationMs) {
    setWindowTitle(tr("Cut Detection && Magnetic Alignment"));
    resize(520, 300);
    auto *root = new QVBoxLayout(this);

    root->addWidget(new QLabel(
        tr("Detect scene cuts in the video, then magnetically snap subtitle boundaries onto the nearest cut so captions change exactly where the picture does.")));

    auto *form = new QFormLayout;
    m_threshold = new QDoubleSpinBox;
    m_threshold->setRange(0.10, 0.95);
    m_threshold->setSingleStep(0.05);
    m_threshold->setValue(0.35);
    form->addRow(tr("Cut sensitivity"), m_threshold);
    m_toleranceMs = new QSpinBox;
    m_toleranceMs->setRange(50, 1000);
    m_toleranceMs->setSingleStep(50);
    m_toleranceMs->setValue(200);
    m_toleranceMs->setSuffix(tr(" ms"));
    form->addRow(tr("Snap tolerance"), m_toleranceMs);
    root->addLayout(form);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->hide();
    root->addWidget(m_progress);

    m_summary = new QLabel(subtitleCount > 0
        ? tr("No scan performed yet.")
        : tr("There are no subtitles to align yet."));
    root->addWidget(m_summary);

    auto *buttons = new QHBoxLayout;
    m_detectButton = new QPushButton(tr("Detect cuts"));
    m_detectButton->setObjectName("yumuBtnSecondary");
    m_detectButton->setCursor(Qt::PointingHandCursor);
    connect(m_detectButton, &QPushButton::clicked, this, &CutAlignDialog::detect);
    buttons->addWidget(m_detectButton);
    m_alignButton = new QPushButton(tr("Magnetic alignment"));
    m_alignButton->setObjectName("yumuBtn");
    m_alignButton->setCursor(Qt::PointingHandCursor);
    m_alignButton->setEnabled(false);
    connect(m_alignButton, &QPushButton::clicked, this, &CutAlignDialog::align);
    buttons->addWidget(m_alignButton);
    buttons->addStretch();
    root->addLayout(buttons);

    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(close);
}

void CutAlignDialog::detect() {
    if (m_videoPath.isEmpty() || m_detector) return;
    m_cuts.clear();
    updateSummary();
    m_detectButton->setEnabled(false);
    m_alignButton->setEnabled(false);
    m_progress->setValue(0);
    m_progress->show();
    if (!m_detector) m_detector = new CutDetector(this);
    connect(m_detector, &CutDetector::progress, m_progress, &QProgressBar::setValue);
    connect(m_detector, &CutDetector::finished, this,
            [this](bool ok, const QVector<qint64> &cuts, const QString &error) {
        m_detectButton->setEnabled(true);
        m_progress->hide();
        if (!ok) {
            m_summary->setText(tr("Scene detection failed: %1").arg(error));
            return;
        }
        m_cuts = cuts;
        m_alignButton->setEnabled(!m_cuts.isEmpty());
        updateSummary();
    });
    m_detector->run(m_videoPath, m_ffmpegPath, m_threshold->value(), m_durationMs);
}

void CutAlignDialog::align() {
    emit requestAlign(m_cuts, qint64(m_toleranceMs->value()));
    accept();
}

void CutAlignDialog::updateSummary() {
    if (m_cuts.isEmpty())
        m_summary->setText(tr("No scene cuts detected yet."));
    else
        m_summary->setText(tr("%1 scene cuts detected. Subtitle boundaries within the snap tolerance will move onto a cut.").arg(m_cuts.size()));
}
