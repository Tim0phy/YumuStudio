#include "localmodelmanager.h"
#include "archiveutil.h"
#include "appconfig.h"
#include "securitypolicy.h"
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTimer>
#include <QElapsedTimer>


// Matches a Windows drive-letter path prefix (e.g. "C:\\"). Compiled once instead of
// per-file at every download step.
static const QRegularExpression kDriveLetterRe(QStringLiteral("^[A-Za-z]:"));

static LocalModelFile hf(const QString &repo, const QString &file, const QString &sha256 = QString()) {
    return {file, QUrl(QString("https://huggingface.co/%1/resolve/main/%2?download=true").arg(repo, file)), sha256};
}

static bool isSafeArchiveMember(const QString &member) {
    return ArchiveUtil::isSafeMemberName(member);
}

static bool validateArchive(const QString &tarPath, const QString &archive, QString &error) {
    QProcess list;
    list.start(tarPath, {"-tf", archive});
    if (!list.waitForFinished(60000)) {
        list.kill();
        list.waitForFinished(5000);
        error = QObject::tr("Timed out while checking the model archive.");
        return false;
    }
    if (list.exitCode() != 0) {
        error = QObject::tr("Could not read the model archive: %1")
            .arg(QString::fromUtf8(list.readAll()).left(400));
        return false;
    }
    for (const QString &member : QString::fromUtf8(list.readAll()).split('\n', Qt::SkipEmptyParts)) {
        if (!isSafeArchiveMember(member)) {
            error = QObject::tr("The model archive contains an unsafe path: %1").arg(member.trimmed());
            return false;
        }
    }
    return true;
}

LocalModelManager::LocalModelManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {
    // Ensure the application-wide proxy settings are respected.  A fresh
    // QNetworkAccessManager inherits the application proxy by default, but
    // explicit application-proxy mode makes the behavior robust against Qt
    // defaults changing or the dialog being constructed before the proxy is set.
    m_network->setProxy(QNetworkProxy::applicationProxy());
    // Stall watchdog: abort a download only when no bytes have arrived for 15
    // minutes (slow links used to be killed by a fixed 30-minute wall-clock
    // cap), or after an absolute 6-hour ceiling for the whole model job.
    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(60 * 1000);
    connect(m_stallTimer, &QTimer::timeout, this, [this] {
        if (!m_reply || !m_reply->isRunning()) return;
        const bool stalled = m_lastActivityBytes >= 0
            && m_activityTimer.isValid() && m_activityTimer.elapsed() > 15 * 60 * 1000;
        const bool expired = m_overallTimer.isValid()
            && m_overallTimer.elapsed() > 6 * 60 * 60 * 1000;
        if (stalled || expired) {
            m_userCancelled = false;
            m_reply->abort();
        }
    });
}

void LocalModelManager::startStallWatchdog() {
    m_lastActivityBytes = -1;
    m_activityTimer.start();
    m_overallTimer.start();
    m_stallTimer->start();
}

void LocalModelManager::stopStallWatchdog() {
    m_stallTimer->stop();
}

