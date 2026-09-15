#pragma once
#include <QObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QMutex>
#include <QAtomicInt>

class QThread;

struct GpuDevice {
    QString name;
    QString vendor;          // nvidia / amd / intel / unknown
    QString driverVersion;
    int     vramMb = 0;
};

// Everything the app needs to know about the machine before it decides which
// transcription backend to install. Filled in by HardwareProbe::detectNow().
struct HardwareInfo {
    bool    probed = false;
    QString cpuName;
    QString osName;
    QString architecture;
    int     logicalCores = 0;
    int     recommendedThreads = 4;
    qint64  totalRamMb = 0;

    QList<GpuDevice> gpus;
    bool    hasNvidia = false;
    QString nvidiaName;
    QString nvidiaDriver;
    QString driverCudaVersion;   // highest CUDA runtime the driver can host
int     nvidiaVramMb = 0;

    bool    vulkanRuntime = false;   // vulkan-1.dll present
    bool    vulkanDrivers = false;   // at least one installed ICD
};

class HardwareProbe : public QObject {
    Q_OBJECT
public:
    static HardwareProbe &instance();

    const HardwareInfo &info() const { return m_info; }
    bool hasResult() const { return m_info.probed; }

    // Runs detection on a worker thread; probeFinished() always arrives on the
    // caller's thread. A cached result is re-emitted unless force is set.
    void refresh(bool force = false);

    static HardwareInfo detectNow();
    // Instant registry-only checks for decision points that cannot afford a
    // full probe (nvidia-smi takes a few seconds).
    static bool hasNvidiaGpuQuick();
    static bool vulkanRuntimeQuick();

signals:
    void probeFinished(const HardwareInfo &info);

private:
    explicit HardwareProbe(QObject *parent = nullptr) : QObject(parent) {}

    HardwareInfo m_info;
    HardwareInfo m_pending;
    QPointer<QThread> m_thread;
    QMutex m_probeMutex;
    QAtomicInt m_probeState = 0;  // 0=idle, 1=running, 2=cancel-requested
};


