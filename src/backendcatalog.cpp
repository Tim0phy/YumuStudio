#include "backendcatalog.h"
#include "appconfig.h"
#include "hardwareprobe.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {

BackendSpec whisperVariant(const QString &variantId, const QString &name, const QString &description,
                           const QString &sizeHint, const QStringList &assetPatterns,
                           const QString &requirementNote)
{
    BackendSpec spec;
    spec.engineId = "whisper.cpp";
    spec.variantId = variantId;
    spec.name = name;
    spec.description = description;
    spec.sizeHint = sizeHint;
    spec.requirementNote = requirementNote;
    spec.releaseRepo = "ggml-org/whisper.cpp";
    spec.assetPatterns = assetPatterns;
    spec.executableNames = {"whisper-cli.exe", "main.exe"};
    return spec;
}

BackendSpec pythonVariant(const QString &engineId, const QString &variantId, const QString &name,
                          const QString &description, const QString &sizeHint,
                          const QStringList &packages, const QString &indexUrl,
                          const QString &requirementNote,
                          const QHash<QString, QString> &packageHashes = {})
{
    BackendSpec spec;
    spec.engineId = engineId;
    spec.variantId = variantId;
    spec.name = name;
    spec.description = description;
    spec.sizeHint = sizeHint;
    spec.requirementNote = requirementNote;
    spec.needsPython = true;
    spec.pipPackages = packages;
    spec.pipPackageHashes = packageHashes;
    spec.pipIndexUrl = indexUrl;
    return spec;
}

} // namespace

