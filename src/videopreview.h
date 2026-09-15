#pragma once
#include <QWidget>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QVideoWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTime>
#include <QThread>
#include <QTimer>
#include <QEnterEvent>
#include <atomic>
#include "subtitlemodel.h"

class VideoPreview : public QWidget {
    Q_OBJECT
public:
    enum SafeFrame {
        SafeFrameNone = 0,
        SafeFrameGeneral,
        SafeFrameYouTube,
        SafeFrameYouTubeShorts,
        SafeFrameFacebook,
        SafeFrameReels,
        SafeFrameTikTok,
        SafeFrameIGStory
    };

    explicit VideoPreview(QWidget *parent = nullptr);
    ~VideoPreview() override;
    void loadMedia(const QString &path);
    void setSafeFrame(int preset);
    void setSubtitleStyle(const SubtitleStyle &style);
    void setModel(SubtitleModel *model);
    void setSelectedRow(int row);
    void onThemeChanged();

    void setLowResPreview(bool enabled);
    void setPreviewWidth(int width);
    void setPreviewFpsCap(int fps);
    int previewWidth() const { return m_previewWidth; }
    int previewFpsCap() const { return m_previewFpsCap; }

    void setHardwarePreview(bool enabled);
    bool isHardwarePreview() const { return m_hardwarePreview; }
    QString hardwareDecoderName() const;
    QString hardwareStatusText() const;
    void renderHardwareOverlay(QPainter &p);

    void setVolume(float volume);
    float volume() const { return m_volume; }
    void setMuted(bool muted);

public slots:
    void seekTo(qint64 posMs);

#ifdef YUMU_SELFTEST_PROBE
    // Test-only read access used by the sandbox self-test harness. Kept out of
    // production builds by the guard; playback state is surfaced so the harness
    // can assert subtitle selection and seek behaviour without a display.
    qint64 probeDuration() const;
    qint64 probeCurrentPosition() const;
    QString probeActiveSubtitleText() const;
    QString probeOverlayGeometryDebug() const;
    QString probeRenderHardwareOverlayDebug() const;
    QString probeHardwareOverlayPixelDebug() const;
    bool probeOverlayIsVisible() const;
    QString probeOverlayParentDebug() const;
#endif

signals:
    void positionChanged(qint64 posMs);
    void durationChanged(qint64 durationMs);
    void subtitlePositionEdited();
    void hardwareDecodeFailed(const QString &reason);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void moveEvent(QMoveEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
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
    void onPlayerErrorOccurred(QMediaPlayer::Error error, const QString &errorString);
    void onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onVolumeSliderChanged(int value);
    void onMuteToggled();

private:
    void applyPosition(qint64 posMs);
    void refreshChrome();
    QRect displayRect() const;
    QRect fittedVideoRect() const;
    void updateVideoWidgetGeometry();
    void updateOverlayGeometry();
    void updateTopLevelOverlayGeometry();
    void applyHardwareMode();
    void applyHardwareDecodePreference();
    void reloadCurrentMedia(bool preservePosition);
    void recreatePlayer();
    void updateMuteButtonIcon();
    void updateVolumeGeometry();
    void showVolumeSlider();
    void scheduleVolumeHide();
    static QString hardwareEnvValue(bool enabled);
    int effectivePreviewWidth() const;
    int effectiveFpsGateMs() const;
    void requestOverlayUpdate(bool force = false);
    void verifyHardwareDecode(const QVideoFrame &frame);

    bool applySnap(qreal &nx, qreal &ny);
    QRectF safeFrameSafeRect(const QRectF &area) const;
    void sliderSeekTo(qreal xInSlider);

    struct BlockLayout {
        bool     valid = false;
        bool     hasTranslation = false;
        QString  mainText;
        QString  transText;
        QPointF  mainCentre;
        QPointF  transCentre;
        QRectF   bounds;
        QPointF  centre() const {
            return hasTranslation ? QPointF((mainCentre.x() + transCentre.x()) / 2.0,
                                            (mainCentre.y() + transCentre.y()) / 2.0)
                                  : mainCentre;
        }
    };
    BlockLayout subtitleBlockLayout() const;
    const BlockLayout &cachedBlockLayout() const;
    void invalidateLayoutCache();
    void drawOutlinedText(QPainter &p, const QString &txt, const QColor &fg,
                          const QPointF &centre, qreal fontPx, int outlinePx,
                          const QColor &outlineColor, bool bgBox, const QColor &bgColor) const;
    void paintSafeFrame(QPainter &p, const QRectF &area) const;
    void paintOverlay(QPainter &p, const QRect &videoRect, const BlockLayout &bl) const;

    QMediaPlayer  *m_player  = nullptr;
    QAudioOutput  *m_audio   = nullptr;
    QVideoSink    *m_sink    = nullptr;
    QVideoWidget  *m_videoWidget = nullptr;
    QWidget       *m_overlay = nullptr;
    QWidget       *m_topLevelOverlay = nullptr; // HW zero-copy subtitle overlay (top-level window)
    QVideoFrame    m_currentFrame;
    QImage         m_frameImage;
    QElapsedTimer  m_convertGate;
    QElapsedTimer  m_convertCost;
    qint64         m_lastSubtitleKey = -1;
    QThread       *m_convertThread = nullptr;
    std::atomic<bool> m_convertBusy{false};
    QSlider       *m_slider  = nullptr;
    QPushButton   *m_playBtn = nullptr;
    QLabel        *m_timeLabel = nullptr;
    QLabel        *m_hoverLabel = nullptr;
    QPushButton   *m_muteBtn = nullptr;
    // Floating volume strip as a TOP-LEVEL window (same trick as the HW
    // subtitle overlay): a child widget can never composite above the
    // QVideoWidget's native D3D surface on Windows, so stackUnder/raise
    // are futile in hardware-decode mode.
    QWidget       *m_volumePopup = nullptr;
    QSlider       *m_volumeSlider = nullptr; // owned by m_volumePopup
    QTimer        *m_volumeHideTimer = nullptr;
    bool           m_volumeHovering = false;
    float          m_volume = 1.0f;
    bool           m_muted = false;
    bool           m_seeking  = false;
    bool           m_hovering  = false;
    qint64         m_hoverPosMs = 0;
    QSize          m_videoResolution;
    int            m_safePreset = 0;

    SubtitleModel *m_model = nullptr;
    SubtitleStyle  m_subtitleStyle;
    bool           m_hasSubtitleStyle = true;
    int            m_selectedRow = -1;

    bool           m_lowResPreview = true;
    int            m_previewWidth = 640;
    int            m_previewFpsCap = 24;
    bool           m_hardwarePreview = false;
    bool           m_hardwareDecodeActive = false;
    bool           m_adaptiveDrop = false;

    QString        m_currentMediaPath;
    qint64         m_pendingSeekMs = -1;
    bool           m_pendingAutoPlay = false;
    bool           m_hardwareErrorNotified = false;

    mutable BlockLayout m_cachedLayout;
    mutable bool        m_layoutDirty = true;
    mutable QRect       m_lastVideoRect;
    mutable qint64      m_lastLayoutKey = -2;
    mutable int         m_lastSelectedRow = -999;

    bool           m_draggingSubtitle = false;
    QPointF        m_dragGrabNorm;
    bool           m_cursorOverSubtitle = false;
    qint64         m_lastDragEndMs = 0;

    bool           m_snapX = false;
    bool           m_snapY = false;
    qreal          m_snapGuideX = 0.0;
    qreal          m_snapGuideY = 0.0;

    bool           m_sliderScrubbing = false;
    QElapsedTimer  m_overlayGate;
};
