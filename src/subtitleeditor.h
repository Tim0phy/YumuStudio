#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QElapsedTimer>
#include "subtitlemodel.h"

class QPushButton;

class SubtitleEditor : public QWidget {
    Q_OBJECT
public:
    explicit SubtitleEditor(QWidget *parent = nullptr);
    void setModel(SubtitleModel *model);

public slots:
    void highlightAtMs(qint64 posMs);
    void refreshTable();
    void retranslateUi();

signals:
    void jumpRequested(qint64 posMs);
    void splitRequested(int row, qint64 posMs);
    void mergeRequested(int r1, int r2);
    void deleteRequested(int row);
    void deleteRequested(const QList<int> &rows);
    void selectionChanged(int row);
    void correctionRequested();

private slots:
    void onAdd();
    void onDelete();
    void onSplit();
    void onMerge();
    void onMoveUp();
    void onMoveDown();
    void onShiftAll();      // v8.7
    void onSearch();        // v8.7
    void onReplace();       // v8.7
    void onCorrect();       // AI subtitle correction
    void onCellChanged(int row, int col);
    void onCellDoubleClicked(int row, int col);

private:
    void buildUI();

    SubtitleModel  *m_model   = nullptr;
    QTableWidget   *m_table   = nullptr;
    bool            m_updating= false;

    // Button bar (kept for retranslation on live language switch)
    QPushButton    *m_addBtn = nullptr;
    QPushButton    *m_delBtn = nullptr;
    QPushButton    *m_splitBtn = nullptr;
    QPushButton    *m_mergeBtn = nullptr;
    QPushButton    *m_upBtn = nullptr;
    QPushButton    *m_downBtn = nullptr;
    QPushButton    *m_shiftBtn = nullptr;
    QPushButton    *m_searchBtn = nullptr;
    QPushButton    *m_replaceBtn = nullptr;
    QPushButton    *m_correctBtn = nullptr;

    static QString msToDisplay(qint64 ms);
    static qint64  displayToMs(const QString &s);

    QElapsedTimer m_highlightGate;
};