QList<BackendSpec> BackendCatalog::all()
{
    QList<BackendSpec> specs;

specs << whisperVariant("cpu",
        QObject::tr("whisper.cpp · CPU"),
        QObject::tr("任何電腦都能執行的官方預編譯後端，無需顯示卡。"),
        QObject::tr("約 15 MB"),
        {"^whisper-bin-x64\\.zip$", "^whisper-blas-bin-x64\\.zip$", "^(?!.*cublas).*bin-x64\\.zip$"},
        QObject::tr("無特別要求。"));

    specs << whisperVariant("vulkan",
        QObject::tr("whisper.cpp · Vulkan GPU"),
        QObject::tr("通用 GPU 加速，支援 AMD、Intel 及 NVIDIA。"),
        QObject::tr("約 20 MB"),
        // Upstream has used both "whisper-vulkan-bin-x64.zip" and
        // "whisper-vulkan-bin-amd64.zip" naming over time.
        {".*vulkan.*(x64|amd64).*\\.zip$"},
        QObject::tr("需要已安裝顯示卡驅動提供的 Vulkan 執行庫。"));

     specs << whisperVariant("cuda",
        QObject::tr("whisper.cpp · NVIDIA CUDA"),
        QObject::tr("NVIDIA 專用加速後端，官方預編譯包已內附 cuBLAS 執行庫。"),
        QObject::tr("約 250 MB"),
        {"cublas.*x64.*\\.zip$", "cuda.*x64.*\\.zip$", ".*whisper.*cuda.*\\.zip$"},
        QObject::tr("需要 NVIDIA 顯示卡，且驅動可執行 CUDA 12 或以上。"));

    specs << pythonVariant("faster-whisper", "cpu",
        QObject::tr("faster-whisper · CPU"),
        QObject::tr("CTranslate2 引擎，int8 量化下 CPU 表現優於原生 whisper.cpp。"),
        QObject::tr("約 250 MB"),
        {"faster-whisper"}, {},
        QObject::tr("會自動安裝私有 Python 執行環境，不影響系統 Python。"));

    specs << pythonVariant("faster-whisper", "cuda",
        QObject::tr("faster-whisper · NVIDIA CUDA"),
        QObject::tr("速度最快的本地轉寫組合，會一併安裝 cuBLAS 與 cuDNN。"),
        QObject::tr("約 2.5 GB"),
        {"faster-whisper", "nvidia-cublas-cu12", "nvidia-cudnn-cu12", "nvidia-cuda-runtime-cu12"}, {},
        QObject::tr("需要 NVIDIA 顯示卡及可執行 CUDA 12 的驅動。"));

    for (const QString &engineId : {"Parakeet", "Qwen3-ASR", "FireRedASR"}) {
        // SECURITY: wheel hashes must be updated after each wheel change.
        const QString cpuWheel = QStringLiteral("https://huggingface.co/csukuangfj2/sherpa-onnx-wheels/resolve/main/cpu/1.13.5/sherpa_onnx-1.13.5-cp312-cp312-win_amd64.whl");
        const QString cudaWheel = QStringLiteral("https://huggingface.co/csukuangfj2/sherpa-onnx-wheels/resolve/main/cuda/1.13.5/sherpa_onnx-1.13.5+cuda12.cudnn9-cp312-cp312-win_amd64.whl");
        specs << pythonVariant(engineId, "cpu",
            QObject::tr("%1 · ONNX Runtime").arg(engineId),
            QObject::tr("以 sherpa-onnx 執行的 ONNX 模型，安裝體積小、啟動快。"),
            QObject::tr("約 120 MB"),
            {cpuWheel}, {},
            QObject::tr("三個 ONNX 引擎共用同一份執行環境，安裝一次即可。"),
            {{cpuWheel, QStringLiteral("54ff685b14db65de0b84395af8eebbd52c4da9652f20e9bd404dd9994a6673db")}});
        specs << pythonVariant(engineId, "cuda",
            QObject::tr("%1 · ONNX Runtime CUDA").arg(engineId),
            QObject::tr("以 NVIDIA CUDA Execution Provider 加速 ONNX 轉寫。"),
            QObject::tr("約 500 MB"),
            {cudaWheel, "nvidia-cublas-cu12", "nvidia-cudnn-cu12", "nvidia-cuda-runtime-cu12", "nvidia-cufft-cu12"}, {},
            QObject::tr("需要 NVIDIA 顯示卡及可執行 CUDA 12 的驅動。"),
            {{cudaWheel, QStringLiteral("aa2f3cb6ec4920eab023c9eaebf9df5d31ff94c0cd8595a33f79705075c8bf02")}});
    }

    {
        const QString cpuWheel = QStringLiteral("https://huggingface.co/csukuangfj2/sherpa-onnx-wheels/resolve/main/cpu/1.13.5/sherpa_onnx-1.13.5-cp312-cp312-win_amd64.whl");
        const QString cudaWheel = QStringLiteral("https://huggingface.co/csukuangfj2/sherpa-onnx-wheels/resolve/main/cuda/1.13.5/sherpa_onnx-1.13.5+cuda12.cudnn9-cp312-cp312-win_amd64.whl");
        specs << pythonVariant("FunASR", "cpu",
            QObject::tr("FunASR · CPU (SeACo-Paraformer)"),
            QObject::tr("阿里 FunASR SeACo-Paraformer，支援熱詞偏置與大詞庫檢索，需要 PyTorch。"),
            QObject::tr("約 900 MB"),
            {"funasr", "torch", "torchaudio", cpuWheel}, "https://download.pytorch.org/whl/cpu",
            QObject::tr("安裝體積較大，建議預留 3 GB 磁碟空間。"),
            {{cpuWheel, QStringLiteral("54ff685b14db65de0b84395af8eebbd52c4da9652f20e9bd404dd9994a6673db")}});

        specs << pythonVariant("FunASR", "cuda",
            QObject::tr("FunASR · NVIDIA CUDA (SeACo-Paraformer)"),
            QObject::tr("以 CUDA 版 PyTorch 執行 SeACo-Paraformer，適合長影片批量與熱詞定制。"),
            QObject::tr("約 2.8 GB"),
            {"funasr", "torch", "torchaudio", "nvidia-cuda-runtime-cu12", "nvidia-cufft-cu12", cudaWheel}, "https://download.pytorch.org/whl/cu124",
            QObject::tr("需要 NVIDIA 顯示卡及可執行 CUDA 12 的驅動，建議預留 8 GB 磁碟空間。"),
            {{cudaWheel, QStringLiteral("aa2f3cb6ec4920eab023c9eaebf9df5d31ff94c0cd8595a33f79705075c8bf02")}});
    }

    specs << pythonVariant("Fun-ASR-Nano", "cpu",
        QObject::tr("Fun-ASR-Nano · CPU"),
        QObject::tr("FunAudioLLM Fun-ASR-Nano-2512（800M LLM），支援熱詞列表與 ITN，中文/英文/日文及方言。"),
        QObject::tr("約 1.1 GB"),
        {"funasr", "torch", "torchaudio"}, "https://download.pytorch.org/whl/cpu",
        QObject::tr("會自動安裝私有 Python 執行環境，建議預留 4 GB 磁碟空間。"));

    specs << pythonVariant("Fun-ASR-Nano", "cuda",
        QObject::tr("Fun-ASR-Nano · NVIDIA CUDA"),
        QObject::tr("以 CUDA 版 PyTorch 執行 Fun-ASR-Nano-2512，批量與串流皆支援熱詞。"),
        QObject::tr("約 3.0 GB"),
        {"funasr", "torch", "torchaudio", "nvidia-cuda-runtime-cu12", "nvidia-cufft-cu12"}, "https://download.pytorch.org/whl/cu124",
        QObject::tr("需要 NVIDIA 顯示卡及可執行 CUDA 12 的驅動，建議預留 8 GB 磁碟空間。"));

    return specs;
}

