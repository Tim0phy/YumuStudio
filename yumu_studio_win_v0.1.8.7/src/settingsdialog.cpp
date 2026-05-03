#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QThread>
#include <QGroupBox>
#include <QStackedWidget>

// Helper: path picker row
static QWidget *makePathRow(QLineEdit *edit, const QString &title,
                             bool isExe, QWidget *parent)
{
    auto *w   = new QWidget(parent);
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(0,0,0,0);
    auto *btn = new QPushButton(QObject::tr("Browse…"), w);
    btn->setFixedWidth(80);
    row->addWidget(edit); row->addWidget(btn);
    QObject::connect(btn, &QPushButton::clicked, w, [edit, title, isExe]{
        QString p;
        if (isExe)
            p = QFileDialog::getOpenFileName(nullptr, title, edit->text(),
                    QObject::tr("Executable (*.exe);;All Files (*.*)"));
        else
            p = QFileDialog::getOpenFileName(nullptr, title, edit->text(),
                    QObject::tr("GGML Model (*.bin);;All Files (*.*)"));
        if (!p.isEmpty()) edit->setText(p);
    });
    return w;
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(580);
    buildUI();
    loadFromConfig();
}

void SettingsDialog::buildUI() {
    auto *root = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);

    // ── Tab 1: Paths ──────────────────────────────────────────────────────────
    {
        auto *tab  = new QWidget;
        auto *form = new QFormLayout(tab);
        form->setSpacing(10);

        m_ffmpegPath = new QLineEdit;
        m_ffmpegPath->setPlaceholderText(tr("e.g. C:\\ffmpeg\\bin\\ffmpeg.exe  (or just \"ffmpeg\" if in PATH)"));
        form->addRow(tr("FFmpeg path:"),
                     makePathRow(m_ffmpegPath, tr("Select ffmpeg.exe"), true, tab));

        m_modelPath = new QLineEdit;
        m_modelPath->setPlaceholderText(tr("e.g. E:\\whisper.cpp\\models\\ggml-medium.bin"));
        form->addRow(tr("Whisper model (.bin):"),
                     makePathRow(m_modelPath, tr("Select Whisper model"), false, tab));

        m_cliPath = new QLineEdit;
        m_cliPath->setPlaceholderText(tr("e.g. E:\\whisper.cpp\\build\\bin\\Release\\whisper-cli.exe"));
        form->addRow(tr("whisper-cli.exe:"),
                     makePathRow(m_cliPath, tr("Select whisper-cli.exe"), true, tab));

        auto *note = new QLabel(tr(
            "<small style='color:#888;'>"
            "Settings saved to<br>"
            "%APPDATA%\\YumuStudio\\settings.ini"
            "</small>"));
        note->setTextFormat(Qt::RichText);
        form->addRow("", note);
        tabs->addTab(tab, tr("Paths"));
    }

    // ── Tab 2: AI Recognition ─────────────────────────────────────────────────
    {
        auto *tab  = new QWidget;
        auto *form = new QFormLayout(tab);
        form->setSpacing(10);

        m_language = new QComboBox;
        m_language->addItems({"auto","zh","en","ja","ko","fr","de","es","it","pt","ru",
                               "ar","hi","nl","sv","pl","tr","vi","th","id"});
        form->addRow(tr("Language:"), m_language);

        m_threads = new QSpinBox;
        m_threads->setRange(1, QThread::idealThreadCount());
        m_threads->setValue(4);
        m_threads->setSuffix(tr(" threads"));
        form->addRow(tr("CPU Threads:"), m_threads);

        m_translate = new QCheckBox(tr("Translate to English (Whisper built-in)"));
        form->addRow("", m_translate);

        auto *hint = new QLabel(tr(
            "<small style='color:#888;'>"
            "Recommended models:<br>"
            "• ggml-small.bin — Fast, lower accuracy<br>"
            "• ggml-medium.bin — Balanced (recommended)<br>"
            "• ggml-large-v3.bin — Best accuracy, slow"
            "</small>"));
        hint->setTextFormat(Qt::RichText);
        form->addRow("", hint);
        tabs->addTab(tab, tr("AI Recognition"));
    }

    // ── Tab 3: Translation (v8.7: Gemini added) ───────────────────────────────
    {
        auto *tab      = new QWidget;
        auto *mainLay  = new QVBoxLayout(tab);
        auto *form     = new QFormLayout;
        form->setSpacing(10);

        m_backend = new QComboBox;
        m_backend->addItems({"OpenAI", "Anthropic Claude", "Ollama (Local)", "Google Gemini"});
        form->addRow(tr("Backend:"), m_backend);

        m_apiKey = new QLineEdit;
        m_apiKey->setEchoMode(QLineEdit::Password);
        m_apiKey->setPlaceholderText(tr("OpenAI / Anthropic / Gemini API Key"));
        form->addRow(tr("API Key:"), m_apiKey);

        m_targetLang = new QLineEdit;
        m_targetLang->setPlaceholderText("zh-TW");
        form->addRow(tr("Target Language:"), m_targetLang);

        mainLay->addLayout(form);

        // ── Ollama group ──────────────────────────────────────────────────────
        auto *ollamaGroup = new QGroupBox(tr("Ollama Settings"));
        auto *ollamaForm  = new QFormLayout(ollamaGroup);
        m_ollamaUrl = new QLineEdit;
        m_ollamaUrl->setPlaceholderText("http://localhost:11434");
        m_ollamaModel = new QComboBox;
        m_ollamaModel->setEditable(true);
        m_ollamaModel->addItems({"llama3","gemma3","mistral","qwen2.5","phi3","deepseek-r1"});
        ollamaForm->addRow(tr("Ollama URL:"),   m_ollamaUrl);
        ollamaForm->addRow(tr("Ollama Model:"), m_ollamaModel);
        mainLay->addWidget(ollamaGroup);

        // ── Gemini group (v8.7 NEW) ───────────────────────────────────────────
        auto *geminiGroup = new QGroupBox(tr("Google Gemini Settings"));
        auto *geminiForm  = new QFormLayout(geminiGroup);
        m_geminiModel = new QComboBox;
        m_geminiModel->setEditable(true);
        m_geminiModel->addItems({"gemini-2.0-flash","gemini-1.5-flash","gemini-1.5-pro"});
        geminiForm->addRow(tr("Gemini Model:"), m_geminiModel);
        auto *geminiNote = new QLabel(tr(
            "<small style='color:#888;'>"
            "Get API key at: "
            "<a href='https://aistudio.google.com/app/apikey' style='color:#5af;'>"
            "aistudio.google.com</a>"
            "</small>"));
        geminiNote->setTextFormat(Qt::RichText);
        geminiNote->setOpenExternalLinks(true);
        geminiForm->addRow("", geminiNote);
        mainLay->addWidget(geminiGroup);
        mainLay->addStretch();

        // Show/hide groups based on backend selection
        auto updateGroups = [ollamaGroup, geminiGroup, this](int idx){
            ollamaGroup->setVisible(idx == 2);
            geminiGroup->setVisible(idx == 3);
            m_apiKey->setEnabled(idx != 2);
        };
        connect(m_backend, QOverload<int>::of(&QComboBox::currentIndexChanged),
                tab, updateGroups);
        updateGroups(0);

        tabs->addTab(tab, tr("Translation"));
    }

    root->addWidget(tabs);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        auto &cfg = AppConfig::instance();
        cfg.paths             = paths();
        cfg.whisper           = whisperParams();
        cfg.translation       = translationParams();
        cfg.whisper.modelPath = cfg.paths.modelPath;
        cfg.whisper.cliPath   = cfg.paths.whisperCliPath;
        cfg.save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);
}

