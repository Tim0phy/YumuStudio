#include "subtitleeditor.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QRegularExpression>
#include <algorithm>

// ── 成員函數實現（修復 LNK2019）────────────────────────────────────────────────
QString SubtitleEditor::msToDisplay(qint64 ms) {
    int h=int(ms/3600000); ms%=3600000;
    int m=int(ms/60000);   ms%=60000;
    int s=int(ms/1000);    ms%=1000;
    return QString("%1:%2:%3.%4")
        .arg(h,1).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ms,3,10,QChar('0'));
}

qint64 SubtitleEditor::displayToMs(const QString &str) {
    // h:mm:ss.mmm  or  mm:ss.mmm
    QString s = str.trimmed();
    auto parts = s.split(':');
    if (parts.size() == 3) {
        auto sm = parts[2].split('.');
        return qint64(parts[0].toInt())*3600000
             + parts[1].toInt()*60000
             + sm[0].toInt()*1000
             + (sm.size()>1 ? sm[1].leftJustified(3,'0').left(3).toInt() : 0);
    } else if (parts.size() == 2) {
        auto sm = parts[1].split('.');
        return parts[0].toInt()*60000
             + sm[0].toInt()*1000
             + (sm.size()>1 ? sm[1].leftJustified(3,'0').left(3).toInt() : 0);
    }
    return 0;
}

SubtitleEditor::SubtitleEditor(QWidget *parent) : QWidget(parent) {
    buildUI();
}

void SubtitleEditor::buildUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->setSpacing(2);

    m_toolbar = new QToolBar;
    m_toolbar->setIconSize({14,14});
    m_toolbar->setStyleSheet("QToolBar { spacing:2px; }");

    auto *addAct    = m_toolbar->addAction(tr("+ Add"));
    auto *delAct    = m_toolbar->addAction(tr("X Delete"));
    m_toolbar->addSeparator();
    auto *splitAct  = m_toolbar->addAction(tr("| Split"));
    auto *mergeAct  = m_toolbar->addAction(tr("<< Merge"));
    m_toolbar->addSeparator();
    auto *upAct     = m_toolbar->addAction(tr("^ Up"));
    auto *downAct   = m_toolbar->addAction(tr("v Down"));
    m_toolbar->addSeparator();
    auto *shiftAct  = m_toolbar->addAction(tr("T Shift"));
    m_toolbar->addSeparator();
    auto *searchAct = m_toolbar->addAction(tr("Search"));
    auto *replaceAct= m_toolbar->addAction(tr("Replace"));

    addAct->setToolTip(tr("Add subtitle (Ins)"));
    delAct->setToolTip(tr("Delete selected (Del)"));
    splitAct->setToolTip(tr("Split at midpoint"));
    mergeAct->setToolTip(tr("Merge selected rows"));
    upAct->setToolTip(tr("Move up"));
    downAct->setToolTip(tr("Move down"));
    shiftAct->setToolTip(tr("Shift all timecodes by offset (ms)"));
    searchAct->setToolTip(tr("Search text (Ctrl+F)"));
    replaceAct->setToolTip(tr("Find & Replace (Ctrl+H)"));

    connect(addAct,    &QAction::triggered, this, &SubtitleEditor::onAdd);
    connect(delAct,    &QAction::triggered, this, &SubtitleEditor::onDelete);
    connect(splitAct,  &QAction::triggered, this, &SubtitleEditor::onSplit);
    connect(mergeAct,  &QAction::triggered, this, &SubtitleEditor::onMerge);
    connect(upAct,     &QAction::triggered, this, &SubtitleEditor::onMoveUp);
    connect(downAct,   &QAction::triggered, this, &SubtitleEditor::onMoveDown);
    connect(shiftAct,  &QAction::triggered, this, &SubtitleEditor::onShiftAll);
    connect(searchAct, &QAction::triggered, this, &SubtitleEditor::onSearch);
    connect(replaceAct,&QAction::triggered, this, &SubtitleEditor::onReplace);
    root->addWidget(m_toolbar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Start"), tr("End"), tr("Text"), tr("Translation")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    root->addWidget(m_table, 1);

    auto *delShortcut = new QShortcut(Qt::Key_Delete, m_table);
    connect(delShortcut, &QShortcut::activated, this, &SubtitleEditor::onDelete);
    auto *insShortcut = new QShortcut(Qt::Key_Insert, m_table);
    connect(insShortcut, &QShortcut::activated, this, &SubtitleEditor::onAdd);

    auto *searchShortcut  = new QShortcut(QKeySequence("Ctrl+F"), this);
    auto *replaceShortcut = new QShortcut(QKeySequence("Ctrl+H"), this);
    connect(searchShortcut,  &QShortcut::activated, this, &SubtitleEditor::onSearch);
    connect(replaceShortcut, &QShortcut::activated, this, &SubtitleEditor::onReplace);

    connect(m_table, &QTableWidget::cellChanged,
            this, &SubtitleEditor::onCellChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &SubtitleEditor::onCellDoubleClicked);
}

void SubtitleEditor::setModel(SubtitleModel *model) {
    m_model = model;
    connect(m_model, &SubtitleModel::entriesChanged,
            this, &SubtitleEditor::refreshTable);
    refreshTable();
}

void SubtitleEditor::refreshTable() {
    if (!m_model) return;
    m_updating = true;
    m_table->setRowCount(m_model->rowCount());
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const auto &e = m_model->entryAt(i);
        auto setCell = [&](int col, const QString &txt, bool editable=true){
            auto *item = new QTableWidgetItem(txt);
            if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(i, col, item);
        };
        setCell(0, QString::number(e.index), false);
        setCell(1, msToDisplay(e.startMs));
        setCell(2, msToDisplay(e.endMs));
        setCell(3, e.text);
        setCell(4, e.translation);
    }
    m_updating = false;
}