QList<BackendSpec> BackendCatalog::forEngine(const QString &engineId)
{
    QList<BackendSpec> specs;
    for (const BackendSpec &spec : all())
        if (spec.engineId.compare(engineId, Qt::CaseInsensitive) == 0) specs << spec;
    return specs;
}

QStringList BackendCatalog::engineIds()
{
    QStringList ids;
    for (const BackendSpec &spec : all())
        if (!ids.contains(spec.engineId)) ids << spec.engineId;
    return ids;
}

BackendSpec BackendCatalog::find(const QString &engineId, const QString &variantId, bool *found)
{
    for (const BackendSpec &spec : all()) {
        if (spec.engineId.compare(engineId, Qt::CaseInsensitive) == 0
            && spec.variantId.compare(variantId, Qt::CaseInsensitive) == 0) {
            if (found) *found = true;
            return spec;
        }
    }
    if (found) *found = false;
    return {};
}

QString BackendCatalog::variantLabel(const QString &variantId)
{
    if (variantId == "cuda") return QObject::tr("NVIDIA CUDA");
    if (variantId == "vulkan") return QObject::tr("Vulkan GPU");
    if (variantId == "cpu") return QObject::tr("CPU");
    return variantId;
}

QString BackendCatalog::installRoot()
{
    const QString configured = AppConfig::instance().paths.engineRoot.trimmed();
    if (!configured.isEmpty()) return configured;
    if (AppConfig::isPortableMode())
        return QDir(AppConfig::portableRoot()).filePath("engines");
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath("engines");
}

QString BackendCatalog::variantDir(const QString &engineId, const QString &variantId)
{
    const QString folder = QString(engineId).replace(' ', '-').toLower();
    return QDir(installRoot()).filePath(folder + "/" + variantId.toLower());
}

QString BackendCatalog::manifestPath(const QString &engineId, const QString &variantId)
{
    return QDir(variantDir(engineId, variantId)).filePath("install.json");
}

QString BackendCatalog::pythonRoot()
{
    return QDir(installRoot()).filePath("python");
}

QString BackendCatalog::pythonExecutable()
{
    const QString exe = QDir(pythonRoot()).filePath("python.exe");
    return QFileInfo::exists(exe) ? exe : QString();
}

QString BackendCatalog::pythonSitePackages()
{
    return QDir(pythonRoot()).filePath("Lib/site-packages");
}

bool BackendCatalog::isPythonRuntimeReady()
{
    return !pythonExecutable().isEmpty() && QFileInfo::exists(pythonSitePackages());
}