QList<LocalModelSpec> LocalModelManager::catalog() {
    const auto cw = [](const QString &id, const QString &name, const QString &desc, const QString &file,
                       const QString &size, int speed, int accuracy, const QStringList &tags = QStringList()) {
        LocalModelSpec spec{"whisper.cpp", id, name, desc, {}, file, {file}, false};
        spec.category = accuracy >= 5 ? "高精度檔" : (accuracy >= 4 ? "均衡檔" : "快速檔");
        spec.sizeLabel = size;
        spec.speedScore = speed;
        spec.accuracyScore = accuracy;
        spec.tags = tags;
        return spec;
    };
    QList<LocalModelSpec> result = {
        cw("tiny", "tiny", "最快出結果，準確度一般", "ggml-tiny.bin", "~75 MB", 5, 2),
        cw("base", "base", "比 tiny 準確一點，依然很快", "ggml-base.bin", "~145 MB", 4, 3),
        cw("small", "small", "速度與準確度的入門平衡點", "ggml-small.bin", "~466 MB", 3, 4),
        cw("medium", "medium", "大多數人的最佳選擇，中文效果好", "ggml-medium.bin", "~1.5 GB", 2, 5, {"推薦"}),
        cw("large-v3-turbo", "large-v3-turbo", "精度接近頂級，速度更快", "ggml-large-v3-turbo.bin", "~1.6 GB", 3, 5, {"推薦"}),
        cw("large-v3", "large-v3", "頂級精度，速度最慢", "ggml-large-v3.bin", "~3 GB", 1, 5)
    };
    for (auto &spec : result)
        spec.files = {hf("ggerganov/whisper.cpp", spec.archiveName)};

    const auto fw = [](const QString &id, const QString &name, const QString &desc, const QString &repo,
                       const QString &size, int speed, int accuracy, const QStringList &tags = QStringList(),
                       const QString &vocabFile = "vocabulary.json", bool hasPreprocessor = true) {
        QList<LocalModelFile> urls;
        urls << hf(repo, "config.json") << hf(repo, "model.bin")
             << hf(repo, "tokenizer.json") << hf(repo, vocabFile);
        if (hasPreprocessor) urls << hf(repo, "preprocessor_config.json");
        const QStringList requiredFiles = {"config.json", "model.bin", "tokenizer.json", vocabFile};
        LocalModelSpec spec{"faster-whisper", id, name, desc, urls, {}, requiredFiles, false};
        spec.category = accuracy >= 5 ? "高精度檔" : (accuracy >= 4 ? "均衡檔" : "快速檔");
        spec.sizeLabel = size;
        spec.speedScore = speed;
        spec.accuracyScore = accuracy;
        spec.tags = tags;
        return spec;
    };
    result << fw("tiny", "tiny", "最快出結果，準確度一般", "Systran/faster-whisper-tiny", "~75 MB", 5, 2, {}, "vocabulary.txt", false);
    result << fw("base", "base", "比 tiny 準一些，依然很快", "Systran/faster-whisper-base", "~145 MB", 4, 3, {}, "vocabulary.txt", false);
    result << fw("distil-small.en", "distil-small.en", "蒸餾版 small，僅英文", "Systran/faster-distil-whisper-small.en", "~166 MB", 5, 4, {"Distil", "僅英文"});
    result << fw("small", "small", "速度與準確度的入門平衡點", "Systran/faster-whisper-small", "~466 MB", 3, 4, {}, "vocabulary.txt", false);
    result << fw("medium", "medium", "大多數人的最佳選擇，中文效果好", "Systran/faster-whisper-medium", "~1.5 GB", 2, 5, {}, "vocabulary.txt", false);
    result << fw("distil-medium.en", "distil-medium.en", "蒸餾版 medium，僅英文", "Systran/faster-distil-whisper-medium.en", "~394 MB", 4, 4, {"Distil", "僅英文"});
    result << fw("large-v3-turbo", "large-v3-turbo", "精度接近頂級，速度不錯", "deepdml/faster-whisper-large-v3-turbo-ct2", "~1.6 GB", 3, 5, {"推薦"});
    result << fw("large-v3", "large-v3", "頂級精度，速度最慢", "Systran/faster-whisper-large-v3", "~3 GB", 1, 5);
    result << fw("distil-large-v3", "distil-large-v3", "蒸餾版 large-v3，體積更小", "Systran/faster-distil-whisper-large-v3", "~756 MB", 4, 5, {"Distil"});

    LocalModelSpec sense{"FunASR", "sensevoice-small", "SenseVoice Small", "中文及多語言識別",
        {hf("csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17", "model.int8.onnx"),
         hf("csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17", "tokens.txt")}, {},
        {"model.int8.onnx", "tokens.txt"}, false};
    sense.category = "均衡檔"; sense.sizeLabel = "~230 MB"; sense.speedScore = 4; sense.accuracyScore = 4;
    result << sense;
    LocalModelSpec funasrFolder{"FunASR", "paraformer-zh", "Paraformer 中文", "中文識別模型，支援一鍵下載與資料夾匯入",
        {hf("funasr/paraformer-zh", "model.pt"),
         hf("funasr/paraformer-zh", "config.yaml"),
         hf("funasr/paraformer-zh", "configuration.json"),
         hf("funasr/paraformer-zh", "tokens.json"),
         hf("funasr/paraformer-zh", "seg_dict"),
         hf("funasr/paraformer-zh", "am.mvn")}, {}, {"model.pt"}, false};
    funasrFolder.category = "高精度檔"; funasrFolder.sizeLabel = "~880 MB"; funasrFolder.speedScore = 3; funasrFolder.accuracyScore = 5; funasrFolder.tags = {};
    result << funasrFolder;
    LocalModelSpec seaco{"FunASR", "seaco-paraformer-large", "SeACo-Paraformer Large", "熱詞定制中英文模型，支援熱詞偏置與大詞庫檢索",
        {{"model.pt", QUrl("https://www.modelscope.cn/models/iic/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-pytorch/resolve/master/model.pt")},
         {"configuration.json", QUrl("https://www.modelscope.cn/models/iic/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-pytorch/resolve/master/configuration.json")},
         {"config.yaml", QUrl("https://www.modelscope.cn/models/iic/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-pytorch/resolve/master/config.yaml")},
         {"tokens.txt", QUrl("https://www.modelscope.cn/models/iic/speech_seaco_paraformer_large_asr_nat-zh-cn-16k-common-vocab8404-pytorch/resolve/master/tokens.txt")}}, {}, {"model.pt", "tokens.txt"}, false};
    seaco.category = "高精度檔"; seaco.sizeLabel = "~1.1 GB"; seaco.speedScore = 3; seaco.accuracyScore = 5; seaco.tags = {"推薦", "熱詞"};
    result << seaco;
    result << LocalModelSpec{"Fun-ASR-Nano", "fun-asr-nano-2512", "Fun-ASR-Nano-2512", "800M LLM 熱詞定制模型，中文/英文/日文及方言（支援一鍵下載與資料夾匯入）",
        {hf("FunAudioLLM/Fun-ASR-Nano-2512", "model.pt"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "config.yaml"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "configuration.json"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "multilingual.tiktoken"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "Qwen3-0.6B/config.json"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "Qwen3-0.6B/tokenizer.json"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "Qwen3-0.6B/tokenizer_config.json"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "Qwen3-0.6B/vocab.json"),
         hf("FunAudioLLM/Fun-ASR-Nano-2512", "Qwen3-0.6B/merges.txt"),
         {"tokenizer.json", QUrl("https://huggingface.co/FunAudioLLM/Fun-ASR-Nano-2512/resolve/main/Qwen3-0.6B/tokenizer.json?download=true")}}, {}, {"model.pt"}, false};
    result.last().category = "高精度檔"; result.last().sizeLabel = "~2.0 GB"; result.last().speedScore = 3; result.last().accuracyScore = 5; result.last().tags = {"推薦", "熱詞"};
    result << LocalModelSpec{"Fun-ASR-Nano", "fun-asr-nano-mlt-2512", "Fun-ASR-Nano-MLT-2512", "31 語言多語言熱詞模型（支援一鍵下載與資料夾匯入）",
        {hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "model.pt"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "config.yaml"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "configuration.json"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "multilingual.tiktoken"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "Qwen3-0.6B/config.json"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "Qwen3-0.6B/tokenizer.json"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "Qwen3-0.6B/tokenizer_config.json"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "Qwen3-0.6B/vocab.json"),
         hf("FunAudioLLM/Fun-ASR-MLT-Nano-2512", "Qwen3-0.6B/merges.txt"),
         {"tokenizer.json", QUrl("https://huggingface.co/FunAudioLLM/Fun-ASR-MLT-Nano-2512/resolve/main/Qwen3-0.6B/tokenizer.json?download=true")}}, {}, {"model.pt"}, false};
    result.last().category = "高精度檔"; result.last().sizeLabel = "~2.0 GB"; result.last().speedScore = 2; result.last().accuracyScore = 5; result.last().tags = {"熱詞"};

    result << LocalModelSpec{"Qwen3-ASR", "qwen3-asr-0.6b", "Qwen3-ASR 0.6B",
        "多語言本地 ASR 模型",
        {
            {"conv_frontend.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_0.6B/conv_frontend.onnx")},
            {"encoder.int8.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_0.6B/encoder.int8.onnx")},
            {"decoder.int8.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_0.6B/decoder.int8.onnx")},
            {"tokenizer/vocab.json", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/vocab.json")},
            {"tokenizer/merges.txt", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/merges.txt")},
            {"tokenizer/tokenizer_config.json", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/tokenizer_config.json")}
        }, {}, {"conv_frontend.onnx", "encoder.int8.onnx", "decoder.int8.onnx", "tokenizer/vocab.json", "tokenizer/merges.txt", "tokenizer/tokenizer_config.json"}, false};
    result.last().category = "均衡檔"; result.last().sizeLabel = "~1.2 GB"; result.last().speedScore = 3; result.last().accuracyScore = 5;
    LocalModelSpec qwenLarge{"Qwen3-ASR", "qwen3-asr-1.7b", "Qwen3-ASR 1.7B", "更高精度的多語言模型（支援一鍵下載與資料夾匯入）",
        {
            {"conv_frontend.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_1.7B/conv_frontend.onnx")},
            {"encoder.int8.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_1.7B/encoder.int8.onnx")},
            {"decoder.int8.onnx", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/model_1.7B/decoder.int8.onnx")},
            {"tokenizer/vocab.json", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/vocab.json")},
            {"tokenizer/merges.txt", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/merges.txt")},
            {"tokenizer/tokenizer_config.json", QUrl("https://www.modelscope.cn/models/zengshuishui/Qwen3-ASR-onnx/resolve/master/tokenizer/tokenizer_config.json")}
        }, {}, {"conv_frontend.onnx", "encoder.int8.onnx", "decoder.int8.onnx",
                 "tokenizer/vocab.json", "tokenizer/merges.txt", "tokenizer/tokenizer_config.json"}, false};
    qwenLarge.category = "高精度檔"; qwenLarge.sizeLabel = "~3.2 GB"; qwenLarge.speedScore = 2; qwenLarge.accuracyScore = 5; qwenLarge.tags = {};
    result << qwenLarge;
    result << LocalModelSpec{"FireRedASR", "fire-red-asr-large-zh-en", "FireRedASR Large",
        "Chinese and English local ASR model",
        {hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "encoder.int8.onnx"),
         hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "decoder.int8.onnx"),
         hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "tokens.txt")}, {},
        {"encoder.int8.onnx", "decoder.int8.onnx", "tokens.txt"}, false};
    result.last().category = "高精度檔"; result.last().sizeLabel = "~1.1 GB"; result.last().speedScore = 2; result.last().accuracyScore = 5;
    LocalModelSpec fireSmall{"FireRedASR", "fire-red-asr-small-zh-en", "FireRedASR Small", "較快的中英語音識別模型（支援一鍵下載與資料夾匯入）",
        {hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "encoder.int8.onnx"),
         hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "decoder.int8.onnx"),
         hf("csukuangfj/sherpa-onnx-fire-red-asr-large-zh_en-2025-02-16", "tokens.txt")}, {}, {"encoder.int8.onnx", "decoder.int8.onnx", "tokens.txt"}, false};
    fireSmall.category = "均衡檔"; fireSmall.sizeLabel = "~1.1 GB"; fireSmall.speedScore = 4; fireSmall.accuracyScore = 4; fireSmall.tags = {};
    result << fireSmall;
    result << LocalModelSpec{"Parakeet", "parakeet-tdt-0.6b-v3", "NVIDIA Parakeet TDT",
        "Fast multilingual local model", {}, "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2",
        {"encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"}, true};
    result.last().files = {{result.last().archiveName, QUrl("https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2")}};
    result.last().category = "均衡檔"; result.last().sizeLabel = "~600 MB"; result.last().speedScore = 5; result.last().accuracyScore = 4;
    LocalModelSpec parakeetLarge{"Parakeet", "parakeet-tdt-1.1b", "NVIDIA Parakeet TDT 1.1B", "更高精度版本（支援一鍵下載與資料夾匯入）",
        {}, "sherpa-onnx-nemo-parakeet-tdt-1.1b-int8.tar.bz2",
        {"encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"}, true};
    parakeetLarge.files = {{parakeetLarge.archiveName, QUrl("https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-1.1b-int8.tar.bz2")}};
    parakeetLarge.category = "高精度檔"; parakeetLarge.sizeLabel = "~1.2 GB"; parakeetLarge.speedScore = 3; parakeetLarge.accuracyScore = 5; parakeetLarge.tags = {};
    result << parakeetLarge;
    if (AppConfig::instance().system.language == "en") {
        for (auto &spec : result) {
            spec.category = spec.accuracyScore >= 5 ? "High accuracy" : (spec.accuracyScore >= 4 ? "Balanced" : "Fast");
            for (auto &tag : spec.tags) {
                if (tag == "推薦") tag = "Recommended";
                else if (tag == "資料夾匯入") tag = "Folder import";
                else if (tag == "僅英文") tag = "English only";
            }
            if (spec.sizeLabel == "資料夾匯入") spec.sizeLabel = "Folder import";
            if (spec.engine == "FunASR" && spec.id == "paraformer-zh") spec.name = "Paraformer Chinese";
            if (spec.engine == "whisper.cpp" || spec.engine == "faster-whisper") {
                if (spec.id == "tiny") spec.description = "Fastest results, general accuracy";
                else if (spec.id == "base") spec.description = "More accurate than tiny, still very fast";
                else if (spec.id == "small") spec.description = "A balanced starting point for speed and accuracy";
                else if (spec.id == "medium") spec.description = "A popular choice with good Chinese recognition";
                else if (spec.id == "large-v3-turbo") spec.description = "Near-top accuracy with faster processing";
                else if (spec.id == "large-v3") spec.description = "Top accuracy with the slowest processing";
                else if (spec.id == "distil-small.en") spec.description = "Distilled small model for English only";
                else if (spec.id == "distil-medium.en") spec.description = "Distilled medium model for English only";
                else if (spec.id == "distil-large-v3") spec.description = "Distilled large-v3 model with a smaller footprint";
            } else if (spec.engine == "FunASR") {
                if (spec.id == "sensevoice-small") spec.description = "Chinese and multilingual recognition";
                else if (spec.id == "paraformer-zh") spec.description = "Chinese recognition model with one-click download and folder import";
                else if (spec.id == "seaco-paraformer-large") spec.description = "Hotword-customizable Chinese/English model with one-click download and folder import";
                else spec.description = "Chinese model with one-click download and folder import";
            } else if (spec.engine == "Fun-ASR-Nano") {
                spec.description = spec.id == "fun-asr-nano-2512"
                    ? "800M LLM hotword model (Chinese/English/Japanese dialects) with one-click download and folder import"
                    : "31-language multilingual hotword model with one-click download and folder import";
            } else if (spec.engine == "Qwen3-ASR") {
                spec.description = spec.id == "qwen3-asr-1.7b"
                    ? "Higher-accuracy multilingual model with one-click download and folder import"
                    : "Multilingual local ASR model";
            } else if (spec.engine == "FireRedASR") {
                spec.description = spec.id == "fire-red-asr-small-zh-en"
                    ? "Fast Chinese and English model with one-click download and folder import"
                    : "Chinese and English local ASR model";
            } else if (spec.engine == "Parakeet") {
                spec.description = spec.id == "parakeet-tdt-1.1b"
                    ? "Higher-accuracy version with one-click download and folder import"
                    : "Fast multilingual local model";
            }
        }
    }
    return result;
}

