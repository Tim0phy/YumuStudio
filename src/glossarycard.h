#pragma once
#include <QWidget>
#include <QList>
#include "glossary.h"

class QTableWidget;

// Right-card Page2: embedded creator glossary (moved out of modal dialog so
// the right toolbar card follows the 3-button switch).
class QLabel;
class QPushButton;

class GlossaryCard : public QWidget {
    Q_OBJECT
public:
    explicit GlossaryCard(QWidget *parent = nullptr);
    void reload();
    void retranslateUi();

signals:
    void glossaryChanged();

private:
    void addRow();
    void removeSelectedRows();
    void saveGlossary();

    QTableWidget *m_table = nullptr;
    QLabel       *m_title = nullptr;
    QLabel       *m_hint  = nullptr;
    QPushButton  *m_addBtn = nullptr;
    QPushButton  *m_delBtn = nullptr;
    QPushButton  *m_saveBtn = nullptr;
};
