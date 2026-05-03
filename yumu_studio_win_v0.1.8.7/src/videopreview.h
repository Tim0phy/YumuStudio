#pragma once
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTime>

class VideoPreview : public QWidget {
    Q_OBJECT
public:
    explicit VideoPreview(QWidget *parent = nullptr);
    void loadMedia(const QString &path);

public slots:
    void seekTo(qint64 posMs);

signals:
    void positionChanged(qint64 posMs);
    void durationChanged(qint64 durationMs);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private slots:
    void onPlayPause();
    void onSliderMoved(int value);
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);

private:
    QMediaPlayer  *m_player  = nullptr;
    QAudioOutput  *m_audio   = nullptr;
    QVideoWidget  *m_video   = nullptr;
    QSlider       *m_slider  = nullptr;
    QPushButton   *m_playBtn = nullptr;
    QLabel        *m_timeLabel = nullptr;
    bool           m_seeking  = false;

    static QString formatTime(qint64 ms);
};
