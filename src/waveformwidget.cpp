#include "waveformwidget.h"
#include "timefmt.h"
#include "uitheme.h"
#include "appconfig.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFontMetrics>
#include <QEnterEvent>
#include <algorithm>

namespace {
// Adaptive ruler tick steps (ms). The smallest step that still leaves at
// least ~72 px between labels is chosen.
const qint64 kTickSteps[] = {
    100, 200, 500, 1000, 2000, 5000, 10000, 15000, 30000,
    60000, 120000, 300000, 600000, 1800000, 3600000
};

qint64 chooseTickStep(qint64 viewLenMs, int widthPx)
{
    for (qint64 step : kTickSteps) {
        const double pxPerStep = double(widthPx) * double(step) / double(qMax<qint64>(viewLenMs, 1));
        if (pxPerStep >= 72.0) return step;
    }
    return kTickSteps[std::size(kTickSteps) - 1];
}

QString rulerLabel(qint64 ms, qint64 step)
{
    const qint64 totalSeconds = ms / 1000;
    const int h = int(totalSeconds / 3600);
    const int m = int((totalSeconds % 3600) / 60);
    const int s = int(totalSeconds % 60);
    if (step >= 3600000) return QString("%1:%2:00").arg(h).arg(m, 2, 10, QChar('0'));
    if (h > 0)          return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    if (step >= 60000)  return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    if (step >= 1000)   return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2.%3").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')).arg(int(ms % 1000) / 100);
}
}

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_hoverLabel = new QLabel(this);
    m_hoverLabel->setStyleSheet(
        QStringLiteral("background: rgba(20, 20, 24, 0.95);"
                       "color: %1;"
                       "padding: 4px 8px;"
                       "border-radius: 4px;"
                       "font-size: 11px;"
                       "font-family: monospace;").arg(UiTheme::hex(UiTheme::accent())));
    m_hoverLabel->setAlignment(Qt::AlignCenter);
    m_hoverLabel->hide();
    m_hoverLabel->setFixedHeight(22);
    m_hoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(&m_peaks, &AudioPeaks::ready, this, QOverload<>::of(&QWidget::update));
    connect(&m_peaks, &AudioPeaks::failed, this, [this](const QString &) { update(); });

    m_paintGate = new QTimer(this);
    m_paintGate->setSingleShot(true);
    connect(m_paintGate, &QTimer::timeout, this, [this] {
        if (m_paintPending) {
            m_paintPending = false;
            update();
            int fps = qBound(10, AppConfig::instance().preview.previewFpsCap, 60);
            int gate = qMax(16, 1000 / fps);
            m_paintGate->start(gate);
        }
    });
}

void WaveformWidget::onThemeChanged() {
    m_hoverLabel->setStyleSheet(
        QStringLiteral("background: %1;"
                       "color: %2;"
                       "padding: 4px 8px;"
                       "border-radius: 4px;"
                       "font-size: 11px;"
                       "font-family: monospace;")
            .arg(UiTheme::hex(UiTheme::popoverBg()), UiTheme::hex(UiTheme::accent())));
    update();
}

void WaveformWidget::setModel(SubtitleModel *model) {
    m_model = model;
    connect(m_model, &SubtitleModel::entriesChanged, this, QOverload<>::of(&QWidget::update));
}

void WaveformWidget::setCuts(const QVector<qint64> &cutsMs) {
    m_cuts = cutsMs;
    update();
}

void WaveformWidget::setMediaFile(const QString &mediaPath) {
    if (m_mediaPath == mediaPath && m_peaks.isReady()) return;
    m_mediaPath = mediaPath;
    if (mediaPath.isEmpty()) {
        m_peaks.cancel();
        m_cuts.clear();
        m_selectedRow = -1;
        update();
        return;
    }
    const auto ffmpeg = AppConfig::instance().paths.ffmpegPath;
    // UniqueConnection: setMediaFile can fire several times per session and a
    // plain connect used to stack duplicate lambdas (progress emitted N times).
    if (!m_peaksProgressConnected) {
        m_peaksProgressConnected = connect(&m_peaks, &AudioPeaks::progress, this,
            [this](int pct) {
                if (pct % 10 == 0 || pct == 100) update();
            }) != nullptr;
    }
    m_cuts.clear();
    m_selectedRow = -1;
    m_peaks.load(mediaPath, ffmpeg, m_durationMs);
    update();
}

