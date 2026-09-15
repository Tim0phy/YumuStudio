#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;

class AppUpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit AppUpdateChecker(QObject *parent = nullptr);

    // Query GitHub Releases for the latest version.
    void check(const QString &owner, const QString &repo);

signals:
    void checkFinished(bool ok, bool updateAvailable,
                       const QString &latestVersion,
                       const QString &releaseUrl,
                       const QString &error);

private:
    static QString stripVersionPrefix(const QString &tag);

    QNetworkAccessManager *m_network = nullptr;
};
