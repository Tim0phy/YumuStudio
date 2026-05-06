#include "translationpreviewdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QTextStream>

TranslationPreviewDialog::TranslationPreviewDialog(const QList<SubtitleEntry> &entries, QWidget *parent)
    : QDialog(parent), m_entries(entries)
{
    setWindowTitle(tr("Translation Preview"));
    setMinimumSize(700, 500);
    setMinimumWidth(800);
    buildUI();
    loadEntries();
}

void TranslationPreviewDialog::buildUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(5, 5, 5, 5);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Index"), tr("Original Text"), tr("Translation")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table);

    auto *btnRow = new QHBoxLayout;
    m_editCb = new QCheckBox(tr("Allow editing translations"), this);
    m_editCb->setToolTip(tr("Enable to manually edit translations in the table"));
    connect(m_editCb, &QCheckBox::toggled, this, [this](bool checked) {
        m_editable = checked;
        m_table->setEditTriggers(checked ? QAbstractItemView::DoubleClicked | QAbstractItemView::AnyKeyPressed
                                           : QAbstractItemView::NoEditTriggers);
    });
    btnRow->addWidget(m_editCb);
    btnRow->addStretch();

    auto *exportBtn = new QPushButton(tr("Export Translation SRT"), this);
    connect(exportBtn, &QPushButton::clicked, this, &TranslationPreviewDialog::onExportClicked);
    btnRow->addWidget(exportBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, [this](){
        saveChanges();
        accept();
    });
    btnRow->addWidget(closeBtn);

    root->addLayout(btnRow);
}

void TranslationPreviewDialog::loadEntries() {
    m_table->setRowCount(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries.at(i);

        auto *idxItem = new QTableWidgetItem(QString::number(i + 1));
        idxItem->setFlags(idxItem->flags() & ~Qt::ItemIsEditable);
        idxItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 0, idxItem);

        auto *origItem = new QTableWidgetItem(e.text);
        origItem->setFlags(origItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, origItem);

        auto *transItem = new QTableWidgetItem(e.translation);
        if (!m_editable) transItem->setFlags(transItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, transItem);
    }
    m_table->resizeRowsToContents();
}

void TranslationPreviewDialog::saveChanges() {
    if (!m_editable) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        auto *transItem = m_table->item(i, 2);
        if (transItem) {
            m_entries[i].translation = transItem->text();
        }
    }
}

void TranslationPreviewDialog::onExportClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Translation SRT"), {}, tr("SRT Subtitles (*.srt)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot open file for writing"));
        return;
    }

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &e = m_entries.at(i);
        if (e.translation.isEmpty()) continue;
        out << (i + 1) << "\n"
            << QString::number(e.startMs / 3600000).rightJustified(2, '0') << ":"
            << QString::number((e.startMs % 3600000) / 60000).rightJustified(2, '0') << ":"
            << QString::number((e.startMs % 60000) / 1000).rightJustified(2, '0') << ","
            << QString::number(e.startMs % 1000).rightJustified(3, '0') << " --> "
            << QString::number(e.endMs / 3600000).rightJustified(2, '0') << ":"
            << QString::number((e.endMs % 3600000) / 60000).rightJustified(2, '0') << ":"
            << QString::number((e.endMs % 60000) / 1000).rightJustified(2, '0') << ","
            << QString::number(e.endMs % 1000).rightJustified(3, '0') << "\n"
            << e.translation << "\n\n";
    }
    f.close();

    QMessageBox::information(this, tr("Success"),
        tr("Translation SRT exported to:\n") + path);
}