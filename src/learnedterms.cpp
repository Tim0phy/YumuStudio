#include "learnedterms.h"
#include "appconfig.h"
#include "glossary.h"

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
#include <utility>

QString LearnedTerms::filePath() {
    if (AppConfig::isPortableMode())
        return QDir(AppConfig::portableRoot()).filePath("config/learned_terms.json");
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath("learned_terms.json");
}

static QStringList tokenize(const QString &text) {
    QStringList tokens;
    QString current;
    auto flush = [&]() {
        if (!current.isEmpty()) {
            tokens << current;
            current.clear();
        }
    };
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        const bool cjk = (u >= 0x4E00 && u <= 0x9FFF)
                      || (u >= 0x3400 && u <= 0x4DBF)
                      || (u >= 0x3000 && u <= 0x303F);
        if (cjk) {
            flush();
            tokens << QString(ch);
        } else if (ch.isSpace() || QString(".,;:!?\"'()[]{}<>-—/\\|@#$%^&*_+=~`").contains(ch)) {
            flush();
        } else {
            current.append(ch);
        }
    }
    flush();
    return tokens;
}

static bool isPurePunctuationOrDigits(const QString &s) {
    if (s.isEmpty()) return true;
    bool hasDigit = false;
    bool hasPunct = false;
    bool hasOther = false;
    for (QChar ch : s) {
        if (ch.isDigit()) hasDigit = true;
        else if (ch.isPunct() || ch.isSpace()) hasPunct = true;
        else hasOther = true;
    }
    return !hasOther;
}

bool LearnedTerms::isCandidate(const QString &fragment) {
    if (fragment.isEmpty() || fragment.size() > 20) return false;
    if (isPurePunctuationOrDigits(fragment)) return false;
    return true;
}

QList<std::pair<QString,QString>> LearnedTerms::extractChangedFragments(
    const QString &oldText, const QString &newText)
{
    QList<std::pair<QString,QString>> out;
    if (oldText == newText) return out;

    const QStringList oldTokens = tokenize(oldText);
    const QStringList newTokens = tokenize(newText);
    if (oldTokens.isEmpty() || newTokens.isEmpty()) return out;

    // Find longest common prefix and suffix of token lists.
    int prefix = 0;
    while (prefix < oldTokens.size() && prefix < newTokens.size()
           && oldTokens[prefix] == newTokens[prefix]) {
        ++prefix;
    }
    int suffix = 0;
    while (suffix < oldTokens.size() - prefix
           && suffix < newTokens.size() - prefix
           && oldTokens[oldTokens.size() - 1 - suffix] == newTokens[newTokens.size() - 1 - suffix]) {
        ++suffix;
    }

    const int oldStart = prefix;
    const int oldEnd   = oldTokens.size() - suffix;
    const int newStart = prefix;
    const int newEnd   = newTokens.size() - suffix;

    QString oldFragment;
    for (int i = oldStart; i < oldEnd; ++i) {
        if (!oldFragment.isEmpty()) oldFragment += " ";
        oldFragment += oldTokens[i];
    }
    QString newFragment;
    for (int i = newStart; i < newEnd; ++i) {
        if (!newFragment.isEmpty()) newFragment += " ";
        newFragment += newTokens[i];
    }

    if (isCandidate(oldFragment) && isCandidate(newFragment) && oldFragment != newFragment)
        out << std::make_pair(oldFragment, newFragment);
    return out;
}

QList<LearnedPair> LearnedTerms::load() {
    QList<LearnedPair> pairs;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) return pairs;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray array = doc.array();
    for (const auto &value : array) {
        const QJsonObject obj = value.toObject();
        LearnedPair p;
        p.error      = obj.value("error").toString();
        p.correction = obj.value("correction").toString();
        p.count      = obj.value("count").toInt();
        p.lastSeen   = QDateTime::fromString(obj.value("lastSeen").toString(), Qt::ISODate);
        p.promoted   = obj.value("promoted").toBool();
        if (!p.error.isEmpty() && !p.correction.isEmpty())
            pairs << p;
    }
    return pairs;
}

bool LearnedTerms::save(const QList<LearnedPair> &pairs) {
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray array;
    for (const auto &p : pairs) {
        QJsonObject obj;
        obj["error"]      = p.error;
        obj["correction"] = p.correction;
        obj["count"]      = p.count;
        obj["lastSeen"]   = p.lastSeen.toString(Qt::ISODate);
        obj["promoted"]   = p.promoted;
        array << obj;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    return true;
}

void LearnedTerms::recordObservation(const QString &oldText, const QString &newText) {
    if (!AppConfig::instance().system.learnMistakes) return;
    const auto fragments = extractChangedFragments(oldText, newText);
    if (fragments.isEmpty()) return;

    QList<LearnedPair> pairs = load();
    for (const auto &frag : fragments) {
        bool found = false;
        for (auto &p : pairs) {
            if (p.error == frag.first && p.correction == frag.second) {
                p.count = qMin(p.count + 1, 9999);
                p.lastSeen = QDateTime::currentDateTime();
                found = true;
                break;
            }
        }
        if (!found) {
            LearnedPair p;
            p.error      = frag.first;
            p.correction = frag.second;
            p.count      = 1;
            p.lastSeen   = QDateTime::currentDateTime();
            p.promoted   = false;
            pairs << p;
        }
    }
    save(pairs);
}

bool LearnedTerms::promoteToGlossary(const LearnedPair &pair) {
    if (pair.error.isEmpty() || pair.correction.isEmpty() || pair.error == pair.correction)
        return false;
    QList<GlossaryEntry> entries = Glossary::load();
    for (const auto &e : entries) {
        if (e.term == pair.error) return false; // already exists
    }
    GlossaryEntry entry;
    entry.term = pair.error;
    entry.replacement = pair.correction;
    entries << entry;
    return Glossary::save(entries);
}

QList<LearnedPair> LearnedTerms::autoPromote(int threshold) {
    QList<LearnedPair> promoted;
    if (threshold <= 0) return promoted;
    QList<LearnedPair> pairs = load();
    bool changed = false;
    for (auto &p : pairs) {
        if (p.promoted || p.count < threshold) continue;
        if (promoteToGlossary(p)) {
            p.promoted = true;
            promoted << p;
            changed = true;
        }
    }
    if (changed) save(pairs);
    return promoted;
}
