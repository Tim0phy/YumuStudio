#include "settingsdialog.h"
#include "localmodelmanager.h"
#include "backendinstaller.h"
#include "backendcatalog.h"
#include "hardwareprobe.h"
#include "uilanguage.h"
#include "uitheme.h"
#include "learnedterms.h"
#include "appupdatechecker.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QAbstractItemView>
#include <QPointer>
#include "stackanimator.h"

namespace {
QLabel *heading(const QString &text, QWidget *parent = nullptr) {
    auto *label = new QLabel(text, parent);
    label->setObjectName("settingsHeading");
    return label;
}

QLabel *muted(const QString &text, QWidget *parent = nullptr) {
    auto *label = new QLabel(text, parent);
    label->setObjectName("settingsMuted");
    label->setWordWrap(true);
    return label;
}

void addSeparator(QVBoxLayout *layout) {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("settingsSeparator");
    layout->addWidget(line);
}

void addToggle(QVBoxLayout *layout, const QString &title, const QString &description, QCheckBox *check) {
    auto *row = new QWidget;
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 2, 0, 2);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(new QLabel(title));
    text->addWidget(muted(description));
    line->addLayout(text, 1);
    line->addWidget(check);
    layout->addWidget(row);
}
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumSize(980, 680);
    setObjectName("settingsPage");
    m_models = new LocalModelManager(this);
    m_backendInstaller = new BackendInstaller(this);
    m_appUpdate = new AppUpdateChecker(this);
    buildUI();
    loadFromConfig();
}

QFrame *SettingsDialog::makeCard(const QString &title, const QString &description, QVBoxLayout **body) {
    auto *card = new QFrame;
    card->setObjectName("settingsCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    layout->addWidget(heading(title));
    if (!description.isEmpty()) layout->addWidget(muted(description));
    if (body) *body = layout;
    return card;
}

QWidget *SettingsDialog::makePathRow(QLineEdit *edit, const QString &title, bool isExe) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *button = new QPushButton(tr("Browse"));
    button->setObjectName("yumuBtnSecondary");
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, row, [edit, title, isExe] {
        QString path;
        if (isExe) {
            path = QFileDialog::getOpenFileName(nullptr, title, edit->text(),
                                                QObject::tr("Executable (*.exe);;All Files (*.*)"));
        } else {
            path = QFileDialog::getOpenFileName(nullptr, title, edit->text(),
                                                QObject::tr("Whisper model (*.bin);;All Files (*.*)"));
        }
        if (!path.isEmpty()) edit->setText(QDir::toNativeSeparators(path));
    });
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    return row;
}

QWidget *SettingsDialog::makeFolderRow(QLineEdit *edit, const QString &title) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton(tr("Browse"));
    browse->setObjectName("yumuBtnSecondary");
    browse->setCursor(Qt::PointingHandCursor);
    auto *open = new QPushButton(tr("Open folder"));
    open->setObjectName("yumuBtnSecondary");
    open->setCursor(Qt::PointingHandCursor);
    connect(browse, &QPushButton::clicked, row, [edit, title] {
        const QString path = QFileDialog::getExistingDirectory(nullptr, title, edit->text());
        if (!path.isEmpty()) edit->setText(QDir::toNativeSeparators(path));
    });
    connect(open, &QPushButton::clicked, row, [edit] {
        const QString path = edit->text().trimmed();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    layout->addWidget(open);
    return row;
}

void SettingsDialog::buildUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    // 12px gaps between body and button row; side/bottom padding lives on
    // the body layout and the button box so all cards sit 12px off the edge
    // exactly like the workspace cards.
    root->setSpacing(12);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(12, 12, 12, 0);
    body->setSpacing(0);

    auto *sidebar = new QFrame;
    sidebar->setObjectName("settingsSidebar");
    sidebar->setFixedWidth(210);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(18, 22, 18, 18);
    sideLayout->setSpacing(8);
    auto *brand = new QLabel("Yumu Studio");
    brand->setObjectName("settingsBrand");
    sideLayout->addWidget(brand);
    sideLayout->addWidget(muted(tr("Settings Center")));
    sideLayout->addSpacing(18);

    const struct { const char *label; } navItems[] = {
        {QT_TR_NOOP("一般")},
        {QT_TR_NOOP("引擎")},
        {QT_TR_NOOP("轉錄進階")},
        {QT_TR_NOOP("翻譯服務")},
        {QT_TR_NOOP("儲存與網絡")},
        {QT_TR_NOOP("About")},
    };
    for (int i = 0; i < 6; ++i) {
        auto *btn = new QPushButton(tr(navItems[i].label));
        btn->setObjectName("settingsNav");
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, i] { showPage(i); });
        m_navButtons.append(btn);
        sideLayout->addWidget(btn);
    }
    sideLayout->addStretch();
    sideLayout->addWidget(muted(AppConfig::isPortableMode()
        ? tr("Portable mode\nSettings are saved in the config folder beside the app")
        : tr("Standard mode\nSettings are saved in the user's configuration folder")));
    body->addWidget(sidebar);
    auto *settingsDivider = new QFrame;
    settingsDivider->setObjectName("settingsDivider");
    settingsDivider->setFrameShape(QFrame::NoFrame);
    body->addWidget(settingsDivider);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildEnginePage());
    QWidget *transcriptionPage = buildTranscriptionPage();
    QWidget *translationPage = buildTranslationPage();
    QWidget *storageNetworkPage = buildStorageNetworkPage();
    m_pages->addWidget(transcriptionPage);
    m_pages->addWidget(translationPage);
    m_pages->addWidget(storageNetworkPage);
    m_pages->addWidget(buildAboutPage());
    connect(m_modelRoot, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_storageRoot->text() != text) {
            const QSignalBlocker blocker(m_storageRoot);
            m_storageRoot->setText(text);
        }
    });
    connect(m_storageRoot, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_modelRoot->text() != text) {
            const QSignalBlocker blocker(m_modelRoot);
            m_modelRoot->setText(text);
        }
    });
    body->addWidget(m_pages, 1);
    root->addLayout(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save settings"));
    buttons->button(QDialogButtonBox::Save)->setObjectName("yumuBtn");
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("yumuBtnSecondary");
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (buttons->layout()) buttons->layout()->setContentsMargins(12, 0, 12, 12);
    root->addWidget(buttons);

    showPage(GeneralPageIndex);
}

