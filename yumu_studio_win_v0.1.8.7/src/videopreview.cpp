#include "videopreview.h"
#include <QKeyEvent>
#include <QTime>
#include <QStyle>

VideoPreview::VideoPreview(QWidget *parent) : QWidget(parent) {
    m_player  = new QMediaPlayer(this);
    m_audio   = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(1.0f);

    m_video   = new QVideoWidget(this);
    m_player->setVideoOutput(m_video);

    m_slider  = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1000);

    m_playBtn = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaPlay), "", this);
    m_playBtn->setFixedSize(32, 32);

    m_timeLabel = new QLabel("00:00 / 00:00", this);

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_playBtn);
    controls->addWidget(m_slider, 1);
    controls->addWidget(m_timeLabel);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->addWidget(m_video, 1);
    root->addLayout(controls);

    connect(m_playBtn, &QPushButton::clicked,  this, &VideoPreview::onPlayPause);
    connect(m_slider,  &QSlider::sliderMoved,  this, &VideoPreview::onSliderMoved);
    connect(m_player,  &QMediaPlayer::positionChanged, this, &VideoPreview::onPositionChanged);
    connect(m_player,  &QMediaPlayer::durationChanged, this, &VideoPreview::onDurationChanged);
    connect(m_player,  &QMediaPlayer::playbackStateChanged, this, &VideoPreview::onPlayerStateChanged);
}

void VideoPreview::loadMedia(const QString &path) {
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause();
}

void VideoPreview::seekTo(qint64 posMs) {
    m_seeking = true;
    m_player->setPosition(posMs);
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
    qint64 pos = qint64(value) * m_player->duration() / 1000;
    m_player->setPosition(pos);
}

void VideoPreview::onPositionChanged(qint64 pos) {
    if (!m_seeking && m_player->duration() > 0) {
        m_slider->setValue(int(pos * 1000 / m_player->duration()));
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

QString VideoPreview::formatTime(qint64 ms) {
    int s = int(ms/1000); int m = s/60; s %= 60;
    return QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
}
