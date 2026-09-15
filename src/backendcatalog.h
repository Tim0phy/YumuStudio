#pragma once
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QProcess;

// A backend variant is one installable runtime for one engine, for example
// "whisper.cpp / cuda". Nothing here is decided at compile time: the app ships
// with the catalog only, and every variant is fetched or built on demand.
struct BackendSpec {
    QString engineId;
    QString variantId;            // cpu | vulkan | cuda
    QString name;
    QString description;
    QString sizeHint;
    QString requirementNote;

// Prebuilt artefact resolution (GitHub releases, matched by regex so a
    // renamed asset does not break the installer).
    QString releaseRepo;
    QStringList assetPatterns;
    QStringList executableNames;  // first match found inside the variant folder

    // Python engines are provisioned with pip into a shared private runtime.
    bool needsPython = false;
    QStringList pipPackages;
    // Map a pip package (or wheel URL) to its expected SHA-256 hex digest.
    QHash<QString, QString> pipPackageHashes;
    QString pipIndexUrl;
    // Optional expected SHA-256 hex digest for the prebuilt archive/asset.
    QString assetSha256;
};

class BackendCatalog {
public:
    static QList<BackendSpec> all();
    static QList<BackendSpec> forEngine(const QString &engineId);
    static QStringList engineIds();
    static BackendSpec find(const QString &engineId, const QString &variantId, bool *found = nullptr);
    static QString variantLabel(const QString &variantId);

    // Layout ------------------------------------------------------------------
    static QString installRoot();
    static QString variantDir(const QString &engineId, const QString &variantId);
    static QString manifestPath(const QString &engineId, const QString &variantId);
    static QString pythonRoot();
    static QString pythonExecutable();
    static QString pythonSitePackages();
    static bool isPythonRuntimeReady();
    // True when the interpreter can actually `import pip`. Empty pythonExe
    // falls back to pythonExecutable(). Returns false when no interpreter exists.
    static bool pythonHasPip(const QString &pythonExe = QString());

    // Install state -----------------------------------------------------------
    static QJsonObject readManifest(const QString &engineId, const QString &variantId);
    static bool writeManifest(const QString &engineId, const QString &variantId, const QJsonObject &manifest);
    static bool isInstalled(const QString &engineId, const QString &variantId);
    static QStringList installedVariants(const QString &engineId);
    static bool removeVariant(const QString &engineId, const QString &variantId);
    static bool activateVariant(const QString &engineId, const QString &variantId);
    static QString installedVersion(const QString &engineId, const QString &variantId);

    // Runtime resolution ------------------------------------------------------
    static QString variantExecutable(const QString &engineId, const QString &variantId);
    // Resolves a whisper.cpp CLI for the requested compute device, falling back
    // to any other installed variant. usedVariant reports what was chosen.
    static QString resolveWhisperCli(const QString &computeDevice, QString *usedVariant = nullptr);
    static QStringList variantsByPreference(const QString &computeDevice);
    static QStringList extraPathEntries(const QString &engineId, const QString &variantId);
    static void applyProcessEnvironment(QProcess &process, const QString &engineId,
                                        const QString &variantId);
};
