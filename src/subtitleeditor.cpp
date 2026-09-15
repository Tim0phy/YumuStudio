#include "subtitleeditor.h"
#include "appconfig.h"
#include "learnedterms.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QShortcut>
#include <QRegularExpression>
#include <algorithm>

QString SubtitleEditor::msToDisplay(qint64 ms) {
    int h=int(ms/3600000); ms%=3600000;
    int m=int(ms/60000);   ms%=60000;
    int s=int(ms/1000);    ms%=1000;
    return QString("%1:%2:%3.%4")
        .arg(h,1).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ms,3,10,QChar('0'));
}

qint64 SubtitleEditor::displayToMs(const QString &str) {
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
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // Compact inline button bar replacing old text-action toolbar
    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(0, 0, 0, 0);
    btnBar->setSpacing(4);

    // Editorial ledger toolbar — small mono caps, hairline micro buttons
    auto mkBtn = [&](const QString &text, const QString &tip) -> QPushButton* {
        auto *b = new QPushButton(text);
        b->setObjectName("microBtn");
        b->setFlat(true);
        b->setToolTip(tip);
        b->setFixedHeight(24);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    auto *addBtn    = mkBtn(tr("＋ ADD"),    tr("Add subtitle (Ins)"));
    auto *delBtn    = mkBtn(tr("— DEL"), tr("Delete selected (Del)"));
    auto *splitBtn  = mkBtn(tr("SPLIT"),  tr("Split at midpoint"));
    auto *mergeBtn  = mkBtn(tr("MERGE"),  tr("Merge selected rows"));
    auto *upBtn     = mkBtn(tr("↑ UP"),     tr("Move up"));
    auto *downBtn   = mkBtn(tr("↓ DOWN"),   tr("Move down"));
    auto *shiftBtn  = mkBtn(tr("SHIFT"),  tr("Shift all timecodes by offset"));
    auto *searchBtn = mkBtn(tr("SEARCH"), tr("Search text (Ctrl+F)"));
    auto *replaceBtn= mkBtn(tr("REPLACE"),tr("Find & Replace (Ctrl+H)"));
    auto *correctBtn= mkBtn(tr("AI 校正"), tr("AI subtitle correction"));

    m_addBtn = addBtn; m_delBtn = delBtn; m_splitBtn = splitBtn; m_mergeBtn = mergeBtn;
    m_upBtn = upBtn; m_downBtn = downBtn; m_shiftBtn = shiftBtn;
    m_searchBtn = searchBtn; m_replaceBtn = replaceBtn; m_correctBtn = correctBtn;

    btnBar->addWidget(addBtn);
    btnBar->addWidget(delBtn);
    btnBar->addSpacing(8);
    btnBar->addWidget(splitBtn);
    btnBar->addWidget(mergeBtn);
    btnBar->addSpacing(8);
    btnBar->addWidget(upBtn);
    btnBar->addWidget(downBtn);
    btnBar->addSpacing(8);
    btnBar->addWidget(shiftBtn);
    btnBar->addSpacing(12);
    btnBar->addStretch();
    btnBar->addWidget(searchBtn);
    btnBar->addWidget(replaceBtn);
    btnBar->addWidget(correctBtn);

    connect(addBtn,    &QPushButton::clicked, this, &SubtitleEditor::onAdd);
    connect(delBtn,    &QPushButton::clicked, this, &SubtitleEditor::onDelete);
    connect(splitBtn,  &QPushButton::clicked, this, &SubtitleEditor::onSplit);
    connect(mergeBtn,  &QPushButton::clicked, this, &SubtitleEditor::onMerge);
    connect(upBtn,     &QPushButton::clicked, this, &SubtitleEditor::onMoveUp);
    connect(downBtn,   &QPushButton::clicked, this, &SubtitleEditor::onMoveDown);
    connect(shiftBtn,  &QPushButton::clicked, this, &SubtitleEditor::onShiftAll);
    connect(searchBtn, &QPushButton::clicked, this, &SubtitleEditor::onSearch);
    connect(replaceBtn,&QPushButton::clicked, this, &SubtitleEditor::onReplace);
    connect(correctBtn,&QPushButton::clicked, this, &SubtitleEditor::onCorrect);
    root->addLayout(btnBar);

    // Magazine ledger table — # folio, mono timecodes, body serif
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("№"), tr("IN —"), tr("OUT —"), tr("TEXT"), tr("TRANSLATION")});
    auto *hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::Fixed);
    hh->setDefaultSectionSize(44);
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(3, QHeaderView::Stretch);
    hh->setSectionResizeMode(4, QHeaderView::Stretch);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true); // ivory ledger stripes via QSS
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(36); // library manuscript ledger
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
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        if (!m_updating)
            emit selectionChanged(m_table->currentRow());
    });
}

