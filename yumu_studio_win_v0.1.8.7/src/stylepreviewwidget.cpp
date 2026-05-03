#include "stylepreviewwidget.h"
#include <QPainter>
#include <QFontMetrics>

StylePreviewWidget::StylePreviewWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(110);
    m_text  = "預覽字幕文字  Preview Text";
    m_trans = "Translation preview 翻譯預覽";
    setStyleSheet("background:#000;border-radius:4px;");
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
    p.fillRect(rect(), Qt::black);

    // Main subtitle font
    QFont f(m_style.fontFamily, m_style.fontSize);
    f.setBold(m_style.bold);
    f.setItalic(m_style.italic);
    p.setFont(f);

    const int margin  = m_style.marginV;
    const int lineH   = m_style.fontSize + 8;
    int       baseY   = height() - margin - lineH;

    // Draw bilingual line first (below main)
    if (m_style.bilingualEnabled && !m_trans.isEmpty()) {
        QFont tf(m_style.fontFamily, m_style.bilingualFontSize);
        tf.setBold(m_style.bold);
        p.setFont(tf);
        int blineH = m_style.bilingualFontSize + 6;

        if (m_style.showBg) {
            QFontMetrics bfm(tf);
            int bw = bfm.horizontalAdvance(m_trans) + 20;
            p.fillRect((width()-bw)/2, baseY - 2, bw, blineH + 4, m_style.bgColor);
        }
        drawOutlinedText(p, m_trans, m_style.bilingualColor,
                         QRect(0, baseY, width(), blineH),
                         Qt::AlignHCenter | Qt::AlignVCenter);
        baseY -= blineH + 2;
        p.setFont(f);
    }

    // Main subtitle
    QFontMetrics fm(f);
    if (m_style.showBg) {
        int bw = fm.horizontalAdvance(m_text) + 20;
        p.fillRect((width()-bw)/2, baseY - 2, bw, lineH + 4, m_style.bgColor);
    }
    drawOutlinedText(p, m_text, m_style.textColor,
                     QRect(0, baseY, width(), lineH),
                     Qt::AlignHCenter | Qt::AlignVCenter);
}
