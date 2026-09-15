#include "audiopeaks.h"
#include "securitypolicy.h"

#include <QFileInfo>
#include <cstring>

AudioPeaks::AudioPeaks(QObject *parent) : QObject(parent) {}

AudioPeaks::~AudioPeaks() { cancel(); }

void AudioPeaks::reset() {
    m_pcm.clear();
    m_peaks.clear();
    m_progress = 0;
    m_ready = false;
    m_error.clear();
    m_bucketSize = 0;
    m_processedSamples = 0;
    m_consumedInBucket = 0;
    m_curLo = 1.0f;
    m_curHi = -1.0f;
}

void AudioPeaks::cancel() {
    if (m_proc) {
        m_cancelled = true;
        m_proc->kill();
        m_proc->waitForFinished(2000);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    reset();
}

void AudioPeaks::load(const QString &mediaPath, const QString &ffmpegPath, qint64 durationMsHint) {
    cancel();
    reset();
    m_cancelled = false;

    if (mediaPath.isEmpty() || !QFileInfo::exists(mediaPath)) {
        emit failed(QStringLiteral("Media file not found"));
        return;
    }
    if (!SecurityPolicy::isSafeMediaInputPath(mediaPath)) {
        emit failed(tr("Invalid or unsafe media input path: %1").arg(mediaPath));
        return;
    }

    QString exe = ffmpegPath.trimmed().isEmpty() ? QStringLiteral("ffmpeg") : ffmpegPath.trimmed();

    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    // Input options come before -i; output options (-ac/-ar/-f) must come
    // AFTER the input file. Putting -f/-ar before -i either treats the media
    // as raw PCM input or fails with "Option sample_rate not found" on
    // newer ffmpeg builds.
    QStringList args{
        QStringLiteral("-v"), QStringLiteral("quiet"),
        QStringLiteral("-vn"),
        QStringLiteral("-i"), mediaPath,
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("-ar"), QString::number(kSampleRate),
        QStringLiteral("-f"), QStringLiteral("s16le"),
        QStringLiteral("-")
    };
    m_proc->setArguments(args);

    // Rough total size estimate for progress reporting (16-bit mono).
    if (durationMsHint > 0)
        m_expectedBytes = durationMsHint / 1000 * kSampleRate * kBytesPerSample;

    // Determine whether we can use the streaming path. Streaming is possible
    // when the duration is known — we can pre-compute the bucket size and
    // know how many buckets there will be.
    if (durationMsHint > 0) {
        const qint64 totalSamples = durationMsHint / 1000 * kSampleRate;
        beginStreaming(totalSamples);
    }

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &AudioPeaks::onReadOutput);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AudioPeaks::onProcessFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_cancelled && m_error.isEmpty())
            m_error = tr("Cannot start ffmpeg. Check the FFmpeg path in Settings.");
    });

    m_proc->start(QIODevice::ReadOnly);
    if (!m_proc->waitForStarted(5000)) {
        if (m_error.isEmpty())
            m_error = tr("Cannot start ffmpeg. Check the FFmpeg path in Settings.");
        emit failed(m_error);
    }
}

// ---------------------------------------------------------------------------
// Streaming pipeline
// ---------------------------------------------------------------------------

void AudioPeaks::beginStreaming(qint64 totalSamples) {
    const qint64 bucketSize = qMax<qint64>(1, totalSamples / kPeakCount);
    m_bucketSize = bucketSize;
    m_peaks.reserve(int(totalSamples / bucketSize) + 2);
    m_processedSamples = 0;
    m_consumedInBucket = 0;
    m_curLo = 1.0f;
    m_curHi = -1.0f;
}

void AudioPeaks::consumeSamples(const char *data, qint64 bytes) {
    // Interpret the buffer as 16-bit signed little-endian mono samples.
    const qint64 sampleCount = bytes / kBytesPerSample;
    if (sampleCount <= 0) return;

    const auto *samples = reinterpret_cast<const int16_t *>(data);

    // Hot loop: register-local counters avoid repeated member reloads.
    const qint64 bucketSize = m_bucketSize;
    qint64 consumed = m_consumedInBucket;
    float curLo = m_curLo;
    float curHi = m_curHi;
    const int existing = m_peaks.size();
    if (m_peaks.capacity() < existing + int(sampleCount / bucketSize) + 2)
        m_peaks.reserve(existing + int(sampleCount / bucketSize) + 2);
    for (qint64 s = 0; s < sampleCount; ++s) {
        const float v = samples[s] * (1.0f / 32768.0f);
        if (v < curLo) curLo = v;
        if (v > curHi) curHi = v;
        if (++consumed >= bucketSize) {
            m_peaks.append({curLo, curHi});
            curLo = 1.0f;
            curHi = -1.0f;
            consumed = 0;
        }
    }
    m_consumedInBucket = consumed;
    m_curLo = curLo;
    m_curHi = curHi;
    m_processedSamples += sampleCount;
}

void AudioPeaks::finalizeTrailingBucket() {
    if (m_consumedInBucket > 0) {
        // Incomplete trailing bucket — still append it.
        m_peaks.append({m_curLo, m_curHi});
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AudioPeaks::onReadOutput() {
    if (!m_proc) return;
    const QByteArray chunk = m_proc->readAllStandardOutput();
    if (chunk.isEmpty()) return;

    if (m_bucketSize > 0) {
        // Streaming path: feed samples into the running bucket calculator.
        consumeSamples(chunk.constData(), chunk.size());
    } else {
        // No duration hint — fall back to accumulating all PCM for a
        // single-pass peak computation in onProcessFinished.
        m_pcm += chunk;
    }

    if (m_expectedBytes > 0) {
        const qint64 bytesSoFar = (m_bucketSize > 0)
            ? qint64(m_processedSamples) * kBytesPerSample
            : m_pcm.size();
        const int pct = int(qMin<qint64>(99, bytesSoFar * 100 / m_expectedBytes));
        if (pct != m_progress) {
            m_progress = pct;
            emit progress(pct);
        }
    }
}

void AudioPeaks::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status)
    m_proc->deleteLater();
    m_proc = nullptr;
    if (m_cancelled) return;

    if (exitCode != 0 && m_pcm.isEmpty() && m_peaks.isEmpty()) {
        if (m_error.isEmpty())
            m_error = tr("ffmpeg could not decode audio from this file.");
        emit failed(m_error);
        return;
    }

    if (m_bucketSize > 0) {
        // Streaming path: flush the last partial bucket.
        finalizeTrailingBucket();
    } else {
        // Legacy path: compute peaks from the accumulated buffer.
        const qint64 sampleCount = m_pcm.size() / kBytesPerSample;
        if (sampleCount <= 0) {
            m_error = tr("No audio track found in this video.");
            emit failed(m_error);
            return;
        }
        const auto *samples = reinterpret_cast<const int16_t *>(m_pcm.constData());
        const qint64 bucketSize = qMax<qint64>(1, sampleCount / kPeakCount);
        m_peaks.reserve(int(sampleCount / bucketSize) + 1);

        for (qint64 start = 0; start < sampleCount; start += bucketSize) {
            const qint64 end = qMin(start + bucketSize, sampleCount);
            float lo = 1.0f, hi = -1.0f;
            for (qint64 i = start; i < end; ++i) {
                const float v = samples[i] / 32768.0f;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            m_peaks.append({lo, hi});
        }
        m_pcm.clear();
        m_pcm.shrink_to_fit();
    }

    m_ready = true;
    m_progress = 100;
    emit progress(100);
    emit ready();
}
