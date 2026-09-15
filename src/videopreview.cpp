#include "videopreview.h"
#include "timefmt.h"
#include "uitheme.h"
#include "appconfig.h"
#include "hardwareprobe.h"
#include <QDateTime>
#include <QCursor>
#include <QKeyEvent>
#include <QMediaMetaData>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>
#include <QTimer>
#include <QTime>
#include <QStyle>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QDebug>
#include <QThreadPool>
#include <QRunnable>
#include <QPointer>

namespace {
constexpr int SLIDER_MAX = 100000;
constexpr int THUMB_SIZE = 16;

double mainLineH(const SubtitleStyle &s) {
    return qMax(1, s.fontSize) * 1.25 + qMax(0, s.outlineWidth) * 2.0 + 8.0;
}
double transLineH(const SubtitleStyle &s) {
    return qMax(1, s.bilingualFontSize) * 1.25 + 6.0;
}
bool wantsTranslationLine(const SubtitleStyle &s, const SubtitleEntry *e) {
    return s.bilingualEnabled && e && !e->translation.isEmpty();
}

class PreviewOverlay : public QWidget {
public:
    explicit PreviewOverlay(VideoPreview *host) : QWidget(host), m_host(host) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setStyleSheet("background: transparent;");
    }
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    VideoPreview *m_host;
};

// Top-level window overlay for HW zero-copy path: composited by DWM above
// the QVideoWidget's native D3D surface. Regular child widgets are always
// beneath the native surface on Windows.
class TopLevelSubtitleOverlay : public QWidget {
public:
    explicit TopLevelSubtitleOverlay(VideoPreview *host)
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint)
        , m_host(host) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAutoFillBackground(false);
        setWindowOpacity(1.0);
        // Not in taskbar
        setWindowFlag(Qt::Tool, true);
    }
protected:
    void paintEvent(QPaintEvent *e) override;
private:
    VideoPreview *m_host;
};

}

