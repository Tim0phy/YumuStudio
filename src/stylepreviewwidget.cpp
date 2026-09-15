#include "stylepreviewwidget.h"
#include "uitheme.h"
#include <QPainter>
#include <QFontMetrics>

StylePreviewWidget::StylePreviewWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(110);
    m_text  = tr("Preview subtitle text");
    m_trans = tr("Translation preview");
}

void StylePreviewWidget::retranslateUi() {
    m_text  = tr("Preview subtitle text");
    m_trans = tr("Translation preview");
    update();
}

void StylePreviewWidget::setStyle(const SubtitleStyle &s) {
    m_style = s;
    update();
}

void StylePreviewWidget::setSampleText(const QString &t, const QString &r) {
    m_text = t; m_trans = r; update();
}

void StylePreviewWidget::drawOutlinedText(QPainter &p, const QString &txt,
                                          QColor fg, QRect rect, int flags)
{
    int ow = m_style.outlineWidth;
    if (m_style.shadowWidth > 0 && m_style.shadowOpacity > 0) {
        QColor sc = m_style.shadowColor.isValid() ? m_style.shadowColor : QColor(0, 0, 0);
        sc.setAlpha(qBound(0, m_style.shadowOpacity * 255 / 100, 255));
        p.setPen(sc);
        p.drawText(rect.translated(m_style.shadowWidth, m_style.shadowWidth), flags, txt);
    }
    if (ow > 0) {
        p.setPen(m_style.outlineColor);
        for (int dx = -ow; dx <= ow; ++dx)
            for (int dy = -ow; dy <= ow; ++dy)
                if (dx || dy)
                    p.drawText(rect.translated(dx, dy), flags, txt);
    }
    p.setPen(fg);
    p.drawText(rect, flags, txt);
}

void StylePreviewWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), UiTheme::panelHover());

    if (m_thumbnail) {
        // Gallery strip: one centred line, pixel-sized from widget height so
        // high-DPI scaling can never blow it out of the strip. If the strip
        // is narrow, shrink the font until the whole sample fits: the preset
        // must always display completely, never clipped.
        const QString txt = m_text.isEmpty() ? QStringLiteral("Aa") : m_text;
        int px = qMax(9, int(height() * 0.30));
        QFont f(m_style.fontFamily);
        f.setBold(m_style.bold);
        f.setItalic(m_style.italic);
        QFontMetrics fm(f);
        for (;;) {
            f.setPixelSize(px);
            fm = QFontMetrics(f);
            if (fm.horizontalAdvance(txt) <= width() - 20 || px <= 10) break;
            px -= 2;
        }
        p.setFont(f);
        const int tw = fm.horizontalAdvance(txt);
        const int lineH = fm.height();
        const QRect line((width() - tw) / 2 - 8, (height() - lineH) / 2 - 2,
                         tw + 16, lineH + 4);
        if (m_style.showBg) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_style.bgColor.isValid() ? m_style.bgColor : QColor(0, 0, 0, 180));
            p.drawRoundedRect(line, 3, 3);
        }
        // Thin the outline for the small strip; full width would blob.
        const SubtitleStyle saved = m_style;
        m_style.outlineWidth = qMin(m_style.outlineWidth, 2);
        m_style.shadowWidth = qMin(m_style.shadowWidth, 1);
        drawOutlinedText(p, txt, m_style.textColor.isValid() ? m_style.textColor : QColor(Qt::white),
                         rect(), Qt::AlignHCenter | Qt::AlignVCenter);
        m_style = saved;
        return;
    }

    // Main subtitle font
    QFont f(m_style.fontFamily, m_style.fontSize);
    f.setBold(m_style.bold);
    f.setItalic(m_style.italic);
    p.setFont(f);

    const int margin  = m_style.marginV;
    const int lineH   = m_style.fontSize + 8;
    const int blineH  = m_style.bilingualFontSize + 6;
    const bool hasTrans = m_style.bilingualEnabled && !m_trans.isEmpty();

    // Free placement (dragged in the video preview): block centred at
    // (posX, posY); otherwise follow the bottom/top preset via marginV.
    int baseY;
    int xShift = 0;
    if (m_style.posY >= 0.0) {
        const int blockH = lineH + (hasTrans ? blineH + 2 : 0);
        const int centreY = int(m_style.posY * height());
        baseY = hasTrans
            ? centreY + blockH / 2 - blineH            // translation line top
            : centreY - lineH / 2;                     // main line top
        xShift = int(m_style.posX * width()) - width() / 2;
    } else if (m_style.position == 1) {
        baseY = margin;
    } else if (m_style.position == 2) {
        baseY = height() / 2 - lineH / 2;
    } else {
        baseY = height() - margin - lineH;
    }
    if (hasTrans && m_style.posY < 0.0 && m_style.position == 1)
        baseY += lineH + 2;   // top preset: translation stacked below the main line

    // Draw bilingual line first (below main)
    if (hasTrans) {
        QFont tf(m_style.fontFamily, m_style.bilingualFontSize);
        tf.setBold(m_style.bold);
        p.setFont(tf);
        int blineH2 = m_style.bilingualFontSize + 6;

        if (m_style.showBg) {
            QFontMetrics bfm(tf);
            int bw = bfm.horizontalAdvance(m_trans) + 20;
            p.fillRect(xShift + (width()-bw)/2, baseY - 2, bw, blineH2 + 4, m_style.bgColor);
        }
        drawOutlinedText(p, m_trans, m_style.bilingualColor,
                         QRect(xShift, baseY, width(), blineH2),
                         Qt::AlignHCenter | Qt::AlignVCenter);
        baseY -= blineH2 + 2;
        p.setFont(f);
    }

    // Main subtitle
    QFontMetrics fm(f);
    if (m_style.showBg) {
        int bw = fm.horizontalAdvance(m_text) + 20;
        p.fillRect(xShift + (width()-bw)/2, baseY - 2, bw, lineH + 4, m_style.bgColor);
    }
    drawOutlinedText(p, m_text, m_style.textColor,
                     QRect(xShift, baseY, width(), lineH),
                     Qt::AlignHCenter | Qt::AlignVCenter);
}
