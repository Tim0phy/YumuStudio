#include "subtitlemodel.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <algorithm>

SubtitleModel::SubtitleModel(QObject *parent) : QObject(parent) {}

void SubtitleModel::appendEntry(const SubtitleEntry &e) {
    m_entries.append(e);
    emit entriesChanged();
}

void SubtitleModel::removeEntry(int row) {
    if (row < 0 || row >= m_entries.size()) return;
    m_entries.removeAt(row);
    reIndex();
    emit entriesChanged();
}

void SubtitleModel::insertEntry(int row, const SubtitleEntry &e) {
    m_entries.insert(row, e);
    reIndex();
    emit entriesChanged();
}

void SubtitleModel::moveUp(int row) {
    if (row <= 0 || row >= m_entries.size()) return;
    m_entries.swapItemsAt(row - 1, row);
    reIndex();
    emit entriesChanged();
}

void SubtitleModel::moveDown(int row) {
    if (row < 0 || row >= m_entries.size() - 1) return;
    m_entries.swapItemsAt(row, row + 1);
    reIndex();
    emit entriesChanged();
}

void SubtitleModel::clear() {
    m_entries.clear();
    emit entriesChanged();
}

void SubtitleModel::reIndex() {
    for (int i = 0; i < m_entries.size(); ++i)
        m_entries[i].index = i + 1;
}

void SubtitleModel::splitAt(int row, qint64 posMs) {
    if (row < 0 || row >= m_entries.size()) return;
    SubtitleEntry orig = m_entries[row];
    if (posMs <= orig.startMs || posMs >= orig.endMs) return;

    // Try to split text at a word boundary
    int splitPos = orig.text.size() / 2;
    int spaceIdx = orig.text.indexOf(' ', splitPos);
    if (spaceIdx > 0) splitPos = spaceIdx;

    SubtitleEntry a = orig, b = orig;
    a.endMs   = posMs;
    a.text    = orig.text.left(splitPos).trimmed();
    b.startMs = posMs;
    b.text    = orig.text.mid(splitPos).trimmed();

    m_entries[row] = a;
    m_entries.insert(row + 1, b);
    reIndex();
    emit entriesChanged();
}

void SubtitleModel::mergeRows(int r1, int r2) {
    if (r1 < 0 || r2 >= m_entries.size() || r1 >= r2) return;
    SubtitleEntry merged = m_entries[r1];
    merged.endMs = m_entries[r2].endMs;
    for (int i = r1 + 1; i <= r2; ++i)
        merged.text += " " + m_entries[i].text;
    merged.text = merged.text.trimmed();
    m_entries[r1] = merged;
    for (int i = r2; i > r1; --i)
        m_entries.removeAt(i);
    reIndex();
    emit entriesChanged();
}

// v8.7 NEW: shift all timecodes
void SubtitleModel::shiftAll(qint64 offsetMs) {
    for (auto &e : m_entries) {
        e.startMs = qMax(qint64(0), e.startMs + offsetMs);
        e.endMs   = qMax(qint64(0), e.endMs   + offsetMs);
    }
    emit entriesChanged();
}

// ── SRT helpers ───────────────────────────────────────────────────────────────
QString SubtitleModel::msToSrtTime(qint64 ms) {
    int h = int(ms / 3600000); ms %= 3600000;
    int m = int(ms / 60000);   ms %= 60000;
    int s = int(ms / 1000);    ms %= 1000;
    return QString("%1:%2:%3,%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

static qint64 srtTimeToMs(const QString &t) {
    auto c = t.trimmed().split(':');
    if (c.size() < 3) return 0;
    auto s = c[2].split(',');
    return qint64(c[0].toInt())*3600000
         + c[1].toInt()*60000
         + s[0].toInt()*1000
         + (s.size()>1 ? s[1].toInt() : 0);
}

bool SubtitleModel::importSRT(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    in.setAutoDetectUnicode(true);

    m_entries.clear();
    static QRegularExpression timeLine(
        R"((\d{2}:\d{2}:\d{2},\d{3})\s*-->\s*(\d{2}:\d{2}:\d{2},\d{3}))");

    SubtitleEntry cur;
    bool inBlock = false;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            if (inBlock && !cur.text.isEmpty()) {
                m_entries.append(cur);
                cur = {};
            }
            inBlock = false;
            continue;
        }
        auto m = timeLine.match(line);
        if (m.hasMatch()) {
            cur.startMs = srtTimeToMs(m.captured(1));
            cur.endMs   = srtTimeToMs(m.captured(2));
            inBlock     = true;
            continue;
        }
        bool ok;
        int idx = line.toInt(&ok);
        if (ok && !inBlock) { cur.index = idx; continue; }
        if (inBlock) {
            if (!cur.text.isEmpty()) cur.text += "\n";
            cur.text += line;
        }
    }
    if (inBlock && !cur.text.isEmpty()) m_entries.append(cur);
    reIndex();
    emit entriesChanged();
    return !m_entries.isEmpty();
}

bool SubtitleModel::exportSRT(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const auto &e : m_entries)
        out << e.index << "\n"
            << msToSrtTime(e.startMs) << " --> " << msToSrtTime(e.endMs) << "\n"
            << e.text << "\n\n";
    return true;
}

bool SubtitleModel::exportBilingualSRT(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const auto &e : m_entries) {
        out << e.index << "\n"
            << msToSrtTime(e.startMs) << " --> " << msToSrtTime(e.endMs) << "\n"
            << e.text;
        if (!e.translation.isEmpty()) out << "\n" << e.translation;
        out << "\n\n";
    }
    return true;
}
