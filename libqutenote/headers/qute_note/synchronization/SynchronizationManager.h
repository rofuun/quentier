#ifndef __LIB_QUTE_NOTE__SYNCHRONIZATION__SYNCHRONIZATION_MANAGER_H
#define __LIB_QUTE_NOTE__SYNCHRONIZATION__SYNCHRONIZATION_MANAGER_H

#include <qute_note/utility/Linkage.h>
#include <qute_note/utility/Qt4Helper.h>
#include <QObject>

namespace qute_note {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerThreadWorker)
QT_FORWARD_DECLARE_CLASS(SynchronizationManagerPrivate)

class QUTE_NOTE_EXPORT SynchronizationManager: public QObject
{
    Q_OBJECT
public:
    SynchronizationManager(LocalStorageManagerThreadWorker & localStorageManagerThreadWorker);
    virtual ~SynchronizationManager();

    bool active() const;
    bool paused() const;

public Q_SLOTS:
    void synchronize();
    void pause();
    void resume();
    void stop();

Q_SIGNALS:
    void failed(QString errorDescription);
    void finished();

    // state signals
    void remoteToLocalSyncPaused(bool pendingAuthenticaton);
    void remoteToLocalSyncStopped();

    void sendLocalChangesPaused(bool pendingAuthenticaton);
    void sendLocalChangesStopped();

    // other informative signals
    void willRepeatRemoteToLocalSyncAfterSendingChanges();
    void detectedConflictDuringLocalChangesSending();
    void rateLimitExceeded(qint32 secondsToWait);

    void remoteToLocalSyncDone();
    void progress(QString message, double workDonePercentage);

private:
    SynchronizationManager() Q_DECL_DELETE;
    Q_DISABLE_COPY(SynchronizationManager)

    SynchronizationManagerPrivate * d_ptr;
    Q_DECLARE_PRIVATE(SynchronizationManager)
};

} // namespace qute_note

#endif // __LIB_QUTE_NOTE__SYNCHRONIZATION__SYNCHRONIZATION_MANAGER_H