#pragma once
#include <QWidget>
#include "subtitlemodel.h"

class StylePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit StylePreviewWidget(QWidget *parent = nullptr);
    void setStyle(const SubtitleStyle &s);
    void setSampleText(const QString &t, const QString &trans = {});
    void retranslateUi();
    // Thumbnail mode: DPI-independent single-line gallery render. Uses pixel
    // fonts sized from the widget height (point sizes scale with system DPI
    // and overflow small strips), centres the line, and clamps outline width.
    void setThumbnailMode(bool on) { m_thumbnail = on; update(); }
    QSize sizeHint() const override { return {420, 130}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    SubtitleStyle m_style;
    QString       m_text;
    QString       m_trans;
    bool          m_thumbnail = false;

    void drawOutlinedText(QPainter &p, const QString &txt,
                          QColor fg, QRect rect, int flags);
};