QWidget *SettingsDialog::buildEnginePage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(12);

    auto *top = new QHBoxLayout;
    top->addWidget(heading(tr("Engines")));
    top->addStretch();
    m_checkUpdatesButton = new QPushButton(tr("Check Engine Updates"));
    m_checkUpdatesButton->setObjectName("yumuBtnSecondary");
    m_checkUpdatesButton->setCursor(Qt::PointingHandCursor);
    top->addWidget(m_checkUpdatesButton);
    outer->addLayout(top);
    outer->addWidget(muted(tr("Manage local transcription engines, runtimes and models. Each engine can use multiple models independently.")));

    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);

    auto *engineSide = new QFrame;
    engineSide->setObjectName("engineChooser");
    engineSide->setMinimumWidth(190);
    engineSide->setMaximumWidth(245);
    auto *sideLayout = new QVBoxLayout(engineSide);
    sideLayout->setContentsMargins(12, 12, 12, 12);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(new QLabel(tr("Local Engines")));
    m_engineList = new QListWidget;
    m_engineList->setObjectName("engineList");
    m_engineList->setSpacing(3);
    for (const auto &engine : LocalModelManager::engines()) {
        m_engineInfo.insert(engine.id, engine);
        auto *item = new QListWidgetItem(engine.name, m_engineList);
        item->setData(Qt::UserRole, engine.id);
        item->setToolTip(engine.description);
    }
    sideLayout->addWidget(m_engineList, 1);
    split->addWidget(engineSide);

    auto *rightScroll = new QScrollArea;
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(12);

    auto *engineHeader = new QFrame;
    engineHeader->setObjectName("engineHeaderCard");
    auto *headerLayout = new QVBoxLayout(engineHeader);
    headerLayout->setContentsMargins(18, 16, 18, 16);
    auto *titleRow = new QHBoxLayout;
    m_engineTitle = heading(tr("whisper.cpp (built-in)"));
    titleRow->addWidget(m_engineTitle);
    titleRow->addStretch();
    m_engineStatus = new QLabel(tr("Available"));
    m_engineStatus->setObjectName("engineAvailable");
    titleRow->addWidget(m_engineStatus);
    headerLayout->addLayout(titleRow);
    m_engineDescription = muted(QString());
    headerLayout->addWidget(m_engineDescription);

    auto *versionRow = new QHBoxLayout;
    m_engineVersion = new QLabel;
    m_engineUpdateStatus = new QLabel;
    m_engineUpdateStatus->setObjectName("settingsMuted");
    m_upgradeButton = new QPushButton(tr("Upgrade"));
    m_upgradeButton->setObjectName("yumuBtn");
    m_upgradeButton->setCursor(Qt::PointingHandCursor);
    m_upgradeButton->setVisible(false);
    versionRow->addWidget(m_engineVersion);
    versionRow->addWidget(m_engineUpdateStatus, 1);
    versionRow->addWidget(m_upgradeButton);
    headerLayout->addLayout(versionRow);
    layout->addWidget(engineHeader);
    auto *backendCard = new QFrame;
    backendCard->setObjectName("statusCard");
    auto *backendLayout = new QVBoxLayout(backendCard);
    backendLayout->addWidget(new QLabel(tr("Backend Variants")));
    backendLayout->addWidget(muted(tr("Each variant installs independently; multiple variants can coexist and tasks run with isolated commands without blocking.")));
    // 移除舊下拉（紅色圈），所有引擎統一以卡片列表展示（藍色圈）
    m_installBackend = nullptr;
    auto *variantsContainer = new QWidget;
    m_backendVariantsLayout = new QVBoxLayout(variantsContainer);
    m_backendVariantsLayout->setContentsMargins(0, 6, 0, 0);
    m_backendVariantsLayout->setSpacing(6);
    backendLayout->addWidget(variantsContainer);
    m_backendLog = new QPlainTextEdit;
    m_backendLog->setReadOnly(true);
    m_backendLog->setMaximumHeight(150);
    m_backendLog->setMaximumWidth(600);
    m_backendLog->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_backendLog->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_backendLog->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_backendLog->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_backendLog->setPlaceholderText(tr("Install commands and progress appear here (isolated per engine; switching engines shows only that engine's log)."));
    backendLayout->addWidget(m_backendLog);
    layout->addWidget(backendCard);

    auto *runtime = new QFrame;
    runtime->setObjectName("statusCard");
    auto *runtimeLayout = new QHBoxLayout(runtime);
    runtimeLayout->setContentsMargins(14, 10, 14, 10);
    runtimeLayout->addWidget(new QLabel(tr("⚡ Current Runtime")));
    runtimeLayout->addWidget(new QLabel(tr("Pick the CPU, CUDA or Vulkan backend used for transcription")), 1);
    m_computeDevice = new QComboBox;
    m_computeDevice->addItem(tr("Auto"), "auto");
    m_computeDevice->addItem(tr("CPU"), "cpu");
    m_computeDevice->addItem(tr("GPU / CUDA"), "cuda");
    m_computeDevice->addItem(tr("GPU / Vulkan"), "vulkan");
    m_computeDevice->setToolTip(tr("Vulkan is provided by whisper.cpp/ggml; other local engines use CPU or CUDA."));
    m_computeDevice->setMaximumWidth(170);
    m_precision = new QComboBox;
    m_precision->addItem(tr("Auto (recommended)"), "auto");
    m_precision->addItem("int8", "int8");
    m_precision->addItem("float16", "float16");
    m_precision->addItem("float32", "float32");
    m_precision->setMaximumWidth(150);
    runtimeLayout->addWidget(m_computeDevice);
    runtimeLayout->addWidget(m_precision);
    layout->addWidget(runtime);

    auto *filterRow = new QHBoxLayout;
    m_modelSearch = new QLineEdit;
    m_modelSearch->setPlaceholderText(tr("Search model names or descriptions"));
    m_onlyInstalled = new QCheckBox(tr("Show installed only"));
    filterRow->addWidget(m_modelSearch, 1);
    filterRow->addWidget(m_onlyInstalled);
    layout->addLayout(filterRow);

    auto *modelPathRow = new QHBoxLayout;
    modelPathRow->addWidget(new QLabel(tr("Model Path")));
    m_modelRoot = new QLineEdit;
    m_modelRoot->setPlaceholderText(LocalModelManager::defaultRoot());
    modelPathRow->addWidget(m_modelRoot, 1);
    auto *openModels = new QPushButton(tr("Open folder"));
    openModels->setObjectName("yumuBtnSecondary");
    openModels->setCursor(Qt::PointingHandCursor);
    connect(openModels, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_modelRoot->text().trimmed()));
    });
    modelPathRow->addWidget(openModels);
    m_modelStatus = new QLabel(tr("Ready"));
    modelPathRow->addWidget(m_modelStatus);
    m_cancelDownload = new QPushButton(tr("Cancel download"));
    m_cancelDownload->setObjectName("yumuBtnSecondary");
    m_cancelDownload->setCursor(Qt::PointingHandCursor);
    m_cancelDownload->setVisible(false);
    connect(m_cancelDownload, &QPushButton::clicked, this, [this] {
        m_models->cancel();
        m_downloadingId.clear();
        m_cancelDownload->setVisible(false);
        m_modelStatus->setText(tr("Cancelled"));
        rebuildModelList();
    });
    modelPathRow->addWidget(m_cancelDownload);
    layout->addLayout(modelPathRow);

    auto *modelListFrame = new QFrame;
    modelListFrame->setObjectName("modelListFrame");
    m_modelListLayout = new QVBoxLayout(modelListFrame);
    m_modelListLayout->setContentsMargins(12, 10, 12, 10);
    m_modelListLayout->setSpacing(7);
    layout->addWidget(modelListFrame);

    QVBoxLayout *advancedBody = nullptr;
    auto *advanced = makeCard(tr("Advanced Settings"), tr("Engine paths and local runtime configuration."), &advancedBody);
    m_cliPath = new QLineEdit;
    m_cliPath->setPlaceholderText(tr("Portable bin\\whisper-cli.exe or system whisper-cli"));
    advancedBody->addWidget(new QLabel(tr("whisper-cli.exe")));
    advancedBody->addWidget(makePathRow(m_cliPath, tr("Select whisper-cli.exe"), true));
    m_runnerPath = new QLineEdit;
    m_runnerPath->setPlaceholderText(tr("Portable runtime\\asr-venv\\Scripts\\python.exe"));
    advancedBody->addWidget(new QLabel(tr("Local Engine Runner")));
    advancedBody->addWidget(makePathRow(m_runnerPath, tr("Select Python runner"), true));
    m_modelPath = new QLineEdit;
    m_modelPath->setPlaceholderText(tr("Whisper model file (can be filled from the model folder)"));
    advancedBody->addWidget(new QLabel(tr("whisper.cpp Model File")));
    advancedBody->addWidget(makePathRow(m_modelPath, tr("Select Whisper model"), false));
    layout->addWidget(advanced);
    layout->addStretch();

    rightScroll->setWidget(content);
    split->addWidget(rightScroll);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    m_engine = new QComboBox(page);
    for (const auto &engine : LocalModelManager::engines()) m_engine->addItem(engine.id);
    m_engine->setVisible(false);
    m_model = new QComboBox(page);
    m_model->setVisible(false);
    m_downloadModel = new QPushButton(page);
    m_downloadModel->setObjectName("yumuBtn");
    m_downloadModel->setCursor(Qt::PointingHandCursor);
    m_downloadModel->setVisible(false);
    connect(m_engineList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !m_engineList->item(row)) return;
        m_selectedEngine = m_engineList->item(row)->data(Qt::UserRole).toString();
        m_engine->setCurrentText(m_selectedEngine);
        // 方案 A：按引擎隔離命令行日誌，切換引擎時僅顯示該引擎的日誌
        if (m_backendLog) {
            m_backendLog->clear();
            const QString engKey = m_selectedEngine.toLower();
            if (m_backendLogBuffers.contains(engKey) && !m_backendLogBuffers[engKey].isEmpty()) {
                m_backendLog->setPlainText(m_backendLogBuffers[engKey]);
                QTextCursor c = m_backendLog->textCursor();
                c.movePosition(QTextCursor::End);
                m_backendLog->setTextCursor(c);
            }
        }
        refreshEngineDetails();
    });
    // Search debounce: typing should not rebuild on every keystroke synchronously.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(120);
    connect(m_searchDebounce, &QTimer::timeout, this, &SettingsDialog::rebuildModelList);
    connect(m_modelSearch, &QLineEdit::textChanged, this, [this]{ if (m_searchDebounce) m_searchDebounce->start(); });
    connect(m_onlyInstalled, &QCheckBox::toggled, this, &SettingsDialog::rebuildModelList);
    connect(m_engine, &QComboBox::currentTextChanged, this, [this] { refreshModelChoices(); refreshLanguageChoices(); });
    connect(m_model, &QComboBox::currentTextChanged, this, [this] { refreshModelChoices(); refreshLanguageChoices(); rebuildModelList(); });
    connect(m_downloadModel, &QPushButton::clicked, this, &SettingsDialog::downloadSelectedModel);
    connect(m_checkUpdatesButton, &QPushButton::clicked, this, &SettingsDialog::checkEngineUpdates);
    // 舊下拉已移除，無需連接 m_installBackend
    // 舊全局 logLine/progress（無 engine 上下文）不再直接顯示，避免與按引擎隔離的日誌重複
    // 按引擎隔離的日誌：僅當前選中引擎可見，其餘緩存於 m_backendLogBuffers
    connect(m_backendInstaller, &BackendInstaller::logLineDetailed, this, [this](const QString &engine, const QString &variant, const QString &line) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) return;
        const QString engKey = engine.toLower();
        QString &buf = m_backendLogBuffers[engKey];
        if (!buf.isEmpty()) buf += QLatin1Char('\n');
        buf += trimmed;
        if (buf.size() > 30000) buf = buf.right(25000);
        if (engine.compare(m_selectedEngine, Qt::CaseInsensitive) == 0 && m_backendLog) {
            m_backendLog->appendPlainText(trimmed);
        }
    });
    // 取消綠色進度框，僅以文字百分比更新狀態（需求 2）
    connect(m_backendInstaller, &BackendInstaller::progressDetailed, this, [this](const QString &engine, const QString &variant, int percent, const QString &stage) {
        const QString key = engine.toLower() + "/" + variant.toLower();
        if (m_backendVariantStatus.contains(key)) {
            m_backendVariantStatus[key]->setText(QStringLiteral("%1 (%2%)").arg(stage).arg(percent));
        }
    });
    connect(m_backendInstaller, &BackendInstaller::finished, this, [this](const QString &engine, const QString &variant, bool ok, const QString &message) {
        if (ok) {
            BackendCatalog::activateVariant(engine, variant);
            // 後端版本卡牌啟用後，自動同步下方「目前執行環境」卡牌
            if (m_computeDevice) {
                const QSignalBlocker blocker(m_computeDevice);
                int idx = m_computeDevice->findData(variant);
                if (idx >= 0) m_computeDevice->setCurrentIndex(idx);
            }
            const QString engKey = engine.toLower();
            QString &buf = m_backendLogBuffers[engKey];
            const QString doneLine = tr("Switched to %1 · %2").arg(engine, BackendCatalog::variantLabel(variant));
            if (!buf.isEmpty()) buf += QLatin1Char('\n');
            buf += doneLine;
            if (engine.compare(m_selectedEngine, Qt::CaseInsensitive) == 0 && m_backendLog) {
                m_backendLog->appendPlainText(doneLine);
            }
        } else {
            const QString engKey = engine.toLower();
            QString &buf = m_backendLogBuffers[engKey];
            const QString failLine = message.left(120);
            if (!buf.isEmpty()) buf += QLatin1Char('\n');
            buf += failLine;
            if (engine.compare(m_selectedEngine, Qt::CaseInsensitive) == 0 && m_backendLog) {
                m_backendLog->appendPlainText(failLine);
            }
        }
        refreshBackendChoices();
        refreshEngineDetails();
    });
    connect(m_upgradeButton, &QPushButton::clicked, this, [this] {
        if (m_engineInfo.contains(m_selectedEngine))
            QDesktopServices::openUrl(m_engineInfo.value(m_selectedEngine).releaseUrl);
    });
    connect(m_models, &LocalModelManager::progress, this,
            [this](const QString &, const QString &id, qint64 got, qint64 total) {
                m_modelStatus->setText(total > 0 ? tr("Downloading %1%").arg(got * 100 / total) : tr("Downloading"));
                Q_UNUSED(id);
            });
    connect(m_models, &LocalModelManager::finished, this,
            [this](const QString &, const QString &, bool ok, const QString &message) {
                m_downloadingId.clear();
                if (m_cancelDownload) m_cancelDownload->setVisible(false);
                m_modelStatus->setText(ok ? tr("Installed") : message);
                refreshModelChoices();
                rebuildModelList();
            });
    connect(m_models, &LocalModelManager::engineUpdateFinished, this,
            [this](const QString &engine, bool ok, const QString &, const QString &latest, bool update, const QString &message) {
                if (m_pendingEngineChecks > 0 && --m_pendingEngineChecks == 0) {
                    m_checkUpdatesButton->setEnabled(true);
                    m_checkUpdatesButton->setText(tr("Check Engine Updates"));
                }
                if (!ok) {
                    if (engine == m_selectedEngine) m_engineUpdateStatus->setText(tr("Check failed: %1").arg(message));
                    return;
                }
                m_latestEngineVersions.insert(engine, latest);
                m_engineUpdates.insert(engine, update);
                if (engine == m_selectedEngine) refreshEngineDetails();
            });
    return page;
}

