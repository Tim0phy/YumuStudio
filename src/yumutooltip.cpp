#include "yumutooltip.h"
#include "uitheme.h"
#include <QApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QFontMetrics>
#include <QTimer>

namespace {
constexpr int kRadius = 10;
constexpr int kPadH = 10;
constexpr int kPadV = 6;
constexpr int kMaxWidth = 280;
} // namespace

YumuToolTip::YumuToolTip()
    : QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    QFont f(QStringLiteral("Inter"), 8);
    f.setStyleHint(QFont::SansSerif);
    setFont(f);
    setContentsMargins(kPadH, kPadV, kPadH, kPadV);
    setWordWrap(true);
    setMaximumWidth(kMaxWidth);
    hide();
}

YumuToolTip *YumuToolTip::instance() {
    static YumuToolTip *s = nullptr;
    if (!s) s = new YumuToolTip;
    return s;
}

void YumuToolTip::showTip(const QString &text, const QPoint &globalPos) {
    const QString tip = text.trimmed();
    if (tip.isEmpty()) { hideTip(); return; }
    YumuToolTip *s = instance();
    // Re-apply token colours every show so theme switches are honoured.
    QFont f(QStringLiteral("Inter"), 8);
    f.setStyleHint(QFont::SansSerif);
    s->setFont(f);
    s->setText(tip);
    s->adjustSize();
    s->placeNear(globalPos);
    s->show();
    s->raise();
    // Native tooltips auto-expire; mirror that so stale bubbles can't stick.
    QTimer::singleShot(6000, s, &YumuToolTip::hide);
}

void YumuToolTip::hideTip() {
    if (YumuToolTip *s = instance()) {
        if (!s->isHidden()) s->hide();
    }
}

void YumuToolTip::placeNear(const QPoint &globalPos) {
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    QPoint p = globalPos + QPoint(14, 18);
    if (p.x() + width() > avail.right())
        p.setX(qMax(avail.left(), globalPos.x() - width() - 10));
    if (p.y() + height() > avail.bottom())
        p.setY(qMax(avail.top(), globalPos.y() - height() - 12));
    move(p);
}

void YumuToolTip::paintEvent(QPaintEvent *e) {
    Q_UNUSED(e)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF body = rect().adjusted(2.5, 3.5, -2.5, -2.5);
    // Soft drop shadow (two translucent underlays, no effect object needed).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 26));
    p.drawRoundedRect(body.translated(0, 2.0), kRadius, kRadius);
    p.setBrush(QColor(0, 0, 0, 18));
    p.drawRoundedRect(body.translated(0, 1.0), kRadius, kRadius);
    // Bubble fill + hairline border from theme tokens.
    p.setBrush(UiTheme::tooltipBg());
    p.setPen(QPen(UiTheme::border(), 1));
    p.drawRoundedRect(body, kRadius, kRadius);
    // Text.
    p.setPen(UiTheme::tooltipText());
    p.setFont(font());
    p.drawText(rect().adjusted(kPadH, kPadV, -kPadH, -kPadV),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, text());
}

YumuToolTipFilter::YumuToolTipFilter(QObject *parent) : QObject(parent) {}

void YumuToolTipFilter::installOn(QApplication *app) {
    if (!app) return;
    // One filter for the whole app; survives theme reloads (colours are
    // re-read from UiTheme on every show).
    static YumuToolTipFilter *s_filter = nullptr;
    if (!s_filter) {
        s_filter = new YumuToolTipFilter(app);
        app->installEventFilter(s_filter);
    }
}

bool YumuToolTipFilter::eventFilter(QObject *obj, QEvent *e) {
    switch (e->type()) {
    case QEvent::ToolTip: {
        auto *he = static_cast<QHelpEvent *>(e);
        QString tip;
        if (auto *w = qobject_cast<QWidget *>(obj))
            tip = w->toolTip();
        if (tip.trimmed().isEmpty()) {
            YumuToolTip::hideTip();
            return false;
        }
        YumuToolTip::showTip(tip, he->globalPos());
        return true; // swallow the native rectangular tooltip
    }
    case QEvent::Leave:
    case QEvent::HoverLeave:
    case QEvent::FocusOut:
    case QEvent::WindowDeactivate:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
    case QEvent::KeyPress:
        YumuToolTip::hideTip();
        break;
    default:
        break;
    }
    return QObject::eventFilter(obj, e);
}
