#pragma once
#include <QLabel>
#include <QPoint>

// Self-drawn tooltip popup with true rounded corners + soft shadow.
// Qt's style-sheeted QToolTip keeps the native rectangular window frame on
// Windows, so border-radius in QSS is never really round. This widget paints
// its own rounded bubble on a translucent window instead.
//
// Usage: install one YumuToolTipFilter on the QApplication (see installOn);
// the filter swallows QEvent::ToolTip and shows this popup at the cursor.
class YumuToolTip : public QLabel {
    Q_OBJECT
public:
    static YumuToolTip *instance();
    // Show plain-text tip near globalPos (clamped inside the screen).
    static void showTip(const QString &text, const QPoint &globalPos);
    static void hideTip();

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    explicit YumuToolTip();
    void placeNear(const QPoint &globalPos);
};

// Application-wide event filter that replaces native tooltips with
// YumuToolTip. Returns true for QEvent::ToolTip (suppressing the native
// rectangular popup) and hides the bubble on leave/press/scroll/focus.
class YumuToolTipFilter : public QObject {
    Q_OBJECT
public:
    explicit YumuToolTipFilter(QObject *parent = nullptr);
    static void installOn(QApplication *app);
protected:
    bool eventFilter(QObject *obj, QEvent *e) override;
};