QList<LocalEngineSpec> LocalModelManager::engines() {
    QList<LocalEngineSpec> result = {
        {"whisper.cpp", "whisper.cpp（內置）", "原生 C++ 引擎，支援 GGML 模型與 GPU 加速", "內置", QUrl("https://api.github.com/repos/ggml-org/whisper.cpp/releases/latest"), QUrl("https://github.com/ggml-org/whisper.cpp/releases")},
        {"faster-whisper", "faster-whisper", "基於 CTranslate2，速度快，支援 NVIDIA CUDA", "未安裝", QUrl("https://api.github.com/repos/SYSTRAN/faster-whisper/releases/latest"), QUrl("https://github.com/SYSTRAN/faster-whisper/releases")},
        {"FunASR", "FunASR", "中文與多語言本地語音識別引擎（SeACo-Paraformer 熱詞）", "未安裝", QUrl("https://api.github.com/repos/modelscope/FunASR/releases/latest"), QUrl("https://github.com/modelscope/FunASR/releases")},
        {"Fun-ASR-Nano", "Fun-ASR-Nano", "FunAudioLLM 800M LLM 熱詞定制引擎", "未安裝", QUrl("https://api.github.com/repos/FunAudioLLM/Fun-ASR/releases/latest"), QUrl("https://github.com/FunAudioLLM/Fun-ASR/releases")},
        {"Qwen3-ASR", "Qwen3-ASR", "多語言本地 ASR 模型，支援資料夾匯入", "未安裝", QUrl("https://api.github.com/repos/QwenLM/Qwen3-ASR/releases/latest"), QUrl("https://github.com/QwenLM/Qwen3-ASR/releases")},
        {"FireRedASR", "FireRedASR", "中文及英文語音識別模型", "未安裝", QUrl("https://api.github.com/repos/FireRedTeam/FireRedASR/releases/latest"), QUrl("https://github.com/FireRedTeam/FireRedASR/releases")},
        {"Parakeet", "Parakeet", "NVIDIA 多語言高效能語音識別模型", "未安裝", QUrl("https://api.github.com/repos/k2-fsa/sherpa-onnx/releases/latest"), QUrl("https://github.com/k2-fsa/sherpa-onnx/releases")}
    };
    if (AppConfig::instance().system.language == "en") {
        result[0].name = "whisper.cpp (built-in)";
        result[0].description = "Native C++ engine with GGML models and GPU acceleration";
        result[0].installedVersion = "Built-in";
        result[1].description = "CTranslate2-based engine with fast processing and NVIDIA CUDA support";
        result[1].installedVersion = "Not installed";
        result[2].description = "Chinese and multilingual local speech recognition engine (SeACo-Paraformer hotwords)";
        result[2].installedVersion = "Not installed";
        result[3].description = "FunAudioLLM 800M LLM hotword customization engine";
        result[3].installedVersion = "Not installed";
        result[4].description = "Multilingual local ASR models with folder import support";
        result[4].installedVersion = "Not installed";
        result[5].description = "Chinese and English speech recognition models";
        result[5].installedVersion = "Not installed";
        result[6].description = "High-performance NVIDIA multilingual speech recognition models";
        result[6].installedVersion = "Not installed";
    }
    return result;
}

