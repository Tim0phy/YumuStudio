#include "hardwareprobe.h"

#include <QList>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QAtomicInt>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString runCommand(const QString &program, const QStringList &args, int timeoutMs, bool *ok = nullptr)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    if (!process.waitForStarted(qMin(timeoutMs, 8000))) {
        if (ok) *ok = false;
        return {};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (ok) *ok = false;
        return {};
    }
    const QString output = QString::fromLocal8Bit(process.readAll());
    if (ok) *ok = process.exitCode() == 0;
    return output;
}

QString normalizeVendor(const QString &text)
{
    const QString s = text.toLower();
    if (s.contains("nvidia") || s.contains("geforce") || s.contains("quadro") || s.contains("tesla"))
        return "nvidia";
    if (s.contains("amd") || s.contains("radeon") || s.contains("advanced micro"))
        return "amd";
    if (s.contains("intel") || s.contains("arc(tm)") || s.contains("iris"))
        return "intel";
    return "unknown";
}

qint64 totalPhysicalMemoryMb()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return static_cast<qint64>(status.ullTotalPhys / (1024ull * 1024ull));
#endif
    return 0;
}

// Display adapters come from the driver class key: instant to read and it also
// covers machines where no vendor tool is installed.
QList<GpuDevice> enumerateDisplayAdapters()
{
    QList<GpuDevice> devices;
#ifdef Q_OS_WIN
    const QString classKey =
        "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\"
        "{4d36e968-e325-11ce-bfc1-08002be10318}";
    QSettings adapters(classKey, QSettings::NativeFormat);
    for (const QString &group : adapters.childGroups()) {
        if (!QRegularExpression("^\\d{4}$").match(group).hasMatch()) continue;
        const QString description = adapters.value(group + "/DriverDesc").toString().trimmed();
        if (description.isEmpty()) continue;
        GpuDevice device;
        device.name = description;
        device.vendor = normalizeVendor(description + " " + adapters.value(group + "/ProviderName").toString());
        device.driverVersion = adapters.value(group + "/DriverVersion").toString();
        const qulonglong bytes = adapters.value(group + "/HardwareInformation.qwMemorySize").toULongLong();
        if (bytes > 0) device.vramMb = static_cast<int>(bytes / (1024ull * 1024ull));
        bool duplicate = false;
        for (const GpuDevice &known : devices)
            if (known.name.compare(device.name, Qt::CaseInsensitive) == 0) duplicate = true;
        if (!duplicate) devices << device;
    }
#endif
    return devices;
}

bool vulkanRuntimePresent()
{
#ifdef Q_OS_WIN
    const QString systemRoot = QProcessEnvironment::systemEnvironment().value("SystemRoot", "C:/Windows");
    if (!QFileInfo::exists(QDir(systemRoot).filePath("System32/vulkan-1.dll")))
        return false;
    // 額外嘗試用 vulkaninfo 輕量探測 ICD 是否可創建實例（H-02）
    // 若 vulkaninfo 存在則用其判斷，否則僅憑 DLL 存在即返回 true
    const QString vulkanInfo = QStandardPaths::findExecutable("vulkaninfo");
    if (!vulkanInfo.isEmpty()) {
        bool ok = false;
        const QString out = runCommand(vulkanInfo, {"--summary"}, 5000, &ok);
        if (ok && out.contains("Vulkan Instance Version", Qt::CaseInsensitive))
            return true;
        // vulkaninfo 存在但執行失敗，仍認為 runtime 存在但驅動缺失，返回 true 但由調用方結合 vulkanDriversInstalled 判斷
    }
    return true;
#else
    return false;
#endif
}

bool vulkanDriversInstalled()
{
#ifdef Q_OS_WIN
    QSettings drivers("HKEY_LOCAL_MACHINE\\SOFTWARE\\Khronos\\Vulkan\\Drivers", QSettings::NativeFormat);
    return !drivers.allKeys().isEmpty();
#else
    return false;
#endif
}

} // namespace

HardwareProbe &HardwareProbe::instance()
{
    static HardwareProbe probe;
    return probe;
}

bool HardwareProbe::hasNvidiaGpuQuick()
{
    // H-03: 同時檢查 ProviderName 與 DriverDesc，避免 OEM 字符串差異
    for (const GpuDevice &gpu : enumerateDisplayAdapters())
        if (gpu.vendor == QLatin1String("nvidia")) return true;
    // 回退：若註冊表未命中，嘗試用 nvidia-smi 探測（與 detectNow 相同的 locator）
    const QString smi = QStandardPaths::findExecutable("nvidia-smi");
    if (!smi.isEmpty()) return true;
#ifdef Q_OS_WIN
    QDir driverStore("C:/Windows/System32/DriverStore/FileRepository");
    if (driverStore.exists()) {
        const QStringList entries = driverStore.entryList({"nv_dispi.inf*"}, QDir::Dirs);
        for (const QString &e : entries) {
            if (QFileInfo::exists(QDir(driverStore).filePath(e + "/nvidia-smi.exe")))
                return true;
        }
    }
    QSettings svc("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\nvlddmkm", QSettings::NativeFormat);
    if (!svc.allKeys().isEmpty() || !svc.value("ImagePath").toString().isEmpty())
        return true;
#endif
    return false;
}

bool HardwareProbe::vulkanRuntimeQuick()
{
    return vulkanRuntimePresent();
}