void PreviewOverlay::paintEvent(QPaintEvent *e) {
    Q_UNUSED(e)
    if (!m_host || !m_host->isHardwarePreview()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    m_host->renderHardwareOverlay(p);
}

void TopLevelSubtitleOverlay::paintEvent(QPaintEvent *e) {
    Q_UNUSED(e)
    if (!m_host || !m_host->isHardwarePreview()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    m_host->renderHardwareOverlay(p);
}

// Top-level floating volume strip. Must be a real window (not a child):
// in HW mode the video is a native D3D surface and DWM always composites it
// above alien child widgets — only a top-level window can float over it.
// Styling comes from the app QSS (QWidget#volumePopup + QSlider#yumuVolume).
class VolumePopup : public QWidget {
public:
    explicit VolumePopup(QWidget *host)
        : QWidget(host, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
        setObjectName(QStringLiteral("volumePopup"));
    }
};

QString VideoPreview::hardwareEnvValue(bool enabled) {
    return enabled ? QStringLiteral("d3d11va,d3d12va,dxva2")
                   : QStringLiteral("");
}

void VideoPreview::applyHardwareDecodePreference() {
    if (m_hardwarePreview) {
        const QByteArray hw = hardwareEnvValue(true).toUtf8();
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", hw);
        qputenv("QT_DISABLE_HW_TEXTURES_CONVERSION", "0");
        qDebug() << "[VideoPreview] HW ON QT_FFMPEG_DECODING_HW_DEVICE_TYPES=" << hw;
    } else {
        qunsetenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES");
        qunsetenv("QT_DISABLE_HW_TEXTURES_CONVERSION");
        qDebug() << "[VideoPreview] HW OFF - unset env, use default SW + fallback";
    }
    qputenv("QT_FFMPEG_HW_ALLOW_PROFILE_MISMATCH", "0");
    m_hardwareDecodeActive = m_hardwarePreview;
}

QString VideoPreview::hardwareDecoderName() const {
    if (!m_hardwarePreview) return tr("CPU 軟體解碼");
    if (HardwareProbe::hasNvidiaGpuQuick()) return tr("GPU 硬體解碼 (NVIDIA d3d11va/cuda)");
    if (HardwareProbe::vulkanRuntimeQuick()) return tr("GPU 硬體解碼 (d3d11va/dxva2)");
    return tr("GPU 硬體解碼 (d3d11va)");
}

QString VideoPreview::hardwareStatusText() const {
    if (!m_hardwarePreview) return tr("當前: CPU 軟體解碼 (已禁用硬解)");
    // 盡量反映真實狀態：若已載入影片且解析度有效，認為HW已嘗試
    if (!m_currentMediaPath.isEmpty() && m_videoResolution.isValid())
        return tr("當前: %1 已啟用").arg(hardwareDecoderName());
    return tr("當前: %1 (待載入影片後生效)").arg(hardwareDecoderName());
}

void VideoPreview::verifyHardwareDecode(const QVideoFrame &frame) {
    // SW path verification: NoHandle = CPU, otherwise HW handle
    bool isHwHandle = frame.handleType() != QVideoFrame::NoHandle;
    if (m_hardwarePreview) {
        m_hardwareDecodeActive = true; // will be cleared on errorOccurred
    } else {
        m_hardwareDecodeActive = false;
    }
    Q_UNUSED(isHwHandle);
}

VideoPreview::VideoPreview(QWidget *parent) : QWidget(parent) {
    m_lowResPreview = AppConfig::instance().preview.lowResPreview;
    m_previewWidth  = qBound(360, AppConfig::instance().preview.previewWidth, 1920);
    m_previewFpsCap = qBound(10, AppConfig::instance().preview.previewFpsCap, 60);
    m_hardwarePreview = AppConfig::instance().preview.hardwarePreview;
    m_subtitleStyle = SubtitleStyle();
    m_hasSubtitleStyle = true;

    m_player  = new QMediaPlayer(this);
    m_audio   = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_volume  = qBound(0.0f, AppConfig::instance().preview.volume, 1.0f);
    m_muted   = AppConfig::instance().preview.muted;
    m_audio->setVolume(m_muted ? 0.0f : m_volume);
    m_audio->setMuted(m_muted);

    m_sink = new QVideoSink(this);
    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    m_videoWidget->hide();
    m_videoWidget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *overlay = new PreviewOverlay(this);
    overlay->hide();
    m_overlay = overlay;
    m_topLevelOverlay = new TopLevelSubtitleOverlay(this);
    m_topLevelOverlay->hide();

    applyHardwareDecodePreference();
    applyHardwareMode();

    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_hardwarePreview) {
            // HW zero-copy path uses QVideoWidget; we still verify decode and
            // update m_videoResolution for layout but skip CPU copy.
            if (frame.isValid() && frame.size().isValid() && !frame.size().isEmpty() && frame.size() != m_videoResolution) {
                if (m_videoResolution.isEmpty() || !m_videoResolution.isValid()) {
                    m_videoResolution = frame.size();
                    invalidateLayoutCache();
                    updateTopLevelOverlayGeometry();
                    if (m_topLevelOverlay && !m_topLevelOverlay->isVisible()) {
                        m_topLevelOverlay->show();
                        m_topLevelOverlay->raise();
                    }
                }
            }
            // Still allow verification
            verifyHardwareDecode(frame);
            return;
        }
        if (!frame.isValid()) return;
        if (frame.size().isValid() && !frame.size().isEmpty() && frame.size() != m_videoResolution) {
            if (m_videoResolution.isEmpty() || !m_videoResolution.isValid()) {
                m_videoResolution = frame.size();
                invalidateLayoutCache();
            }
        }
        m_currentFrame = frame;
        invalidateLayoutCache();
        const int gateMs = effectiveFpsGateMs();
        if (m_convertGate.isValid() && m_convertGate.elapsed() < gateMs) return;
        if (m_adaptiveDrop) { m_adaptiveDrop = false; return; }
        if (m_convertBusy.load()) { m_adaptiveDrop = true; return; }
        m_convertGate.restart();
        m_convertCost.restart();
        // Offload CPU-heavy toImage + scale + convert to thread pool so GUI stays responsive for 4K 60fps
        QVideoFrame frameCopy = frame;
        int targetW = effectivePreviewWidth();
        m_convertBusy = true;
        QPointer<VideoPreview> guard(this);
        QThreadPool::globalInstance()->start(QRunnable::create([guard, frameCopy, targetW]() mutable {
            QImage img = frameCopy.toImage();
            if (img.isNull()) {
                QMetaObject::invokeMethod(guard.data(), [guard]() {
                    if (guard) guard->m_convertBusy = false;
                }, Qt::QueuedConnection);
                return;
            }
            if (targetW > 0 && img.width() > targetW)
                img = img.scaledToWidth(targetW, Qt::FastTransformation);
            if (img.format() != QImage::Format_RGB32
                && img.format() != QImage::Format_ARGB32_Premultiplied)
                img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QMetaObject::invokeMethod(guard.data(), [guard, img]() {
                if (!guard) return;
                guard->m_frameImage = img;
                const qint64 cost = guard->m_convertCost.elapsed();
                if (cost > guard->effectiveFpsGateMs() * 0.7) guard->m_adaptiveDrop = true;
                guard->m_convertBusy = false;
                guard->update();
                guard->requestOverlayUpdate(true);
            }, Qt::QueuedConnection);
        }));
    });

    m_slider  = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, SLIDER_MAX);
    m_slider->setPageStep(SLIDER_MAX / 100);
    m_slider->setTickPosition(QSlider::NoTicks);
    m_slider->setObjectName(QStringLiteral("yumuProgress"));

    m_playBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"), this);
    m_playBtn->setObjectName(QStringLiteral("playBtn"));
    m_playBtn->setFixedSize(32, 32);
    m_playBtn->setFlat(true);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setToolTip(tr("播放 / 暫停 (Space)"));

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setMinimumWidth(100);
    m_timeLabel->setVisible(false); // pastel mockup has no time text in the bar

    m_hoverLabel = new QLabel(this);
    m_hoverLabel->setAlignment(Qt::AlignCenter);
    m_hoverLabel->hide();
    m_hoverLabel->setFixedHeight(24);
    m_hoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Mockup: vertical volume strip floating above the mute button.
    m_muteBtn = new QPushButton(this);
    m_muteBtn->setObjectName(QStringLiteral("muteBtn"));
    m_muteBtn->setFixedSize(28, 28);
    m_muteBtn->setFlat(true);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    m_muteBtn->setToolTip(tr("靜音 / 取消靜音"));

    m_volumePopup = new VolumePopup(this);
    m_volumeSlider = new QSlider(Qt::Vertical, m_volumePopup);
    m_volumeSlider->setObjectName(QStringLiteral("yumuVolume"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(qRound(m_volume * 100));
    m_volumeSlider->setFixedWidth(20);
    m_volumeSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_volumeSlider->setPageStep(10);
    m_volumeSlider->setSingleStep(5);
    m_volumeSlider->setTickPosition(QSlider::NoTicks);
    m_volumeSlider->setToolTip(tr("音量"));
    // Hover-popup UX anchored above the mute button: hidden until the user
    // hovers the mute button or the strip itself, so it never blocks the
    // picture. The strip lives in a top-level VolumePopup (see above) so it
    // stays visible over the native D3D surface in hardware-decode mode.
    m_volumeSlider->setInvertedAppearance(false);
    m_volumeSlider->setInvertedControls(false);
    m_volumeSlider->setAttribute(Qt::WA_Hover, true);
    m_volumeSlider->setMouseTracking(true);
    auto *popLay = new QVBoxLayout(m_volumePopup);
    popLay->setContentsMargins(8, 8, 8, 8);
    popLay->setSpacing(0);
    popLay->addWidget(m_volumeSlider);
    m_volumePopup->hide();
    m_muteBtn->setAttribute(Qt::WA_Hover, true);

    m_volumeHideTimer = new QTimer(this);
    m_volumeHideTimer->setSingleShot(true);
    m_volumeHideTimer->setInterval(1400);
    connect(m_volumeHideTimer, &QTimer::timeout, this, [this] {
        if (m_volumeHovering) return;
        if (!m_volumePopup || m_volumePopup->isHidden()) return;
        if (m_volumeSlider && m_volumeSlider->isSliderDown()) return;
        // Explicit global-position checks (immune to Enter/Leave quirks when
        // the cursor crosses between the popup panel and the slider child).
        const QPoint cur = QCursor::pos();
        if (m_volumePopup->geometry().contains(cur)) return;
        if (m_muteBtn && !m_muteBtn->isHidden()) {
            const QRect mg(m_muteBtn->mapToGlobal(QPoint(0, 0)), m_muteBtn->size());
            if (mg.adjusted(-6, -6, 6, 6).contains(cur)) return;
        }
        m_volumePopup->hide();
    });

    updateMuteButtonIcon();

    refreshChrome();

    auto *controls = new QHBoxLayout;
    controls->addWidget(m_playBtn);
    controls->addWidget(m_slider, 1);
    controls->addWidget(m_timeLabel);
    controls->addSpacing(6);
    controls->addWidget(m_muteBtn);
    controls->setSpacing(8);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0,0,0,0);
    root->addStretch(1);
    root->addLayout(controls);
    root->setSpacing(4);

    connect(m_playBtn, &QPushButton::clicked,  this, &VideoPreview::onPlayPause);
    connect(m_slider,  &QSlider::sliderMoved,  this, &VideoPreview::onSliderMoved);
    connect(m_slider,  &QSlider::sliderPressed, this, &VideoPreview::onSliderPressed);
    connect(m_slider,  &QSlider::sliderReleased, this, &VideoPreview::onSliderReleased);
    connect(m_volumeSlider, &QSlider::valueChanged, this, &VideoPreview::onVolumeSliderChanged);
    connect(m_muteBtn, &QPushButton::clicked, this, &VideoPreview::onMuteToggled);
    connect(m_volumeSlider, &QSlider::sliderPressed, this, [this] {
        if (m_volumeHideTimer) m_volumeHideTimer->stop();
        m_volumeHovering = true;
    });
    connect(m_volumeSlider, &QSlider::sliderReleased, this, [this] {
        m_volumeHovering = false;
        scheduleVolumeHide();
    });
    connect(m_player,  &QMediaPlayer::positionChanged, this, &VideoPreview::onPositionChanged);
    connect(m_player,  &QMediaPlayer::durationChanged, this, &VideoPreview::onDurationChanged);
    connect(m_player,  &QMediaPlayer::playbackStateChanged, this, &VideoPreview::onPlayerStateChanged);
    connect(m_player,  &QMediaPlayer::metaDataChanged, this, [this]{
        QVariant res = m_player->metaData().value(QMediaMetaData::Resolution);
        if (res.isValid() && res.canConvert<QSize>()) {
            QSize sz = res.value<QSize>();
            if (sz.isValid() && !sz.isEmpty() && sz != m_videoResolution) {
                m_videoResolution = sz;
                invalidateLayoutCache();
                updateOverlayGeometry();
                update();
                requestOverlayUpdate(true);
            }
        }
        if (m_pendingSeekMs >= 0 && m_player->duration() > 0) {
            qint64 target = qBound<qint64>(0, m_pendingSeekMs, m_player->duration());
            m_player->setPosition(target);
            m_pendingSeekMs = -1;
            if (m_pendingAutoPlay) {
                m_player->play();
                m_pendingAutoPlay = false;
            }
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, &VideoPreview::onPlayerErrorOccurred);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoPreview::onPlayerMediaStatusChanged);

    m_slider->installEventFilter(this);
    m_muteBtn->installEventFilter(this);
    m_volumePopup->installEventFilter(this);
    m_volumeSlider->installEventFilter(this);
    setMouseTracking(true);
}

VideoPreview::~VideoPreview() {
    if (m_volumePopup) {
        m_volumePopup->hide();
        m_volumePopup->deleteLater();
        m_volumePopup = nullptr;
    }
    if (m_topLevelOverlay) {
        m_topLevelOverlay->hide();
        m_topLevelOverlay->deleteLater();
        m_topLevelOverlay = nullptr;
    }
    if (m_convertThread) {
        m_convertThread->quit();
        m_convertThread->wait();
        delete m_convertThread;
        m_convertThread = nullptr;
    }
    if (window()) window()->removeEventFilter(this);
}

int VideoPreview::effectivePreviewWidth() const {
    if (!m_lowResPreview) return 0;
    return qBound(360, m_previewWidth, 1920);
}
int VideoPreview::effectiveFpsGateMs() const {
    int fps = qBound(10, m_previewFpsCap, 60);
    return qMax(16, 1000 / fps);
}

void VideoPreview::applyHardwareMode() {
    if (m_hardwarePreview) {
        // HW: zero-copy via QVideoWidget + top-level window overlay (GPU, DWM composited)
        m_player->setVideoSink(nullptr);
        if (m_videoWidget) {
            m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
            m_player->setVideoOutput(m_videoWidget);
            m_videoWidget->show();
            m_videoWidget->raise();
        }
        if (m_overlay) m_overlay->hide();
        if (m_topLevelOverlay) {
            m_topLevelOverlay->show();
            m_topLevelOverlay->raise();
        }
        updateVideoWidgetGeometry();
        updateTopLevelOverlayGeometry();
        updateVolumeGeometry();
    } else {
        // SW: QVideoSink → m_frameImage → paintEvent
        m_player->setVideoOutput(nullptr);
        m_player->setVideoSink(m_sink);
        if (m_videoWidget) m_videoWidget->hide();
        if (m_topLevelOverlay) m_topLevelOverlay->hide();
        if (m_overlay) m_overlay->hide();
    }
    m_frameImage = QImage();
    m_adaptiveDrop = false;
    m_convertGate.invalidate();
    m_overlayGate.invalidate();
    invalidateLayoutCache();
    update();
    requestOverlayUpdate(true);
    AppConfig::instance().preview.hardwarePreview = m_hardwarePreview;
    AppConfig::instance().preview.lowResPreview = m_lowResPreview;
    AppConfig::instance().preview.previewWidth = m_previewWidth;
    AppConfig::instance().preview.previewFpsCap = m_previewFpsCap;
}

void VideoPreview::updateVideoWidgetGeometry() {
    if (!m_videoWidget) return;
    if (!m_hardwarePreview) return;
    const QRect vr = fittedVideoRect();
    if (!vr.isValid() || vr.width() <= 0 || vr.height() <= 0) return;
    m_videoWidget->setGeometry(vr);
    updateTopLevelOverlayGeometry();
}

void VideoPreview::updateOverlayGeometry() {
    // Legacy inline overlay no longer used; HW uses top-level window
    if (m_overlay && m_overlay->isVisible()) m_overlay->hide();
    if (m_hardwarePreview) updateTopLevelOverlayGeometry();
}

void VideoPreview::updateTopLevelOverlayGeometry() {
    if (!m_topLevelOverlay || !m_hardwarePreview) return;
    const QRect vr = fittedVideoRect();
    if (!vr.isValid() || vr.width() <= 0 || vr.height() <= 0) {
        // Don't hide - keep overlay visible; geometry updates once video resolution is known
        return;
    }
    // Map VideoPreview's fitted rect to global screen coords for top-level window
    const QPoint globalTopLeft = mapToGlobal(vr.topLeft());
    m_topLevelOverlay->setGeometry(QRect(globalTopLeft, vr.size()));
    if (isVisible() && m_videoWidget && m_videoWidget->isVisible() && !m_topLevelOverlay->isVisible())
        m_topLevelOverlay->show();
    m_topLevelOverlay->raise();
    m_topLevelOverlay->update();
}
void VideoPreview::requestOverlayUpdate(bool force) {
    QWidget *target = m_hardwarePreview ? m_topLevelOverlay : nullptr;
    // SW path subtitles are in VideoPreview paint, so no overlay gate needed (but keep for HW)
    if (m_hardwarePreview) {
        if (!target) return;
        if (force) {
            // Ensure overlay is visible and aligned — AI transcription may have
            // fired entriesChanged while overlay was hidden (e.g. via hideEvent,
            // or right after HW toggle before geometry was set). Without an
            // explicit show()/raise() Qt only schedules a paint on hidden
            // windows and the subtitle block stays invisible in the preview.
            if (isVisible()) {
                updateTopLevelOverlayGeometry();
                if (!target->isVisible()) target->show();
                target->raise();
            }
            m_overlayGate.invalidate();
            target->update();
            return;
        }
        const int gateMs = effectiveFpsGateMs();
        if (m_overlayGate.isValid() && m_overlayGate.elapsed() < gateMs) return;
        m_overlayGate.restart();
        target->update();
        return;
    }
    // SW: subtitles are part of VideoPreview::paintEvent, just ensure VideoPreview updates
    // (callers already do update(), so overlay gate is not needed)
    if (!m_overlay) return;
    if (force) {
        m_overlayGate.invalidate();
        // no overlay to update in SW, but keep gate invalidated
        return;
    }
    const int gateMs = effectiveFpsGateMs();
    if (m_overlayGate.isValid() && m_overlayGate.elapsed() < gateMs) return;
    m_overlayGate.restart();
    m_overlay->update();
}

void VideoPreview::recreatePlayer() {
    qint64 pos = m_player ? m_player->position() : 0;
    QMediaPlayer::PlaybackState st = m_player ? m_player->playbackState() : QMediaPlayer::StoppedState;
    QString src = m_currentMediaPath;
    // disconnect old
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
        m_player->setVideoOutput(nullptr);
        m_player->setVideoSink(nullptr);
        m_player->setSource(QUrl());
        m_player->deleteLater();
    }
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(m_muted ? 0.0f : m_volume);
    m_audio->setMuted(m_muted);
    connect(m_player,  &QMediaPlayer::positionChanged, this, &VideoPreview::onPositionChanged);
    connect(m_player,  &QMediaPlayer::durationChanged, this, &VideoPreview::onDurationChanged);
    connect(m_player,  &QMediaPlayer::playbackStateChanged, this, &VideoPreview::onPlayerStateChanged);
    connect(m_player,  &QMediaPlayer::metaDataChanged, this, [this]{
        QVariant res = m_player->metaData().value(QMediaMetaData::Resolution);
        if (res.isValid() && res.canConvert<QSize>()) {
            QSize sz = res.value<QSize>();
            if (sz.isValid() && !sz.isEmpty() && sz != m_videoResolution) {
                m_videoResolution = sz;
                invalidateLayoutCache();
                updateOverlayGeometry();
                update();
                requestOverlayUpdate(true);
                if (m_hardwarePreview && m_topLevelOverlay && !m_topLevelOverlay->isVisible()) {
                    m_topLevelOverlay->show();
                    m_topLevelOverlay->raise();
                }
            }
        }
        if (m_pendingSeekMs >= 0 && m_player->duration() > 0) {
            qint64 target = qBound<qint64>(0, m_pendingSeekMs, m_player->duration());
            m_player->setPosition(target);
            m_pendingSeekMs = -1;
            if (m_pendingAutoPlay) { m_player->play(); m_pendingAutoPlay = false; }
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, &VideoPreview::onPlayerErrorOccurred);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoPreview::onPlayerMediaStatusChanged);
    // HW uses zero-copy QVideoWidget + top-level overlay (GPU), SW uses QVideoSink → QImage
    if (m_hardwarePreview) {
        m_player->setVideoSink(nullptr);
        m_player->setVideoOutput(m_videoWidget);
    } else {
        m_player->setVideoOutput(nullptr);
        m_player->setVideoSink(m_sink);
    }
    if (!src.isEmpty()) {
        m_currentMediaPath = src;
        m_pendingSeekMs = pos;
        m_pendingAutoPlay = (st == QMediaPlayer::PlayingState);
        m_player->setSource(QUrl::fromLocalFile(src));
        m_player->pause();
    }
}

void VideoPreview::reloadCurrentMedia(bool preservePosition) {
    if (m_currentMediaPath.isEmpty()) return;
    qint64 pos = preservePosition ? m_player->position() : 0;
    bool wasPlaying = preservePosition && m_player->playbackState() == QMediaPlayer::PlayingState;
    m_frameImage = QImage();
    m_currentFrame = QVideoFrame();
    m_videoResolution = QSize();
    invalidateLayoutCache();
    m_pendingSeekMs = pos;
    m_pendingAutoPlay = wasPlaying;
    m_hardwareErrorNotified = false;
    recreatePlayer();
    update();
}

void VideoPreview::setLowResPreview(bool enabled) {
    if (m_lowResPreview == enabled) return;
    m_lowResPreview = enabled;
    AppConfig::instance().preview.lowResPreview = enabled;
    AppConfig::instance().save();
    m_frameImage = QImage();
    update();
}
void VideoPreview::setPreviewWidth(int width) {
    width = qBound(360, width, 1920);
    if (m_previewWidth == width) return;
    m_previewWidth = width;
    AppConfig::instance().preview.previewWidth = width;
    AppConfig::instance().save();
    m_frameImage = QImage();
    invalidateLayoutCache();
    update();
}
void VideoPreview::setPreviewFpsCap(int fps) {
    fps = qBound(10, fps, 60);
    if (m_previewFpsCap == fps) return;
    m_previewFpsCap = fps;
    AppConfig::instance().preview.previewFpsCap = fps;
    AppConfig::instance().save();
}
void VideoPreview::setHardwarePreview(bool enabled) {
    if (m_hardwarePreview == enabled) return;
    m_hardwarePreview = enabled;
    AppConfig::instance().preview.hardwarePreview = enabled;
    AppConfig::instance().save();
    applyHardwareDecodePreference();
    // HW 切換需要重建播放器才能讓新的 QT_FFMPEG 環境生效
    reloadCurrentMedia(true);
    applyHardwareMode();
    update();
    requestOverlayUpdate(true);
}

void VideoPreview::refreshChrome() {
    // Pastel redesign: deep-purple played / light-lavender unplayed, thin
    // divider handle. ObjectName QSS in uitheme carries the same look; the
    // inline sheet here is a fallback so the bar looks right even if the
    // app stylesheet is reloaded late.
    m_slider->setStyleSheet(
        QStringLiteral("QSlider#yumuProgress::groove:horizontal {height:12px; border-radius:6px; background:#C9B8E8;}"
            "QSlider#yumuProgress::sub-page:horizontal {background:#7B61B8; border-radius:6px;}"
            "QSlider#yumuProgress::add-page:horizontal {background:#C9B8E8; border-radius:6px;}"
            "QSlider#yumuProgress::handle:horizontal {width:4px; margin:-6px 0; background:#4A3A7A; border-radius:2px;}"));

    m_timeLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; background: transparent;")
            .arg(UiTheme::hex(UiTheme::textSecond())));

    // Volume strip: deep-purple = current volume, pale lavender = remaining.
    // NOTE (Qt vertical semantics): for a vertical QSlider the sub-page is
    // the groove segment ABOVE the handle and add-page is BELOW it, i.e. the
    // reverse of the horizontal case. With the handle at the top (100%) the
    // add-page fills almost the whole groove, so the FILLED colour must live
    // on add-page for "100% = full deep-purple, 0% = full pale" to hold.
    // Keep this in sync with QSlider#yumuVolume in uitheme.cpp.
    const QString volFilled = UiTheme::hex(UiTheme::accent());
    const QString volTrack  = UiTheme::hex(UiTheme::border());
    m_volumeSlider->setStyleSheet(
        QStringLiteral("QSlider#yumuVolume::groove:vertical {width:6px; border-radius:3px; background:%1;}"
            "QSlider#yumuVolume::sub-page:vertical {background:%1; border-radius:3px;}"
            "QSlider#yumuVolume::add-page:vertical {background:%2; border-radius:3px;}"
            "QSlider#yumuVolume::handle:vertical {height:14px; width:14px; margin:0 -4px; background:#FFFFFF; border:1px solid #B9A8D8; border-radius:7px;}")
            .arg(volTrack, volFilled));

    updateMuteButtonIcon();
    m_playBtn->setStyleSheet(
        QStringLiteral("QPushButton#playBtn { background: transparent; border: none; color:%1; font-size:15pt; }"
                       "QPushButton#playBtn:hover { color:%2; }")
            .arg(UiTheme::hex(UiTheme::text()), UiTheme::hex(UiTheme::accent())));
    m_muteBtn->setStyleSheet(
        QStringLiteral("QPushButton#muteBtn { background: transparent; border: none; color:%1; font-size:11pt; }"
                       "QPushButton#muteBtn:hover { color:%2; }")
            .arg(UiTheme::hex(UiTheme::text()), UiTheme::hex(UiTheme::accent())));

    m_hoverLabel->setStyleSheet(
        QStringLiteral("background: %1;"
                       "color: %2;"
                       "padding: 4px 8px;"
                       "border-radius: 4px;"
                       "font-size: 12px;"
                       "font-family: monospace;")
            .arg(UiTheme::popoverBg().name(QColor::HexArgb),
                 UiTheme::hex(UiTheme::accent())));
}