QStringList LocalModelManager::supportedLanguageCodes(const QString &engine, const QString &modelId) {
    const QStringList whisperLanguages = {
        "zh", "en", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl", "ca", "nl", "ar", "sv", "it",
        "id", "hi", "fi", "vi", "he", "uk", "el", "ms", "cs", "ro", "da", "hu", "ta", "no", "th", "ur",
        "hr", "bg", "lt", "la", "mi", "ml", "cy", "sk", "te", "fa", "lv", "bn", "sr", "az", "sl", "kn",
        "et", "mk", "br", "eu", "is", "hy", "ne", "mn", "bs", "kk", "sq", "sw", "gl", "mr", "pa", "si",
        "km", "sn", "yo", "so", "af", "oc", "ka", "be", "tg", "sd", "gu", "am", "yi", "lo", "uz", "fo",
        "ht", "ps", "tk", "nn", "mt", "sa", "lb", "my", "bo", "tl", "mg", "as", "tt", "haw", "ln", "ha",
        "ba", "jw", "su", "yue"
    };
    const QString normalizedEngine = engine.trimmed().toLower();
    const QString normalizedModel = modelId.trimmed().toLower();

    if (normalizedEngine == "faster-whisper" && normalizedModel.endsWith(".en"))
        return {"en"};
    if (normalizedEngine == "funasr") {
        if (normalizedModel == "sensevoice-small") return {"zh", "yue", "en", "ja", "ko"};
        if (normalizedModel == "seaco-paraformer-large") return {"zh", "en"};
        return {"zh"};
    }
    if (normalizedEngine == "fun-asr-nano") {
        if (normalizedModel == "fun-asr-nano-mlt-2512") return {"zh", "en", "yue", "ja", "ko", "vi", "id", "th", "ms", "fil", "ar", "hi"};
        return {"zh", "en", "ja", "yue"};
    }
    if (normalizedEngine == "fireredasr") return {"zh", "en"};
    if (normalizedEngine == "parakeet") {
        // Parakeet TDT v3 is trained for European languages, not Whisper's
        // multilingual language set. Keeping unsupported languages here
        // makes the UI accept requests that the model cannot recognize.
        return {"en", "de", "es", "fr", "it", "pt", "pl", "nl", "cs", "uk",
                "ru", "ro", "bg", "hr", "sk", "sl", "sv", "da", "fi", "no",
                "el", "hu", "et", "lv", "lt"};
    }
    if (normalizedEngine == "qwen3-asr") return whisperLanguages;
    return whisperLanguages;
}