QWidget *SettingsDialog::buildGeneralPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);
    outer->addWidget(heading(tr("General")));
    outer->addWidget(muted(tr("Language & Startup")));

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(14);

    QVBoxLayout *systemBody = nullptr;
    auto *systemCard = makeCard(tr("System Settings"), tr("Language & Startup"), &systemBody);
    auto *languageRow = new QHBoxLayout;
    languageRow->addWidget(new QLabel(tr("Interface Language")));
    m_uiLanguage = new QComboBox;
    // UI-language entries always show their own native names (繁體中文 /
    // English), independent of the currently active interface language.
    m_uiLanguage->addItem(QStringLiteral("繁體中文"), "zh");
    m_uiLanguage->addItem(QStringLiteral("English"), "en");
    languageRow->addWidget(m_uiLanguage);
    systemBody->addLayout(languageRow);
    auto *themeRow = new QHBoxLayout;
    themeRow->addWidget(new QLabel(tr("Appearance")));
    m_themeCombo = new QComboBox;
    m_themeCombo->addItem(tr("Dark"), "dark");
    m_themeCombo->addItem(tr("Light"), "light");
    themeRow->addWidget(m_themeCombo);
    systemBody->addLayout(themeRow);
    // Live preview: switching the theme applies instantly and persists.
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        const QString theme = m_themeCombo->currentData().toString();
        if (theme == AppConfig::instance().system.theme) return;
        AppConfig::instance().system.theme = theme;
        AppConfig::instance().save();
        UiTheme::applyTheme(qApp, theme);
        emit appearanceChanged();
    });
    m_checkUpdates = new QCheckBox;
    addToggle(systemBody, tr("Check for engine updates at startup"), tr("Check for available transcription engine updates when the app starts"), m_checkUpdates);
    m_checkAppUpdates = new QCheckBox;
    addToggle(systemBody, tr("Check for app updates at startup"), tr("Check whether a newer Yumu Studio version is available on GitHub Releases"), m_checkAppUpdates);
    m_preventSleep = new QCheckBox;
    addToggle(systemBody, tr("Prevent sleep while a task is running"), tr("Keep the computer awake during transcription, translation or export"), m_preventSleep);
    m_learnMistakes = new QCheckBox;
    addToggle(systemBody, tr("Learn frequent corrections"), tr("Remember words you correct often and auto-add them to the creator glossary"), m_learnMistakes);
    auto *thresholdRow = new QHBoxLayout;
    thresholdRow->addWidget(new QLabel(tr("Auto-promote after")));
    m_mistakeThreshold = new QSpinBox;
    m_mistakeThreshold->setRange(1, 50);
    thresholdRow->addWidget(m_mistakeThreshold);
    thresholdRow->addWidget(new QLabel(tr("corrections")));
    thresholdRow->addStretch();
    systemBody->addLayout(thresholdRow);
    auto *promoteNow = new QPushButton(tr("Promote now"));
    promoteNow->setObjectName("yumuBtnSecondary");
    promoteNow->setCursor(Qt::PointingHandCursor);
    connect(promoteNow, &QPushButton::clicked, this, [this] {
        const int threshold = m_mistakeThreshold->value();
        const auto promoted = LearnedTerms::autoPromote(threshold);
        if (promoted.isEmpty()) {
            QMessageBox::information(this, tr("Learned Terms"), tr("No new terms reached the threshold."));
        } else {
            QStringList lines;
            for (const auto &p : promoted) lines << tr("%1 → %2").arg(p.error, p.correction);
            QMessageBox::information(this, tr("Learned Terms"),
                tr("Added %1 term(s) to the glossary:\n%2").arg(promoted.size()).arg(lines.join("\n")));
        }
    });
    systemBody->addWidget(promoteNow, 0, Qt::AlignLeft);
    addSeparator(systemBody);
    systemBody->addWidget(new QLabel(tr("GitHub Repository for App Updates")));
    auto *repoLink = new QLabel(QStringLiteral("<a href=\"https://github.com/Tim0phy/YumuStudio\">github.com/Tim0phy/YumuStudio</a>"));
    repoLink->setObjectName("settingsMuted");
    repoLink->setOpenExternalLinks(true);
    systemBody->addWidget(repoLink);
    auto *checkNow = new QPushButton(tr("Check now"));
    checkNow->setObjectName("yumuBtnSecondary");
    checkNow->setCursor(Qt::PointingHandCursor);
    connect(m_appUpdate, &AppUpdateChecker::checkFinished, this,
            [this, checkNow](bool ok, bool updateAvailable, const QString &latestVersion,
                   const QString &releaseUrl, const QString &error) {
        if (checkNow) checkNow->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(this, tr("Update Check"),
                error.isEmpty() ? tr("Failed to check for updates.") : error);
            return;
        }
        if (!updateAvailable) {
            QMessageBox::information(this, tr("Update Check"),
                tr("You are running the latest version (%1).").arg(QStringLiteral(APP_VERSION)));
            return;
        }
        auto *box = new QMessageBox(this);
        box->setWindowTitle(tr("Update Available"));
        box->setText(tr("Yumu Studio %1 is available.\n\nYou are currently running %2.")
                     .arg(latestVersion, QStringLiteral(APP_VERSION)));
        box->setInformativeText(tr("Would you like to open the release page and download it now?"));
        box->setIcon(QMessageBox::Information);
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->button(QMessageBox::Yes)->setText(tr("Go to Download"));
        box->button(QMessageBox::No)->setText(tr("Later"));
        box->setDefaultButton(QMessageBox::Yes);
        connect(box, &QMessageBox::finished, this, [box, releaseUrl](int result) {
            if (result == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(releaseUrl));
            box->deleteLater();
        });
        box->show();
    });
    connect(checkNow, &QPushButton::clicked, this, [this, checkNow] {
        checkNow->setEnabled(false); // Prevent overlapping checks while in flight.
        m_appUpdate->check(AppConfig::instance().update.repoOwner,
                           AppConfig::instance().update.repoName);
    });
    systemBody->addWidget(checkNow, 0, Qt::AlignLeft);
    m_skippedVersionLabel = new QLabel;
    m_skippedVersionLabel->setObjectName("settingsMuted");
    systemBody->addWidget(m_skippedVersionLabel);
    m_clearSkippedBtn = new QPushButton(tr("Clear skipped version"));
    m_clearSkippedBtn->setObjectName("yumuBtnSecondary");
    m_clearSkippedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearSkippedBtn, &QPushButton::clicked, this, [this] {
        AppConfig::instance().update.skippedVersion.clear();
        AppConfig::instance().save();
        if (m_skippedVersionLabel) m_skippedVersionLabel->setText({});
        m_clearSkippedBtn->setVisible(false);
    });
    systemBody->addWidget(m_clearSkippedBtn, 0, Qt::AlignLeft);
    layout->addWidget(systemCard);

    // Preview performance card (A+B方案)
    QVBoxLayout *previewBody = nullptr;
    auto *previewCard = makeCard(tr("Preview Performance"), tr("Affects preview smoothness only, not final export quality. Low-res preview and GPU hardware decoding greatly reduce 4K stutter."), &previewBody);
    m_previewLowRes = new QCheckBox;
    addToggle(previewBody, tr("Low-res Preview"), tr("Preview scales to the chosen width; CPU usage and GPU readback drop significantly"), m_previewLowRes);
    auto *widthRow = new QHBoxLayout;
    widthRow->addWidget(new QLabel(tr("Preview Resolution")));
    m_previewWidthCombo = new QComboBox;
    m_previewWidthCombo->addItem("360p", 360);
    m_previewWidthCombo->addItem("480p", 480);
    m_previewWidthCombo->addItem(tr("640p (Recommended)"), 640);
    m_previewWidthCombo->addItem("720p", 720);
    m_previewWidthCombo->addItem("960p", 960);
    m_previewWidthCombo->addItem("1024p", 1024);
    m_previewWidthCombo->addItem(tr("Original"), 0);
    widthRow->addWidget(m_previewWidthCombo);
    previewBody->addLayout(widthRow);
    auto *fpsRow = new QHBoxLayout;
    fpsRow->addWidget(new QLabel(tr("FPS Cap")));
    m_previewFpsCombo = new QComboBox;
    m_previewFpsCombo->addItem("15 fps", 15);
    m_previewFpsCombo->addItem(tr("24 fps (Recommended)"), 24);
    m_previewFpsCombo->addItem("30 fps", 30);
    fpsRow->addWidget(m_previewFpsCombo);
    previewBody->addLayout(fpsRow);
    m_previewHardware = new QCheckBox;
    addToggle(previewBody, tr("Hardware Passthrough (GPU Hardware Decoding)"), tr("When enabled, uses GPU hardware decoding (d3d11va/d3d12va/dxva2); when disabled, forces CPU software decoding. Automatic fallback; subtitle dragging always works."), m_previewHardware);
    m_previewHardwareStatus = muted(tr("Detecting GPU..."));
    m_previewHardwareStatus->setObjectName("settingsMuted");
    m_previewHardwareStatus->setWordWrap(true);
    previewBody->addWidget(m_previewHardwareStatus);
    layout->addWidget(previewCard);
    connect(m_previewLowRes, &QCheckBox::toggled, this, [this](bool on){
        if (m_previewWidthCombo) m_previewWidthCombo->setEnabled(on);
        AppConfig::instance().preview.lowResPreview = on;
        AppConfig::instance().save();
        emit previewConfigChanged();
    });
    connect(m_previewWidthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
        if (idx < 0) return;
        int w = m_previewWidthCombo->itemData(idx).toInt();
        if (w == 0) {
            if (m_previewLowRes) {
                const QSignalBlocker b(m_previewLowRes);
                m_previewLowRes->setChecked(false);
            }
            m_previewWidthCombo->setEnabled(false);
            AppConfig::instance().preview.lowResPreview = false;
            AppConfig::instance().preview.previewWidth = 640;
        } else {
            if (m_previewLowRes && !m_previewLowRes->isChecked()) {
                const QSignalBlocker b(m_previewLowRes);
                m_previewLowRes->setChecked(true);
            }
            m_previewWidthCombo->setEnabled(true);
            AppConfig::instance().preview.previewWidth = w;
            AppConfig::instance().preview.lowResPreview = true;
        }
        AppConfig::instance().save();
        emit previewConfigChanged();
    });
    connect(m_previewFpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){
        AppConfig::instance().preview.previewFpsCap = m_previewFpsCombo->currentData().toInt();
        AppConfig::instance().save();
        emit previewConfigChanged();
    });
    connect(m_previewHardware, &QCheckBox::toggled, this, [this](bool on){
        AppConfig::instance().preview.hardwarePreview = on;
        AppConfig::instance().save();
        // 立即刷新狀態文字，無需等待外部 sync
        updateHardwareStatusText(on);
        emit previewConfigChanged();
    });
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

