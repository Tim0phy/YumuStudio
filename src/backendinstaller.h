#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QString>
#include <QMap>
#include <QList>

class BackendInstaller : public QObject {
    Q_OBJECT
public:
    explicit BackendInstaller(QObject *parent = nullptr);
    ~BackendInstaller();

    bool isBusy() const; // any session busy
    bool isBusy(const QString &engineId, const QString &variantId) const;
    static QString installLabel(const QString &engineId, const QString &variantId);

public slots:
    void install(const QString &engineId, const QString &variantId);
    void cancel(); // cancel all
    void cancel(const QString &engineId, const QString &variantId);

signals:
    void progressDetailed(const QString &engineId, const QString &variantId, int percent, const QString &stage);
    void logLineDetailed(const QString &engineId, const QString &variantId, const QString &line);
    void finished(const QString &engineId, const QString &variantId, bool ok, const QString &message);

private:
    class Session;
    QString sessionKey(const QString &engineId, const QString &variantId) const;
    Session *findSession(const QString &engineId, const QString &variantId) const;
    bool isPythonBusy() const;
    void enqueuePythonPending(const QString &engineId, const QString &variantId);
    void tryStartPendingPython();
    void onSessionFinished(const QString &key, bool ok, const QString &message);
    void startSession(const QString &engineId, const QString &variantId);

    QMap<QString, Session*> m_sessions;
    struct PendingInstall { QString engineId; QString variantId; };
    QList<PendingInstall> m_pendingPythonQueue;
};