void VideoPreview::onThemeChanged() { refreshChrome(); update(); requestOverlayUpdate(true); }

void VideoPreview::updateVolumeGeometry() {
    if (!m_volumePopup || !m_muteBtn) return;
    if (!isVisible()) return;
    // Anchor the strip above the mute button: centred on it, 8px gap.
    // Top-level window, so position in global coords and clamp to screen.
    static constexpr int kPopW = 36;
    const int volH = qBound(120, height() - 140, 200);
    m_volumePopup->setFixedSize(kPopW, volH + 16);
    const QRect btn = m_muteBtn->geometry();
    int x = btn.center().x() - kPopW / 2;
    int y = btn.top() - 8 - (volH + 16);
    x = qBound(8, x, qMax(8, width() - kPopW - 8));
    y = qMax(8, y);
    QPoint global = mapToGlobal(QPoint(x, y));
    if (QScreen *sc = QGuiApplication::screenAt(mapToGlobal(btn.center()))) {
        const QRect avail = sc->availableGeometry();
        if (global.x() + kPopW > avail.right()) global.setX(avail.right() - kPopW);
        if (global.x() < avail.left()) global.setX(avail.left());
        if (global.y() < avail.top()) global.setY(avail.top());
    }
    m_volumePopup->move(global);
    if (!m_volumePopup->isHidden()) m_volumePopup->raise();
}

