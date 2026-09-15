#include "glossarycard.h"
#include "glossary.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QSet>
#include <algorithm>

GlossaryCard::GlossaryCard(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    // Flush with the card: rightCard already pads 12H/8V (same as the toolbar
    // above), so page content left edges align across all three pages.
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    m_title = new QLabel(tr("專屬詞庫"), this);
    m_title->setObjectName(QStringLiteral("yumuCardTitle"));
    root->addWidget(m_title);
    m_hint = new QLabel(tr("常被辨識錯的人名、品牌、術語。儲存後會套用到之後的轉寫，並作為 whisper.cpp / faster-whisper 的提示詞。"), this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    root->addWidget(m_hint);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("辨識到的詞"), tr("正確用詞")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("新增"), this);
    m_addBtn->setObjectName(QStringLiteral("pillBtnSecondary"));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &GlossaryCard::addRow);
    m_delBtn = new QPushButton(tr("刪除"), this);
    m_delBtn->setObjectName(QStringLiteral("pillBtnSecondary"));
    m_delBtn->setCursor(Qt::PointingHandCursor);
    connect(m_delBtn, &QPushButton::clicked, this, &GlossaryCard::removeSelectedRows);
    m_saveBtn = new QPushButton(tr("儲存詞庫"), this);
    m_saveBtn->setObjectName(QStringLiteral("pillBtn"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &GlossaryCard::saveGlossary);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_saveBtn);
    root->addLayout(btnRow);

    reload();
}

void GlossaryCard::retranslateUi() {
    if (m_title) m_title->setText(tr("專屬詞庫"));
    if (m_hint) m_hint->setText(tr("常被辨識錯的人名、品牌、術語。儲存後會套用到之後的轉寫，並作為 whisper.cpp / faster-whisper 的提示詞。"));
    if (m_table) m_table->setHorizontalHeaderLabels({tr("辨識到的詞"), tr("正確用詞")});
    if (m_addBtn) m_addBtn->setText(tr("新增"));
    if (m_delBtn) m_delBtn->setText(tr("刪除"));
    if (m_saveBtn) m_saveBtn->setText(tr("儲存詞庫"));
}

void GlossaryCard::reload() {
    m_table->setRowCount(0);
    const QList<GlossaryEntry> entries = Glossary::load();
    for (const auto &e : entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(e.term));
        m_table->setItem(row, 1, new QTableWidgetItem(e.replacement));
    }
}

void GlossaryCard::addRow() {
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setCurrentCell(row, 0);
}

void GlossaryCard::removeSelectedRows() {
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    QSet<int> rows;
    for (auto *item : selected) rows << item->row();
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted) m_table->removeRow(row);
}

void GlossaryCard::saveGlossary() {
    QList<GlossaryEntry> entries;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        GlossaryEntry entry;
        entry.term = m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed() : QString();
        entry.replacement = m_table->item(row, 1) ? m_table->item(row, 1)->text().trimmed() : QString();
        if (!entry.term.isEmpty()) entries << entry;
    }
    if (Glossary::save(entries))
        emit glossaryChanged();
}
