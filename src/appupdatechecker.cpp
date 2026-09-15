#include "appupdatechecker.h"
#include "securitypolicy.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QVersionNumber>
#include <QTimer>
#include <QJsonParseError>

AppUpdateChecker::AppUpdateChecker(QObject *parent) : QObject(parent) {
    m_network = new QNetworkAccessManager(this);
}

QString AppUpdateChecker::stripVersionPrefix(const QString &tag) {
    QString s = tag.trimmed();
    if (s.startsWith('v', Qt::CaseInsensitive))
        s = s.mid(1);
    return s.trimmed();
}

void AppUpdateChecker::check(const QString &owner, const QString &repo) {
    const QString ownerTrimmed = owner.trimmed();
    const QString repoTrimmed  = repo.trimmed();
    if (ownerTrimmed.isEmpty() || repoTrimmed.isEmpty()) {
        emit checkFinished(false, false, {}, {},
                           QObject::tr("GitHub repository is not configured."));
        return;
    }

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                       .arg(ownerTrimmed, repoTrimmed));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("YumuStudio/%1").arg(QStringLiteral(APP_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);

    auto *reply = m_network->get(request);
    QTimer::singleShot(15000, reply, [reply] {
        if (reply && reply->isRunning()) reply->abort();
    });

    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &target) {
        if (!reply) return;
        const QUrl targetUrl = reply->url().resolved(target);
        if (SecurityPolicy::isAllowedRedirect(reply->url(), targetUrl))
            reply->redirectAllowed();
        else
            reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, ownerTrimmed, repoTrimmed] {
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString message = (status == 403 || status == 429)
                ? QObject::tr("GitHub API rate limited. Please try again later.")
                : reply->errorString();
            emit checkFinished(false, false, {}, {}, message);
            reply->deleteLater();
            return;
        }

        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit checkFinished(false, false, {}, {},
                               QObject::tr("Update service returned invalid JSON."));
            reply->deleteLater();
            return;
        }

        const QJsonObject obj = doc.object();
        const QString tag = obj.value("tag_name").toString();
        const QString releaseUrl = obj.value("html_url").toString();
        if (tag.isEmpty()) {
            emit checkFinished(false, false, {}, {},
                               QObject::tr("Release version was not found."));
            reply->deleteLater();
            return;
        }

        const QString latest = stripVersionPrefix(tag);
        const QString current = stripVersionPrefix(QStringLiteral(APP_VERSION));
        const QVersionNumber currentVer = QVersionNumber::fromString(current);
        const QVersionNumber latestVer  = QVersionNumber::fromString(latest);
        const bool comparable = !currentVer.isNull() && !latestVer.isNull();
        const bool updateAvailable = comparable && latestVer > currentVer;

        emit checkFinished(true, updateAvailable, tag,
                           releaseUrl.isEmpty()
                               ? QStringLiteral("https://github.com/%1/%2/releases").arg(ownerTrimmed, repoTrimmed)
                               : releaseUrl,
                           {});
        reply->deleteLater();
    });
}
