#pragma once
#include <QString>
#include <QDir>
#include <QRegularExpression>

// Shared archive safety checks used by both the backend installer and the
// model downloader. An archive member is rejected when it is empty, absolute,
// drive-letter rooted, or walks up with ".." (zip/tar slip guard).
namespace ArchiveUtil {

inline bool isSafeMemberName(const QString &member) {
    static const QRegularExpression kDriveLetterRe(QStringLiteral("^[A-Za-z]:"));
    const QString normalized = QDir::fromNativeSeparators(member.trimmed());
    if (normalized.isEmpty() || normalized.startsWith('/')
        || kDriveLetterRe.match(normalized).hasMatch())
        return false;
    for (const QString &part : normalized.split('/', Qt::SkipEmptyParts))
        if (part == "..") return false;
    return true;
}

} // namespace ArchiveUtil