void SettingsDialog::updateHardwareStatusText(bool hwOn) {
    if (!m_previewHardwareStatus) return;
    const bool hasNvidia = HardwareProbe::hasNvidiaGpuQuick();
    const bool hasVulkan = HardwareProbe::vulkanRuntimeQuick();
    QString status;
    if (hwOn) {
        if (hasNvidia) status = tr("Current: d3d11va enabled (NVIDIA GPU detected, d3d11va/dxva2 preferred)");
        else if (hasVulkan) status = tr("Current: d3d11va enabled (Vulkan runtime detected)");
        else status = tr("Current: d3d11va enabled (no discrete GPU, will fallback to CPU if decoding fails)");
    } else {
        if (hasNvidia || hasVulkan) status = tr("Current: CPU software decoding (GPU available, enable hardware decoding above)");
        else status = tr("Current: CPU software decoding (no GPU detected)");
    }
    m_previewHardwareStatus->setText(status);
}

void SettingsDialog::syncPreviewControls() {
    const auto &cfg = AppConfig::instance().preview;
    if (m_previewLowRes) {
        const QSignalBlocker b(m_previewLowRes);
        m_previewLowRes->setChecked(cfg.lowResPreview);
    }
    if (m_previewWidthCombo) {
        const QSignalBlocker b(m_previewWidthCombo);
        int idx = m_previewWidthCombo->findData(cfg.lowResPreview ? cfg.previewWidth : 0);
        if (idx < 0) idx = m_previewWidthCombo->findData(640);
        m_previewWidthCombo->setCurrentIndex(qMax(0, idx));
        m_previewWidthCombo->setEnabled(cfg.lowResPreview);
    }
    if (m_previewFpsCombo) {
        const QSignalBlocker b(m_previewFpsCombo);
        int idx = m_previewFpsCombo->findData(cfg.previewFpsCap);
        if (idx >= 0) m_previewFpsCombo->setCurrentIndex(idx);
    }
    if (m_previewHardware) {
        const QSignalBlocker b(m_previewHardware);
        m_previewHardware->setChecked(cfg.hardwarePreview);
    }
    updateHardwareStatusText(cfg.hardwarePreview);
}