QString LocalModelManager::languageDisplayName(const QString &code) {
    static const QHash<QString, QString> names = {
        {"auto", "自動檢測"}, {"zh", "中文"}, {"en", "English"}, {"de", "Deutsch"}, {"es", "Español"},
        {"ru", "Русский"}, {"ko", "한국어"}, {"fr", "Français"}, {"ja", "日本語"}, {"pt", "Português"},
        {"tr", "Türkçe"}, {"pl", "Polski"}, {"ca", "Català"}, {"nl", "Nederlands"}, {"ar", "العربية"},
        {"sv", "Svenska"}, {"it", "Italiano"}, {"id", "Bahasa Indonesia"}, {"hi", "हिन्दी"}, {"fi", "Suomi"},
        {"vi", "Tiếng Việt"}, {"he", "עברית"}, {"uk", "Українська"}, {"el", "Ελληνικά"}, {"ms", "Bahasa Melayu"},
        {"cs", "Čeština"}, {"ro", "Română"}, {"da", "Dansk"}, {"hu", "Magyar"}, {"ta", "தமிழ்"},
        {"no", "Norsk"}, {"th", "ไทย"}, {"ur", "اردو"}, {"hr", "Hrvatski"}, {"bg", "Български"},
        {"lt", "Lietuvių"}, {"la", "Latine"}, {"mi", "Māori"}, {"ml", "മലയാളം"}, {"cy", "Cymraeg"},
        {"sk", "Slovenčina"}, {"te", "తెలుగు"}, {"fa", "فارسی"}, {"lv", "Latviešu"}, {"bn", "বাংলা"},
        {"sr", "Српски"}, {"az", "Azərbaycanca"}, {"sl", "Slovenščina"}, {"kn", "ಕನ್ನಡ"}, {"et", "Eesti"},
        {"mk", "Македонски"}, {"br", "Brezhoneg"}, {"eu", "Euskara"}, {"is", "Íslenska"}, {"hy", "Հայերեն"},
        {"ne", "नेपाली"}, {"mn", "Монгол"}, {"bs", "Bosanski"}, {"kk", "Қазақша"}, {"sq", "Shqip"},
        {"sw", "Kiswahili"}, {"gl", "Galego"}, {"mr", "मराठी"}, {"pa", "ਪੰਜਾਬੀ"}, {"si", "සිංහල"},
        {"km", "ខ្មែរ"}, {"sn", "ChiShona"}, {"yo", "Yorùbá"}, {"so", "Soomaali"}, {"af", "Afrikaans"},
        {"oc", "Occitan"}, {"ka", "ქართული"}, {"be", "Беларуская"}, {"tg", "Тоҷикӣ"}, {"sd", "سنڌي"},
        {"gu", "ગુજરાતી"}, {"am", "አማርኛ"}, {"yi", "ייִדיש"}, {"lo", "ລາວ"}, {"uz", "Oʻzbekcha"},
        {"fo", "Føroyskt"}, {"ht", "Kreyòl ayisyen"}, {"ps", "پښتو"}, {"tk", "Türkmençe"}, {"nn", "Nynorsk"},
        {"mt", "Malti"}, {"sa", "संस्कृतम्"}, {"lb", "Lëtzebuergesch"}, {"my", "မြန်မာ"}, {"bo", "བོད་ཡིག"},
        {"tl", "Filipino"}, {"mg", "Malagasy"}, {"as", "অসমীয়া"}, {"tt", "Татарча"}, {"haw", "ʻŌlelo Hawaiʻi"},
        {"ln", "Lingála"}, {"ha", "Hausa"}, {"ba", "Башҡортса"}, {"jw", "Basa Jawa"}, {"su", "Basa Sunda"},
        {"yue", "廣東話"}
    };
    const QString normalized = code.trimmed().toLower();
    return names.value(normalized, QString("Language (%1)").arg(code.toUpper()));
}