void VideoPreview::showVolumeSlider() {
    if (!m_volumePopup) return;
    if (m_volumeHideTimer) m_volumeHideTimer->stop();
    m_volumeHovering = true;
    updateVolumeGeometry();
    m_volumePopup->show();
    m_volumePopup->raise();
}

void VideoPreview::scheduleVolumeHide() {
    m_volumeHovering = false;
    if (m_volumeHideTimer && !m_volumeHideTimer->isActive())
        m_volumeHideTimer->start();
}

bool VideoPreview::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_muteBtn) {
        if (e->type() == QEvent::Enter || e->type() == QEvent::HoverEnter) {
            showVolumeSlider();
            return false;
        }
        if (e->type() == QEvent::Leave || e->type() == QEvent::HoverLeave) {
            scheduleVolumeHide();
            return false;
        }
    }
    if (obj == m_volumePopup || obj == m_volumeSlider) {
        if (e->type() == QEvent::Enter || e->type() == QEvent::HoverEnter) {
            if (m_volumeHideTimer) m_volumeHideTimer->stop();
            m_volumeHovering = true;
            return false;
        }
        if (e->type() == QEvent::Leave || e->type() == QEvent::HoverLeave) {
            scheduleVolumeHide();
            return false;
        }
    }
    if (obj == m_slider) {
        auto *me = static_cast<QMouseEvent*>(e);
        if (e->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton) {
            if (m_player->duration() > 0) {
                m_sliderScrubbing = true;
                m_seeking = true;
                sliderSeekTo(me->position().x());
            }
            return true;
        }
        if (e->type() == QEvent::MouseMove) {
            if (m_sliderScrubbing) { sliderSeekTo(me->position().x()); return true; }
            if (m_hovering && m_player->duration() > 0) {
                updateHoverTime(me->globalPosition().toPoint() - mapToGlobal(QPoint()));
                return true;
            }
        }
        if (e->type() == QEvent::MouseButtonRelease && me->button() == Qt::LeftButton && m_sliderScrubbing) {
            m_sliderScrubbing = false; m_seeking = false; sliderSeekTo(me->position().x()); return true;
        }
    }
    // Keep top-level overlays aligned when main window moves/resizes
    if (obj == window()) {
        if (e->type() == QEvent::Move || e->type() == QEvent::Resize || e->type() == QEvent::WindowStateChange) {
            if (m_hardwarePreview && m_topLevelOverlay) updateTopLevelOverlayGeometry();
            updateVolumeGeometry();
        }
    }
    return QWidget::eventFilter(obj, e);
}

void VideoPreview::sliderSeekTo(qreal xInSlider) {
    if (m_player->duration() <= 0) return;
    const int sliderWidth = qMax(1, m_slider->width() - m_slider->style()->pixelMetric(QStyle::PM_SliderThickness));
    const int value = qBound(0, int(xInSlider * SLIDER_MAX / sliderWidth), SLIDER_MAX);
    m_hoverPosMs = qint64(value) * m_player->duration() / SLIDER_MAX;
    m_slider->setValue(value);
    m_hoverLabel->setText(TimeFmt::formatClock(m_hoverPosMs, true));
    applyPosition(m_hoverPosMs);
}

bool VideoPreview::applySnap(qreal &nx, qreal &ny) {
    m_snapX = false; m_snapY = false;
    constexpr qreal kThreshold = 0.025;
    QVector<qreal> xTargets { 0.5 };
    QVector<qreal> yTargets { 0.5 };
    const QRectF video = fittedVideoRect();
    const QRectF safe = safeFrameSafeRect(video);
    if (!safe.isEmpty() && video.width() > 1 && video.height() > 1) {
        xTargets << (safe.left() - video.left()) / video.width()
                 << (safe.right() - video.left()) / video.width();
        yTargets << (safe.top() - video.top()) / video.height()
                 << (safe.bottom() - video.top()) / video.height();
    }
    for (qreal t : xTargets) if (qAbs(nx - t) < kThreshold) { nx = t; m_snapX = true; m_snapGuideX = t; break; }
    for (qreal t : yTargets) if (qAbs(ny - t) < kThreshold) { ny = t; m_snapY = true; m_snapGuideY = t; break; }
    return m_snapX || m_snapY;
}

void VideoPreview::loadMedia(const QString &path) {
    m_currentMediaPath = path;
    m_pendingSeekMs = -1; m_pendingAutoPlay = false; m_hardwareErrorNotified = false;
    m_videoResolution = QSize();
    m_currentFrame = QVideoFrame();
    m_frameImage = QImage();
    invalidateLayoutCache();
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->pause();
    updateOverlayGeometry();
}

void VideoPreview::setSafeFrame(int preset) {
    m_safePreset = preset;
    invalidateLayoutCache();
    update(); requestOverlayUpdate(true);
}

void VideoPreview::setSubtitleStyle(const SubtitleStyle &style) {
    m_subtitleStyle = style; m_hasSubtitleStyle = true;
    invalidateLayoutCache(); update(); requestOverlayUpdate(true);
}

void VideoPreview::setModel(SubtitleModel *model) {
    if (m_model == model) return;
    m_model = model;
    if (m_model) {
        m_subtitleStyle = m_model->style(); m_hasSubtitleStyle = true;
        connect(m_model, &SubtitleModel::entriesChanged, this, [this] {
            m_lastSubtitleKey = -1; invalidateLayoutCache(); update(); requestOverlayUpdate(true);
            if (m_hardwarePreview && m_topLevelOverlay && isVisible()) {
                if (!m_topLevelOverlay->isVisible()) m_topLevelOverlay->show();
                m_topLevelOverlay->raise();
                updateTopLevelOverlayGeometry();
            }
        });
    }
    invalidateLayoutCache(); update();
}

QRect VideoPreview::displayRect() const {
    const int bottom = m_slider ? qMax(0, height() - m_slider->geometry().top() - 2) : 0;
    return QRect(0, 0, width(), qMax(10, height() - bottom));
}

QRect VideoPreview::fittedVideoRect() const {
    const QRect area = displayRect();
    QSize res = m_currentFrame.isValid() ? m_currentFrame.size() : m_videoResolution;
    if (!res.isValid() || res.isEmpty()) res = QSize(16, 9);
    if (area.width() <= 0 || area.height() <= 0) return area;
    const qreal videoAspect = qreal(res.width()) / qreal(res.height());
    const qreal widgetAspect = qreal(area.width()) / qreal(area.height());
    QSize scaled;
    if (videoAspect > widgetAspect) { scaled.setWidth(area.width()); scaled.setHeight(qMax(1, int(area.width() / videoAspect))); }
    else { scaled.setHeight(area.height()); scaled.setWidth(qMax(1, int(area.height() * videoAspect))); }
    return QRect(QPoint(area.x() + (area.width() - scaled.width()) / 2, area.y() + (area.height() - scaled.height()) / 2), scaled);
}

void VideoPreview::setSelectedRow(int row) {
    if (m_selectedRow == row) return;
    m_selectedRow = row; invalidateLayoutCache(); update(); requestOverlayUpdate(true);
}

void VideoPreview::invalidateLayoutCache() { m_layoutDirty = true; }

const VideoPreview::BlockLayout &VideoPreview::cachedBlockLayout() const {
    const QRect curVideo = const_cast<VideoPreview*>(this)->fittedVideoRect();
    qint64 curKey = -1;
    const qint64 posMs = m_player ? m_player->position() : 0;
    if (m_model) for (int i = 0; i < m_model->rowCount(); ++i) { const auto &e = m_model->entryAt(i); if (posMs >= e.startMs && posMs < e.endMs) { curKey = e.startMs; break; } }
    if (!m_layoutDirty && m_lastVideoRect == curVideo && m_lastLayoutKey == curKey && m_lastSelectedRow == m_selectedRow) return m_cachedLayout;
    m_cachedLayout = const_cast<VideoPreview*>(this)->subtitleBlockLayout();
    m_lastVideoRect = curVideo; m_lastLayoutKey = curKey; m_lastSelectedRow = m_selectedRow; m_layoutDirty = false;
    return m_cachedLayout;
}