QWidget *SettingsDialog::buildTranscriptionPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);
    outer->addWidget(heading(tr("Transcription")));
    outer->addWidget(muted(tr("Controls how whisper.cpp and the local runner recognize audio.")));

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(14);

    QVBoxLayout *recognitionBody = nullptr;
    auto *recognitionCard = makeCard(tr("Transcription"), tr("Controls how whisper.cpp and the local runner recognize audio."), &recognitionBody);
    auto *language = new QHBoxLayout;
    language->addWidget(new QLabel(tr("Recognition Language")));
    m_language = new QComboBox;
    m_language->setObjectName("recognitionLanguageCombo");
    m_language->setMinimumWidth(180);
    m_language->setMinimumContentsLength(10);
    m_language->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_language->setMaxVisibleItems(10);
    m_language->view()->setMinimumWidth(180);
    m_language->view()->setMinimumHeight(64);
    m_language->addItem(tr(LocalModelManager::languageDisplayName("auto").toUtf8().constData()), "auto");
    language->addWidget(m_language);
    recognitionBody->addLayout(language);
    auto *threads = new QHBoxLayout;
    threads->addWidget(new QLabel(tr("CPU Threads")));
    m_threads = new QSpinBox;
    m_threads->setRange(1, qMax(1, QThread::idealThreadCount()));
    threads->addWidget(m_threads);
    recognitionBody->addLayout(threads);
    m_translate = new QCheckBox(tr("Translate to English (Whisper built-in)"));
    recognitionBody->addWidget(m_translate);
    m_useGlossary = new QCheckBox(tr("Use Creator Glossary"));
    m_useGlossary->setToolTip(tr("Apply the creator glossary to every transcription. Hotwords are injected via natural-sentence --prompt (whisper.cpp), hotwords (faster-whisper), SeACo bias + Nano LLM prompt (FunASR/Fun-ASR-Nano), Aho-Corasick (Parakeet) and prompt (Qwen3-ASR); FireRedASR uses post-processing only. Large glossaries are auto-filtered via lightweight H-PRM."));
    recognitionBody->addWidget(m_useGlossary);
    layout->addWidget(recognitionCard);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsDialog::buildTranslationPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);
    outer->addWidget(heading(tr("Translation Services")));
    outer->addWidget(muted(tr("Existing OpenAI, Anthropic, Ollama and Gemini settings.")));

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(14);

    QVBoxLayout *translationBody = nullptr;
    auto *translationCard = makeCard(tr("Translation Services"), tr("Existing OpenAI, Anthropic, Ollama and Gemini settings."), &translationBody);
    m_backend = new QComboBox;
    m_backend->addItems({"OpenAI", "Anthropic Claude", "Ollama (Local)", "Google Gemini"});
    translationBody->addWidget(new QLabel(tr("Service")));
    translationBody->addWidget(m_backend);
    m_apiKey = new QLineEdit;
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("API Key"));
    translationBody->addWidget(m_apiKey);
    m_targetLang = new QLineEdit;
    m_targetLang->setPlaceholderText("zh-TW");
    translationBody->addWidget(m_targetLang);
    m_ollamaUrl = new QLineEdit;
    m_ollamaModel = new QComboBox;
    m_ollamaModel->setEditable(true);
    m_ollamaModel->addItems({"llama3", "gemma3", "mistral", "qwen2.5", "phi3", "deepseek-r1"});
    translationBody->addWidget(new QLabel(tr("Ollama URL")));
    translationBody->addWidget(m_ollamaUrl);
    translationBody->addWidget(new QLabel(tr("Ollama Model")));
    translationBody->addWidget(m_ollamaModel);
    m_geminiModel = new QComboBox;
    m_geminiModel->setEditable(true);
    m_geminiModel->addItems({"gemini-2.0-flash", "gemini-1.5-flash", "gemini-1.5-pro"});
    translationBody->addWidget(new QLabel(tr("Gemini Model")));
    translationBody->addWidget(m_geminiModel);
    layout->addWidget(translationCard);

    QVBoxLayout *correctionBody = nullptr;
    auto *correctionCard = makeCard(tr("AI Subtitle Correction"), tr("Correct transcription errors using the same services above. The API key can be shared with translation or set independently."), &correctionBody);
    m_correctionBackend = new QComboBox;
    m_correctionBackend->addItems({"OpenAI", "Anthropic Claude", "Ollama (Local)", "Google Gemini"});
    correctionBody->addWidget(new QLabel(tr("Service")));
    correctionBody->addWidget(m_correctionBackend);
    m_correctionApiKey = new QLineEdit;
    m_correctionApiKey->setEchoMode(QLineEdit::Password);
    m_correctionApiKey->setPlaceholderText(tr("API Key (leave blank to reuse translation key)"));
    correctionBody->addWidget(m_correctionApiKey);
    m_correctionOllamaUrl = new QLineEdit;
    m_correctionOllamaUrl->setPlaceholderText("http://localhost:11434");
    correctionBody->addWidget(new QLabel(tr("Ollama URL")));
    correctionBody->addWidget(m_correctionOllamaUrl);
    m_correctionOllamaModel = new QComboBox;
    m_correctionOllamaModel->setEditable(true);
    m_correctionOllamaModel->addItems({"llama3", "gemma3", "mistral", "qwen2.5", "phi3", "deepseek-r1"});
    correctionBody->addWidget(new QLabel(tr("Ollama Model")));
    correctionBody->addWidget(m_correctionOllamaModel);
    m_correctionGeminiModel = new QComboBox;
    m_correctionGeminiModel->setEditable(true);
    m_correctionGeminiModel->addItems({"gemini-2.0-flash", "gemini-1.5-flash", "gemini-1.5-pro"});
    correctionBody->addWidget(new QLabel(tr("Gemini Model")));
    correctionBody->addWidget(m_correctionGeminiModel);
    auto *batchRow = new QHBoxLayout;
    batchRow->addWidget(new QLabel(tr("Batch Size")));
    m_correctionBatchSize = new QSpinBox;
    m_correctionBatchSize->setRange(1, 100);
    batchRow->addWidget(m_correctionBatchSize);
    correctionBody->addLayout(batchRow);
    m_correctionInstruction = new QLineEdit;
    m_correctionInstruction->setPlaceholderText(tr("Optional custom instruction (e.g. keep Cantonese slang)"));
    correctionBody->addWidget(new QLabel(tr("Custom Instruction")));
    correctionBody->addWidget(m_correctionInstruction);
    layout->addWidget(correctionCard);
    layout->addStretch();

    connect(m_backend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_apiKey->setEnabled(index != 2);
        m_ollamaUrl->setEnabled(index == 2);
        m_ollamaModel->setEnabled(index == 2);
        m_geminiModel->setEnabled(index == 3);
    });
    connect(m_correctionBackend, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_correctionApiKey->setEnabled(index != 2);
        m_correctionOllamaUrl->setEnabled(index == 2);
        m_correctionOllamaModel->setEnabled(index == 2);
        m_correctionGeminiModel->setEnabled(index == 3);
    });

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsDialog::buildStorageNetworkPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);
    outer->addWidget(heading(tr("Storage & Network")));
    outer->addWidget(muted(tr("Manage storage folder, temporary files, FFmpeg path and network proxy.")));

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(14);

    QVBoxLayout *storageBody = nullptr;
    auto *storageCard = makeCard(tr("Storage Locations"), tr("Manage model, engine runtime and temporary file locations."), &storageBody);
    storageBody->addWidget(new QLabel(tr("Unified Storage Folder")));
    m_storageRoot = new QLineEdit;
    storageBody->addWidget(makeFolderRow(m_storageRoot, tr("Select storage folder")));
    storageBody->addWidget(muted(tr("Portable builds use the models folder beside the app; regular installs use the user data folder.")));
    addSeparator(storageBody);
    m_customTemp = new QCheckBox;
    addToggle(storageBody, tr("Use a custom temporary folder"), tr("Clean it periodically to prevent temporary audio and subtitles from using too much space"), m_customTemp);
    m_tempPath = new QLineEdit;
    m_tempPath->setPlaceholderText(tr("Temporary files folder"));
    storageBody->addWidget(makeFolderRow(m_tempPath, tr("Select temporary folder")));
    auto *clearCache = new QPushButton(tr("Clear cache"));
    clearCache->setObjectName("yumuBtnSecondary");
    clearCache->setCursor(Qt::PointingHandCursor);
    connect(clearCache, &QPushButton::clicked, this, [this] {
        const QString path = m_tempPath->text().trimmed();
        if (path.isEmpty() || !QDir(path).exists()) return;
        if (QMessageBox::question(this, tr("Clear cache"), tr("Delete temporary files in this folder?")) == QMessageBox::Yes) {
            for (const auto &file : QDir(path).entryList(QDir::Files)) QFile::remove(QDir(path).filePath(file));
        }
    });
    storageBody->addWidget(clearCache, 0, Qt::AlignLeft);
    layout->addWidget(storageCard);

    QVBoxLayout *networkBody = nullptr;
    auto *networkCard = makeCard(tr("Network Proxy"), tr("Configure one proxy for model downloads, update checks and translation services."), &networkBody);
    auto *proxyRow = new QHBoxLayout;
    proxyRow->addWidget(new QLabel(tr("Proxy Mode")));
    m_proxyMode = new QComboBox;
    m_proxyMode->addItems({tr("None"), tr("Use system proxy"), tr("Custom proxy")});
    proxyRow->addWidget(m_proxyMode);
    networkBody->addLayout(proxyRow);
    auto *proxyDetails = new QHBoxLayout;
    m_proxyHost = new QLineEdit;
    m_proxyHost->setPlaceholderText(tr("Proxy host"));
    m_proxyPort = new QSpinBox;
    m_proxyPort->setRange(0, 65535);
    m_proxyPort->setSpecialValueText(tr("Default port"));
    proxyDetails->addWidget(m_proxyHost, 1);
    proxyDetails->addWidget(m_proxyPort);
    networkBody->addLayout(proxyDetails);
    connect(m_proxyMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool custom = index == 2;
        m_proxyHost->setEnabled(custom);
        m_proxyPort->setEnabled(custom);
    });
    layout->addWidget(networkCard);

    QVBoxLayout *pathsBody = nullptr;
    auto *pathsCard = makeCard(tr("FFmpeg Path"), tr("ffmpeg.exe is used for video export, cut detection and timeline waveform analysis."), &pathsBody);
    m_ffmpegPath = new QLineEdit;
    m_ffmpegPath->setPlaceholderText(tr("Portable bin\\ffmpeg.exe or ffmpeg on PATH"));
    pathsBody->addWidget(makePathRow(m_ffmpegPath, tr("Select ffmpeg.exe"), true));
    layout->addWidget(pathsCard);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsDialog::buildAboutPage() {
    auto *page = new QWidget;
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(16);
    outer->addWidget(heading(tr("About Yumu Studio")));

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(14);

    QVBoxLayout *aboutBody = nullptr;
    auto *aboutCard = makeCard(QString(), QString(), &aboutBody);
    if (auto *titleItem = aboutBody->itemAt(0)) {
        if (auto *title = qobject_cast<QLabel *>(titleItem->widget())) title->hide();
    }
    auto *versionLabel = muted(QStringLiteral("v%1").arg(QStringLiteral(APP_VERSION)));
    versionLabel->setObjectName("settingsHeading");
    aboutBody->addWidget(versionLabel);
    aboutBody->addSpacing(4);
    aboutBody->addWidget(muted(tr("<b>Yumu Studio v%1</b><br>Windows native subtitle workstation - Qt 6 Widgets (C++17)").arg(QStringLiteral(APP_VERSION))));
    aboutBody->addWidget(muted(tr("An integrated tool for local audio/video transcription, subtitle editing and final export, built for creators. Six engines x 13 variants are provisioned on demand via the in-app Engine Center -- no prebuilt whisper.cpp required at build time.")));
    aboutBody->addWidget(muted(tr("<b>Highlights (from README v2.0)</b><br>- 6 engines x 13 variants: whisper.cpp {cpu,vulkan,cuda}, faster-whisper {cpu,cuda}, FunASR {cpu,cuda}, Parakeet / Qwen3-ASR / FireRedASR {ONNX cpu,cuda}<br>- New-style workspace: top bar + nav rail + card-based center; hardware (QVideoWidget) and CPU (QVideoSink) preview paths with 360-1024p low-res and 15/24/30 fps throttling<br>- True waveform timeline with adaptive ruler, pixel-accurate magnetic snapping and 1x-64x zoom; draggable subtitle blocks with safe frames<br>- Creator tools: glossary (JSON, hotword/prompt injection), smart segmentation (refine) and cut-detection magnetic alignment<br>- Export: SRT / VTT / TXT, MP4 burn-in and ProRes 4444 transparent MOV (CRF 18 slow / 23 medium / 28 fast) with bilingual toggle")));
    aboutBody->addWidget(muted(tr("Shortcuts: Ctrl+G transcribe | Ctrl+T translate | Ctrl+E export | Ctrl+Shift+E advanced export | Ctrl+Z / Ctrl+Shift+Z undo/redo")));
    aboutBody->addWidget(muted(tr("Dependencies: Qt 6 - whisper.cpp - FFmpeg - sherpa-onnx - CTranslate2 - FunASR - Vulkan SDK (optional)<br>License: GPL-3.0 - Portable mode via portable.flag - API keys encrypted with DPAPI")));
    aboutBody->addSpacing(6);
    aboutBody->addWidget(muted(tr("See Releases for the latest portable package (YumuStudio_Portable.zip). For build instructions see docs/BUILD_FROM_SOURCE.md and docs/MAKE_PORTABLE.md.")));
    layout->addWidget(aboutCard);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    return page;
}