QString LocalModelManager::defaultRoot() {
    if (AppConfig::isPortableMode())
        return QDir(AppConfig::portableRoot()).filePath("models");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
}
QString LocalModelManager::modelDir(const QString &engine, const QString &id, const QString &root) {
    const QString base = root.isEmpty() ? defaultRoot() : root;
    return QDir(base).filePath(engine.toLower().replace(" ", "-") + "/" + id);
}
bool LocalModelManager::isInstalled(const LocalModelSpec &spec, const QString &root) {
    const QString dir = modelDir(spec.engine, spec.id, root);
    for (const auto &file : spec.requiredFiles) if (!QFileInfo::exists(QDir(dir).filePath(file))) return false;
    return !spec.requiredFiles.isEmpty();
}
QString LocalModelManager::installedCacheKey(const QString &engine, const QString &root) const {
    return engine.trimmed().toLower() + QStringLiteral("|") + root;
}

QStringList LocalModelManager::installed(const QString &engine, const QString &root) {
    const QString key = installedCacheKey(engine, root);
    auto it = m_installedCache.constFind(key);
    if (it != m_installedCache.constEnd()) return it.value();
    QStringList out;
    for (const auto &s : catalog())
        if (s.engine.compare(engine, Qt::CaseInsensitive) == 0 && isInstalled(s, root))
            out << s.id;
    m_installedCache.insert(key, out);
    return out;
}
void LocalModelManager::download(const LocalModelSpec &spec, const QString &root) {
    if (m_reply || m_extractProcess) { emit finished(spec.engine, spec.id, false, tr("Another model download is already running.")); return; }
    m_current = spec; m_root = root; m_fileIndex = -1; m_doneBytes = 0; m_totalBytes = 0;
    m_userCancelled = false;
    m_redirectRejectedHost.clear();
    const QString destDir = modelDir(spec.engine, spec.id, root);
    if (!QDir().mkpath(destDir)) {
        emit finished(spec.engine, spec.id, false,
                      tr("無法建立模型目錄：%1").arg(QDir::toNativeSeparators(destDir)));
        return;
    }
    m_installedCache.remove(installedCacheKey(spec.engine, root));
    startStallWatchdog();
    downloadNext();
}
void LocalModelManager::downloadNext() {
    ++m_fileIndex;
    if (m_fileIndex >= m_current.files.size()) {
        if (m_current.archive) {
            const QString dir = modelDir(m_current.engine, m_current.id, m_root);
            const QString archive = QDir(dir).filePath(m_current.archiveName);
            QString validationError;
            if (!validateArchive("tar", archive, validationError)) {
                finish(false, validationError);
                return;
            }
            QTemporaryDir *stagingPtr = new QTemporaryDir(QDir(dir).filePath(".extract-XXXXXX"));
            if (!stagingPtr->isValid()) {
                delete stagingPtr;
                finish(false, tr("Could not create a private extraction directory."));
                return;
            }
            m_extractProcess = new QProcess(this);
            connect(m_extractProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, stagingPtr, dir, archive](int exitCode, QProcess::ExitStatus) {
                bool extractOk = true;
                QString extractErr;
                if (exitCode != 0) {
                    extractOk = false;
                    extractErr = m_userCancelled
                        ? tr("已由使用者取消。")
                        : tr("Could not extract model archive. Install Windows tar or import the extracted folder.");
                } else {
                    QDirIterator extracted(stagingPtr->path(), QDir::Files, QDirIterator::Subdirectories);
                    while (extracted.hasNext()) {
                        const QString source = extracted.next();
                        if (QFileInfo(source).isSymLink()) {
                            extractOk = false;
                            extractErr = tr("The model archive contains a symbolic link, which is not allowed.");
                            break;
                        }
                        const QString relative = QDir(stagingPtr->path()).relativeFilePath(source);
                        // SECURITY: block traversal out of the staging directory.
                        if (SecurityPolicy::hasParentReference(relative) ||
                            !SecurityPolicy::isPathInsideDirectory(source, stagingPtr->path())) {
                            extractOk = false;
                            extractErr = tr("The extracted archive contains an unsafe path: %1").arg(relative);
                            break;
                        }
                        const QString target = QDir(dir).filePath(relative);
                        QDir().mkpath(QFileInfo(target).absolutePath());
                        QFile::remove(target);
                        if (!QFile::copy(source, target)) {
                            extractOk = false;
                            extractErr = tr("Could not finalize extracted model file: %1").arg(relative);
                            break;
                        }
                    }
                }
                QFile::remove(archive);
                delete stagingPtr;
                m_extractProcess = nullptr;
                finish(extractOk && isInstalled(m_current, m_root),
                       extractOk ? (isInstalled(m_current, m_root) ? tr("Model ready.") : tr("Download completed but required model files are missing.")) : extractErr);
            });
            m_extractProcess->start("tar", {"-xf", archive, "-C", stagingPtr->path(), "--strip-components=1", "--no-same-owner"});
            // Extraction of a 600 MB+ archive is I/O bound; give it a generous
            // stall-based window instead of a fixed 30-minute kill.
            QTimer::singleShot(2 * 60 * 60 * 1000, m_extractProcess, [this]() {
                if (m_extractProcess && m_extractProcess->state() != QProcess::NotRunning) {
                    m_userCancelled = false;
                    m_extractProcess->kill();
                    m_extractProcess->waitForFinished(5000);
                }
            });
            return;
        }
        stopStallWatchdog();
        finish(isInstalled(m_current, m_root), isInstalled(m_current, m_root) ? tr("Model ready.") : tr("Download completed but required model files are missing.")); return;
    }
    const auto &modelFile = m_current.files[m_fileIndex];
    const QString relative = QDir::fromNativeSeparators(modelFile.relativePath);
    if (relative.isEmpty() || relative.startsWith('/')
        || kDriveLetterRe.match(relative).hasMatch()
        || relative.split('/', Qt::SkipEmptyParts).contains("..")) {
        finish(false, tr("The model file path is invalid."));
        return;
    }
    const QString path = QDir(modelDir(m_current.engine, m_current.id, m_root)).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        finish(false, tr("無法建立模型檔案目錄：%1").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
        return;
    }
    // 若已存在完整文件且大小合理，則跳過下載（支持斷點續傳語義的簡化版：避免重複下載）
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
        // 檢查是否已包含於 requiredFiles，若存在則認為已下載，累計進度
        bool alreadyExists = false;
        for (const QString &reqFile : m_current.requiredFiles) {
            if (QDir::fromNativeSeparators(reqFile) == relative) { alreadyExists = true; break; }
        }
        if (alreadyExists) {
            m_doneBytes += QFileInfo(path).size();
            // 估算總量以保持進度條連續
            if (m_totalBytes <= m_doneBytes) m_totalBytes = m_doneBytes + 1024;
            emit progress(m_current.engine, m_current.id, m_doneBytes, qMax<qint64>(m_totalBytes, 1));
            downloadNext();
            return;
        }
    }
    // 清理舊的 .part 殘留，確保 Truncate 後重試不會追加
    QFile::remove(path + ".part");
    m_file = new QFile(path + ".part", this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) { m_file->deleteLater(); m_file = nullptr; finish(false, tr("Cannot create model file.")); return; }
    QNetworkRequest req(modelFile.url);
    // SECURITY: reject HTTPS->HTTP downgrades and rely on final URL allowlist.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YumuStudio/2.0"));
    m_reply = m_network->get(req);
    if (m_reply) {
        connect(m_reply, &QNetworkReply::redirected, this, [this](const QUrl &target) {
            if (!m_reply) return;
            const QUrl targetUrl = m_reply->url().resolved(target);
            if (SecurityPolicy::isAllowedRedirect(m_reply->url(), targetUrl))
                m_reply->redirectAllowed();
            else {
                // Record the refused host so the abort can be reported by cause
                // instead of a generic "Operation canceled".
                m_redirectRejectedHost = targetUrl.host();
                m_reply->abort();
            }
        });
    }
    // Capture raw pointers: cancel()/fail paths null the members but the
    // objects stay alive until deleteLater, so late signals stay safe.
    QNetworkReply *reply = m_reply;
    QFile *file = m_file;
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0 && m_totalBytes < m_doneBytes + total) m_totalBytes = m_doneBytes + total;
        m_lastActivityBytes = m_doneBytes + got;
        m_activityTimer.restart();
        emit progress(m_current.engine, m_current.id, m_doneBytes + got, qMax<qint64>(m_totalBytes, 1));
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, file] {
        const QByteArray data = reply->readAll();
        if (file->write(data) != data.size()) {
            m_userCancelled = false;
            file->close();
            fail(tr("寫入模型檔案失敗（磁碟空間不足或 IO 錯誤）：%1")
                     .arg(QDir::toNativeSeparators(file->fileName())));
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, path, reply, file, modelFile] {
        if (file->isOpen()) { file->write(reply->readAll()); file->close(); }
        const bool ok = reply->error() == QNetworkReply::NoError;
        QString err = reply->errorString();
        const QUrl finalUrl = reply->url();
        const qint64 expectedBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        reply->deleteLater();
        if (m_reply == reply) m_reply = nullptr;
        if (m_file == file) m_file = nullptr;
        file->deleteLater();
        if (!ok) {
            QFile::remove(file->fileName());
            if (!m_userCancelled && !m_redirectRejectedHost.isEmpty()) {
                finish(false, tr("下載被中斷：重定向目標 %1 不在允許清單中，已拒絕連線。").arg(m_redirectRejectedHost));
                return;
            }
            finish(false, m_userCancelled ? tr("已由使用者取消。") : err);
            return;
        }
        // SECURITY: final URL must be on the application allowlist.
        if (!SecurityPolicy::isAllowedDownloadUrl(finalUrl)) {
            QFile::remove(file->fileName());
            finish(false, tr("下載最終網址不在允許清單中：%1").arg(finalUrl.host()));
            return;
        }
        // SECURITY: verify SHA-256 when a manifest hash is supplied.
        if (!modelFile.sha256.isEmpty()) {
            if (!SecurityPolicy::verifyFileSha256(file->fileName(), modelFile.sha256)) {
                QFile::remove(file->fileName());
                finish(false, tr("模型檔案 SHA-256 校驗失敗：%1").arg(modelFile.relativePath));
                return;
            }
        }
        // D-04/D-09: 校驗 Content-Length，避免 ModelScope 重定向後空文件
        const qint64 actualPartSize = QFileInfo(file->fileName()).size();
        if (actualPartSize == 0) {
            QFile::remove(file->fileName());
            finish(false, tr("下載檔案為空，請檢查重試。"));
            return;
        }
        if (expectedBytes > 0 && actualPartSize != expectedBytes) {
            QFile::remove(file->fileName());
            finish(false, tr("下載檔案大小不符（預期 %1，實際 %2）。").arg(expectedBytes).arg(actualPartSize));
            return;
        }
        QFile::remove(path);
        if (!QFile::rename(file->fileName(), path)) {
            QFile::remove(file->fileName());
            fail(tr("無法保存模型檔案：%1").arg(QDir::toNativeSeparators(path)));
            return;
        }
        m_doneBytes += QFileInfo(path).size();
        downloadNext();
    });
}
void LocalModelManager::finish(bool ok, const QString &message) {
    stopStallWatchdog();
    // 清理殘留的 .part 文件，避免重試時誤判續傳 (D-09)
    if (!ok) {
        for (const auto &f : m_current.files) {
            const QString part = QDir(modelDir(m_current.engine, m_current.id, m_root)).filePath(QDir::fromNativeSeparators(f.relativePath) + ".part");
            QFile::remove(part);
        }
    }
    // D-10: 確保進度狀態重置，避免下次下載進度條起點錯誤
    if (!ok) {
        m_doneBytes = 0;
        m_totalBytes = 0;
    }
    emit progress(m_current.engine, m_current.id, ok ? 1 : 0, 1);
    m_installedCache.remove(installedCacheKey(m_current.engine, m_root));
    emit finished(m_current.engine, m_current.id, ok, message);
}
void LocalModelManager::fail(const QString &message) {
    stopStallWatchdog();
    for (const auto &f : m_current.files) {
        const QString part = QDir(modelDir(m_current.engine, m_current.id, m_root)).filePath(QDir::fromNativeSeparators(f.relativePath) + ".part");
        QFile::remove(part);
    }
    m_doneBytes = 0;
    m_totalBytes = 0;
    emit progress(m_current.engine, m_current.id, 0, 1);
    m_installedCache.remove(installedCacheKey(m_current.engine, m_root));
    emit finished(m_current.engine, m_current.id, false, message);
}
void LocalModelManager::cancel() { m_userCancelled = true; if (m_reply) m_reply->abort(); if (m_extractProcess && m_extractProcess->state() != QProcess::NotRunning) { m_extractProcess->kill(); m_extractProcess->waitForFinished(5000); } }
bool LocalModelManager::remove(const LocalModelSpec &spec, const QString &root) {
    const bool ok = QDir(modelDir(spec.engine, spec.id, root)).removeRecursively();
    m_installedCache.remove(installedCacheKey(spec.engine, root));
    return ok;
}