VideoPreview::BlockLayout VideoPreview::subtitleBlockLayout() const {
    BlockLayout bl;
    const QRectF video = fittedVideoRect();
    if (video.isEmpty()) return bl;
    const SubtitleStyle defaultFallback;
    const SubtitleStyle &styleRef = m_hasSubtitleStyle ? m_subtitleStyle : defaultFallback;
    const SubtitleEntry *entry = nullptr;
    const qint64 posMs = m_player->position();
    if (m_model && m_model->rowCount() > 0) {
        for (int i = 0; i < m_model->rowCount(); ++i) { const auto &e = m_model->entryAt(i); if (posMs >= e.startMs && posMs < e.endMs) { entry = &m_model->entryAt(i); break; } }
        if (!entry && m_selectedRow >= 0 && m_selectedRow < m_model->rowCount()) entry = &m_model->entryAt(m_selectedRow);
    }
    if (entry) { bl.mainText = entry->text; bl.transText = entry->translation; }
    else { bl.mainText = tr("字幕預覽"); bl.transText.clear(); }
    const SubtitleStyle &s = styleRef;
    const double scale = video.height() / 1080.0;
    const bool hasTrans = wantsTranslationLine(s, entry) && !bl.transText.isEmpty();
    if (!hasTrans) bl.transText.clear(); bl.hasTranslation = hasTrans;
    const double mainH = mainLineH(s); const double transH = hasTrans ? transLineH(s) : 0.0;
    double mainCentre1080 = 0.0, transCentre1080 = 0.0;
    if (s.posY >= 0.0) { const double blockH = mainH + transH; const double centerY = qBound(0.02, s.posY, 0.98) * 1080.0; mainCentre1080 = centerY - blockH / 2.0 + mainH / 2.0; transCentre1080 = centerY + blockH / 2.0 - transH / 2.0; }
    else if (s.position == 1) { mainCentre1080 = s.marginV + mainH / 2.0; transCentre1080 = s.marginV + mainH + transH / 2.0 + 4.0; }
    else if (s.position == 2) { mainCentre1080 = 540.0 - transH / 2.0; transCentre1080 = 540.0 + mainH / 2.0; }
    else { mainCentre1080 = 1080.0 - s.marginV - mainH / 2.0; transCentre1080 = 1080.0 - (s.marginV + qMax(1, s.fontSize) + 4.0) - transH / 2.0; }
    const double centreXNorm = (s.posY >= 0.0 ? qBound(0.02, s.posX, 0.98) : 0.5);
    const double centrePxX = video.left() + centreXNorm * video.width();
    bl.mainCentre = QPointF(centrePxX, video.top() + mainCentre1080 * scale);
    bl.transCentre = QPointF(centrePxX, video.top() + transCentre1080 * scale);
    QFont f(s.fontFamily.isEmpty() ? QStringLiteral("Arial") : s.fontFamily);
    f.setPixelSize(qMax(6, int(s.fontSize * scale))); f.setBold(s.bold); f.setItalic(s.italic);
    QFontMetrics fm(f); qreal textW = fm.horizontalAdvance(bl.mainText); qreal lineH = fm.height();
    QRectF bounds(bl.mainCentre.x() - textW / 2.0 - 12, bl.mainCentre.y() - lineH / 2.0 - 5, textW + 24, lineH + 10);
    if (hasTrans) { QFont tf = f; tf.setPixelSize(qMax(6, int(s.bilingualFontSize * scale))); QFontMetrics tfm(tf); const qreal tw = tfm.horizontalAdvance(bl.transText); const qreal th = tfm.height(); bounds = bounds.united(QRectF(bl.transCentre.x() - tw / 2.0 - 12, bl.transCentre.y() - th / 2.0 - 5, tw + 24, th + 10)); }
    bl.bounds = bounds; bl.valid = true; return bl;
}

void VideoPreview::drawOutlinedText(QPainter &p, const QString &txt, const QColor &fg, const QPointF &centre, qreal fontPx, int outlinePx, const QColor &outlineColor, bool bgBox, const QColor &bgColor) const {
    if (txt.isEmpty()) return;
    QFont f = p.font(); f.setPixelSize(qMax(6, int(fontPx))); p.setFont(f);
    QFontMetrics fm(p.font()); const int textW = fm.horizontalAdvance(txt); const int lineH = fm.height();
    if (bgBox) { QRectF box(centre.x() - textW / 2.0 - 10, centre.y() - lineH / 2.0 - 3, textW + 20, lineH + 6); p.setPen(Qt::NoPen); p.setBrush(bgColor); p.drawRoundedRect(box, 3, 3); }
    QRectF target(centre.x() - textW / 2.0, centre.y() - lineH / 2.0, textW, lineH);
    if (outlinePx > 0) { QPainterPath path; QFontMetricsF fmF(p.font()); const qreal textWf = fmF.horizontalAdvance(txt); const qreal ascent = fmF.ascent(); const QPointF baseline(target.left() + (target.width() - textWf)/2.0, target.top() + (target.height() + ascent)/2.0 - 1); path.addText(baseline, p.font(), txt);
        const SubtitleStyle &ss = m_subtitleStyle;
        if (ss.shadowWidth > 0 && ss.shadowOpacity > 0) {
            QColor sc = ss.shadowColor.isValid() ? ss.shadowColor : QColor(0, 0, 0);
            sc.setAlpha(qBound(0, ss.shadowOpacity * 255 / 100, 255));
            p.setPen(Qt::NoPen); p.setBrush(sc);
            QPainterPath sp = path;
            sp.translate(ss.shadowWidth, ss.shadowWidth);
            p.drawPath(sp);
        }
        p.setPen(QPen(outlineColor, outlinePx * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush); p.drawPath(path); p.setPen(Qt::NoPen); p.setBrush(fg); p.drawPath(path); p.setBrush(Qt::NoBrush); }
    else {
        const SubtitleStyle &ss = m_subtitleStyle;
        if (ss.shadowWidth > 0 && ss.shadowOpacity > 0) {
            QColor sc = ss.shadowColor.isValid() ? ss.shadowColor : QColor(0, 0, 0);
            sc.setAlpha(qBound(0, ss.shadowOpacity * 255 / 100, 255));
            p.setPen(sc);
            p.drawText(target.translated(ss.shadowWidth, ss.shadowWidth), Qt::AlignCenter, txt);
        }
        p.setPen(fg); p.drawText(target, Qt::AlignCenter, txt);
    }
}

QRectF VideoPreview::safeFrameSafeRect(const QRectF &area) const {
    if (m_safePreset == int(SafeFrameGeneral)) return QRectF(area.left() + area.width()*0.06, area.top() + area.height()*0.06, area.width()*0.88, area.height()*0.88);
    if (m_safePreset == int(SafeFrameYouTube)) return QRectF(area.left() + area.width()*0.07, area.top() + area.height()*0.07, area.width()*0.86, area.height()*0.69);
    if (m_safePreset == int(SafeFrameYouTubeShorts)) return QRectF(area.left() + area.width()*0.07, area.top() + area.height()*0.12, area.width()*0.80, area.height()*0.66);
    if (m_safePreset == int(SafeFrameFacebook)) return QRectF(area.left() + area.width()*0.06, area.top() + area.height()*0.06, area.width()*0.88, area.height()*0.76);
    if (m_safePreset == int(SafeFrameReels)) return QRectF(area.left() + area.width()*0.07, area.top() + area.height()*0.13, area.width()*0.79, area.height()*0.58);
    if (m_safePreset == int(SafeFrameTikTok)) return QRectF(area.left() + area.width()*0.06, area.top() + area.height()*0.13, area.width()*0.80, area.height()*0.64);
    if (m_safePreset == int(SafeFrameIGStory)) return QRectF(area.left() + area.width()*0.07, area.top() + area.height()*0.16, area.width()*0.86, area.height()*0.60);
    return QRectF();
}

void VideoPreview::paintSafeFrame(QPainter &painter, const QRectF &area) const {
    if (m_safePreset == 0 || area.width() < 10 || area.height() < 10) return;
    auto band = [&](const QRectF &zone, const QString &label, const QColor &fill) {
        if (zone.width() < 2 || zone.height() < 2) return;
        painter.fillRect(zone, fill); painter.setPen(QPen(fill.darker(120), 1, Qt::DashLine)); painter.drawRect(zone.adjusted(0.5, 0.5, -0.5, -0.5));
        if (area.width() > 200 && zone.height() > 18) { painter.setPen(QColor(255, 255, 255, 210)); QFont f = painter.font(); f.setPointSizeF(7.5); f.setBold(true); painter.setFont(f); painter.drawText(zone.adjusted(5, 2, -5, -2), Qt::AlignCenter, label); }
    };
    struct BandInfo { QRectF rect; QString label; };
    QVector<BandInfo> bands; const QRectF safeRect = safeFrameSafeRect(area); QString presetName;
    if (m_safePreset == int(SafeFrameGeneral)) presetName = tr("general");
    else if (m_safePreset == int(SafeFrameYouTube)) bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.20, area.width(), area.height()*0.20), tr("YouTube · 標題/控制列")};
    else if (m_safePreset == int(SafeFrameYouTubeShorts)) { presetName = QStringLiteral("YouTube Shorts"); bands << BandInfo{QRectF(area.left(), area.top(), area.width(), area.height()*0.10), tr("Shorts · 頂部")}; bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.18, area.width(), area.height()*0.18), tr("Shorts · 文案/音樂")}; bands << BandInfo{QRectF(area.right() - area.width()*0.13, area.top(), area.width()*0.13, area.height()), tr("Shorts · 按鈕列")}; }
    else if (m_safePreset == int(SafeFrameFacebook)) { presetName = QStringLiteral("Facebook"); bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.15, area.width(), area.height()*0.15), tr("Facebook · 文案/按鈕")}; }
    else if (m_safePreset == int(SafeFrameReels)) { presetName = QStringLiteral("Reels"); bands << BandInfo{QRectF(area.left(), area.top(), area.width(), area.height()*0.11), tr("Reels · 帳號/音訊")}; bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.26, area.width(), area.height()*0.26), tr("Reels · 文案/按鈕")}; bands << BandInfo{QRectF(area.right() - area.width()*0.14, area.top(), area.width()*0.14, area.height()), tr("Reels · 側邊按鈕")}; }
    else if (m_safePreset == int(SafeFrameTikTok)) { presetName = QStringLiteral("TikTok"); bands << BandInfo{QRectF(area.left(), area.top(), area.width(), area.height()*0.12), tr("TikTok · 帳號/說明")}; bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.18, area.width(), area.height()*0.18), tr("TikTok · 文案/音樂")}; bands << BandInfo{QRectF(area.right() - area.width()*0.14, area.top(), area.width()*0.14, area.height()), tr("TikTok · 按鈕列")}; }
    else if (m_safePreset == int(SafeFrameIGStory)) { presetName = QStringLiteral("IG Story"); bands << BandInfo{QRectF(area.left(), area.top(), area.width(), area.height()*0.14), tr("限動 · 帳號/時間")}; bands << BandInfo{QRectF(area.left(), area.bottom() - area.height()*0.20, area.width(), area.height()*0.20), tr("限動 · 回覆列")}; }
    Q_UNUSED(presetName) for (const auto &b : bands) band(b.rect, b.label, QColor(255, 64, 64, 70));
    if (m_safePreset == int(SafeFrameGeneral)) { QPen pen(QColor(120, 220, 255, 170), 1, Qt::DashLine); painter.setPen(pen); painter.drawRect(QRectF(area.left() + area.width()*0.05, area.top() + area.height()*0.05, area.width()*0.90, area.height()*0.90)); painter.drawRect(QRectF(area.left() + area.width()*0.10, area.top() + area.height()*0.10, area.width()*0.80, area.height()*0.80)); }
    if (!safeRect.isEmpty()) { QPen greenPen(QColor(92, 255, 120, 220), 1.6, Qt::DashLine); greenPen.setDashPattern({6, 4}); painter.setPen(greenPen); painter.setBrush(QColor(92, 255, 120, 18)); painter.drawRoundedRect(safeRect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6); const QString safeLabel = tr("字幕安全區"); QFont labelFont = painter.font(); labelFont.setPointSizeF(8); labelFont.setBold(true); painter.setFont(labelFont); QFontMetricsF fm(labelFont); const qreal pillW = fm.horizontalAdvance(safeLabel) + 18; const qreal pillH = fm.height() + 6; QRectF pill(safeRect.center().x() - pillW/2, safeRect.top() - pillH/2, pillW, pillH); painter.setPen(Qt::NoPen); painter.setBrush(QColor(92, 255, 120, 235)); painter.drawRoundedRect(pill, pillH/2, pillH/2); painter.setPen(QColor(20, 40, 20)); painter.drawText(pill, Qt::AlignCenter, safeLabel); }
}

