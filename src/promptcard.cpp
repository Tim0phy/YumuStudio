#include "promptcard.h"
#include "appconfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>

PromptCard::PromptCard(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    // Flush with the card: rightCard already pads 12H/8V (same as the toolbar
    // above), so page content left edges align across all three pages.
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);
    m_title = new QLabel(tr("轉寫提示詞"), this);
    m_title->setObjectName(QStringLiteral("yumuCardTitle"));
    root->addWidget(m_title);
    m_hint = new QLabel(tr("一句起兩句止，描述口音、領域或專有名詞。生效於 whisper.cpp 與 faster-whisper；其他引擎沿用專屬詞庫。清空即還原。"), this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    root->addWidget(m_hint);

    m_edit = new QTextEdit(this);
    m_edit->setPlaceholderText(tr("例如：這是香港科技播客，講者有陳生同阿May，成日提到大語言模型同RAG。"));
    m_edit->setAcceptRichText(false);
    root->addWidget(m_edit, 1);
    connect(m_edit, &QTextEdit::textChanged, this, [this] {
        if (m_count) m_count->setText(tr("%1 字").arg(m_edit->toPlainText().trimmed().size()));
    });

    auto *btnRow = new QHBoxLayout;
    m_count = new QLabel(tr("0 字"), this);
    m_count->setStyleSheet(QStringLiteral("color:#8E86A0; font-size:8pt; background:transparent;"));
    btnRow->addWidget(m_count);
    btnRow->addStretch();
    m_clearBtn = new QPushButton(tr("清除"), this);
    m_clearBtn->setObjectName(QStringLiteral("pillBtnSecondary"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    connect(m_clearBtn, &QPushButton::clicked, this, [this] {
        m_edit->clear();
        savePrompt();
    });
    m_saveBtn = new QPushButton(tr("儲存提示詞"), this);
    m_saveBtn->setObjectName(QStringLiteral("pillBtn"));
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveBtn, &QPushButton::clicked, this, &PromptCard::savePrompt);
    btnRow->addWidget(m_clearBtn);
    btnRow->addWidget(m_saveBtn);
    root->addLayout(btnRow);

    reload();
}

void PromptCard::retranslateUi() {
    if (m_title) m_title->setText(tr("轉寫提示詞"));
    if (m_hint) m_hint->setText(tr("一句起兩句止，描述口音、領域或專有名詞。生效於 whisper.cpp 與 faster-whisper；其他引擎沿用專屬詞庫。清空即還原。"));
    if (m_edit) m_edit->setPlaceholderText(tr("例如：這是香港科技播客，講者有陳生同阿May，成日提到大語言模型同RAG。"));
    if (m_count) m_count->setText(tr("%1 字").arg(m_edit ? m_edit->toPlainText().trimmed().size() : 0));
    if (m_clearBtn) m_clearBtn->setText(tr("清除"));
    if (m_saveBtn) m_saveBtn->setText(tr("儲存提示詞"));
}

void PromptCard::reload() {
    const QSignalBlocker blocker(m_edit);
    m_edit->setPlainText(AppConfig::instance().whisper.customPrompt);
    m_count->setText(tr("%1 字").arg(m_edit->toPlainText().trimmed().size()));
}

void PromptCard::savePrompt() {
    auto &cfg = AppConfig::instance();
    cfg.whisper.customPrompt = m_edit->toPlainText().trimmed();
    cfg.save();
    emit promptChanged();
}