void SettingsDialog::showPage(int index) {
    if (!m_pages || index < 0 || index >= m_pages->count()) return;
    if (m_pages->currentIndex() == index) {
        if (index >= 0 && index < m_navButtons.size() && !m_navButtons[index]->isChecked())
            m_navButtons[index]->setChecked(true);
        return;
    }
    // Instant nav feedback + sync controls before capture so pixmap is up-to-date
    if (index >= 0 && index < m_navButtons.size())
        m_navButtons[index]->setChecked(true);
    syncPreviewControls();
    const int cur = m_pages->currentIndex();
    auto dir = (index > cur) ? FadeOverlay::FromRight : FadeOverlay::FromLeft;
    // 180ms editorial slide+fade masks the ~80-120ms model list layout
    const bool animated = StackAnimator::animate(m_pages, index, 180, dir);
    if (!animated) {
        m_pages->setCurrentIndex(index);
    }
}

void SettingsDialog::loadFromConfig() {
    const auto &cfg = AppConfig::instance();
    m_ffmpegPath->setText(cfg.paths.ffmpegPath);
    m_modelPath->setText(cfg.paths.modelPath);
    m_cliPath->setText(cfg.paths.whisperCliPath);
    m_modelRoot->setText(cfg.paths.modelRoot.isEmpty() ? LocalModelManager::defaultRoot() : cfg.paths.modelRoot);
    m_storageRoot->setText(m_modelRoot->text());
    m_runnerPath->setText(cfg.paths.runnerPath);
    m_uiLanguage->setCurrentIndex(m_uiLanguage->findData(cfg.system.language));
    {
        const QSignalBlocker themeBlocker(m_themeCombo);
        m_themeCombo->setCurrentIndex(qMax(0, m_themeCombo->findData(cfg.system.theme)));
    }
    m_checkUpdates->setChecked(cfg.system.checkUpdates);
    m_checkAppUpdates->setChecked(cfg.update.enabled);
    if (m_skippedVersionLabel) {
        if (cfg.update.skippedVersion.isEmpty()) {
            m_skippedVersionLabel->setText({});
        } else {
            m_skippedVersionLabel->setText(tr("Skipped version: %1").arg(cfg.update.skippedVersion));
        }
    }
    if (m_clearSkippedBtn) m_clearSkippedBtn->setVisible(!cfg.update.skippedVersion.isEmpty());
    m_preventSleep->setChecked(cfg.system.preventSleep);
    m_learnMistakes->setChecked(cfg.system.learnMistakes);
    m_mistakeThreshold->setValue(qBound(1, cfg.system.mistakeThreshold, 50));
    m_customTemp->setChecked(cfg.system.useCustomTemp);
    m_tempPath->setText(cfg.system.tempPath);
    m_proxyMode->setCurrentIndex(qBound(0, cfg.system.proxyMode, m_proxyMode->count() - 1));
    m_proxyHost->setText(cfg.system.proxyHost);
    m_proxyPort->setValue(qBound(0, cfg.system.proxyPort, 65535));
    m_language->setCurrentIndex(qMax(0, m_language->findData(cfg.whisper.language.isEmpty() ? "auto" : cfg.whisper.language)));
    m_computeDevice->setCurrentIndex(qMax(0, m_computeDevice->findData(
        cfg.whisper.computeDevice.isEmpty() ? "auto" : cfg.whisper.computeDevice)));
    m_precision->setCurrentIndex(qMax(0, m_precision->findData(
        cfg.whisper.precision.isEmpty() ? "auto" : cfg.whisper.precision)));
    m_threads->setValue(cfg.whisper.threads > 0 ? cfg.whisper.threads : 4);
    m_translate->setChecked(cfg.whisper.translate);
    m_useGlossary->setChecked(cfg.whisper.useGlossary);
    syncPreviewControls();
    m_backend->setCurrentIndex(static_cast<int>(cfg.translation.backend));
    m_apiKey->setText(cfg.translation.apiKey);
    m_targetLang->setText(cfg.translation.targetLang.isEmpty() ? "zh-TW" : cfg.translation.targetLang);
    m_ollamaUrl->setText(cfg.translation.ollamaUrl.isEmpty() ? "http://localhost:11434" : cfg.translation.ollamaUrl);
    m_ollamaModel->setCurrentText(cfg.translation.ollamaModel.isEmpty() ? "llama3" : cfg.translation.ollamaModel);
    m_geminiModel->setCurrentText(cfg.translation.geminiModel.isEmpty() ? "gemini-2.0-flash" : cfg.translation.geminiModel);

    m_correctionBackend->setCurrentIndex(static_cast<int>(cfg.correction.backend));
    m_correctionApiKey->setText(cfg.correction.apiKey);
    m_correctionOllamaUrl->setText(cfg.correction.ollamaUrl.isEmpty() ? "http://localhost:11434" : cfg.correction.ollamaUrl);
    m_correctionOllamaModel->setCurrentText(cfg.correction.ollamaModel.isEmpty() ? "llama3" : cfg.correction.ollamaModel);
    m_correctionGeminiModel->setCurrentText(cfg.correction.geminiModel.isEmpty() ? "gemini-2.0-flash" : cfg.correction.geminiModel);
    m_correctionBatchSize->setValue(qBound(1, cfg.correction.batchSize, 100));
    m_correctionInstruction->setText(cfg.correction.customInstruction);

    m_selectedEngine = cfg.whisper.engine.isEmpty() ? "whisper.cpp" : cfg.whisper.engine;
    m_activeEngine = m_selectedEngine;
    m_activeModelId = cfg.whisper.modelId;
    m_engine->setCurrentText(m_selectedEngine);
    for (int row = 0; row < m_engineList->count(); ++row) {
        if (m_engineList->item(row)->data(Qt::UserRole).toString() == m_selectedEngine) {
            m_engineList->setCurrentRow(row);
            break;
        }
    }
    refreshModelChoices();
    const int modelIndex = m_model->findData(cfg.whisper.modelId);
    if (modelIndex >= 0) m_model->setCurrentIndex(modelIndex);
    refreshLanguageChoices();
    m_language->setCurrentIndex(qMax(0, m_language->findData(cfg.whisper.language.isEmpty() ? "auto" : cfg.whisper.language)));
    emit m_backend->currentIndexChanged(m_backend->currentIndex());
}

void SettingsDialog::saveAndAccept() {
    auto &cfg = AppConfig::instance();
    const QString storageRoot = m_storageRoot->text().trimmed();
    if (!storageRoot.isEmpty()) m_modelRoot->setText(storageRoot);
    cfg.paths = paths();
    cfg.whisper = whisperParams();
    cfg.translation = translationParams();
    cfg.correction = correctionParams();
    cfg.system.language = m_uiLanguage->currentData().toString();
    cfg.system.theme = m_themeCombo->currentData().toString();
    cfg.system.checkUpdates = m_checkUpdates->isChecked();
    cfg.update.enabled      = m_checkAppUpdates->isChecked();
    // Repository owner/name are fixed; not exposed in the UI.
    cfg.system.preventSleep = m_preventSleep->isChecked();
    cfg.system.learnMistakes = m_learnMistakes->isChecked();
    cfg.system.mistakeThreshold = m_mistakeThreshold->value();
    cfg.system.useCustomTemp = m_customTemp->isChecked();
    cfg.system.tempPath = m_tempPath->text().trimmed();
    cfg.system.proxyMode = m_proxyMode->currentIndex();
    cfg.system.proxyHost = m_proxyHost->text().trimmed();
    cfg.system.proxyPort = m_proxyPort->value();
    if (m_previewLowRes) cfg.preview.lowResPreview = m_previewLowRes->isChecked();
    if (m_previewWidthCombo) cfg.preview.previewWidth = m_previewWidthCombo->currentData().toInt();
    if (m_previewFpsCombo) cfg.preview.previewFpsCap = m_previewFpsCombo->currentData().toInt();
    if (m_previewHardware) cfg.preview.hardwarePreview = m_previewHardware->isChecked();
    cfg.whisper.modelPath = cfg.paths.modelPath;
    cfg.whisper.cliPath = cfg.paths.whisperCliPath;
    cfg.whisper.runnerPath = cfg.paths.runnerPath;
    for (const auto &spec : LocalModelManager::catalog()) {
        if (spec.engine.compare(cfg.whisper.engine, Qt::CaseInsensitive) == 0 && spec.id == cfg.whisper.modelId) {
            const QString modelRoot = LocalModelManager::modelDir(spec.engine, spec.id, cfg.paths.modelRoot);
            const QString marker = spec.requiredFiles.isEmpty() ? spec.archiveName : spec.requiredFiles.first();
            cfg.whisper.modelPath = QDir(modelRoot).filePath(marker);
            break;
        }
    }
    cfg.save();
    UiLanguage::installTranslator(qApp);
    emit languageChanged();
    accept();
}

AppPaths SettingsDialog::paths() const {
    return {m_ffmpegPath->text().trimmed(), m_cliPath->text().trimmed(), m_modelPath->text().trimmed(),
            m_modelRoot->text().trimmed(), m_runnerPath->text().trimmed()};
}

WhisperParams SettingsDialog::whisperParams() const {
    WhisperParams params;
    params.engine = m_activeEngine.isEmpty() ? m_engine->currentText().trimmed() : m_activeEngine;
    params.modelId = m_activeModelId.isEmpty() ? m_model->currentData().toString() : m_activeModelId;
    params.modelPath = m_modelPath->text().trimmed();
    params.cliPath = m_cliPath->text().trimmed();
    params.runnerPath = m_runnerPath->text().trimmed();
    params.language = m_language->currentData().toString();
    params.computeDevice = m_computeDevice->currentData().toString();
    params.precision = m_precision->currentData().toString();
    params.threads = m_threads->value();
    params.translate = m_translate->isChecked();
    params.useGlossary = m_useGlossary->isChecked();
    return params;
}

