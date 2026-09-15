#pragma once
#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QVector>
#include "subtitlemodel.h"
#include "audiopeaks.h"

// Professional-style subtitle timeline (Premiere / DaVinci / CapCut inspired):
//  - ruler with adaptive time-code ticks
//  - drag cue bodies to move whole cues, drag edges to trim
//  - magnetic snapping to other cues, scene cuts, playhead and boundaries
//    with a visual snap guide line
//  - cursor-anchored zoom, keyboard nudge / frame stepping, undo-friendly
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setModel(SubtitleModel *model);
    // Loads the real audio waveform of the given media file (async).
    void setMediaFile(const QString &mediaPath);

    // Scene cut markers (ms) from CutDetector; drawn and used as snap points.
    void setCuts(const QVector<qint64> &cutsMs);

public slots:
    void setPositionMs(qint64 ms);
    void setDurationMs(qint64 ms);
    void onThemeChanged();

signals:
    void seekRequested(qint64 posMs);
    void subtitleMoved();
    void selectionChanged(int row);

protected:
    void paintEvent(QPaintEvent *)    override;
    void mousePressEvent(QMouseEvent *)   override;
    void mouseMoveEvent(QMouseEvent *)    override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *)       override;
    void wheelEvent(QWheelEvent *)       override;
    void enterEvent(QEnterEvent *)     override;
    void leaveEvent(QEvent *)       override;
    void updateCursor();

private:
    enum class DragMode { None, Playhead, TrimStart, TrimEnd, MoveCue };

    bool   m_draggingActive() const { return m_dragMode != DragMode::None; }

    qint64 viewLengthMs() const;
    qint64 xToMs(int x) const;
    int    msToX(qint64 ms) const;
    // Pixel-based magnetic snap: searches cue edges, playhead, scene cuts and
    // timeline bounds. Returns the snapped value (unchanged when nothing is
    // within threshold) and sets *snapped for drawing the guide line.
    qint64 snapMs(qint64 ms, int excludeRow, bool *snapped) const;
    void nudgeSelected(qint64 deltaMs);
    qreal snapThresholdPx() const { return 8.0; }
    static constexpr int kRulerHeight = 20;

    SubtitleModel *m_model      = nullptr;
    qint64         m_posMs      = 0;
    qint64         m_durationMs = 0;
    double         m_zoom       = 1.0;   // 1x – 64x
    qint64         m_viewStart  = 0;     // viewport start in ms

    DragMode m_dragMode = DragMode::None;
    int      m_dragRow = -1;
    qint64   m_dragGrabOffsetMs = 0;     // MoveCue: click position inside the cue

    int m_selectedRow = -1;

    QVector<qint64> m_cuts;

    // Transient snap guide line while dragging.
    bool   m_snapLineVisible = false;
    qint64 m_snapLineMs = 0;

    QLabel *m_hoverLabel = nullptr;
    bool   m_hovering = false;
    qint64 m_hoverPosMs = 0;

    // Playhead repaint gate: position ticks arrive far more often than the
    // eye needs; coalesce them to ~30fps.
    QTimer *m_paintGate = nullptr;
    bool   m_paintPending = false;

    AudioPeaks m_peaks;
    QString    m_mediaPath;
    bool       m_peaksProgressConnected = false;
};
