#include "waveformwidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFontMetrics>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
}

void WaveformWidget::setModel(SubtitleModel *model) {
    m_model = model;
    connect(m_model, &SubtitleModel::entriesChanged, this, QOverload<>::of(&QWidget::update));
}

void WaveformWidget::setPositionMs(qint64 ms) {
    m_posMs = ms;
    // v8.7: auto-scroll viewport to keep playhead visible
    if (m_durationMs > 0 && m_zoom > 1.0) {
        qint64 viewLen = qint64(m_durationMs / m_zoom);
        if (ms < m_viewStart || ms > m_viewStart + viewLen)
            m_viewStart = qMax(qint64(0), ms - viewLen/4);
    }
    update();
}

void WaveformWidget::setDurationMs(qint64 ms) {
    m_durationMs = ms;
    m_viewStart  = 0;
    update();
}

qint64 WaveformWidget::xToMs(int x) const {
    if (width() <= 0 || m_durationMs <= 0) return 0;
    qint64 viewLen = (m_zoom > 0) ? qint64(m_durationMs / m_zoom) : m_durationMs;
    return m_viewStart + qint64(x) * viewLen / width();
}

int WaveformWidget::msToX(qint64 ms) const {
    if (m_durationMs <= 0) return 0;
    qint64 viewLen = (m_zoom > 0) ? qint64(m_durationMs / m_zoom) : m_durationMs;
    return int((ms - m_viewStart) * width() / viewLen);
}

qint64 WaveformWidget::snapMs(qint64 ms, int excludeRow, bool isStart) const {
    if (!m_model) return ms;
    for (int i=0; i<m_model->rowCount(); ++i) {
        if (i==excludeRow) continue;
        const auto &e = m_model->entryAt(i);
        for (qint64 t : {e.startMs, e.endMs}) {
            if (qAbs(ms - t) < m_snapThresholdMs) return t;
        }
    }
    return ms;
}

void WaveformWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20,20,30));

    if (m_durationMs <= 0 || !m_model) {
        p.setPen(QColor(100,100,120));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a video to see the waveform"));
        return;
    }

    // Grid lines (every 10 s)
    p.setPen(QColor(45,45,60));
    qint64 gridStep = 10000;
    qint64 viewLen  = qint64(m_durationMs / m_zoom);
    for (qint64 t = 0; t <= m_durationMs; t += gridStep) {
        if (t < m_viewStart || t > m_viewStart + viewLen) continue;
        int x = msToX(t);
        p.drawLine(x, 0, x, height());
    }

    // Fake waveform (sine-like decoration)
    p.setPen(QColor(60, 120, 200, 100));
    const int cy = height()/2;
    for (int x=0; x<width(); ++x) {
        int amp = int(cy * 0.4 * (0.5 + 0.5*qSin(x * 0.15)));
        p.drawLine(x, cy-amp, x, cy+amp);
    }

    // Subtitle blocks
    const int trackH  = 28;
    const int trackY  = height() - trackH - 4;
    QFont fnt = font(); fnt.setPointSize(8); p.setFont(fnt);
    QFontMetrics fm(fnt);

    for (int i=0; i<m_model->rowCount(); ++i) {
        const auto &e = m_model->entryAt(i);
        int x1 = msToX(e.startMs);
        int x2 = msToX(e.endMs);
        if (x2 < 0 || x1 > width()) continue;
        x1 = qMax(x1, 0); x2 = qMin(x2, width());

        QColor fill = (i==m_dragRow) ? QColor(100,180,80,200) : QColor(70,130,60,180);
        p.fillRect(x1, trackY, x2-x1, trackH, fill);
        p.setPen(QColor(200,255,180));
        p.drawRect(x1, trackY, x2-x1, trackH);

        // Drag handles
        p.fillRect(x1, trackY, 5, trackH, QColor(180,255,140,220));
        p.fillRect(x2-5, trackY, 5, trackH, QColor(180,255,140,220));

        // Label
        QString lbl = fm.elidedText(e.text, Qt::ElideRight, qMax(0,x2-x1-6));
        p.setPen(Qt::white);
        p.drawText(x1+3, trackY, x2-x1-6, trackH, Qt::AlignVCenter, lbl);
    }

    // Playhead
    int px = msToX(m_posMs);
    p.setPen(QPen(QColor(255,80,80), 2));
    p.drawLine(px, 0, px, height());

    // Zoom indicator (v8.7)
    if (m_zoom > 1.0) {
        p.setPen(QColor(200,200,100));
        p.drawText(4, 14, QString("Zoom: %1×").arg(m_zoom, 0, 'f', 1));
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent *e) {
    if (!m_model) return;
    const int trackH = 28, trackY = height()-trackH-4;
    if (e->button() == Qt::LeftButton) {
        qint64 clickMs = xToMs(e->pos().x());
        // Check for handle grabs
        for (int i=0; i<m_model->rowCount(); ++i) {
            const auto &en = m_model->entryAt(i);
            int x1=msToX(en.startMs), x2=msToX(en.endMs);
            if (e->pos().y() >= trackY) {
                if (qAbs(e->pos().x()-x1) < 8) { m_dragRow=i; m_dragStart=true; return; }
                if (qAbs(e->pos().x()-x2) < 8) { m_dragRow=i; m_dragStart=false;return; }
            }
        }
        // Seek
        emit seekRequested(clickMs);
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragRow < 0 || !m_model) return;
    qint64 ms = xToMs(e->pos().x());
    ms = snapMs(ms, m_dragRow, m_dragStart);
    auto &en = m_model->entryAt(m_dragRow);
    if (m_dragStart) {
        en.startMs = qMin(ms, en.endMs - 100);
    } else {
        en.endMs = qMax(ms, en.startMs + 100);
    }
    update();
    emit subtitleMoved();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *) {
    if (m_dragRow >= 0) {
        m_dragRow = -1;
        if (m_model) emit m_model->entriesChanged();
    }
}

// v8.7: Ctrl+Wheel = zoom
void WaveformWidget::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        double delta = (e->angleDelta().y() > 0) ? 1.25 : 0.8;
        m_zoom = qBound(1.0, m_zoom * delta, 32.0);
        update();
        e->accept();
    } else {
        // Horizontal scroll when zoomed
        if (m_zoom > 1.0 && m_durationMs > 0) {
            qint64 viewLen = qint64(m_durationMs / m_zoom);
            qint64 step    = viewLen / 10;
            m_viewStart = qBound(qint64(0),
                                 m_viewStart - e->angleDelta().y() * step / 120,
                                 m_durationMs - viewLen);
            update();
            e->accept();
        } else {
            QWidget::wheelEvent(e);
        }
    }
}