void HardwareProbe::refresh(bool force)
{
    // Reject re-entrant refresh() calls without holding the mutex across the
    // early-return check, otherwise two consecutive calls would both pass the
    // m_thread == nullptr test and spawn two worker threads.
    QMutexLocker locker(&m_probeMutex);
    if (m_thread) return;
    if (m_info.probed && !force) {
        emit probeFinished(m_info);
        return;
    }
    m_probeState.storeRelease(1);
    m_thread = QThread::create([this] {
        m_pending = detectNow();
        // m_probeState flipped to 2 = cancel-requested means the caller asked
        // to bail out; we still let detectNow() finish but skip publishing.
    });
    connect(m_thread, &QThread::finished, this, [this] {
        QMutexLocker locker(&m_probeMutex);
        const bool cancelled = m_probeState.loadAcquire() == 2;
        if (!cancelled)
            m_info = m_pending;
        if (m_thread) m_thread->deleteLater();
        m_thread = nullptr;
        m_probeState.storeRelease(0);
        if (!cancelled)
            emit probeFinished(m_info);
    });
    m_thread->start();
}

HardwareInfo HardwareProbe::detectNow()
{
    HardwareInfo info;
    info.probed = true;
    info.osName = QSysInfo::prettyProductName();
    info.architecture = QSysInfo::currentCpuArchitecture();
    info.logicalCores = qMax(1, QThread::idealThreadCount());
    info.recommendedThreads = qBound(2, info.logicalCores - 2, 16);
    info.totalRamMb = totalPhysicalMemoryMb();

#ifdef Q_OS_WIN
    QSettings cpu("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                  QSettings::NativeFormat);
    info.cpuName = cpu.value("ProcessorNameString").toString().trimmed();
#endif
    if (info.cpuName.isEmpty()) info.cpuName = info.architecture;

    info.gpus = enumerateDisplayAdapters();

    // NVIDIA details are only trustworthy through nvidia-smi: it reports the
    // driver's maximum CUDA runtime, which decides whether a CUDA build can run.
    // H-01: nvidia-smi 不在 PATH（僅在 DriverStore），需用 findExecutable + 枚舉回退
    auto locateNvidiaSmi = []() -> QString {
        QString exe = QStandardPaths::findExecutable("nvidia-smi");
        if (!exe.isEmpty()) return exe;
#ifdef Q_OS_WIN
        QDir ds("C:/Windows/System32/DriverStore/FileRepository");
        if (ds.exists()) {
            const QStringList entries = ds.entryList({"nv_dispi.inf*"}, QDir::Dirs);
            QString best;
            qint64 bestTime = 0;
            for (const QString &e : entries) {
                const QString cand = QDir(ds).filePath(e + "/nvidia-smi.exe");
                if (QFileInfo::exists(cand)) {
                    const qint64 mtime = QFileInfo(cand).lastModified().toMSecsSinceEpoch();
                    if (mtime > bestTime) { bestTime = mtime; best = cand; }
                }
            }
            if (!best.isEmpty()) return best;
        }
        // 最後嘗試常見路徑
        const QString progFiles = QProcessEnvironment::systemEnvironment().value("ProgramFiles", "C:/Program Files");
        const QString cand2 = QDir(progFiles).filePath("NVIDIA Corporation/NVSMI/nvidia-smi.exe");
        if (QFileInfo::exists(cand2)) return cand2;
#endif
        return QStringLiteral("nvidia-smi");
    };
    const QString nvidiaSmiExe = locateNvidiaSmi();
    // nvidia-smi reports GPU name, driver version and VRAM in one query.
    const QString smiQuery = runCommand(nvidiaSmiExe,
        {"--query-gpu=name,driver_version,memory.total", "--format=csv,noheader,nounits"}, 12000);
    const bool smiOk = !smiQuery.isEmpty();
    if (smiOk) {
        const QString firstLine = smiQuery.split('\n', Qt::SkipEmptyParts).value(0);
        const QStringList fields = firstLine.split(',');
        if (fields.size() >= 3) {
            info.hasNvidia = true;
            info.nvidiaName = fields.at(0).trimmed();
            info.nvidiaDriver = fields.at(1).trimmed();
            info.nvidiaVramMb = fields.at(2).trimmed().toInt();
        }
    }
    if (info.hasNvidia) {
        const QString smiTable = runCommand(nvidiaSmiExe, {}, 12000);
        // Driver 600+ renamed the header to "CUDA UMD Version".
        const auto match = QRegularExpression("CUDA(?:\\s+UMD)?\\s+Version\\s*:\\s*(\\d+(?:\\.\\d+)?)",
                                              QRegularExpression::CaseInsensitiveOption).match(smiTable);
        if (match.hasMatch()) info.driverCudaVersion = match.captured(1);

        bool found = false;
        for (GpuDevice &gpu : info.gpus) {
            if (gpu.vendor != "nvidia") continue;
            found = true;
            if (gpu.vramMb <= 0) gpu.vramMb = info.nvidiaVramMb;
            if (!info.nvidiaName.isEmpty()) gpu.name = info.nvidiaName;
            if (!info.nvidiaDriver.isEmpty()) gpu.driverVersion = info.nvidiaDriver;
        }
        if (!found)
            info.gpus.prepend(GpuDevice{info.nvidiaName, "nvidia", info.nvidiaDriver, info.nvidiaVramMb});
    }

    info.vulkanRuntime = vulkanRuntimePresent();
    info.vulkanDrivers = vulkanDriversInstalled();

    return info;
}