void SettingsDialog::loadFromConfig() {
    const auto &cfg = AppConfig::instance();
    m_ffmpegPath ->setText(cfg.paths.ffmpegPath);
    m_modelPath  ->setText(cfg.paths.modelPath);
    m_cliPath    ->setText(cfg.paths.whisperCliPath);
    m_language   ->setCurrentText(cfg.whisper.language.isEmpty() ? "auto" : cfg.whisper.language);
    m_threads    ->setValue(cfg.whisper.threads > 0 ? cfg.whisper.threads : 4);
    m_translate  ->setChecked(cfg.whisper.translate);
    m_backend    ->setCurrentIndex(static_cast<int>(cfg.translation.backend));
    m_apiKey     ->setText(cfg.translation.apiKey);
    m_targetLang ->setText(cfg.translation.targetLang.isEmpty() ? "zh-TW" : cfg.translation.targetLang);
    m_ollamaUrl  ->setText(cfg.translation.ollamaUrl.isEmpty()  ? "http://localhost:11434" : cfg.translation.ollamaUrl);
    m_ollamaModel->setCurrentText(cfg.translation.ollamaModel.isEmpty() ? "llama3" : cfg.translation.ollamaModel);
    m_geminiModel->setCurrentText(cfg.translation.geminiModel.isEmpty() ? "gemini-2.0-flash" : cfg.translation.geminiModel);
    // trigger visibility
    m_backend->currentIndexChanged(static_cast<int>(cfg.translation.backend));
}

AppPaths SettingsDialog::paths() const {
    return { m_ffmpegPath->text().trimmed(),
             m_cliPath->text().trimmed(),
             m_modelPath->text().trimmed() };
}

WhisperParams SettingsDialog::whisperParams() const {
    WhisperParams p;
    p.modelPath = m_modelPath->text().trimmed();
    p.cliPath   = m_cliPath->text().trimmed();
    p.language  = m_language->currentText();
    p.threads   = m_threads->value();
    p.translate = m_translate->isChecked();
    return p;
}

TranslationParams SettingsDialog::translationParams() const {
    TranslationParams p;
    p.backend     = static_cast<TranslationBackend>(m_backend->currentIndex());
    p.apiKey      = m_apiKey->text().trimmed();
    p.targetLang  = m_targetLang->text().trimmed();
    p.ollamaUrl   = m_ollamaUrl->text().trimmed();
    p.ollamaModel = m_ollamaModel->currentText().trimmed();
    p.geminiModel = m_geminiModel->currentText().trimmed();
    return p;
}
