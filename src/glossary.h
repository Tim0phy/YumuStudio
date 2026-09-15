#pragma once
#include <QString>
#include <QList>
#include <QRegularExpression>

struct GlossaryEntry {
    QString term;
    QString replacement;
    QRegularExpression regex; // pre-compiled in Glossary::load() for applyToText()
};

class Glossary {
public:
    static QString filePath();
    static QList<GlossaryEntry> load();
    static bool save(const QList<GlossaryEntry> &entries);

    static QString applyToText(const QList<GlossaryEntry> &entries, const QString &text);
    // 自然句提示詞（whisper initial_prompt 專用，稀有詞置尾，適合 224 token 限制）
    static QString hotwordPromptNatural(const QList<GlossaryEntry> &entries, int maxTerms = 50);
    static QStringList preferredTerms(const QList<GlossaryEntry> &entries, int maxTerms = 50);
    // 輕量 H-PRM：大詞庫時按長度/中文優先做 top-N 截斷，保留可擴充為拼音相似度檢索
    static QList<GlossaryEntry> selectTopEntries(const QList<GlossaryEntry> &entries, int maxTerms);
};