bool LocalModelManager::importFolder(const LocalModelSpec &spec, const QString &source, const QString &root, QString *error) {
    const QDir sourceDir(source);
    if (!sourceDir.exists()) {
        if (error) *error = tr("Source folder does not exist.");
        return false;
    }
    const QString destination = modelDir(spec.engine, spec.id, root);
    QDir().mkpath(destination);
    QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString sourceFile = files.next();
        // R-04: reject symbolic links so import cannot point at system files.
        if (QFileInfo(sourceFile).isSymLink()) {
            if (error) *error = tr("Imported folder contains a symbolic link, which is not allowed: %1").arg(sourceFile);
            return false;
        }
        const QString relative = sourceDir.relativeFilePath(sourceFile);
        // SECURITY: block directory traversal via junctions or malicious relative paths.
        if (SecurityPolicy::hasParentReference(relative) ||
            !SecurityPolicy::isPathInsideDirectory(sourceFile, source)) {
            if (error) *error = tr("Imported folder contains an unsafe path: %1").arg(relative);
            return false;
        }
        const QString target = QDir(destination).filePath(relative);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(sourceFile, target)) {
            if (error) *error = tr("Cannot copy model file: %1").arg(relative);
            return false;
        }
    }
    if (!isInstalled(spec, root)) {
        if (error) *error = tr("Imported folder is missing required model files.");
        m_installedCache.remove(installedCacheKey(spec.engine, root));
        return false;
    }
    m_installedCache.remove(installedCacheKey(spec.engine, root));
    return true;
}

