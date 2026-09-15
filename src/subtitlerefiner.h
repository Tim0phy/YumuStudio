#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

struct SubtitleEntry;
struct RefineParams {
    int maxCharsPerLine = 16;
    int maxLines = 2;
    qint64 minCueMs = 900;
    qint64 maxCueMs = 6000;
};

class SubtitleRefiner {
public:
    static QList<SubtitleEntry> refine(const QList<SubtitleEntry> &entries,
                                       const RefineParams &params);
    static void snapToCuts(QList<SubtitleEntry> &entries, const QVector<qint64> &cutPointsMs,
                           qint64 toleranceMs);
};
