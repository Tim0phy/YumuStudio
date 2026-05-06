#include "videopreview.h"
#include <QKeyEvent>
#include <QTime>
#include <QStyle>
#include <QMouseEvent>
#include <QWheelEvent>

namespace {
constexpr int SLIDER_MAX = 100000;
constexpr int THUMB_SIZE = 16;
}

VideoPreview::VideoPreview(QWidget *parent) : QWidget(parent) {
    m_player  = new QMediaPlayer(this);
    m_audio   = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(1.0f);

    m_video   = new QVideoWidget(this);
    m_player->setVideoOutput(m_video);

    m_slider  = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, SLIDER_MAX);
    m_slider->setPageStep(SLIDER_MAX / 100);
    m_slider->setTickPosition(QSlider::NoTicks);
    m_slider->setStyleSheet(
        QString("QSlider::groove:horizontal {"
            "height: 8px;"
            "background: #3a3a4a;"
            "border-radius: 4px;"
            "}"
            "QSlider::handle:horizontal {"
            "width: %1px;"
            "margin: -4px 0;"
            "background: #4a9eff;"
            "border-radius: 4px;"
            "}"
            "QSlider::sub-page:horizontal {"
            "background: #4a9eff;"
            "border-radius: 4px;"
            "}")
        .arg(THUMB_SIZE));

    m_playBtn = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaPlay), "", this);
    m_playBtn->setFixedSize(32, 32);
    m_playBtn->setIconSize(QSize(20, 20));

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    m_timeLabel->setMinimumWidth(100);

    m_hoverLabel = new QLabel(this);
    m_hoverLabel->setStyleSheet(
        "background: rgba(30, 30, 40, 0.95);"
        "color: #4a9eff;"
        "padding: 4px 8px;"
        "border-radius: 4px;"
        "font-size: 12px;"
        "font-family: monospace;"
    );
    m_hoverLabel->setAlignment(Qt::AlignCenter);
    m_hoverLabel->hide();
    m_hoverLabel->setFixedHeight(24);
    m_hoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_playBtn);
    controls->addWidget(m_slider, 1);
    controls->addWidget(m_timeLabel);
    controls->setSpacing(8);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->addWidget(m_video, 1);
    root->addLayout(controls);
    root->setSpacing(4);

    connect(m_playBtn, &QPushButton::clicked,  this, &VideoPreview::onPlayPause);
    connect(m_slider,  &QSlider::sliderMoved,  this, &VideoPreview::onSliderMoved);
    connect(m_slider,  &QSlider::sliderPressed, this, &VideoPreview::onSliderPressed);
    connect(m_slider,  &QSlider::sliderReleased, this, &VideoPreview::onSliderReleased);
    connect(m_player,  &QMediaPlayer::positionChanged, this, &VideoPreview::onPositionChanged);
    connect(m_player,  &QMediaPlayer::durationChanged, this, &VideoPreview::onDurationChanged);
    connect(m_player,  &QMediaPlayer::playbackStateChanged, this, &VideoPreview::onPlayerStateChanged);

    m_slider->installEventFilter(this);
}

bool VideoPreview::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_slider && m_hovering && m_player->duration() > 0) {
        if (e->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent*>(e);
            updateHoverTime(me->globalPosition().toPoint() - mapToGlobal(QPoint()));
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void VideoPreview::loadMedia(const QString &path) {
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause();
}

void VideoPreview::seekTo(qint64 posMs) {
    m_seeking = true;
    m_player->setPosition(qBound(qint64(0), posMs, m_player->duration()));
    m_seeking = false;
}

void VideoPreview::onPlayPause() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoPreview::onSliderMoved(int value) {
    if (m_player->duration() <= 0) return;
    qint64 pos = qint64(value) * m_player->duration() / SLIDER_MAX;
    m_hoverPosMs = pos;

    m_hoverLabel->setText(formatTime(pos, true));
    m_player->setPosition(pos);
}

void VideoPreview::onSliderPressed() {
    m_seeking = true;
}

void VideoPreview::onSliderReleased() {
    m_seeking = false;
    emit positionChanged(m_hoverPosMs);
}

void VideoPreview::onPositionChanged(qint64 pos) {
    if (!m_seeking && m_player->duration() > 0) {
        m_slider->setValue(int(pos * SLIDER_MAX / m_player->duration()));
    }
    m_timeLabel->setText(formatTime(pos) + " / " + formatTime(m_player->duration()));
    if (!m_seeking) emit positionChanged(pos);
}

void VideoPreview::onDurationChanged(qint64 dur) {
    emit durationChanged(dur);
}

void VideoPreview::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    m_playBtn->setIcon(style()->standardIcon(
        state == QMediaPlayer::PlayingState
            ? QStyle::SP_MediaPause
            : QStyle::SP_MediaPlay));
}

void VideoPreview::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) { onPlayPause(); e->accept(); return; }
    QWidget::keyPressEvent(e);
}