void WaveformWidget::setPositionMs(qint64 ms) {
    m_posMs = ms;
    // Auto-scroll viewport to keep the playhead visible — but never while the
    // user is dragging, or the coordinate mapping jumps under their cursor.
    if (!m_draggingActive() && m_durationMs > 0 && m_zoom > 1.0) {
        const qint64 viewLen = viewLengthMs();
        if (ms < m_viewStart || ms > m_viewStart + viewLen)
            m_viewStart = qBound(qint64(0), ms - viewLen / 4, qMax(qint64(0), m_durationMs - viewLen));
    }
    // Coalesce playhead repaints to previewFpsCap; drags and seeks stay immediate
    // because they always run outside an active gate window.
    // 讀取 AppConfig::preview.previewFpsCap 動態 gate，對 HW 疊加層同樣生效
    {
        int fps = qBound(10, AppConfig::instance().preview.previewFpsCap, 60);
        int gate = qMax(16, 1000 / fps);
        if (!m_paintGate->isActive()) {
            update();
            m_paintGate->start(gate);
        } else {
            m_paintPending = true;
        }
    }
}

void WaveformWidget::setDurationMs(qint64 ms) {
    m_durationMs = ms;
    m_viewStart  = 0;
    update();
}

qint64 WaveformWidget::viewLengthMs() const {
    if (m_durationMs <= 0) return 1;
    return qMax<qint64>(1, qint64(double(m_durationMs) / m_zoom));
}

qint64 WaveformWidget::xToMs(int x) const {
    if (width() <= 0 || m_durationMs <= 0) return 0;
    return m_viewStart + qint64(x) * viewLengthMs() / width();
}

int WaveformWidget::msToX(qint64 ms) const {
    if (m_durationMs <= 0 || width() <= 0) return 0;
    return int(double(ms - m_viewStart) * width() / viewLengthMs());
}

qint64 WaveformWidget::snapMs(qint64 ms, int excludeRow, bool *snapped) const
{
    if (snapped) *snapped = false;
    if (!m_model || m_durationMs <= 0 || width() <= 0) return ms;
    // Threshold follows the current zoom so snapping stays ~8 px on screen:
    // precise when zoomed in, never a huge invisible field when zoomed out.
    const qint64 thresholdMs = qint64(snapThresholdPx() * viewLengthMs() / width());
    if (thresholdMs <= 0) return ms;

    qint64 best = -1;
    qint64 bestDist = thresholdMs + 1;

    auto consider = [&](qint64 t) {
        const qint64 d = qAbs(ms - t);
        if (d < bestDist) { bestDist = d; best = t; }
    };

    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (i == excludeRow) continue;
        consider(m_model->entryAt(i).startMs);
        consider(m_model->entryAt(i).endMs);
    }
    for (qint64 cut : m_cuts) consider(cut);
    consider(m_posMs);          // playhead
    consider(qint64(0));        // timeline start
    consider(m_durationMs);     // timeline end

    if (best < 0) return ms;
    if (snapped) *snapped = true;
    return best;
}

void WaveformWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), UiTheme::timelineBg());

    if (!m_model || m_durationMs <= 0) {
        p.setPen(UiTheme::textMuted());
        p.drawText(rect(), Qt::AlignCenter, tr("Open a video to see the waveform timeline"));
        return;
    }

    const qint64 viewLen = viewLengthMs();
    const int trackH = 28;
    const int trackY = height() - trackH - 4;
    const int waveTop = kRulerHeight;
    const int waveBottom = trackY - 2;

    // ── Ruler with adaptive time-code ticks ─────────────────────────────────
    const qint64 step = chooseTickStep(viewLen, width());
    QFont rulerFont = font(); rulerFont.setPointSize(7); p.setFont(rulerFont);
    QFontMetrics rfm(rulerFont);
    const qint64 firstTick = (m_viewStart / step) * step;
    for (qint64 t = firstTick; t <= m_viewStart + viewLen; t += step) {
        if (t < 0) continue;
        const int x = msToX(t);
        p.setPen(UiTheme::grid());
        p.drawLine(x, waveTop, x, height());
        p.setPen(UiTheme::textMuted());
        p.drawText(x + 3, waveTop + 12, rulerLabel(t, step));
    }

    // ── Real waveform rendered as a smooth line-wave ────────────────────────
    // Per-column samples come from the decoded peak array. When zoomed in
    // (more than ~1 px per peak) neighbouring peaks are interpolated so the
    // wave reads as a continuous curved line instead of blocky pixel bars.
    const bool havePeaks = m_peaks.isReady() && !m_peaks.isEmpty();
    if (havePeaks) {
        const QVector<AudioPeaks::Peak> &peaks = m_peaks.peaks();
        const double totalMs = double(m_durationMs);
        const double msPerPeak = totalMs / peaks.size();
        const double viewLenD = double(qMax<qint64>(viewLen, 1));
        const double pxPerPeak = width() * msPerPeak / viewLenD;
        const bool smooth = pxPerPeak >= 1.25;
        const double midY = (waveTop + waveBottom) / 2.0;
        const double amp  = (waveBottom - waveTop) * 0.45;
        const int lastPeak = static_cast<int>(peaks.size()) - 1;

        auto sampleAt = [&](double tMs, float &lo, float &hi) {
            if (smooth) {
                const double c = tMs / msPerPeak;
                const int a = std::clamp(int(std::floor(c)), 0, lastPeak);
                const int b = std::min(a + 1, lastPeak);
                const double f = qBound(0.0, c - a, 1.0);
                lo = float(peaks[a].lo + (peaks[b].lo - peaks[a].lo) * f);
                hi = float(peaks[a].hi + (peaks[b].hi - peaks[a].hi) * f);
            } else {
                const int i0 = std::clamp(int(tMs / msPerPeak), 0, lastPeak);
                const double tEnd = tMs + viewLenD / qMax(1, width());
                const int i1 = std::clamp(int(std::max(tEnd / msPerPeak, double(i0 + 1))), i0 + 1, lastPeak + 1);
                lo = 1.0f; hi = -1.0f;
                for (int i = i0; i < i1; ++i) {
                    lo = std::min(lo, peaks[i].lo);
                    hi = std::max(hi, peaks[i].hi);
                }
                if (hi < lo) { lo = 0.0f; hi = 0.0f; }
            }
        };

        // One closed envelope path spanning the visible range: upper contour
        // left→right, lower contour right→left.
        const int w = width();
        QPolygonF outline;
        outline.reserve((w + 1) * 2);
        for (int x = 0; x <= w; ++x) {
            float lo, hi;
            sampleAt(double(m_viewStart) + double(x) * viewLenD / w, lo, hi);
            outline << QPointF(qreal(x), midY - hi * amp);
        }
        for (int x = w; x >= 0; --x) {
            float lo, hi;
            sampleAt(double(m_viewStart) + double(x) * viewLenD / w, lo, hi);
            outline << QPointF(qreal(x), midY - lo * amp);
        }
        QPainterPath envelope;
        envelope.addPolygon(outline);
        envelope.closeSubpath();

        QColor pendFill = UiTheme::wave();       pendFill.setAlpha(80);
        QColor pendLine = UiTheme::wave();       pendLine.setAlpha(210);
        QColor playFill = UiTheme::wavePlayed(); playFill.setAlpha(120);
        QColor playLine = UiTheme::wavePlayed(); playLine.setAlpha(240);

        p.setRenderHint(QPainter::Antialiasing, true);
        const auto drawRegion = [&](const QRect &clip, const QColor &fill, const QColor &line) {
            if (!clip.isValid() || clip.width() <= 0) return;
            p.save();
            p.setClipRect(clip);
            p.fillPath(envelope, fill);
            p.strokePath(envelope, QPen(line, 1));
            p.restore();
        };
        const int playX = msToX(m_posMs);
        drawRegion(QRect(0, 0, playX, height()), playFill, playLine);
        drawRegion(QRect(playX, 0, w - playX, height()), pendFill, pendLine);
        p.setRenderHint(QPainter::Antialiasing, false);
    } else {
        p.setPen(UiTheme::textMuted());
        QString msg = tr("Analyzing audio waveform...");
        if (!m_peaks.lastError().isEmpty()) msg = m_peaks.lastError();
        p.drawText(QRect(0, waveTop, width(), qMax(1, waveBottom - waveTop)), Qt::AlignCenter, msg);
    }

    // ── Scene cut markers ────────────────────────────────────────────────────
    p.setFont(rulerFont);
    for (qint64 cut : m_cuts) {
        const int x = msToX(cut);
        if (x < -1 || x > width() + 1) continue;
        QColor cutColor = UiTheme::warning();
        cutColor.setAlpha(160);
        p.setPen(QPen(cutColor, 1, Qt::DashLine));
        p.drawLine(x, waveTop, x, trackY);
    }

    // ── Snap guide line while dragging ───────────────────────────────────────
    if (m_snapLineVisible) {
        const int sx = msToX(m_snapLineMs);
        QColor guideColor = UiTheme::accent();
        guideColor.setAlpha(220);
        p.setPen(QPen(guideColor, 1));
        p.drawLine(sx, waveTop, sx, trackY + trackH);
    }

    // ── Subtitle blocks ──────────────────────────────────────────────────────
    QFont fnt = font(); fnt.setPointSize(8); p.setFont(fnt);
    QFontMetrics fm(fnt);

    for (int i=0; i<m_model->rowCount(); ++i) {
        const auto &e = m_model->entryAt(i);
        int x1 = msToX(e.startMs);
        int x2 = msToX(e.endMs);
        if (x2 < 0 || x1 > width()) continue;
        x1 = qMax(x1, 0); x2 = qMin(x2, width());

        // Library ledger blocks — cream / amber active
        const bool active = (i == m_dragRow) || (i == m_selectedRow);
        QColor baseFill = UiTheme::isDark() ? QColor(38,38,40) : QColor(255,253,240);
        baseFill.setAlpha(active ? 0 : 212);
        QColor amberFill = UiTheme::accent(); amberFill.setAlpha(210);
        QColor fill;
        if (i == m_dragRow) fill = amberFill;
        else if (i == m_selectedRow) fill = amberFill;
        else fill = baseFill;
        QColor borderCol = active ? UiTheme::accent() : UiTheme::borderSoft();
        if (!active && UiTheme::isDark()) borderCol = QColor(46,46,50);
        p.fillRect(x1, trackY, x2-x1, trackH, fill);
        p.setPen(borderCol);
        p.drawRect(x1, trackY, x2-x1, trackH);

        // Trim handles — ink hairline with accent dot when active
        QColor handleCol = active ? UiTheme::accent() : UiTheme::text();
        handleCol.setAlpha(active ? 230 : 180);
        p.fillRect(x1, trackY, 4, trackH, handleCol);
        p.fillRect(x2-4, trackY, 4, trackH, handleCol);

        // Label — ink on paper, white on accent
        QString lbl = fm.elidedText(e.text, Qt::ElideRight, qMax(0,x2-x1-6));
        p.setPen(active ? QColor(255,255,255) : UiTheme::text());
        p.drawText(x1+6, trackY, x2-x1-12, trackH, Qt::AlignVCenter, lbl);
    }

    // ── Playhead ─────────────────────────────────────────────────────────────
    const int px = msToX(m_posMs);
    p.setPen(QPen(UiTheme::accent(), 1.5));
    p.drawLine(px, waveTop, px, height());
    {
        QPolygon head;
        head << QPoint(px - 6, waveTop) << QPoint(px + 6, waveTop) << QPoint(px, waveTop + 9);
        p.setBrush(UiTheme::accent());
        p.setPen(Qt::NoPen);
        p.drawPolygon(head);
    }

    // Zoom indicator
    if (m_zoom > 1.0) {
        p.setPen(UiTheme::warning());
        p.drawText(4, 14, tr("Zoom: %1×").arg(m_zoom, 0, 'f', 1));
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent *e) {
    if (!m_model) return;
    if (e->button() != Qt::LeftButton) {
        // Right-click anywhere = seek without grabbing anything.
        if (e->button() == Qt::RightButton)
            emit seekRequested(xToMs(e->pos().x()));
        return;
    }

    const int trackH = 28, trackY = height()-trackH-4;
    const bool inTrackArea = e->pos().y() >= trackY;
    setFocus(Qt::MouseFocusReason);

    // 1) Subtitle handles/body take priority over the playhead so edges near
    //    the playhead stay grabbable (the old code masked them out).
    if (inTrackArea) {
        int bestRow = -1; DragMode bestMode = DragMode::None; int bestDist = 7;
        for (int i=0; i<m_model->rowCount(); ++i) {
            const auto &en = m_model->entryAt(i);
            const int x1 = msToX(en.startMs), x2 = msToX(en.endMs);
            const int dStart = qAbs(e->pos().x()-x1);
            const int dEnd   = qAbs(e->pos().x()-x2);
            if (dStart < bestDist) { bestDist = dStart; bestRow = i; bestMode = DragMode::TrimStart; }
            if (dEnd   < bestDist) { bestDist = dEnd;   bestRow = i; bestMode = DragMode::TrimEnd; }
        }
        if (bestRow >= 0) {
            m_dragRow = bestRow; m_dragMode = bestMode;
            m_model->beginTimelineEdit();
            if (m_selectedRow != bestRow) { m_selectedRow = bestRow; emit selectionChanged(bestRow); }
            return;
        }
        // Body grab → move the whole cue.
        for (int i=0; i<m_model->rowCount(); ++i) {
            const auto &en = m_model->entryAt(i);
            const int x1 = msToX(en.startMs), x2 = msToX(en.endMs);
            if (e->pos().x() >= x1 && e->pos().x() <= x2) {
                m_dragRow = i; m_dragMode = DragMode::MoveCue;
                m_dragGrabOffsetMs = xToMs(e->pos().x()) - en.startMs;
                m_model->beginTimelineEdit();
                if (m_selectedRow != i) { m_selectedRow = i; emit selectionChanged(i); }
                return;
            }
        }
        // Clicked empty track area: deselect and seek.
        if (m_selectedRow >= 0) { m_selectedRow = -1; emit selectionChanged(-1); }
    }

    // 2) Playhead drag.
    const int px = msToX(m_posMs);
    if (qAbs(e->pos().x() - px) < 10) {
        m_dragMode = DragMode::Playhead;
        emit seekRequested(xToMs(e->pos().x()));
        return;
    }

    // 3) Plain seek.
    emit seekRequested(xToMs(e->pos().x()));
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *e) {
    // Hover timestamp
    if (m_hovering && m_durationMs > 0 && !m_draggingActive()) {
        m_hoverPosMs = qBound(qint64(0), xToMs(e->pos().x()), m_durationMs);
        m_hoverLabel->setText(TimeFmt::formatClock(m_hoverPosMs, true));

        int px = msToX(m_hoverPosMs);
        int labelX = px - m_hoverLabel->width() / 2;
        labelX = qBound(0, labelX, width() - m_hoverLabel->width());
        m_hoverLabel->move(labelX, kRulerHeight + 2);
    }
    updateCursor();

    if (m_dragMode == DragMode::Playhead) {
        qint64 ms = qBound(qint64(0), xToMs(e->pos().x()), m_durationMs);
        emit seekRequested(ms);
        update();
        return;
    }

    if (m_dragMode == DragMode::None || m_dragRow < 0 || !m_model) return;

    qint64 ms = xToMs(e->pos().x());
    auto &en = m_model->entryAt(m_dragRow);

    if (m_dragMode == DragMode::MoveCue) {
        const qint64 len = en.endMs - en.startMs;
        qint64 newStart = ms - m_dragGrabOffsetMs;
        bool snapped = false;
        // Prefer snapping the leading edge; fall back to the trailing edge.
        qint64 s = snapMs(newStart, m_dragRow, &snapped);
        if (snapped) {
            m_snapLineVisible = true;
            m_snapLineMs = s;
        } else {
            const qint64 snappedEnd = snapMs(newStart + len, m_dragRow, &snapped);
            if (snapped) {
                s = snappedEnd - len;
                m_snapLineVisible = true;
                m_snapLineMs = snappedEnd;
            } else {
                m_snapLineVisible = false;
                s = newStart;
            }
        }
        s = qBound(qint64(0), s, qMax(qint64(0), m_durationMs - len));
        en.endMs   = s + len;
        en.startMs = s;
    } else {
        bool snapped = false;
        const qint64 s = snapMs(ms, m_dragRow, &snapped);
        m_snapLineVisible = snapped;
        m_snapLineMs = s;
        if (m_dragMode == DragMode::TrimStart) {
            en.startMs = std::clamp<qint64>(s, 0, std::max<qint64>(0, en.endMs - 100));
        } else {
            const qint64 maxEnd = m_durationMs > 0 ? m_durationMs : s;
            en.endMs = std::clamp<qint64>(s, en.startMs + 100, std::max(en.startMs + 100, maxEnd));
        }
    }
    update();
    emit subtitleMoved();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *) {
    if (m_dragMode != DragMode::None) {
        if (m_dragRow >= 0 && m_model) emit m_model->entriesChanged();
        m_dragRow = -1;
        m_dragMode = DragMode::None;
        m_snapLineVisible = false;
        update();
    }
}

