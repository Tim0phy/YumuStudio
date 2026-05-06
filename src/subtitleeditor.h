#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QLineEdit>
#include "subtitlemodel.h"

class SubtitleEditor : public QWidget {
    Q_OBJECT
public:
    explicit SubtitleEditor(QWidget *parent = nullptr);
    void setModel(SubtitleModel *model);

public slots:
    void highlightAtMs(qint64 posMs);
    void refreshTable();

signals:
    void jumpRequested(qint64 posMs);
    void splitRequested(int row, qint64 posMs);
    void mergeRequested(int r1, int r2);
    void deleteRequested(int row);

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
    void onCellChanged(int row, int col);
    void onCellDoubleClicked(int row, int col);

private:
    void buildUI();

    SubtitleModel  *m_model   = nullptr;
    QTableWidget   *m_table   = nullptr;
    QToolBar       *m_toolbar = nullptr;
    bool            m_updating= false;

    static QString msToDisplay(qint64 ms);
    static qint64  displayToMs(const QString &s);
};