void VideoPreview::enterEvent(QEnterEvent *e) {
    m_hovering = true;
    m_hoverLabel->show();
    QWidget::enterEvent(e);
}

void VideoPreview::leaveEvent(QEvent *e) {
    m_hovering = false;
    m_hoverLabel->hide();
    QWidget::leaveEvent(e);
}

void VideoPreview::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && m_slider->geometry().contains(e->pos())) {
        int sliderWidth = m_slider->width() - m_slider->style()->pixelMetric(QStyle::PM_SliderThickness);
        int x = e->pos().x() - m_slider->geometry().left();
        int value = qBound(0, x * SLIDER_MAX / qMax(1, sliderWidth), SLIDER_MAX);

        if (m_player->duration() > 0) {
            qint64 pos = qint64(value) * m_player->duration() / SLIDER_MAX;
            m_seeking = true;
            m_player->setPosition(pos);
            m_slider->setValue(value);
            m_seeking = false;
            emit positionChanged(pos);
        }
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void VideoPreview::wheelEvent(QWheelEvent *e) {
    if (m_slider->geometry().contains(e->position().toPoint())) {
        if (m_player->duration() <= 0) {
            e->accept();
            return;
        }

        int delta = e->angleDelta().y();
        int step = (delta > 0) ? 100 : -100;

        qint64 pos = m_player->position();
        qint64 newPos = qBound(qint64(0), pos + step * m_player->duration() / 1000, m_player->duration());

        m_seeking = true;
        m_player->setPosition(newPos);
        m_slider->setValue(int(newPos * SLIDER_MAX / m_player->duration()));
        m_seeking = false;

        e->accept();
        return;
    }
    QWidget::wheelEvent(e);
}

void VideoPreview::updateHoverTime(const QPoint &pos) {
    if (!m_hovering || m_player->duration() <= 0) return;

    int sliderWidth = m_slider->width() - m_slider->style()->pixelMetric(QStyle::PM_SliderThickness);
    int x = pos.x() - m_slider->geometry().left();
    int value = qBound(0, x * SLIDER_MAX / qMax(1, sliderWidth), SLIDER_MAX);

    qint64 posMs = qint64(value) * m_player->duration() / SLIDER_MAX;
    m_hoverPosMs = posMs;

    m_hoverLabel->setText(formatTime(posMs, true));

    int labelX = m_slider->geometry().left() + x - m_hoverLabel->width() / 2;
    labelX = qBound(0, labelX, width() - m_hoverLabel->width());
    m_hoverLabel->move(labelX, m_slider->geometry().top() - 30);
}

QString VideoPreview::formatTime(qint64 ms, bool showMs) {
    qint64 totalSeconds = ms / 1000;
    int hours = int(totalSeconds / 3600);
    int minutes = int((totalSeconds % 3600) / 60);
    int seconds = int(totalSeconds % 60);
    int millis = int(ms % 1000);

    if (showMs) {
        if (hours > 0) {
            return QString("%1:%2:%3.%4")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'));
        } else {
            return QString("%1:%2.%3")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'));
        }
    } else {
        if (hours > 0) {
            return QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        } else {
            return QString("%1:%2")
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
        }
    }
}