void SubtitleEditor::retranslateUi() {
    if (!m_table) return;
    m_addBtn->setText(tr("＋ ADD"));       m_addBtn->setToolTip(tr("Add subtitle (Ins)"));
    m_delBtn->setText(tr("— DEL"));    m_delBtn->setToolTip(tr("Delete selected (Del)"));
    m_splitBtn->setText(tr("SPLIT"));   m_splitBtn->setToolTip(tr("Split at midpoint"));
    m_mergeBtn->setText(tr("MERGE"));   m_mergeBtn->setToolTip(tr("Merge selected rows"));
    m_upBtn->setText(tr("↑ UP"));         m_upBtn->setToolTip(tr("Move up"));
    m_downBtn->setText(tr("↓ DOWN"));     m_downBtn->setToolTip(tr("Move down"));
    m_shiftBtn->setText(tr("SHIFT"));   m_shiftBtn->setToolTip(tr("Shift all timecodes by offset"));
    m_searchBtn->setText(tr("SEARCH")); m_searchBtn->setToolTip(tr("Search text (Ctrl+F)"));
    m_replaceBtn->setText(tr("REPLACE")); m_replaceBtn->setToolTip(tr("Find & Replace (Ctrl+H)"));
    m_correctBtn->setText(tr("AI 校正")); m_correctBtn->setToolTip(tr("AI subtitle correction"));
    m_table->setHorizontalHeaderLabels({tr("№"), tr("IN —"), tr("OUT —"), tr("TEXT"), tr("TRANSLATION")});
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
    // 節流：與 previewFpsCap 同步，避免 4K 60fps 下每 tick 觸發 O(n) 掃描與選行導致 UI 掉幀
    {
        int fps = qBound(10, AppConfig::instance().preview.previewFpsCap, 60);
        int gate = qMax(40, 1000 / fps);
        if (m_highlightGate.isValid() && m_highlightGate.elapsed() < gate) {
            const int cur = m_table->currentRow();
            if (cur >= 0 && cur < m_model->rowCount()) {
                const auto &c = m_model->entryAt(cur);
                if (posMs >= c.startMs && posMs <= c.endMs) return;
            }
            // 門限內且已離開當前字幕段，跳過本次掃描以保流暢（最多延遲 gate ms）
            return;
        }
    }
    // Fast path: during playback this fires on every position tick — keep the
    // current highlight when it still covers the position instead of
    // re-selecting and re-scrolling the table each time.
    const int cur = m_table->currentRow();
    if (cur >= 0 && cur < m_model->rowCount()) {
        const auto &c = m_model->entryAt(cur);
        if (posMs >= c.startMs && posMs <= c.endMs) return;
    }
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const auto &e = m_model->entryAt(i);
        if (posMs >= e.startMs && posMs <= e.endMs) {
            m_highlightGate.restart();
            m_table->selectRow(i);
            m_table->scrollToItem(m_table->item(i, 0));
            return;
        }
    }
    // 未命中任何字幕段時也更新 gate，避免空掃描風暴
    m_highlightGate.restart();
}

void SubtitleEditor::onCellChanged(int row, int col) {
    if (m_updating || !m_model || row >= m_model->rowCount()) return;
    auto *item = m_table->item(row, col);
    if (!item) return;
    auto &e = m_model->entryAt(row);
    if (col == 1 || col == 2) {
        const qint64 parsed = displayToMs(item->text());
        const qint64 other = col == 1 ? e.endMs : e.startMs;
        const bool valid = parsed >= 0 && (col == 1 ? parsed < other : parsed > other);
        if (!valid) {
            m_updating = true;
            item->setText(col == 1 ? msToDisplay(e.startMs) : msToDisplay(e.endMs));
            m_updating = false;
            QMessageBox::warning(this, tr("Invalid timecode"),
                tr("The start time must be before the end time, and the timecode must be valid."));
            return;
        }
        m_updating = true;
        if (col == 1) e.startMs = parsed;
        else          e.endMs = parsed;
    } else if (col == 3) {
        m_updating = true;
        const QString oldText = e.text;
        e.text = item->text();
        LearnedTerms::recordObservation(oldText, e.text);
    } else if (col == 4) {
        m_updating = true;
        const QString oldText = e.translation;
        e.translation = item->text();
        LearnedTerms::recordObservation(oldText, e.translation);
    }
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
    if (!m_model) return;
    auto selRows = m_table->selectionModel()->selectedRows();
    if (selRows.isEmpty()) return;
    QList<int> rows;
    for (const auto &idx : selRows) rows << idx.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    if (rows.size() == 1) {
        auto r = QMessageBox::question(this, tr("Delete"),
            tr("Delete subtitle #%1?").arg(rows.first()+1), QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::Yes) emit deleteRequested(rows.first());
    } else {
        auto r = QMessageBox::question(this, tr("Delete"),
            tr("Delete %1 selected subtitles?").arg(rows.size()), QMessageBox::Yes | QMessageBox::No);
        if (r == QMessageBox::Yes) emit deleteRequested(rows);
    }
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
    for (int i = 1; i < rows.size(); ++i) {
        if (rows[i] != rows[i - 1] + 1) {
            QMessageBox::information(this, tr("Merge"),
                tr("Select consecutive rows to merge."));
            return;
        }
    }
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

void SubtitleEditor::onCorrect() {
    if (!m_model || m_model->rowCount() == 0) {
        QMessageBox::information(this, tr("Info"), tr("No subtitles to correct."));
        return;
    }
    emit correctionRequested();
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