void LocalModelManager::checkEngineUpdates() {
    for (auto *reply : m_updateReplies) {
        reply->abort();
        reply->deleteLater();
    }
    m_updateReplies.clear();
    for (const auto &engine : engines()) {
        QNetworkRequest request(engine.releaseApi);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YumuStudio/2.0"));
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        auto *reply = m_network->get(request);
        m_updateReplies.insert(engine.id, reply);
        QTimer::singleShot(60000, reply, [reply] {
            if (reply->isRunning()) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, engine] {
            m_updateReplies.remove(engine.id);
            const auto finish = [this, engine](bool ok, const QString &latest, bool update, const QString &message) {
                emit engineUpdateFinished(engine.id, ok, engine.installedVersion, latest, update, message);
            };
            if (reply->error() != QNetworkReply::NoError) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QString message = (status == 403 || status == 429) ? tr("GitHub API 暫時限流，稍後再試；不影響引擎下載。") : reply->errorString();
                finish(false, {}, false, message);
                reply->deleteLater();
                return;
            }
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                finish(false, {}, false, tr("Update service returned invalid JSON."));
                reply->deleteLater();
                return;
            }
            const QString latest = document.object().value("tag_name").toString();
            if (latest.isEmpty()) {
                finish(false, {}, false, tr("Release version was not found."));
                reply->deleteLater();
                return;
            }
            const QVersionNumber current = QVersionNumber::fromString(engine.installedVersion);
            const QVersionNumber remote = QVersionNumber::fromString(latest);
            const bool comparable = !current.isNull() && !remote.isNull();
            finish(true, latest, comparable && remote > current, tr("Latest release checked."));
            reply->deleteLater();
        });
    }
}

