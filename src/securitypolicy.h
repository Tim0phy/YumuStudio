#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStandardPaths>

// Centralized security helpers used by download, installation and media
// processing code.  All functions are stateless and re-entrant.
namespace SecurityPolicy {

// ---------------------------------------------------------------------------
// Media input paths (passed to FFmpeg -i)
// ---------------------------------------------------------------------------

// Returns true only when the path is a regular readable local file and does
// not contain FFmpeg protocol specifiers that could trigger concat/pipe/file
// protocol handling (e.g. "test|calc.mp4", "concat:evil.mp4|", "file://...").
inline bool isSafeMediaInputPath(const QString &path) {
    if (path.isEmpty()) return false;
    const QString trimmed = path.trimmed();
    if (trimmed.contains('|')) return false;
    if (trimmed.contains(QStringLiteral("://"))) return false;
    // Reject common FFmpeg protocol prefixes.  Drive letters like C: are kept.
    const QString lower = trimmed.toLower();
    const QStringList dangerousPrefixes = {
        QStringLiteral("concat:"),
        QStringLiteral("file:"),
        QStringLiteral("pipe:"),
        QStringLiteral("subfile:"),
        QStringLiteral("gopher:"),
        QStringLiteral("http:"),
        QStringLiteral("https:"),
        QStringLiteral("ftp:"),
        QStringLiteral("sftp:"),
        QStringLiteral("tcp:"),
        QStringLiteral("udp:"),
    };
    for (const QString &prefix : dangerousPrefixes)
        if (lower.startsWith(prefix)) return false;

    const QFileInfo fi(trimmed);
    if (!fi.isFile() || !fi.isReadable()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Download host allowlist and redirect validation
// ---------------------------------------------------------------------------

// Hosts that the application is expected to download binaries/models from.
//
// NOTE: several entries exist only because a "stable" URL 302-redirects to a
// CDN host. The redirect policy is UserVerifiedRedirectPolicy, so the FINAL
// host must be listed here or the transfer is aborted mid-flight with
// "Operation canceled" — even though the first request succeeded.
// Verified redirect targets (2026-09):
//   github.com/.../releases/download/...  -> release-assets.githubusercontent.com
//   huggingface.co/.../resolve/main/...   -> us.aws.cdn.hf.co (region-scoped)
//   modelscope.cn/.../resolve/master/...  -> cdn-lfs-*.modelscope.cn (region-scoped)
// Keep these in sync with the real chain; do not assume the origin host is
// the last hop.
inline QStringList allowedDownloadHosts() {
    return {
        QStringLiteral("huggingface.co"),
        QStringLiteral("www.huggingface.co"),
        // HuggingFace LFS/CDN tier. The AWS variant is region-scoped, so match
        // the whole family with a wildcard rather than one hard-coded region.
        QStringLiteral("*.hf.co"),
        QStringLiteral("cdn-lfs.huggingface.co"),
        QStringLiteral("cdn-lfs-us-1.huggingface.co"),
        QStringLiteral("cdn-lfs-eu-1.huggingface.co"),
        QStringLiteral("modelscope.cn"),
        QStringLiteral("www.modelscope.cn"),
        // ModelScope LFS/CDN tier. The cdn-lfs-* subdomains are region-scoped,
        // so match the whole family with a wildcard rather than one hard-coded region.
        QStringLiteral("*.modelscope.cn"),
        QStringLiteral("github.com"),
        QStringLiteral("www.github.com"),
        // GitHub's raw file-content host; used to serve commit-addressed,
        // immutable copies of scripts whose rolling "latest" endpoints rotate
        // content (e.g. pypa/get-pip). Same trust family and TLS as github.com.
        QStringLiteral("raw.githubusercontent.com"),
        // GitHub release-asset download tiers. GitHub has migrated release
        // downloads across several host names over time and still serves them
        // from more than one; a variant installed without these fails on the
        // very first asset redirect.
        QStringLiteral("objects.githubusercontent.com"),
        QStringLiteral("github-releases.githubusercontent.com"),
        QStringLiteral("release-assets.githubusercontent.com"),
        QStringLiteral("github-production-release-asset-2e65be.s3.amazonaws.com"),
        QStringLiteral("download.pytorch.org"),
        QStringLiteral("bootstrap.pypa.io"),
        QStringLiteral("www.python.org"),
        QStringLiteral("python.org"),
        QStringLiteral("files.pythonhosted.org"),
        QStringLiteral("pypi.org"),
        QStringLiteral("www.pypi.org"),
    };
}

inline bool isAllowedDownloadHost(const QString &host) {
    const QString h = host.trimmed().toLower();
    if (h.isEmpty()) return false;
    for (const QString &allowed : allowedDownloadHosts()) {
        if (allowed.startsWith(QLatin1String("*."))) {
            const QString suffix = allowed.mid(1); // e.g. ".huggingface.co"
            if (h.endsWith(suffix)) return true;
        } else if (h == allowed) {
            return true;
        }
    }
    return false;
}

inline bool isAllowedDownloadUrl(const QUrl &url) {
    return isAllowedDownloadHost(url.host());
}

// Validates a redirect.  Returns false for downgrades from HTTPS to HTTP
// unless the original request was already HTTP, or when the target host is
// not in the allowlist.
inline bool isAllowedRedirect(const QUrl &from, const QUrl &to) {
    if (!to.isValid() || to.host().isEmpty()) return false;
    if (from.scheme().toLower() == QLatin1String("https") &&
        to.scheme().toLower() != QLatin1String("https")) {
        return false;
    }
    return isAllowedDownloadUrl(to);
}

// ---------------------------------------------------------------------------
// SHA-256 file verification
// ---------------------------------------------------------------------------

inline bool verifyFileSha256(const QString &filePath, const QByteArray &expectedHex) {
    if (expectedHex.isEmpty()) return true; // nothing to verify
    const QByteArray expected = QByteArray::fromHex(expectedHex);
    if (expected.isEmpty()) return false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 chunkSize = 1024 * 1024;
    while (!file.atEnd()) {
        hash.addData(file.read(chunkSize));
    }
    return hash.result() == expected;
}

inline bool verifyFileSha256(const QString &filePath, const QString &expectedHex) {
    return verifyFileSha256(filePath, expectedHex.toLatin1());
}

// ---------------------------------------------------------------------------
// Temporary directory safety
// ---------------------------------------------------------------------------

// Returns true when root is under a known user-writable location and is not
// a reparse point / symlink itself.  This reduces the risk of an attacker
// pre-creating directories or junctions to redirect temporary files.
inline bool isSafeTempRoot(const QString &root) {
    if (root.isEmpty()) return false;
    const QFileInfo fi(QDir(root).absolutePath());
    if (!fi.exists() || !fi.isDir()) return false;
    if (fi.isSymLink()) return false;
    const QString canonical = fi.canonicalFilePath().toLower();
    const QStringList safeRoots = {
        QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).toLower(),
        QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).toLower(),
        QDir::fromNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).toLower(),
    };
    for (const QString &safe : safeRoots) {
        if (!safe.isEmpty() && (canonical == safe || canonical.startsWith(safe + QLatin1String("/"))))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// SSRF / private address blocking
// ---------------------------------------------------------------------------

inline bool isPrivateOrLoopbackHost(const QUrl &url) {
    const QString host = url.host().trimmed().toLower();
    if (host.isEmpty()) return true;
    if (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") ||
        host == QLatin1String("::1") || host == QLatin1String("[::1]"))
        return true;
    // IPv4 private / link-local ranges
    const auto ipv4Octets = [](const QString &h) -> QList<int> {
        QList<int> out;
        for (const QString &part : h.split(QLatin1String("."), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int v = part.toInt(&ok);
            if (!ok || v < 0 || v > 255) return {};
            out << v;
        }
        return out;
    };
    const QList<int> oct = ipv4Octets(host);
    if (oct.size() == 4) {
        if (oct[0] == 10) return true;
        if (oct[0] == 172 && oct[1] >= 16 && oct[1] <= 31) return true;
        if (oct[0] == 192 && oct[1] == 168) return true;
        if (oct[0] == 127) return true;
        if (oct[0] == 169 && oct[1] == 254) return true;
    }
    // IPv6 link-local / unique-local
    if (host.startsWith(QLatin1String("fc")) || host.startsWith(QLatin1String("fd")) ||
        host.startsWith(QLatin1String("fe80:")) || host.startsWith(QLatin1String("::1")))
        return true;
    return false;
}

// ---------------------------------------------------------------------------
// Secret redaction for logging / error messages
// ---------------------------------------------------------------------------

inline QString redactSecrets(const QString &text) {
    QString out = text;
    // Redact common API-key patterns (case-insensitive, preserve prefix for context).
    static const QRegularExpression bearerRe(QStringLiteral("(bearer\\s+)[a-zA-Z0-9_\\-\\.]{16,}"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression keyRe(QStringLiteral("(x-api-key\\s*[:=]?\\s*|[\"']?x-goog-api-key[\"']?\\s*[:=]?\\s*)[a-zA-Z0-9_\\-\\.]{16,}"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression genericRe(QStringLiteral("(api[_-]?key\\s*[:=]?\\s*['\"]?)([a-zA-Z0-9_\\-\\.]{16,})"),
                                               QRegularExpression::CaseInsensitiveOption);
    out.replace(bearerRe, QStringLiteral("\\1<redacted>"));
    out.replace(keyRe, QStringLiteral("\\1<redacted>"));
    out.replace(genericRe, QStringLiteral("\\1<redacted>"));
    return out;
}

// ---------------------------------------------------------------------------
// Path containment check (defense against directory traversal / junctions)
// ---------------------------------------------------------------------------

// Returns true when 'child' (file or directory) is inside 'parent' after
// resolving both paths canonically.  Use this after computing relative paths
// or when copying files out of an archive/staging directory.
inline bool isPathInsideDirectory(const QString &child, const QString &parent) {
    const QString canonicalChild = QFileInfo(child).canonicalFilePath();
    const QString canonicalParent = QFileInfo(parent).canonicalFilePath();
    if (canonicalChild.isEmpty() || canonicalParent.isEmpty()) return false;
    const QString lowerChild = canonicalChild.toLower();
    const QString lowerParent = canonicalParent.toLower();
    return lowerChild == lowerParent || lowerChild.startsWith(lowerParent + QLatin1String("/"));
}

inline bool hasParentReference(const QString &relativePath) {
    const QString normalized = QDir::fromNativeSeparators(relativePath.trimmed());
    for (const QString &part : normalized.split(QLatin1String("/"), Qt::SkipEmptyParts))
        if (part == QLatin1String("..")) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Prompt sanitization
// ---------------------------------------------------------------------------

inline QString sanitizePrompt(const QString &prompt, int maxLength = 800) {
    QString out = prompt.trimmed();
    // Remove control characters except tab/newline, then truncate to a safe length.
    out.remove(QRegularExpression(QStringLiteral("[\x00-\x08\x0B-\x0C\x0E-\x1F\x7F]")));
    if (out.size() > maxLength)
        out.truncate(maxLength);
    return out;
}

} // namespace SecurityPolicy
