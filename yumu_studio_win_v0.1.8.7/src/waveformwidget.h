#pragma once
#include <QWidget>
#include "subtitlemodel.h"

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setModel(SubtitleModel *model);

public slots:
    void setPositionMs(qint64 ms);
    void setDurationMs(qint64 ms);

signals:
    void seekRequested(qint64 posMs);
    void subtitleMoved();

protected:
    void paintEvent(QPaintEvent *)    override;
    void mousePressEvent(QMouseEvent *)   override;
    void mouseMoveEvent(QMouseEvent *)    override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *)       override;  // v8.7 zoom

private:
    SubtitleModel *m_model      = nullptr;
    qint64         m_posMs      = 0;
    qint64         m_durationMs = 0;
    double         m_zoom       = 1.0;   // v8.7: 1x – 32x
    qint64         m_viewStart  = 0;     // v8.7: viewport start in ms

    int    m_dragRow = -1;
    bool   m_dragStart = false;
    qint64 m_snapThresholdMs = 80;

    qint64 xToMs(int x) const;
    int    msToX(qint64 ms) const;
    qint64 snapMs(qint64 ms, int excludeRow, bool isStart) const;
};