void WaveformWidget::keyPressEvent(QKeyEvent *e) {
    if (!m_model) { QWidget::keyPressEvent(e); return; }
    const qint64 viewLen = viewLengthMs();
    // Frame-like step: ~1/20 of the visible window, clamped to 10–500 ms.
    qint64 stepMs = qBound(qint64(10), viewLen / 20, qint64(500));

    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const qint64 dir = (e->key() == Qt::Key_Right) ? 1 : -1;
        if (m_selectedRow >= 0 && e->modifiers() & Qt::AltModifier) {
            nudgeSelected(dir * (e->modifiers() & Qt::ShiftModifier ? 10 : 100));
            return;
        }
        emit seekRequested(qBound(qint64(0), m_posMs + dir * stepMs, m_durationMs));
        return;
    }
    case Qt::Key_Home:
        emit seekRequested(0);
        return;
    case Qt::Key_End:
        emit seekRequested(m_durationMs);
        return;
    case Qt::Key_Escape:
        if (m_selectedRow >= 0) { m_selectedRow = -1; emit selectionChanged(-1); update(); }
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selectedRow >= 0 && m_selectedRow < m_model->rowCount()) {
            m_model->removeEntry(m_selectedRow);
            m_selectedRow = -1;
            emit selectionChanged(-1);
        }
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void WaveformWidget::nudgeSelected(qint64 deltaMs) {
    if (m_selectedRow < 0 || m_selectedRow >= m_model->rowCount()) return;
    auto &en = m_model->entryAt(m_selectedRow);
    m_model->beginTimelineEdit();
    const qint64 len = en.endMs - en.startMs;
    qint64 s = qBound(qint64(0), en.startMs + deltaMs, qMax(qint64(0), m_durationMs - len));
    en.startMs = s;
    en.endMs   = s + len;
    emit subtitleMoved();
    update();
}