void VideoPreview::renderHardwareOverlay(QPainter &p) {
    const QRect video = fittedVideoRect();
    if (!m_overlay || video.isEmpty()) return;
    QRect videoInOverlay(0, 0, video.width(), video.height());
    const BlockLayout &blOrig = cachedBlockLayout();
    BlockLayout bl = blOrig;
    QPointF offF(-video.left(), -video.top());
    bl.mainCentre += offF; bl.transCentre += offF; bl.bounds.translate(offF);
    paintOverlay(p, videoInOverlay, bl);
}

void VideoPreview::paintOverlay(QPainter &p, const QRect &videoRect, const BlockLayout &bl) const {
    paintSafeFrame(const_cast<QPainter&>(p), QRectF(videoRect));
    if (bl.valid) {
        const SubtitleStyle &s = m_subtitleStyle;
        const double scale = videoRect.height() / 1080.0;
        drawOutlinedText(const_cast<QPainter&>(p), bl.mainText, s.textColor.isValid() ? s.textColor : QColor(Qt::white), bl.mainCentre, s.fontSize * scale, s.outlineWidth, s.outlineColor, s.showBg, s.showBg && s.bgColor.isValid() ? s.bgColor : QColor(0, 0, 0, 165));
        if (bl.hasTranslation) drawOutlinedText(const_cast<QPainter&>(p), bl.transText, s.bilingualColor.isValid() ? s.bilingualColor : QColor(255, 255, 180), bl.transCentre, s.bilingualFontSize * scale, s.outlineWidth, s.outlineColor, s.showBg, s.showBg && s.bgColor.isValid() ? s.bgColor : QColor(0, 0, 0, 165));
        if (m_cursorOverSubtitle || m_draggingSubtitle) { QPen pen(UiTheme::accent(), m_draggingSubtitle ? 1.8 : 1.1, Qt::DashLine); pen.setDashPattern({4, 3}); const_cast<QPainter&>(p).setPen(pen); const_cast<QPainter&>(p).setBrush(Qt::NoBrush); const_cast<QPainter&>(p).drawRoundedRect(bl.bounds.adjusted(-4, -4, 4, 4), 6, 6); }
    }
    if (m_draggingSubtitle && (m_snapX || m_snapY)) {
        QPen guide(UiTheme::accent(), 1.4, Qt::DashLine); guide.setDashPattern({6, 4}); const_cast<QPainter&>(p).setPen(guide); const_cast<QPainter&>(p).setBrush(Qt::NoBrush);
        if (m_snapX) { const qreal gx = videoRect.left() + m_snapGuideX * videoRect.width(); const_cast<QPainter&>(p).drawLine(QPointF(gx, videoRect.top()), QPointF(gx, videoRect.bottom())); }
        if (m_snapY) { const qreal gy = videoRect.top() + m_snapGuideY * videoRect.height(); const_cast<QPainter&>(p).drawLine(QPointF(videoRect.left(), gy), QPointF(videoRect.right(), gy)); }
    }
}

void VideoPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), UiTheme::windowBg());
    const QRect display = displayRect();
    const QRect video = fittedVideoRect();
    p.fillRect(display, UiTheme::panelHover());

    if (m_hardwarePreview) {
        // HW zero-copy: video rendered by QVideoWidget (D3D surface), subtitles
        // by top-level window overlay (DWM composited above native surface).
        // Keep status texts here for when widget not yet visible.
        if (m_player->source().isEmpty()) { p.setPen(UiTheme::textMuted()); p.drawText(display, Qt::AlignCenter, tr("No video loaded")); }
        else if (!m_videoWidget || !m_videoWidget->isVisible()) { p.setPen(UiTheme::textMuted()); p.drawText(display, Qt::AlignCenter, tr("GPU 硬體解碼中…")); p.setPen(UiTheme::textSecond()); QFont f=p.font(); f.setPointSizeF(8); p.setFont(f); p.drawText(display.adjusted(0,20,0,20), Qt::AlignCenter, hardwareStatusText()); }
        // Badge for HW
        if (!m_currentMediaPath.isEmpty() && m_videoResolution.isValid()) {
            p.setRenderHint(QPainter::Antialiasing, false);
            QFont f = p.font(); f.setPointSizeF(7); p.setFont(f);
            QString badge = QStringLiteral("HW");
            QRect badgeRect(display.right() - 42, display.top() + 4, 38, 14);
            p.setBrush(QColor(0, 0, 0, 90)); p.setPen(QColor(255, 255, 255, 30)); p.drawRoundedRect(badgeRect, 3, 3);
            p.setPen(QColor(255, 255, 255, 180)); p.drawText(badgeRect, Qt::AlignCenter, badge);
        }
        return;
    }

    if (!m_frameImage.isNull()) p.drawImage(video, m_frameImage);
    else if (m_player->source().isEmpty()) { p.setPen(UiTheme::textMuted()); p.drawText(display, Qt::AlignCenter, tr("No video loaded")); }
    else { p.setPen(UiTheme::textMuted()); p.drawText(display, Qt::AlignCenter, tr("正在載入影片…")); }
    p.setRenderHint(QPainter::Antialiasing, true);
    const BlockLayout &bl = cachedBlockLayout();
    paintOverlay(p, video, bl);
    if (!m_currentMediaPath.isEmpty() && m_videoResolution.isValid()) {
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = p.font(); f.setPointSizeF(7); p.setFont(f);
        p.setPen(QColor(200, 200, 210, 180));
        QString badge = m_hardwarePreview ? QStringLiteral("HW") : QStringLiteral("SW");
        QRect badgeRect(display.right() - 42, display.top() + 4, 38, 14);
        p.setBrush(QColor(0, 0, 0, 90)); p.setPen(QColor(255, 255, 255, 30)); p.drawRoundedRect(badgeRect, 3, 3);
        p.setPen(QColor(255, 255, 255, 180)); p.drawText(badgeRect, Qt::AlignCenter, badge);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void VideoPreview::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    invalidateLayoutCache();
    updateVideoWidgetGeometry();
    updateOverlayGeometry();
    updateTopLevelOverlayGeometry();
    updateVolumeGeometry();
    update();
    requestOverlayUpdate(true);
}
void VideoPreview::moveEvent(QMoveEvent *e) {
    QWidget::moveEvent(e);
    updateOverlayGeometry();
    updateTopLevelOverlayGeometry();
    updateVolumeGeometry();
}
void VideoPreview::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (window()) window()->installEventFilter(this);
    // Defer heavy overlay geometry to next event loop so QStackedWidget
    // page switch returns immediately (~0ms vs ~50-80ms for native window ops).
    QTimer::singleShot(0, this, [this]{
        if (!isVisible()) return;
        updateVideoWidgetGeometry();
        updateOverlayGeometry();
        updateTopLevelOverlayGeometry();
        updateVolumeGeometry();
        requestOverlayUpdate(true);
        if (m_hardwarePreview && m_topLevelOverlay) {
            m_topLevelOverlay->show();
            m_topLevelOverlay->raise();
            m_topLevelOverlay->update();
        }
    });
}
void VideoPreview::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    if (m_overlay) m_overlay->hide();
    if (m_topLevelOverlay) m_topLevelOverlay->hide();
    if (m_volumePopup) m_volumePopup->hide();
    if (window()) window()->removeEventFilter(this);
}