TranslationParams SettingsDialog::translationParams() const {
    TranslationParams params;
    params.backend = static_cast<TranslationBackend>(m_backend->currentIndex());
    params.apiKey = m_apiKey->text().trimmed();
    params.targetLang = m_targetLang->text().trimmed();
    params.ollamaUrl = m_ollamaUrl->text().trimmed();
    params.ollamaModel = m_ollamaModel->currentText().trimmed();
    params.geminiModel = m_geminiModel->currentText().trimmed();
    return params;
}

CorrectionParams SettingsDialog::correctionParams() const {
    CorrectionParams params;
    params.backend = static_cast<CorrectionBackend>(m_correctionBackend->currentIndex());
    params.apiKey = m_correctionApiKey->text().trimmed();
    params.ollamaUrl = m_correctionOllamaUrl->text().trimmed();
    params.ollamaModel = m_correctionOllamaModel->currentText().trimmed();
    params.geminiModel = m_correctionGeminiModel->currentText().trimmed();
    params.batchSize = m_correctionBatchSize->value();
    params.customInstruction = m_correctionInstruction->text().trimmed();
    return params;
}

void SettingsDialog::refreshModelChoices() {
    if (!m_model || !m_engine) return;
    const QString engine = m_selectedEngine.isEmpty() ? m_engine->currentText() : m_selectedEngine;
    const QString wanted = engine.compare(m_activeEngine, Qt::CaseInsensitive) == 0 && !m_activeModelId.isEmpty()
        ? m_activeModelId
        : m_model->currentData().toString();
    m_model->blockSignals(true);
    m_model->clear();
    for (const auto &spec : LocalModelManager::catalog()) {
        if (spec.engine.compare(engine, Qt::CaseInsensitive) == 0)
            m_model->addItem(spec.name + " (" + spec.id + ")", spec.id);
    }
    const int index = m_model->findData(wanted);
    m_model->setCurrentIndex(index >= 0 ? index : 0);
    m_model->blockSignals(false);
    for (const auto &spec : LocalModelManager::catalog()) {
        if (spec.engine.compare(engine, Qt::CaseInsensitive) == 0 && spec.id == m_model->currentData().toString()) {
            const bool ready = LocalModelManager::isInstalled(spec, m_modelRoot->text().trimmed());
            m_modelStatus->setText(ready ? tr("Installed") : (spec.files.isEmpty() ? tr("Folder import required") : tr("Not downloaded")));
            m_downloadModel->setEnabled(!ready && !spec.files.isEmpty());
            if (spec.engine.compare("whisper.cpp", Qt::CaseInsensitive) == 0 && !spec.requiredFiles.isEmpty()) {
                const QString modelFile = QDir(LocalModelManager::modelDir(spec.engine, spec.id, m_modelRoot->text().trimmed())).filePath(spec.requiredFiles.first());
                if (ready || m_modelPath->text().trimmed().isEmpty()) m_modelPath->setText(modelFile);
            }
            return;
        }
    }
}

void SettingsDialog::refreshLanguageChoices() {
    if (!m_language || !m_engine || !m_model) return;
    const QString engine = m_selectedEngine.isEmpty() ? m_engine->currentText() : m_selectedEngine;
    const QString wanted = m_language->currentData().toString().isEmpty()
        ? AppConfig::instance().whisper.language
        : m_language->currentData().toString();
    const QSignalBlocker blocker(m_language);
    m_language->clear();
    m_language->addItem(tr(LocalModelManager::languageDisplayName("auto").toUtf8().constData()), "auto");
    for (const auto &code : LocalModelManager::supportedLanguageCodes(engine, m_model->currentData().toString())) {
        if (code == "auto") continue;
        m_language->addItem(tr(LocalModelManager::languageDisplayName(code).toUtf8().constData()), code);
    }
    const int index = m_language->findData(wanted.isEmpty() ? "auto" : wanted);
    m_language->setCurrentIndex(index >= 0 ? index : 0);
}

void SettingsDialog::downloadSelectedModel() {
    const QString engine = m_selectedEngine.isEmpty() ? m_engine->currentText() : m_selectedEngine;
    for (const auto &spec : LocalModelManager::catalog()) {
        if (spec.engine.compare(engine, Qt::CaseInsensitive) == 0 && spec.id == m_model->currentData().toString()) {
            if (spec.files.isEmpty()) {
                importModelFolder(spec);
                return;
            }
            m_downloadingId = spec.id;
            m_modelStatus->setText(tr("Preparing download..."));
            if (m_cancelDownload) m_cancelDownload->setVisible(true);
            rebuildModelList();
            m_models->download(spec, m_modelRoot->text().trimmed());
            return;
        }
    }
}

