#pragma once
#include <QDialog>
#include "appconfig.h"

class QLineEdit; class QSpinBox; class QComboBox; class QCheckBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    AppPaths          paths()             const;
    WhisperParams     whisperParams()     const;
    TranslationParams translationParams() const;

private:
    void buildUI();
    void loadFromConfig();

    // Paths
    QLineEdit *m_ffmpegPath  = nullptr;
    QLineEdit *m_modelPath   = nullptr;
    QLineEdit *m_cliPath     = nullptr;
    // Whisper
    QSpinBox  *m_threads     = nullptr;
    QComboBox *m_language    = nullptr;
    QCheckBox *m_translate   = nullptr;
    // Translation
    QComboBox *m_backend     = nullptr;
    QLineEdit *m_apiKey      = nullptr;
    QLineEdit *m_targetLang  = nullptr;
    QLineEdit *m_ollamaUrl   = nullptr;
    QComboBox *m_ollamaModel = nullptr;
    // v8.7 Gemini
    QComboBox *m_geminiModel = nullptr;
};
