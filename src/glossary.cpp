#include "glossary.h"
#include "appconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

QString Glossary::filePath() {
    if (AppConfig::isPortableMode())
        return QDir(AppConfig::portableRoot()).filePath("config/glossary.json");
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("glossary.json");
}

static bool isAsciiWord(const QString &term) {
    static const QRegularExpression asciiWord(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 '&.-]*$"));
    return asciiWord.match(term).hasMatch();
}

static QRegularExpression regexForTerm(const QString &term) {
    QString pattern = QRegularExpression::escape(term);
    if (isAsciiWord(term))
        pattern = QStringLiteral("\\b%1\\b").arg(pattern);
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
}

QList<GlossaryEntry> Glossary::load() {
    QList<GlossaryEntry> entries;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return entries;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = doc.array();
    for (const auto &value : array) {
        const QJsonObject obj = value.toObject();
        GlossaryEntry entry;
        entry.term = obj.value("term").toString().trimmed();
        entry.replacement = obj.value("replacement").toString().trimmed();
        if (!entry.term.isEmpty()) {
            entry.regex = regexForTerm(entry.term);
            entries << entry;
        }
    }
    return entries;
}

bool Glossary::save(const QList<GlossaryEntry> &entries) {
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray array;
    for (const auto &entry : entries) {
        if (entry.term.trimmed().isEmpty()) continue;
        array << QJsonObject{{"term", entry.term.trimmed()},
                             {"replacement", entry.replacement.trimmed()}};
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

QString Glossary::applyToText(const QList<GlossaryEntry> &entries, const QString &text) {
    QString result = text;
    for (const auto &entry : entries) {
        if (entry.term.isEmpty() || entry.term == entry.replacement) continue;
        if (!entry.regex.isValid()) continue;
        result.replace(entry.regex, entry.replacement);
    }
    return result;
}

QStringList Glossary::preferredTerms(const QList<GlossaryEntry> &entries, int maxTerms)
{
    QStringList terms;
    QSet<QString> seen;
    const QList<GlossaryEntry> selected = selectTopEntries(entries, maxTerms);
    for (const auto &entry : selected) {
        const QString preferred = !entry.replacement.isEmpty() ? entry.replacement : entry.term;
        const QString trimmed = preferred.trimmed();
        if (trimmed.isEmpty()) continue;
        const QString key = trimmed.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        terms << trimmed;
    }
    return terms;
}

QList<GlossaryEntry> Glossary::selectTopEntries(const QList<GlossaryEntry> &entries, int maxTerms)
{
    if (maxTerms <= 0 || entries.size() <= maxTerms) return entries;
    // 輕量 H-PRM：大詞庫時按「中文 > 長詞 > 原序」加權，保留前 maxTerms
    // 1) 中文詞優先（CJK 占比高者更需偏置）
    // 2) 長詞優先（更具體，誤觸發率低）
    // 3) 原始順序作為穩定性 tie-breaker
    struct Scored { int index; int score; int len; };
    QList<Scored> scored;
    scored.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const QString preferred = !entries[i].replacement.isEmpty() ? entries[i].replacement : entries[i].term;
        int cjk = 0;
        for (QChar ch : preferred) {
            const ushort u = ch.unicode();
            if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF) || (u >= 0x3000 && u <= 0x303F))
                ++cjk;
        }
        int len = preferred.trimmed().size();
        // 加權：CJK 每字 10 分 + 長度 + 反序微調（保持原序穩定）
        int score = cjk * 10 + len;
        scored << Scored{i, score, len};
    }
    // 穩定排序：分數高者前，分数相同則原序前
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b){
        if (a.score != b.score) return a.score > b.score;
        return a.index < b.index;
    });
    QSet<int> keep;
    for (int i = 0; i < maxTerms && i < scored.size(); ++i) keep.insert(scored[i].index);
    QList<GlossaryEntry> out;
    out.reserve(maxTerms);
    for (int i = 0; i < entries.size(); ++i) if (keep.contains(i)) out << entries[i];
    return out;
}

QString Glossary::hotwordPromptNatural(const QList<GlossaryEntry> &entries, int maxTerms)
{
    const QStringList terms = preferredTerms(entries, maxTerms);
    if (terms.isEmpty()) return {};
    // 自然句：符合 OpenAI 建議，稀有詞置尾，避免 224 token 截斷丟失重點
    // 中文語境句式，英文詞亦可共存
    const QString joined = terms.join(QStringLiteral("、"));
    // 控制長度：粗略按字符估算 token（中文 1 字 ~1.5 token，英文 1 詞 ~1.3 token），超長時再截斷
    QString prompt = QStringLiteral("這段音訊涉及以下專有名詞與關鍵術語，請保持拼寫一致：%1。").arg(joined);
    // 若仍過長（>800 字符 ≈ 500 tokens），進一步截斷至 20 詞
    if (prompt.size() > 800 && terms.size() > 20) {
        const QStringList shortTerms = preferredTerms(entries, 20);
        prompt = QStringLiteral("這段音訊涉及以下專有名詞與關鍵術語，請保持拼寫一致：%1。").arg(shortTerms.join(QStringLiteral("、")));
    }
    return prompt;
}
