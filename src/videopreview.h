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
#include <QEnterEvent>

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
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private slots:
    void onPlayPause();
    void onSliderMoved(int value);
    void onSliderPressed();
    void onSliderReleased();
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 dur);
    void onPlayerStateChanged(QMediaPlayer::PlaybackState state);
    void updateHoverTime(const QPoint &pos);

private:
    QMediaPlayer  *m_player  = nullptr;
    QAudioOutput  *m_audio   = nullptr;
    QVideoWidget  *m_video   = nullptr;
    QSlider       *m_slider  = nullptr;
    QPushButton   *m_playBtn = nullptr;
    QLabel        *m_timeLabel = nullptr;
    QLabel        *m_hoverLabel = nullptr;
    bool           m_seeking  = false;
    bool           m_hovering  = false;
    qint64         m_hoverPosMs = 0;

    static QString formatTime(qint64 ms, bool showMs = false);
};
