#pragma once
#include <QDialog>
#include <QList>
#include "subtitlemodel.h"

class QTableWidget;
class QPushButton;
class QCheckBox;
class QTableWidgetItem;

class TranslationPreviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit TranslationPreviewDialog(const QList<SubtitleEntry> &entries, QWidget *parent = nullptr);
    QList<SubtitleEntry> updatedEntries() const { return m_entries; }

private slots:
    void onExportClicked();

private:
    void buildUI();
    void loadEntries();
    void saveChanges();

    QList<SubtitleEntry> m_entries;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_editCb = nullptr;
    bool m_editable = false;
};