void SubtitleEditor::highlightAtMs(qint64 posMs) {
    if (!m_model) return;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const auto &e = m_model->entryAt(i);
        if (posMs >= e.startMs && posMs <= e.endMs) {
            m_table->selectRow(i);
            m_table->scrollToItem(m_table->item(i, 0));
            return;
        }
    }
}

void SubtitleEditor::onCellChanged(int row, int col) {
    if (m_updating || !m_model || row >= m_model->rowCount()) return;
    auto *item = m_table->item(row, col);
    if (!item) return;
    m_updating = true;
    auto &e = m_model->entryAt(row);
    if (col == 1)      e.startMs = displayToMs(item->text());
    else if (col == 2) e.endMs   = displayToMs(item->text());
    else if (col == 3) e.text    = item->text();
    else if (col == 4) e.translation = item->text();
    m_updating = false;
    emit m_model->entriesChanged();
}

void SubtitleEditor::onCellDoubleClicked(int row, int col) {
    if (col == 0 && m_model && row < m_model->rowCount())
        emit jumpRequested(m_model->entryAt(row).startMs);
}

void SubtitleEditor::onAdd() {
    if (!m_model) return;
    int row = m_table->currentRow();
    SubtitleEntry e;
    e.text = tr("New subtitle");
    if (row >= 0 && row < m_model->rowCount()) {
        e.startMs = m_model->entryAt(row).endMs;
        e.endMs   = e.startMs + 2000;
        m_model->insertEntry(row + 1, e);
    } else {
        if (m_model->rowCount() > 0) {
            const auto &last = m_model->entryAt(m_model->rowCount()-1);
            e.startMs = last.endMs; e.endMs = e.startMs + 2000;
        } else { e.startMs = 0; e.endMs = 2000; }
        m_model->appendEntry(e);
    }
}

void SubtitleEditor::onDelete() {
    if (m_table->selectedItems().isEmpty() || !m_model) return;
    int row = m_table->currentRow();
    if (row < 0 || row >= m_model->rowCount()) return;
    auto r = QMessageBox::question(this, tr("Delete"),
        tr("Delete subtitle #%1?").arg(row+1), QMessageBox::Yes | QMessageBox::No);
    if (r == QMessageBox::Yes) emit deleteRequested(row);
}

void SubtitleEditor::onSplit() {
    if (!m_model) return;
    int row = m_table->currentRow();
    if (row < 0 || row >= m_model->rowCount()) return;
    const auto &e = m_model->entryAt(row);
    emit splitRequested(row, (e.startMs + e.endMs) / 2);
}

void SubtitleEditor::onMerge() {
    if (!m_model) return;
    auto selRows = m_table->selectionModel()->selectedRows();
    if (selRows.size() < 2) {
        QMessageBox::information(this, tr("Merge"),
            tr("Select two or more consecutive rows to merge."));
        return;
    }
    QList<int> rows;
    for (const auto &idx : selRows) rows << idx.row();
    std::sort(rows.begin(), rows.end());
    emit mergeRequested(rows.first(), rows.last());
}

void SubtitleEditor::onMoveUp() {
    if (!m_model) return;
    int row = m_table->currentRow();
    m_model->moveUp(row);
    if (row > 0) m_table->selectRow(row-1);
}

void SubtitleEditor::onMoveDown() {
    if (!m_model) return;
    int row = m_table->currentRow();
    m_model->moveDown(row);
    if (row < m_model->rowCount()-1) m_table->selectRow(row+1);
}

void SubtitleEditor::onShiftAll() {
    if (!m_model || m_model->rowCount() == 0) return;
    bool ok;
    int offset = QInputDialog::getInt(this,
        tr("Shift All Timecodes"),
        tr("Offset in milliseconds\n(positive = delay, negative = advance):"),
        0, -9999999, 9999999, 100, &ok);
    if (ok && offset != 0) m_model->shiftAll(qint64(offset));
}

void SubtitleEditor::onSearch() {
    if (!m_model) return;
    bool ok;
    QString query = QInputDialog::getText(this, tr("Search"),
        tr("Find text (supports regex):"), QLineEdit::Normal, {}, &ok);
    if (!ok || query.isEmpty()) return;
    QRegularExpression re(query, QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->entryAt(i).text.contains(re)) {
            m_table->selectRow(i);
            m_table->scrollToItem(m_table->item(i,0));
            return;
        }
    }
    QMessageBox::information(this, tr("Search"), tr("No match found."));
}

void SubtitleEditor::onReplace() {
    if (!m_model) return;
    bool ok;
    QString query = QInputDialog::getText(this, tr("Find & Replace"),
        tr("Find text (supports regex):"), QLineEdit::Normal, {}, &ok);
    if (!ok || query.isEmpty()) return;
    QString repl = QInputDialog::getText(this, tr("Find & Replace"),
        tr("Replace with:"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    QRegularExpression re(query, QRegularExpression::CaseInsensitiveOption);
    int count = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto &e = m_model->entryAt(i);
        QString n = e.text; n.replace(re, repl);
        if (n != e.text) { e.text = n; ++count; }
    }
    if (count > 0) {
        emit m_model->entriesChanged();
        QMessageBox::information(this, tr("Replace"),
            tr("Replaced %1 occurrence(s).").arg(count));
    } else {
        QMessageBox::information(this, tr("Replace"), tr("No match found."));
    }
}