void SettingsDialog::refreshBackendChoices(){
    if (!m_backendVariantsLayout) return;
    // Batch widget creation to avoid N layout recalcs.
    QWidget *parentWidget = m_backendVariantsLayout->parentWidget();
    if (parentWidget) parentWidget->setUpdatesEnabled(false);
    while (QLayoutItem *it = m_backendVariantsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    m_backendVariantStatus.clear();
    m_backendVariantButtons.clear();
    m_backendVariantRows.clear();
    if (m_selectedEngine.isEmpty()) {
        if (parentWidget) parentWidget->setUpdatesEnabled(true);
        return;
    }
    const QString activeEngine = AppConfig::instance().whisper.engine;
    const QString activeVariant = AppConfig::instance().whisper.computeDevice;
    for (const BackendSpec &spec : BackendCatalog::forEngine(m_selectedEngine)) {
        const QString key = spec.engineId.toLower() + "/" + spec.variantId.toLower();
        const bool installed = BackendCatalog::isInstalled(spec.engineId, spec.variantId);
        const bool busy = m_backendInstaller && m_backendInstaller->isBusy(spec.engineId, spec.variantId);
        const bool isActive = installed
            && activeEngine.compare(spec.engineId, Qt::CaseInsensitive) == 0
            && activeVariant.compare(spec.variantId, Qt::CaseInsensitive) == 0;
        auto *row = new QFrame;
        // 跟隨模型下載後的高光：使用與 modelRowInstalled 相同的綠色高光（共用 QSS），避免兩個「使用中」
        row->setObjectName(isActive ? "modelRowInstalled" : "modelRow");
        row->setFrameShape(QFrame::StyledPanel);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(8);
        auto *info = new QVBoxLayout;
        info->setSpacing(2);
        auto *nameLabel = new QLabel(spec.name);
        nameLabel->setObjectName("modelName");
        if (isActive) { QFont f = nameLabel->font(); f.setBold(true); nameLabel->setFont(f); }
        info->addWidget(nameLabel);
        auto *meta = new QLabel(QStringLiteral("%1  ·  %2").arg(spec.sizeHint, spec.requirementNote));
        meta->setObjectName("settingsMuted");
        meta->setWordWrap(true);
        info->addWidget(meta);
        rowLayout->addLayout(info, 1);
        // 僅在非使用中時顯示狀態文字，避免與按鈕「使用中」重複
        QString statusText;
        if (busy) statusText = tr("Installing...");
        else if (installed) statusText = tr("Installed");
        else statusText = tr("Not installed");
        auto *status = new QLabel(statusText);
        status->setObjectName(busy ? "engineAvailable" : "settingsMuted");
        status->setMinimumWidth(90);
        status->setAlignment(Qt::AlignCenter);
        if (isActive) status->setVisible(false);
        rowLayout->addWidget(status);
        m_backendVariantStatus.insert(key, status);
        QPushButton *btn = nullptr;
        if (isActive) {
            btn = new QPushButton(tr("In use"));
            btn->setObjectName("yumuBtn");
            btn->setEnabled(false);
            btn->setFixedWidth(78);
        } else if (busy) {
            btn = new QPushButton(tr("Cancel"));
            btn->setObjectName("yumuBtnSecondary");
            btn->setFixedWidth(78);
            btn->setCursor(Qt::PointingHandCursor);
        } else if (installed) {
            btn = new QPushButton(tr("Activate"));
            btn->setObjectName("yumuBtnSecondary");
            btn->setFixedWidth(78);
            btn->setCursor(Qt::PointingHandCursor);
        } else {
            btn = new QPushButton(tr("Install"));
            btn->setObjectName("yumuBtn");
            btn->setFixedWidth(78);
            btn->setCursor(Qt::PointingHandCursor);
        }
        if (!isActive) {
            connect(btn, &QPushButton::clicked, this, [this, spec]() {
                const bool isBusy = m_backendInstaller && m_backendInstaller->isBusy(spec.engineId, spec.variantId);
                if (isBusy) {
                    m_backendInstaller->cancel(spec.engineId, spec.variantId);
                    return;
                }
                if (BackendCatalog::isInstalled(spec.engineId, spec.variantId)) {
                    BackendCatalog::activateVariant(spec.engineId, spec.variantId);
                    if (m_computeDevice) {
                        const QSignalBlocker blocker(m_computeDevice);
                        int idx = m_computeDevice->findData(spec.variantId);
                        if (idx >= 0) m_computeDevice->setCurrentIndex(idx);
                    }
                    refreshBackendChoices();
                    refreshEngineDetails();
                    return;
                }
                if (m_backendInstaller) m_backendInstaller->install(spec.engineId, spec.variantId);
            });
        }
        rowLayout->addWidget(btn);
        m_backendVariantButtons.insert(key, btn);
        m_backendVariantRows.insert(key, row);
        m_backendVariantsLayout->addWidget(row);
    }
    if (parentWidget) parentWidget->setUpdatesEnabled(true);
}
void SettingsDialog::installSelectedBackend(){
    // 下拉已移除，此函數保留僅為兼容舊調用；實際安裝由卡片按鈕直接驅動
    Q_UNUSED(m_installBackend);
}
void SettingsDialog::refreshEngineDetails() {
    refreshBackendChoices();
    if (!m_engineInfo.contains(m_selectedEngine)) return;
    const auto info = m_engineInfo.value(m_selectedEngine);
    m_engineTitle->setText(tr(info.name.toUtf8().constData()));
    m_engineDescription->setText(tr(info.description.toUtf8().constData()));
    const bool available = m_selectedEngine == "whisper.cpp"
        ? (!m_cliPath->text().trimmed().isEmpty() || QFileInfo::exists(QDir(AppConfig::portableRoot()).filePath("bin/whisper-cli.exe")))
        : QFileInfo::exists(m_runnerPath->text().trimmed());
    m_engineStatus->setText(available ? tr("Available") : tr("Not configured"));
    m_engineStatus->setProperty("ready", available);
    m_engineStatus->update();
    // Defer polish so engine list switching stays instant (style recalc with
    // full app QSS is ~80ms).
    if (auto *lbl = m_engineStatus) {
        QPointer<QLabel> guard(lbl);
        QTimer::singleShot(0, lbl, [guard]{
            if (!guard) return;
            guard->style()->unpolish(guard);
            guard->style()->polish(guard);
            guard->update();
        });
    }
    m_engineVersion->setText(tr("Installed version: %1").arg(info.installedVersion));
    if (m_latestEngineVersions.contains(m_selectedEngine)) {
        const QString latest = m_latestEngineVersions.value(m_selectedEngine);
        if (m_engineUpdates.value(m_selectedEngine, false)) {
            m_engineUpdateStatus->setText(tr("New version available: %1").arg(latest));
            m_upgradeButton->setVisible(true);
        } else {
            m_engineUpdateStatus->setText(tr("Latest version checked: %1").arg(latest));
            m_upgradeButton->setVisible(false);
        }
    } else {
        m_engineUpdateStatus->setText(tr("Not checked yet"));
        m_upgradeButton->setVisible(false);
    }
    refreshModelChoices();
    rebuildModelList();
}

void SettingsDialog::rebuildModelList() {
    if (!m_modelListLayout) return;
    QWidget *parentWidget = m_modelListLayout->parentWidget();
    if (parentWidget) parentWidget->setUpdatesEnabled(false);
    while (auto *item = m_modelListLayout->takeAt(0)) {
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    const QString query = m_modelSearch->text().trimmed();
    const bool activeEngine = m_selectedEngine.compare(m_activeEngine, Qt::CaseInsensitive) == 0;
    const QString activeId = activeEngine ? m_activeModelId : QString();
    QString lastCategory;
    for (const auto &spec : LocalModelManager::catalog()) {
        if (spec.engine.compare(m_selectedEngine, Qt::CaseInsensitive) != 0) continue;
        if (!query.isEmpty() && !spec.name.contains(query, Qt::CaseInsensitive) && !spec.description.contains(query, Qt::CaseInsensitive)) continue;
        const bool installed = LocalModelManager::isInstalled(spec, m_modelRoot->text().trimmed());
        if (m_onlyInstalled->isChecked() && !installed) continue;
        if (spec.category != lastCategory) {
            auto *category = new QLabel(spec.category.isEmpty() ? tr("Models") : tr(spec.category.toUtf8().constData()));
            category->setObjectName("modelCategory");
            m_modelListLayout->addWidget(category);
            lastCategory = spec.category;
        }
        auto *row = new QFrame;
        row->setObjectName(installed ? "modelRowInstalled" : (activeId == spec.id ? "modelRowActive" : "modelRow"));
        auto *grid = new QGridLayout(row);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(10);
            auto *name = new QLabel(tr(spec.name.toUtf8().constData()));
        name->setObjectName("modelName");
        grid->addWidget(name, 0, 0);
            auto *desc = new QLabel(tr(spec.description.toUtf8().constData()));
        desc->setObjectName("settingsMuted");
        grid->addWidget(desc, 0, 1);
        auto *metrics = new QWidget;
        metrics->setObjectName("modelMetrics");
        auto *metricsLayout = new QVBoxLayout(metrics);
        metricsLayout->setContentsMargins(0, 0, 0, 0);
        metricsLayout->setSpacing(3);
        const auto addMetric = [metricsLayout](const QString &labelText, int value, const QString &objectName) {
            auto *metricRow = new QHBoxLayout;
            metricRow->setContentsMargins(0, 0, 0, 0);
            metricRow->setSpacing(6);
            auto *label = new QLabel(labelText);
            label->setFixedWidth(34);
            metricRow->addWidget(label);
            auto *bar = new QProgressBar;
            bar->setObjectName(objectName);
            bar->setRange(0, 5);
            bar->setValue(qBound(0, value, 5));
            bar->setTextVisible(false);
            bar->setFixedHeight(8);
            metricRow->addWidget(bar, 1);
            auto *score = new QLabel(QStringLiteral("%1/5").arg(value));
            score->setFixedWidth(24);
            score->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            metricRow->addWidget(score);
            metricsLayout->addLayout(metricRow);
        };
        addMetric(tr("Speed"), spec.speedScore, "modelSpeedBar");
        addMetric(tr("Accuracy"), spec.accuracyScore, "modelAccuracyBar");
            auto *size = new QLabel(tr(spec.sizeLabel.toUtf8().constData()));
        size->setObjectName("modelSize");
        metricsLayout->addWidget(size);
        grid->addWidget(metrics, 0, 2);
            if (!spec.tags.isEmpty()) {
                QStringList translatedTags;
                translatedTags.reserve(spec.tags.size());
                for (const QString &tag : spec.tags)
                    translatedTags << tr(tag.toUtf8().constData());
                auto *tags = new QLabel(translatedTags.join(QStringLiteral("  ")));
            tags->setObjectName("modelTags");
            grid->addWidget(tags, 1, 0, 1, 2);
        }
        auto *actions = new QHBoxLayout;
        const bool canDownload = !installed && !spec.files.isEmpty();
        const bool active = installed && activeId == spec.id;
        auto *primary = new QPushButton(installed
            ? (active ? tr("Current") : tr("Use"))
            : (canDownload ? tr("Download") : tr("Import from folder")));
        primary->setEnabled(activeId != spec.id || canDownload);
        connect(primary, &QPushButton::clicked, this, [this, spec, id = spec.id, installed, canDownload, active] {
            const int index = m_model->findData(id);
            if (installed) {
                if (active) return;
                m_activeEngine = m_selectedEngine;
                m_activeModelId = id;
                if (index >= 0) {
                    const QSignalBlocker blocker(m_model);
                    m_model->setCurrentIndex(index);
                }
                refreshModelChoices();
                refreshLanguageChoices();
                rebuildModelList();
            } else if (canDownload) {
                if (index >= 0) {
                    const QSignalBlocker blocker(m_model);
                    m_model->setCurrentIndex(index);
                }
                downloadSelectedModel();
            } else {
                importModelFolder(spec);
            }
        });
        actions->addWidget(primary);
        // 保留 Import Folder 入口：可下載模型亦同時提供匯入按鈕，供微調模型使用
        if (!installed && canDownload) {
            auto *importBtn = new QPushButton(tr("Import from folder"));
            importBtn->setToolTip(tr("Import a fine-tuned model folder"));
            connect(importBtn, &QPushButton::clicked, this, [this, spec] {
                importModelFolder(spec);
            });
            actions->addWidget(importBtn);
        }
        if (installed) {
            auto *remove = new QPushButton(tr("Delete"));
            connect(remove, &QPushButton::clicked, this, [this, spec] {
                if (QMessageBox::question(this, tr("Delete Model"), tr("Delete %1?").arg(spec.name)) == QMessageBox::Yes) {
                    m_models->remove(spec, m_modelRoot->text().trimmed());
                    if (m_selectedEngine.compare(m_activeEngine, Qt::CaseInsensitive) == 0
                        && m_activeModelId == spec.id) {
                        m_activeEngine.clear();
                        m_activeModelId.clear();
                    }
                    refreshModelChoices();
                    rebuildModelList();
                }
            });
            actions->addWidget(remove);
        } else {
            if (m_downloadingId == spec.id) {
                auto *downloading = new QLabel(tr("Downloading..."));
                downloading->setObjectName("modelDownloading");
                actions->addWidget(downloading);
            }
        }
        grid->addLayout(actions, 0, 3, 2, 1);
        grid->setColumnStretch(1, 1);
        m_modelListLayout->addWidget(row);
    }
    if (m_modelListLayout->count() == 0) m_modelListLayout->addWidget(muted(tr("No matching models.")));
    if (parentWidget) parentWidget->setUpdatesEnabled(true);
}


void SettingsDialog::checkEngineUpdates() {
    m_pendingEngineChecks = LocalModelManager::engines().size();
    m_checkUpdatesButton->setEnabled(false);
    m_checkUpdatesButton->setText(tr("Checking..."));
    m_models->checkEngineUpdates();
}

void SettingsDialog::importModelFolder(const LocalModelSpec &spec) {
    const QString source = QFileDialog::getExistingDirectory(this, tr("Select model folder"), m_modelRoot->text().trimmed());
    if (source.isEmpty()) return;
    QString error;
    if (!m_models->importFolder(spec, source, m_modelRoot->text().trimmed(), &error)) {
        QMessageBox::warning(this, tr("Import failed"), error);
        return;
    }
    m_modelStatus->setText(tr("Installed"));
    refreshModelChoices();
    rebuildModelList();
}
