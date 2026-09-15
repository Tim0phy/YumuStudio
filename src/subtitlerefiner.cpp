#include "subtitlerefiner.h"
#include "subtitlemodel.h"

#include <QRegularExpression>
#include <algorithm>
#include <numeric>

namespace {

int visualLength(const QString &text) {
    int length = 0;
    for (const QChar &ch : text)
        length += ch.unicode() > 0x2E80 ? 2 : 1;
    return (length + 1) / 2;
}

QStringList breakIntoChunks(const QString &text, int maxChars) {
    QStringList chunks;
    QString current;
    const QStringList tokens = text.split(' ', Qt::SkipEmptyParts);
    const bool spaced = tokens.size() > 1 || text.contains(' ');
    if (!spaced) {
        for (int i = 0; i < text.size(); i += maxChars)
            chunks << text.mid(i, maxChars);
        return chunks;
    }
    for (const QString &token : tokens) {
        if (!current.isEmpty() && current.size() + 1 + token.size() > maxChars) {
            chunks << current;
            current.clear();
        }
        if (token.size() > maxChars * 2) {
            if (!current.isEmpty()) { chunks << current; current.clear(); }
            for (int i = 0; i < token.size(); i += maxChars)
                chunks << token.mid(i, maxChars);
            continue;
        }
        current = current.isEmpty() ? token : current + QLatin1Char(' ') + token;
    }
    if (!current.isEmpty()) chunks << current;
    return chunks;
}

QList<SubtitleEntry> splitEntry(const SubtitleEntry &entry, const RefineParams &params) {
    QList<SubtitleEntry> out;
    const int maxCueChars = qMax(4, params.maxCharsPerLine * qMax(1, params.maxLines));
    QStringList sentences;
    QString pending;
    const QRegularExpression sentenceEnd(QStringLiteral("[。！？!?；;\n]+|…+"));
    qsizetype start = 0;
    auto match = sentenceEnd.globalMatch(entry.text);
    while (match.hasNext()) {
        const auto m = match.next();
        const QString piece = entry.text.mid(start, m.capturedEnd() - start).trimmed();
        start = m.capturedEnd();
        if (!piece.isEmpty()) sentences << piece;
    }
    const QString tail = entry.text.mid(start).trimmed();
    if (!tail.isEmpty()) sentences << tail;

    QStringList units;
    for (const QString &sentence : sentences) {
        if (visualLength(sentence) <= maxCueChars) { units << sentence; continue; }
        const QStringList chunks = breakIntoChunks(sentence, params.maxCharsPerLine);
        QString merged;
        for (const QString &chunk : chunks) {
            if (!merged.isEmpty()
                && visualLength(merged) + visualLength(chunk) <= maxCueChars
                && !sentenceEnd.match(chunk).hasMatch()) {
                merged += QLatin1Char(' ') + chunk;
            } else {
                if (!merged.isEmpty()) units << merged;
                merged = chunk;
            }
        }
        if (!merged.isEmpty()) units << merged;
    }
    if (units.isEmpty())
        return {entry};

    qint64 totalWeight = 0;
    QVector<qint64> weights;
    weights.reserve(units.size());
    for (const QString &unit : units) {
        const qint64 weight = qMax<qint64>(1, visualLength(unit));
        weights << weight;
        totalWeight += weight;
    }
    const qint64 span = qMax<qint64>(1, entry.endMs - entry.startMs);
    qint64 cursorMs = entry.startMs;
    for (int i = 0; i < units.size(); ++i) {
        qint64 endMs = (i == units.size() - 1)
            ? entry.endMs
            : entry.startMs + span * std::accumulate(weights.begin(), weights.begin() + i + 1, qint64(0)) / totalWeight;
        endMs = qBound(cursorMs, endMs, entry.endMs);
        SubtitleEntry piece;
        piece.startMs = cursorMs;
        piece.endMs = qMax(endMs, cursorMs + 200);
        piece.text = units.at(i);
        piece.translation = (i == 0) ? entry.translation : QString();
        out << piece;
        cursorMs = piece.endMs;
    }
    return out;
}

qint64 nearestCut(const QVector<qint64> &cuts, qint64 value, qint64 tolerance, bool *hit) {
    auto it = std::lower_bound(cuts.begin(), cuts.end(), value - tolerance);
    qint64 best = value;
    qint64 bestDelta = tolerance + 1;
    while (it != cuts.end() && *it <= value + tolerance) {
        const qint64 delta = qAbs(*it - value);
        if (delta < bestDelta) { bestDelta = delta; best = *it; }
        ++it;
    }
    if (hit) *hit = bestDelta <= tolerance;
    return best;
}

} // namespace

QList<SubtitleEntry> SubtitleRefiner::refine(const QList<SubtitleEntry> &entries,
                                             const RefineParams &params) {
    QList<SubtitleEntry> out;
    for (const auto &entry : entries) {
        if (entry.text.trimmed().isEmpty() || entry.endMs <= entry.startMs) {
            out << entry;
            continue;
        }
        const int length = visualLength(entry.text);
        const qint64 duration = entry.endMs - entry.startMs;
        const bool tooLong = length > qMax(4, params.maxCharsPerLine * qMax(1, params.maxLines))
                          || duration > params.maxCueMs;
        if (!tooLong) { out << entry; continue; }
        out << splitEntry(entry, params);
    }
    for (int i = 0; i < out.size(); ++i) out[i].index = i + 1;
    return out;
}

void SubtitleRefiner::snapToCuts(QList<SubtitleEntry> &entries, const QVector<qint64> &cutPointsMs,
                                 qint64 toleranceMs) {
    if (cutPointsMs.isEmpty() || toleranceMs <= 0) return;
    QVector<qint64> cuts = cutPointsMs;
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (auto &entry : entries) {
        bool hitStart = false, hitEnd = false;
        const qint64 snappedStart = nearestCut(cuts, entry.startMs, toleranceMs, &hitStart);
        const qint64 snappedEnd = nearestCut(cuts, entry.endMs, toleranceMs, &hitEnd);
        if (hitStart && (!hitEnd || snappedStart < snappedEnd))
            entry.startMs = snappedStart;
        if (hitEnd && snappedEnd > entry.startMs)
            entry.endMs = snappedEnd;
    }
}
