#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

// Decodes the audio track of a media file into normalized peak pairs
// (min/max per bucket) using the runtime ffmpeg binary, so the timeline
// can visualize the real waveform of the loaded video.
//
// When a duration hint is supplied to load(), peaks are computed in a
// streaming fashion as the decoded PCM arrives. Only the bucket currently
// being filled plus the finalized buckets live in memory, so peak memory
// drops from O(whole-track PCM) to O(kPeakCount Peak + one read chunk).
class AudioPeaks : public QObject {
    Q_OBJECT
public:
    struct Peak { float lo; float hi; };

    explicit AudioPeaks(QObject *parent = nullptr);
    ~AudioPeaks() override;

    // Starts an asynchronous decode; ready() is emitted when finished.
    void load(const QString &mediaPath, const QString &ffmpegPath, qint64 durationMsHint = 0);
    void cancel();

    bool isReady() const { return m_ready; }
    bool isEmpty() const { return m_peaks.isEmpty(); }
    const QVector<Peak> &peaks() const { return m_peaks; }
    QString lastError() const { return m_error; }

signals:
    void ready();
    void failed(const QString &message);
    void progress(int percent);

private slots:
    void onReadOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    // Consume `data` as 16-bit signed little-endian mono samples and fold
    // them into the bucket currently being filled. When enough samples
    // have been folded to fill a bucket the (lo, hi) pair is appended to
    // m_peaks and the active bucket is reset. Caller must have called
    // beginStreaming() with a valid total sample count first.
    void beginStreaming(qint64 totalSamples);
    void consumeSamples(const char *data, qint64 bytes);
    void finalizeTrailingBucket();

    void reset();
    static const int kSampleRate = 8000;   // mono s16le
    // High enough that even at max timeline zoom several peaks map to each
    // screen pixel, letting the renderer interpolate a smooth wave line
    // instead of showing blocky per-bucket columns.
    static const int kPeakCount  = 16000;
    static const int kBytesPerSample = 2;

    QProcess      *m_proc   = nullptr;
    // Used only when streaming is not possible (no duration hint) to keep
    // the legacy behaviour working. With a hint, m_pcm stays empty and the
    // streaming pipeline writes straight into m_peaks.
    QByteArray     m_pcm;
    qint64         m_expectedBytes = 0;
    int            m_progress = 0;
    bool           m_ready    = false;
    bool           m_cancelled = false;
    QString        m_error;
    QVector<Peak>  m_peaks;

    // Streaming pipeline state. bucketSize is the number of samples per
    // (lo, hi) bucket. m_processedSamples counts how many samples have
    // already been folded into the current bucket. m_curLo/m_curHi track
    // the running minimum/maximum of the active bucket.
    qint64         m_bucketSize = 0;
    qint64         m_processedSamples = 0;
    qint64         m_consumedInBucket = 0;
    float          m_curLo = 1.0f;
    float          m_curHi = -1.0f;
};
