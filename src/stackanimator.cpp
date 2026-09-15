#include "stackanimator.h"
#include <QPainter>
#include <QEasingCurve>
#include <QPointer>
#include <QVariantAnimation>

FadeOverlay::FadeOverlay(QStackedWidget *parent, const QPixmap &from, const QPixmap &to)
    : QWidget(parent), m_from(from), m_to(to) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    // Cover the entire stack
    if (parent) resize(parent->size());
    hide();
}

void FadeOverlay::setProgress(qreal p) {
    m_progress = qBound(0.0, p, 1.0);
    update();
}

void FadeOverlay::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    update();
}

void FadeOverlay::paintEvent(QPaintEvent *) {
    if (m_from.isNull() && m_to.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Editorial: subtle slide + cross-fade. Slide distance scales with width
    // but stays restrained (12-36px) to keep the magazine grid feeling sharp.
    int slide = 24;
    if (width() > 1000) slide = 32;
    else if (width() > 600) slide = 20;
    if (m_direction == FadeOnly) slide = 0;

    qreal fromOffset = 0, toOffset = 0;
    if (m_direction == FromRight) {
        fromOffset = -m_progress * slide;
        toOffset = (1.0 - m_progress) * slide;
    } else if (m_direction == FromLeft) {
        fromOffset = m_progress * slide;
        toOffset = -(1.0 - m_progress) * slide;
    }

    // Ensure pixmaps map to overlay size (handle DPI)
    auto drawPix = [&](const QPixmap &pm, qreal offsetX, qreal opacity) {
        if (pm.isNull()) return;
        p.setOpacity(opacity);
        // Center pixmap if size mismatch (e.g. stack resized since capture)
        int x = int(offsetX);
        int y = (height() - pm.height() / pm.devicePixelRatio()) / 2;
        // If pixmap size equals overlay, just draw at offset
        if (pm.width() / pm.devicePixelRatio() == width() && pm.height() / pm.devicePixelRatio() == height()) {
            p.drawPixmap(x, 0, pm);
        } else {
            // Scale to fit while keeping aspect (should not happen, but safe)
            QRect target(x, 0, width(), height());
            p.drawPixmap(target, pm, pm.rect());
            Q_UNUSED(y);
        }
    };

    // From fades out, To fades in
    drawPix(m_from, fromOffset, 1.0 - m_progress);
    drawPix(m_to, toOffset, m_progress);
}

namespace StackAnimator {

static QPixmap grabWidget(QWidget *w) {
    if (!w) return QPixmap();
    // Ensure widget has a valid size
    if (w->width() <= 0 || w->height() <= 0) return QPixmap();
    // Try grab() first (handles hidden widgets via render)
    QPixmap pm = w->grab();
    if (!pm.isNull()) return pm;
    // Fallback: manual render
    QPixmap fallback(w->size());
    fallback.fill(Qt::transparent);
    w->render(&fallback);
    return fallback;
}

bool animate(QStackedWidget *stack, int newIndex, int duration, FadeOverlay::Direction dir) {
    if (!stack) return false;
    if (newIndex < 0 || newIndex >= stack->count()) return false;
    const int cur = stack->currentIndex();
    if (cur == newIndex) return false;
    if (!stack->isVisible()) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    QWidget *curr = stack->widget(cur);
    QWidget *next = stack->widget(newIndex);
    if (!curr || !next) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    // If an animation is already running, snap it to its target and continue
    for (QObject *child : stack->children()) {
        if (auto *existing = dynamic_cast<FadeOverlay*>(child)) {
            existing->deleteLater();
            break;
        }
    }

    // Ensure both widgets have been laid out at stack size
    const QSize sz = stack->size();
    if (sz.isEmpty()) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    // Make sure next widget is polished and has correct geometry for grab
    next->resize(sz);
    // Process layout so grab captures up-to-date content (e.g. after syncPreviewControls)
    // Use singleShot to let layout happen, but we need pixmap now, so force polish
    next->ensurePolished();
    curr->ensurePolished();

    QPixmap pixCurr = grabWidget(curr);
    QPixmap pixNext = grabWidget(next);

    // If either grab failed (e.g. widget not yet shown), fallback to instant
    if (pixCurr.isNull() && pixNext.isNull()) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    if (pixCurr.isNull()) pixCurr = QPixmap(sz);
    if (pixNext.isNull()) pixNext = QPixmap(sz);
    // Fill transparent if needed
    if (pixCurr.isNull()) pixCurr = QPixmap(sz);
    if (pixNext.isNull()) pixNext = QPixmap(sz);

    // Create overlay and animate with editorial easing
    auto *overlay = new FadeOverlay(stack, pixCurr, pixNext);
    overlay->setDuration(qBound(80, duration, 400));
    overlay->setDirection(dir);
    overlay->resize(sz);
    overlay->show();
    overlay->raise();

    auto *anim = new QVariantAnimation(overlay);
    anim->setDuration(overlay->duration());
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, overlay, [overlay](const QVariant &v){
        overlay->setProgress(v.toReal());
    });
    QPointer<QStackedWidget> guard(stack);
    QObject::connect(anim, &QVariantAnimation::finished, overlay, [guard, newIndex, overlay, anim]() {
        if (guard) {
            const bool blocked = guard->blockSignals(true);
            guard->setCurrentIndex(newIndex);
            guard->blockSignals(blocked);
            if (auto *w = guard->widget(newIndex)) w->update();
        }
        anim->deleteLater();
        overlay->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
}

bool fadeIn(QStackedWidget *stack, int newIndex, int duration) {
    if (!stack) return false;
    if (newIndex < 0 || newIndex >= stack->count()) return false;
    const int cur = stack->currentIndex();
    if (cur == newIndex) return false;
    QWidget *next = stack->widget(newIndex);
    if (!next) return false;
    if (!stack->isVisible()) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    // Switch first so the first paint is immediate; the overlay only fades
    // the new page in (one grab instead of two, no cross-fade wait).
    for (QObject *child : stack->children()) {
        if (auto *existing = dynamic_cast<FadeOverlay*>(child)) {
            existing->deleteLater();
            break;
        }
    }
    const QSize sz = stack->size();
    if (sz.isEmpty()) {
        stack->setCurrentIndex(newIndex);
        return false;
    }
    next->resize(sz);
    next->ensurePolished();
    stack->setCurrentIndex(newIndex);
    next->update();
    QPixmap pixNext = grabWidget(next);
    if (pixNext.isNull()) return false; // already switched; nothing to fade

    auto *overlay = new FadeOverlay(stack, QPixmap(), pixNext);
    overlay->setDuration(qBound(60, duration, 300));
    overlay->setDirection(FadeOverlay::FadeOnly);
    overlay->resize(sz);
    overlay->show();
    overlay->raise();

    auto *anim = new QVariantAnimation(overlay);
    anim->setDuration(overlay->duration());
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, overlay, [overlay](const QVariant &v){
        overlay->setProgress(v.toReal());
    });
    QObject::connect(anim, &QVariantAnimation::finished, overlay, [overlay, anim]() {
        anim->deleteLater();
        overlay->deleteLater();
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
    return true;
}

} // namespace StackAnimator