bool BackendCatalog::pythonHasPip(const QString &pythonExe)
{
    const QString exe = pythonExe.isEmpty() ? pythonExecutable() : pythonExe;
    if (exe.isEmpty()) return false;

    // Probed by running the interpreter rather than by testing for a directory:
    // pip's location depends on how it was installed, so the only trustworthy
    // check is whether the interpreter can actually import it. python.exe is a
    // local exe; it starts and exits in well under a second, and the bounds
    // below match the sync wait the installer already uses elsewhere.
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(exe, {QStringLiteral("-c"), QStringLiteral("import pip")});
    if (!probe.waitForStarted(3000)) return false;
    if (!probe.waitForFinished(8000)) {
        probe.kill();
        probe.waitForFinished(2000);
        return false;
    }
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

QJsonObject BackendCatalog::readManifest(const QString &engineId, const QString &variantId)
{
    QFile file(manifestPath(engineId, variantId));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

bool BackendCatalog::writeManifest(const QString &engineId, const QString &variantId,
                                   const QJsonObject &manifest)
{
    const QString path = manifestPath(engineId, variantId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    return true;
}

bool BackendCatalog::isInstalled(const QString &engineId, const QString &variantId)
{
    const QJsonObject manifest = readManifest(engineId, variantId);
    if (manifest.isEmpty()) return false;
    bool found = false;
    const BackendSpec spec = find(engineId, variantId, &found);
    if (found && spec.needsPython)
        return isPythonRuntimeReady() && manifest.value("complete").toBool();
    return !variantExecutable(engineId, variantId).isEmpty();
}

QStringList BackendCatalog::installedVariants(const QString &engineId)
{
    QStringList variants;
    for (const BackendSpec &spec : forEngine(engineId))
        if (isInstalled(spec.engineId, spec.variantId)) variants << spec.variantId;
    return variants;
}

bool BackendCatalog::removeVariant(const QString &engineId, const QString &variantId)
{
    const QString dir = variantDir(engineId, variantId);
    if (!QFileInfo::exists(dir)) return true;
    return QDir(dir).removeRecursively();
}

bool BackendCatalog::activateVariant(const QString &engineId, const QString &variantId){
    bool found = false;
    const BackendSpec spec = find(engineId, variantId, &found);
    if (!found || !isInstalled(engineId, variantId)) return false;
    AppConfig &config = AppConfig::instance();
    config.paths.engineRoot = installRoot();
    config.whisper.engine = engineId;
    config.whisper.computeDevice = variantId;
    config.whisper.precision = QStringLiteral("auto");
    if (spec.needsPython) {
        const QString python = pythonExecutable();
        if (python.isEmpty()) return false;
        config.paths.runnerPath = python; config.whisper.runnerPath = python;
    } else {
        const QString executable = variantExecutable(engineId, variantId);
        if (executable.isEmpty()) return false;
        config.paths.whisperCliPath = executable; config.whisper.cliPath = executable;
    }
    config.save(); return true;
}

QString BackendCatalog::installedVersion(const QString &engineId, const QString &variantId)
{
    return readManifest(engineId, variantId).value("version").toString();
}

QString BackendCatalog::variantExecutable(const QString &engineId, const QString &variantId)
{
    const QString dir = variantDir(engineId, variantId);
    if (!QFileInfo::exists(dir)) return {};

    const QJsonObject manifest = readManifest(engineId, variantId);
    const QString recorded = manifest.value("executable").toString();
    if (!recorded.isEmpty()) {
        const QString path = QDir(dir).filePath(recorded);
        if (QFileInfo::exists(path)) return QDir::toNativeSeparators(path);
    }

    bool found = false;
    const BackendSpec spec = find(engineId, variantId, &found);
    QStringList names = found ? spec.executableNames : QStringList{"whisper-cli.exe"};
    for (const QString &name : names) {
        QDirIterator it(dir, {name}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) return QDir::toNativeSeparators(it.next());
    }
    return {};
}

QStringList BackendCatalog::variantsByPreference(const QString &computeDevice)
{
    const QString device = computeDevice.trimmed().toLower();
    if (device == "cuda") return {"cuda", "vulkan", "cpu"};
    if (device == "vulkan") return {"vulkan", "cuda", "cpu"};
    if (device == "cpu") return {"cpu", "cuda", "vulkan"};
    // auto: only lead with the CUDA build when an NVIDIA GPU is actually
    // present; otherwise the CUDA whisper-cli would crash or crawl on a
    // machine that can never use it while a working Vulkan/CPU build sits
    // installed next to it.
    if (HardwareProbe::hasNvidiaGpuQuick()) return {"cuda", "vulkan", "cpu"};
    return {"vulkan", "cpu", "cuda"};
}

QString BackendCatalog::resolveWhisperCli(const QString &computeDevice, QString *usedVariant)
{
    for (const QString &variant : variantsByPreference(computeDevice)) {
        const QString exe = variantExecutable("whisper.cpp", variant);
        if (!exe.isEmpty()) {
            if (usedVariant) *usedVariant = variant;
            return exe;
        }
    }
    if (usedVariant) usedVariant->clear();
    return {};
}

QStringList BackendCatalog::extraPathEntries(const QString &engineId, const QString &variantId)
{
    QStringList entries;
    const QString exe = variantExecutable(engineId, variantId);
    if (!exe.isEmpty()) entries << QFileInfo(exe).absolutePath();

    bool found = false;
    BackendSpec spec = find(engineId, variantId, &found);
    // "auto" never matches a concrete variant. Fall back to the most
    // CUDA-capable installed variant so an "auto→cuda" request still gets
    // the pip-installed CUDA library folders on PATH. Without this, an
    // "auto" request resolves to CUDA inside the runner but cublas64_12.dll
    // is not reachable and CTranslate2 fails (H-08).
    if (!found) {
        const QStringList preference = variantsByPreference(QStringLiteral("cuda"));
        const QStringList installed = installedVariants(engineId);
        // Prefer cuda > vulkan > cpu per variantsByPreference
        for (const QString &preferred : preference) {
            if (!installed.contains(preferred)) continue;
            const BackendSpec candidate = find(engineId, preferred, &found);
            if (found && candidate.needsPython) { spec = candidate; found = true; break; }
        }
        // Fallback: first installed python variant if preference loop missed
        if (!found) {
            for (const QString &installedVariant : installed) {
                const BackendSpec candidate = find(engineId, installedVariant, &found);
                if (found && candidate.needsPython) { spec = candidate; found = true; break; }
            }
        }
    }
    if (found && spec.needsPython && isPythonRuntimeReady()) {
        entries << QDir::toNativeSeparators(pythonRoot());
        entries << QDir::toNativeSeparators(QDir(pythonRoot()).filePath("Scripts"));
        // CTranslate2 and PyTorch load the pip-installed CUDA libraries through
        // PATH.  Only add well-known NVIDIA package subdirectories (avoid
        // loading arbitrary DLLs from an unknown nvidia/*/bin folder).
        const QString nvidiaRoot = QDir(pythonSitePackages()).filePath("nvidia");
        const QStringList knownNvidiaPackages = {
            QStringLiteral("cublas"), QStringLiteral("cudnn"),
            QStringLiteral("cuda_runtime"), QStringLiteral("cufft"),
            QStringLiteral("curand"), QStringLiteral("cusparse"),
            QStringLiteral("cusolver"), QStringLiteral("nccl"),
            QStringLiteral("nvjitlink"), QStringLiteral("nvtx")
        };
        if (QFileInfo::exists(nvidiaRoot)) {
            for (const QString &pkg : knownNvidiaPackages) {
                for (const QString &sub : {QStringLiteral("bin"), QStringLiteral("lib")}) {
                    const QString path = QDir(nvidiaRoot).filePath(pkg + QLatin1String("/") + sub);
                    if (QFileInfo::exists(path))
                        entries << QDir::toNativeSeparators(path);
                }
            }
        }
    }
    entries.removeDuplicates();
    return entries;
}

void BackendCatalog::applyProcessEnvironment(QProcess &process, const QString &engineId,
                                             const QString &variantId)
{
    const QStringList extra = extraPathEntries(engineId, variantId);
    if (extra.isEmpty()) return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString existing = env.value("PATH");
    // Append engine-specific directories to PATH so system directories keep
    // precedence, reducing the impact of a malicious package planted in the
    // engine's private environment.
    env.insert("PATH", existing + (existing.isEmpty() ? QString() : QLatin1String(";")) + extra.join(QLatin1String(";")));
    process.setProcessEnvironment(env);
}