void WaveformWidget::enterEvent(QEnterEvent *) {
    m_hovering = true;
    m_hoverLabel->show();
    updateCursor();
}

void WaveformWidget::leaveEvent(QEvent *) {
    m_hovering = false;
    m_hoverLabel->hide();
    unsetCursor();
}

void WaveformWidget::updateCursor() {
    if (!m_model || m_durationMs <= 0 || m_draggingActive()) return;
    const QPoint pos = mapFromGlobal(QCursor::pos());
    const int trackY = height() - 28 - 4;

    // Over a trim handle?
    if (pos.y() >= trackY) {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const auto &en = m_model->entryAt(i);
            if (qAbs(pos.x() - msToX(en.startMs)) < 6 ||
                qAbs(pos.x() - msToX(en.endMs))   < 6) {
                setCursor(Qt::SizeHorCursor);
                return;
            }
        }
    }
    if (qAbs(pos.x() - msToX(m_posMs)) < 10) { setCursor(Qt::SizeHorCursor); return; }
    unsetCursor();
}

void WaveformWidget::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        // Cursor-anchored zoom: the time-code under the mouse stays put.
        const int mouseX = e->position().x();
        const qint64 anchorMs = xToMs(mouseX);
        const double factor = (e->angleDelta().y() > 0) ? 1.25 : 0.8;
        m_zoom = qBound(1.0, m_zoom * factor, 64.0);
        if (m_durationMs > 0 && width() > 0) {
            const qreal ratio = qreal(mouseX) / width();
            m_viewStart = qBound(qint64(0),
                                 anchorMs - qint64(ratio * viewLengthMs()),
                                 qMax(qint64(0), m_durationMs - viewLengthMs()));
        }
        update();
        e->accept();
    } else if (m_zoom > 1.0 && m_durationMs > 0) {
        // Plain wheel pans horizontally once zoomed in.
        const qint64 viewLen = viewLengthMs();
        const qint64 step = viewLen / 10;
        m_viewStart = qBound(qint64(0),
                             m_viewStart - e->angleDelta().y() * step / 120,
                             m_durationMs - viewLen);
        update();
        e->accept();
    } else {
        QWidget::wheelEvent(e);
    }
}