void VideoPreview::applyPosition(qint64 posMs) {
    if (m_player->duration() <= 0) {
        // 媒體尚未就緒（例如硬體/軟體模式切換時重建播放器、source 重新載入中）。
        // 記住目標位置，待媒體 ready 後由 metaDataChanged/LoadedMedia 自動跳轉，
        // 避免 seek 被靜默丟棄而導致影片/字幕停留在 0ms（字幕錯位）。
        m_pendingSeekMs = qMax<qint64>(0, posMs);
        return;
    }
    const qint64 pos = qBound<qint64>(0, posMs, m_player->duration());
    m_pendingSeekMs = -1;
    m_player->setPosition(pos);
    if (!m_seeking) m_slider->setValue(int(pos * SLIDER_MAX / m_player->duration()));
m_timeLabel->setText(TimeFmt::formatClock(pos) + " / " + TimeFmt::formatClock(m_player->duration()));
    m_overlayGate.invalidate();
    emit positionChanged(pos); invalidateLayoutCache(); update(); requestOverlayUpdate(true);
}

void VideoPreview::seekTo(qint64 posMs) { applyPosition(posMs); }

#ifdef YUMU_SELFTEST_PROBE
qint64 VideoPreview::probeDuration() const {
    return m_player ? m_player->duration() : 0;
}
qint64 VideoPreview::probeCurrentPosition() const {
    return m_player ? m_player->position() : 0;
}
QString VideoPreview::probeActiveSubtitleText() const {
    return subtitleBlockLayout().mainText;
}
QString VideoPreview::probeOverlayGeometryDebug() const {
    const QRect vr = const_cast<VideoPreview*>(this)->fittedVideoRect();
    QRect ov;
    QString par;
    if (m_hardwarePreview && m_topLevelOverlay) {
        // Top-level window geometry is in screen coords; map to VideoPreview local for comparison
        QRect global = m_topLevelOverlay->geometry();
        if (global.isValid() && isVisible()) {
            ov = QRect(mapFromGlobal(global.topLeft()), global.size());
        } else {
            ov = m_topLevelOverlay->geometry();
        }
        par = m_topLevelOverlay->isVisible() ? QString("topLevelWindow visible=yes") : QString("topLevelWindow visible=no");
        if (!m_topLevelOverlay->isVisible()) par += " (hidden)";
    } else {
        ov = m_overlay ? m_overlay->geometry() : QRect();
        par = !m_overlay ? QString("none") : (m_overlay->parent() == m_videoWidget ? QString("videoWidget") : (m_overlay->parent() == this ? QString("VideoPreview") : QString("other")));
    }
    const QRect vw = m_videoWidget ? m_videoWidget->geometry() : QRect();
    return QString("vidRect=%1,%2,%3,%4 overlay=%5,%6,%7,%8[%9] videoWidget=%10,%11,%12,%13 overlayW=%14 overlayH=%15")
        .arg(vr.x()).arg(vr.y()).arg(vr.width()).arg(vr.height())
        .arg(ov.x()).arg(ov.y()).arg(ov.width()).arg(ov.height()).arg(par)
        .arg(vw.x()).arg(vw.y()).arg(vw.width()).arg(vw.height())
        .arg(vr.width() - ov.width()).arg(vr.height() - ov.height());
}
QString VideoPreview::probeRenderHardwareOverlayDebug() const {
    const QRect video = fittedVideoRect();
    const BlockLayout &bl = cachedBlockLayout();
    QPointF offF(-video.left(), -video.top());
    const QPointF localCentre = bl.mainCentre + offF;
    // Unified: check against video rect (where paintOverlay draws), not overlay widget
    const bool inside = video.width() > 0 && video.height() > 0
        && localCentre.x() >= 0 && localCentre.x() <= video.width()
        && localCentre.y() >= 0 && localCentre.y() <= video.height();
    return QString("localMainCentre=%1,%2 inside=%3 boundsValid=%4 text='%5'")
        .arg(localCentre.x(), 0, 'f', 1).arg(localCentre.y(), 0, 'f', 1)
        .arg(inside ? "yes" : "NO")
        .arg(bl.valid ? "yes" : "no")
        .arg(bl.mainText);
}
QString VideoPreview::probeHardwareOverlayPixelDebug() const {
    if (!m_hardwarePreview) return QString("hw=off");
    const QRect video = fittedVideoRect();
    if (video.isEmpty()) return QString("empty");
    // Unified path: subtitles are composited in VideoPreview::paintEvent via
    // paintOverlay (HW decode still via QT_FFMPEG_DECODING_HW_DEVICE_TYPES).
    // Render the same way into an offscreen image and count painted pixels.
    QImage img(video.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const BlockLayout &bl = cachedBlockLayout();
    // paintOverlay expects videoRect at (0,0) and centres translated accordingly
    BlockLayout local = bl;
    QPointF offF(-video.x(), -video.y());
    local.mainCentre += offF;
    local.transCentre += offF;
    local.bounds.translate(offF);
    QRect videoInImg(0, 0, video.width(), video.height());
    const_cast<VideoPreview*>(this)->paintOverlay(p, videoInImg, local);
    p.end();
    int nonTransparent = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 10) ++nonTransparent;
        }
    }
    const BlockLayout &orig = cachedBlockLayout();
    return QString("video=%1x%2 pixels=%3 valid=%4 text='%5'")
        .arg(img.width()).arg(img.height()).arg(nonTransparent)
        .arg(orig.valid ? "yes" : "no").arg(orig.mainText);
}
bool VideoPreview::probeOverlayIsVisible() const {
    if (m_hardwarePreview) {
        return m_topLevelOverlay && m_topLevelOverlay->isVisible();
    }
    // SW path uses direct paint, overlay stays hidden but subtitle still visible via paintEvent
    // For probe we consider SW visible if VideoPreview has valid layout
    return cachedBlockLayout().valid;
}
QString VideoPreview::probeOverlayParentDebug() const {
    if (m_hardwarePreview) {
        if (!m_topLevelOverlay) return QString("no-topLevelOverlay");
        // top-level window has no parent widget (nullptr) but host is VideoPreview
        if (m_topLevelOverlay->isVisible()) return QString("parent=topLevelWindow visible=yes");
        return QString("parent=topLevelWindow visible=no");
    }
    if (!m_overlay) return QString("no-overlay");
    QObject *par = m_overlay->parent();
    if (par == m_videoWidget) return QString("parent=videoWidget");
    if (par == this) return QString("parent=VideoPreview");
    if (!par) return QString("parent=null");
    return QString("parent=%1").arg(par->objectName().isEmpty() ? par->metaObject()->className() : par->objectName());
}
#endif

void VideoPreview::onPlayPause() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
    else m_player->play();
}

void VideoPreview::onSliderMoved(int value) {
    if (m_player->duration() <= 0) return;
    qint64 pos = qint64(value) * m_player->duration() / SLIDER_MAX;
    m_hoverPosMs = pos; m_hoverLabel->setText(TimeFmt::formatClock(pos, true)); applyPosition(pos);
}

void VideoPreview::onSliderPressed() { m_seeking = true; }
void VideoPreview::onSliderReleased() { m_seeking = false; applyPosition(m_hoverPosMs); }

void VideoPreview::onPositionChanged(qint64 pos) {
    // 計算字幕 key，供後續門控與切換判斷
    qint64 key = -1;
    if (m_model) for (int i = 0; i < m_model->rowCount(); ++i) { const auto &e = m_model->entryAt(i); if (pos >= e.startMs && pos < e.endMs) { key = e.startMs; break; } }
    const bool keyChanged = (key != m_lastSubtitleKey);

    // 節流：slider / timeLabel / 波形/表格 廣播均受 previewFpsCap 門控，字幕切換時強制刷新
    const int gateMs = effectiveFpsGateMs();
    bool allowTick = keyChanged || !m_overlayGate.isValid() || m_overlayGate.elapsed() >= gateMs;

    if (!m_seeking && m_player->duration() > 0) {
        if (allowTick) m_slider->setValue(int(pos * SLIDER_MAX / m_player->duration()));
    }
    if (allowTick) m_timeLabel->setText(TimeFmt::formatClock(pos) + " / " + TimeFmt::formatClock(m_player->duration()));
    if (!m_seeking && allowTick) emit positionChanged(pos);

    if (keyChanged) {
        m_lastSubtitleKey = key;
        invalidateLayoutCache();
        update();
        requestOverlayUpdate(true);
    } else if (allowTick) {
        // 非切換 tick 也需按門控刷新疊加層（位置驅動的 layout 緩存可能因 videoRect 微變）
        requestOverlayUpdate();
    }
}

void VideoPreview::onDurationChanged(qint64 dur) { emit durationChanged(dur); updateOverlayGeometry(); }

void VideoPreview::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    m_playBtn->setText(state == QMediaPlayer::PlayingState
        ? QString::fromUtf8("\xe2\x8f\xb8") : QString::fromUtf8("\xe2\x96\xb6"));
}

