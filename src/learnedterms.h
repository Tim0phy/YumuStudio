#pragma once
#include <QString>
#include <QList>
#include <QDateTime>

struct LearnedPair {
    QString error;
    QString correction;
    int count = 0;
    QDateTime lastSeen;
    bool promoted = false;
};

class LearnedTerms {
public:
    static QString filePath();
    static QList<LearnedPair> load();
    static bool save(const QList<LearnedPair> &pairs);

    // Record one edit observation. Thread-safe enough for UI thread only.
    static void recordObservation(const QString &oldText, const QString &newText);

    // Promote frequently seen pairs to the creator glossary.
    // Returns the list of newly promoted pairs.
    static QList<LearnedPair> autoPromote(int threshold);

    // Merge a pair into the glossary manually.
    static bool promoteToGlossary(const LearnedPair &pair);

private:
    static bool isCandidate(const QString &fragment);
    static QList<std::pair<QString,QString>> extractChangedFragments(
        const QString &oldText, const QString &newText);
};
