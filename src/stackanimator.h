#pragma once
#include <QStackedWidget>
#include <QWidget>
#include <QPixmap>

// Editorial slide+fade overlay for QStackedWidget transitions.
// Intended to mask perceived latency (~0.5s) with a 160-220ms animation
// that provides instant visual feedback.
class FadeOverlay : public QWidget {
public:
    enum Direction { FromRight, FromLeft, FadeOnly };
    explicit FadeOverlay(QStackedWidget *parent, const QPixmap &from, const QPixmap &to);
    void setDuration(int ms) { m_duration = ms; }
    void setDirection(Direction d) { m_direction = d; }
    qreal progress() const { return m_progress; }
    void setProgress(qreal p);
    int duration() const { return m_duration; }
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    QPixmap m_from, m_to;
    qreal m_progress = 0.0;
    int m_duration = 200;
    Direction m_direction = FromRight;
};

namespace StackAnimator {
    // Animated transition. Returns true if animation started, false if fell back
    // to instant switch (e.g. pixmap capture failed, same index, or stack hidden).
    bool animate(QStackedWidget *stack, int newIndex, int duration = 160,
                 FadeOverlay::Direction dir = FadeOverlay::FromRight);
    // Instant switch + quick fade-in of the new page (single pixmap grab).
    // Preferred for heavy pages (e.g. Settings): first paint is immediate,
    // the 140ms fade only masks the pop. Falls back to instant on failure.
    bool fadeIn(QStackedWidget *stack, int newIndex, int duration = 140);
}
