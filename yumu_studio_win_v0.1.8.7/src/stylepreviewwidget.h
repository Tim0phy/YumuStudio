#pragma once
#include <QWidget>
#include "subtitlemodel.h"

class StylePreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit StylePreviewWidget(QWidget *parent = nullptr);
    void setStyle(const SubtitleStyle &s);
    void setSampleText(const QString &t, const QString &trans = {});
    QSize sizeHint() const override { return {420, 130}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    SubtitleStyle m_style;
    QString       m_text;
    QString       m_trans;

    void drawOutlinedText(QPainter &p, const QString &txt,
                          QColor fg, QRect rect, int flags);
};
