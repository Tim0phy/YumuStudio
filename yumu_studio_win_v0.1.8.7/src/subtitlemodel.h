#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <QColor>

struct SubtitleEntry {
    int     index   = 0;
    qint64  startMs = 0;
    qint64  endMs   = 0;
    QString text;
    QString translation;
};

struct SubtitleStyle {
    QString fontFamily      = "Arial";
    int     fontSize        = 36;
    bool    bold            = false;
    bool    italic          = false;
    QColor  textColor       = Qt::white;
    QColor  outlineColor    = Qt::black;
    int     outlineWidth    = 2;
    bool    showBg          = false;
    QColor  bgColor         = QColor(0,0,0,128);
    int     position        = 0;    // 0=bottom, 1=top, 2=centre
    int     marginV         = 20;
    bool    bilingualEnabled= false;
    int     bilingualFontSize = 28;
    QColor  bilingualColor  = QColor(255,255,180);
};

class SubtitleModel : public QObject {
    Q_OBJECT
public:
    explicit SubtitleModel(QObject *parent = nullptr);

    int  rowCount() const { return m_entries.size(); }
    const QList<SubtitleEntry> &entries() const { return m_entries; }
    SubtitleEntry &entryAt(int i)       { return m_entries[i]; }
    const SubtitleEntry &entryAt(int i) const { return m_entries[i]; }

    SubtitleStyle       &style()       { return m_style; }
    const SubtitleStyle &style() const { return m_style; }

    void appendEntry(const SubtitleEntry &e);
    void removeEntry(int row);
    void insertEntry(int row, const SubtitleEntry &e);
    void moveUp(int row);
    void moveDown(int row);
    void clear();
    void reIndex();

    // ── Editing ──────────────────────────────────────────────────────────────
    void splitAt(int row, qint64 posMs);
    void mergeRows(int r1, int r2);
    // v8.7: shift all timecodes by offsetMs (positive = delay, negative = advance)
    void shiftAll(qint64 offsetMs);

    // ── I/O ──────────────────────────────────────────────────────────────────
    bool importSRT(const QString &path);
    bool exportSRT(const QString &path) const;
    bool exportBilingualSRT(const QString &path) const;

signals:
    void entriesChanged();

private:
    QList<SubtitleEntry> m_entries;
    SubtitleStyle        m_style;
    static QString msToSrtTime(qint64 ms);
};