void VideoPreview::updateMuteButtonIcon() {
    if (!m_muteBtn) return;
    m_muteBtn->setText(m_muted
        ? QString::fromUtf8("\xf0\x9f\x94\x87") : QString::fromUtf8("\xf0\x9f\x94\x8a"));
}

void VideoPreview::onVolumeSliderChanged(int value) {
    const float v = qBound(0, value, 100) / 100.0f;
    m_volume = v;
    if (!m_muted) m_audio->setVolume(v);
    if (m_muted && v > 0.0f) {
        m_muted = false;
        m_audio->setMuted(false);
        m_audio->setVolume(v);
        updateMuteButtonIcon();
    }
    AppConfig::instance().preview.volume = v;
    AppConfig::instance().preview.muted = m_muted;
    AppConfig::instance().save();
}

void VideoPreview::onMuteToggled() {
    m_muted = !m_muted;
    m_audio->setMuted(m_muted);
    if (m_volumeSlider) {
        m_volumeSlider->blockSignals(true);
        if (m_muted) {
            m_audio->setVolume(0.0f);
            m_volumeSlider->setValue(0);
        } else {
            m_audio->setVolume(m_volume);
            m_volumeSlider->setValue(qRound(m_volume * 100));
        }
        m_volumeSlider->blockSignals(false);
    } else {
        if (m_muted) m_audio->setVolume(0.0f);
        else m_audio->setVolume(m_volume);
    }
    updateMuteButtonIcon();
    AppConfig::instance().preview.muted = m_muted;
    AppConfig::instance().save();
    // Let the user see the effect: pop the strip briefly (it auto-hides).
    showVolumeSlider();
    scheduleVolumeHide();
}

void VideoPreview::setVolume(float volume) {
    volume = qBound(0.0f, volume, 1.0f);
    m_volume = volume;
    if (m_volumeSlider) {
        const int v = qRound(volume * 100);
        if (m_volumeSlider->value() != v) m_volumeSlider->setValue(v);
    }
    if (!m_muted && m_audio) m_audio->setVolume(volume);
    AppConfig::instance().preview.volume = volume;
    AppConfig::instance().save();
}

void VideoPreview::setMuted(bool muted) {
    if (m_muted == muted) return;
    m_muted = muted;
    if (m_audio) {
        m_audio->setMuted(muted);
        m_audio->setVolume(muted ? 0.0f : m_volume);
    }
    updateMuteButtonIcon();
    AppConfig::instance().preview.muted = muted;
    AppConfig::instance().save();
}

void VideoPreview::onPlayerErrorOccurred(QMediaPlayer::Error error, const QString &errorString) {
    Q_UNUSED(error)
    if (errorString.isEmpty()) return;
    if (m_hardwarePreview && !m_hardwareErrorNotified) {
        const QString lower = errorString.toLower();
        bool isHwIssue = lower.contains("hardware") || lower.contains("d3d11") || lower.contains("dxva") || lower.contains("cuda") || lower.contains("hwaccel") || lower.contains("codec");
        if (isHwIssue || m_frameImage.isNull()) {
            m_hardwareErrorNotified = true;
            qWarning() << "[VideoPreview] hardware decode error:" << errorString << " -> fallback to SW";
            emit hardwareDecodeFailed(errorString);
            m_hardwarePreview = false;
            AppConfig::instance().preview.hardwarePreview = false;
            AppConfig::instance().save();
            applyHardwareDecodePreference();
            reloadCurrentMedia(true);
            applyHardwareMode();
        }
    }
}

void VideoPreview::onPlayerMediaStatusChanged(QMediaPlayer::MediaStatus status) {
    if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
        if (m_pendingSeekMs >= 0 && m_player->duration() > 0) {
            qint64 target = qBound<qint64>(0, m_pendingSeekMs, m_player->duration());
            m_player->setPosition(target); m_pendingSeekMs = -1;
            if (m_pendingAutoPlay) { m_player->play(); m_pendingAutoPlay = false; }
        }
    }
}

void VideoPreview::keyPressEvent(QKeyEvent *e) { if (e->key() == Qt::Key_Space) { onPlayPause(); e->accept(); return; } QWidget::keyPressEvent(e); }
void VideoPreview::enterEvent(QEnterEvent *e) { m_hovering = true; QWidget::enterEvent(e); }
void VideoPreview::leaveEvent(QEvent *e) { m_hovering = false; m_cursorOverSubtitle = false; scheduleVolumeHide(); unsetCursor(); update(); requestOverlayUpdate(true); QWidget::leaveEvent(e); }

void VideoPreview::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        const BlockLayout bl = cachedBlockLayout();
        if (bl.valid && bl.bounds.adjusted(-10, -10, 10, 10).contains(e->position())) {
            const QRectF video = fittedVideoRect();
            if (video.width() > 1 && video.height() > 1) {
                m_draggingSubtitle = true;
                const QPointF centre = bl.centre();
                const QPointF cursorNorm((e->position().x() - video.left()) / video.width(), (e->position().y() - video.top()) / video.height());
                const QPointF centreNorm((centre.x() - video.left()) / video.width(), (centre.y() - video.top()) / video.height());
                m_dragGrabNorm = centreNorm - cursorNorm;
                if (m_subtitleStyle.posY < 0.0) {
                    m_subtitleStyle.posX = qBound(0.02, centreNorm.x(), 0.98);
                    m_subtitleStyle.posY = qBound(0.02, centreNorm.y(), 0.98);
                    if (m_model) { m_model->style().posX = m_subtitleStyle.posX; m_model->style().posY = m_subtitleStyle.posY; }
                }
                setCursor(Qt::ClosedHandCursor); update(); requestOverlayUpdate(true); e->accept(); return;
            }
        }
    }
    QWidget::mousePressEvent(e);
}

void VideoPreview::mouseMoveEvent(QMouseEvent *e) {
    if (m_draggingSubtitle) {
        const QRectF video = fittedVideoRect();
        if (video.width() > 1 && video.height() > 1) {
            const QPointF cursorNorm(qBound(0.0, (e->position().x() - video.left()) / video.width(), 1.0), qBound(0.0, (e->position().y() - video.top()) / video.height(), 1.0));
            qreal nx = cursorNorm.x() + m_dragGrabNorm.x(); qreal ny = cursorNorm.y() + m_dragGrabNorm.y();
            applySnap(nx, ny);
            m_subtitleStyle.posX = qBound(0.02, nx, 0.98); m_subtitleStyle.posY = qBound(0.02, ny, 0.98);
            if (m_model) { m_model->style().posX = m_subtitleStyle.posX; m_model->style().posY = m_subtitleStyle.posY; }
            invalidateLayoutCache(); update(); requestOverlayUpdate(true); e->accept(); return;
        }
    }
    const BlockLayout bl = cachedBlockLayout();
    const bool over = bl.valid && bl.bounds.adjusted(-6, -6, 6, 6).contains(e->position());
    if (over != m_cursorOverSubtitle) { m_cursorOverSubtitle = over; setCursor(over ? Qt::OpenHandCursor : Qt::ArrowCursor); update(); requestOverlayUpdate(true); }
    QWidget::mouseMoveEvent(e);
}

void VideoPreview::mouseReleaseEvent(QMouseEvent *e) {
    if (m_draggingSubtitle && e->button() == Qt::LeftButton) {
        m_draggingSubtitle = false; m_snapX = false; m_snapY = false;
        m_lastDragEndMs = QDateTime::currentMSecsSinceEpoch();
        setCursor(m_cursorOverSubtitle ? Qt::OpenHandCursor : Qt::ArrowCursor);
        emit subtitlePositionEdited(); e->accept(); return;
    }
    QWidget::mouseReleaseEvent(e);
}

void VideoPreview::mouseDoubleClickEvent(QMouseEvent *e) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastDragEndMs < 600) { e->accept(); return; }
    const BlockLayout bl = cachedBlockLayout();
    if (bl.valid && bl.bounds.adjusted(-6, -6, 6, 6).contains(e->position())) {
        m_subtitleStyle.posX = 0.5; m_subtitleStyle.posY = -1.0;
        if (m_model) { m_model->style().posX = 0.5; m_model->style().posY = -1.0; }
        invalidateLayoutCache(); emit subtitlePositionEdited(); update(); requestOverlayUpdate(true); e->accept(); return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void VideoPreview::wheelEvent(QWheelEvent *e) {
    if (m_slider->geometry().contains(e->position().toPoint())) {
        if (m_player->duration() <= 0) { e->accept(); return; }
        int delta = e->angleDelta().y(); int step = (delta > 0) ? 100 : -100;
        const qint64 pos = m_player->position();
        applyPosition(pos + step * m_player->duration() / 1000); e->accept(); return;
    }
    QWidget::wheelEvent(e);
}

void VideoPreview::updateHoverTime(const QPoint &pos) {
    if (!m_hovering || m_player->duration() <= 0) return;
    int sliderWidth = m_slider->width() - m_slider->style()->pixelMetric(QStyle::PM_SliderThickness);
    int x = pos.x() - m_slider->geometry().left();
    int value = qBound(0, x * SLIDER_MAX / qMax(1, sliderWidth), SLIDER_MAX);
    qint64 posMs = qint64(value) * m_player->duration() / SLIDER_MAX;
    m_hoverPosMs = posMs; m_hoverLabel->setText(TimeFmt::formatClock(posMs, true));
    int labelX = m_slider->geometry().left() + x - m_hoverLabel->width() / 2;
    labelX = qBound(0, labelX, width() - m_hoverLabel->width());
    m_hoverLabel->move(labelX, m_slider->geometry().top() - 30); m_hoverLabel->show();
}
