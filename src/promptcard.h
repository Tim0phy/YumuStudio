#pragma once
#include <QWidget>

class QTextEdit;
class QLabel;
class QPushButton;

// Right-card page: free-text transcription prompt. Unlike the glossary
// (term list), this is a natural-language hint sent to engines with a native
// prompt channel (whisper.cpp --prompt, faster-whisper initial_prompt).
// Other engines keep using glossary hotwords + post-processing.
class PromptCard : public QWidget {
    Q_OBJECT
public:
    explicit PromptCard(QWidget *parent = nullptr);
    void reload();
    void retranslateUi();

signals:
    void promptChanged();

private:
    void savePrompt();

    QTextEdit    *m_edit = nullptr;
    QLabel       *m_count = nullptr;
    QLabel       *m_title = nullptr;
    QLabel       *m_hint  = nullptr;
    QPushButton  *m_clearBtn = nullptr;
    QPushButton  *m_saveBtn = nullptr;
};
