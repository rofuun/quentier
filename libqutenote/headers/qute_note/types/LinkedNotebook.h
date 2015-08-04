#ifndef __LIB_QUTE_NOTE__TYPES__LINKED_NOTEBOOK_H
#define __LIB_QUTE_NOTE__TYPES__LINKED_NOTEBOOK_H

#include "INoteStoreDataElement.h"
#include <QEverCloud.h>
#include <QSharedDataPointer>

namespace qute_note {

QT_FORWARD_DECLARE_CLASS(LinkedNotebookData)

class QUTE_NOTE_EXPORT LinkedNotebook: public INoteStoreDataElement
{
public:
    QN_DECLARE_DIRTY

public:
    LinkedNotebook();
    LinkedNotebook(const LinkedNotebook & other);
    LinkedNotebook(LinkedNotebook && other);
    LinkedNotebook & operator=(const LinkedNotebook & other);
    LinkedNotebook & operator=(LinkedNotebook && other);

    LinkedNotebook(const qevercloud::LinkedNotebook & linkedNotebook);
    LinkedNotebook(qevercloud::LinkedNotebook && linkedNotebook);

    virtual ~LinkedNotebook();

    operator const qevercloud::LinkedNotebook & () const;
    operator qevercloud::LinkedNotebook & ();

    bool operator==(const LinkedNotebook & other) const;
    bool operator!=(const LinkedNotebook & other) const;

    virtual void clear();

    virtual bool hasGuid() const Q_DECL_OVERRIDE;
    virtual const QString & guid() const Q_DECL_OVERRIDE;
    virtual void setGuid(const QString & guid) Q_DECL_OVERRIDE;

    virtual bool hasUpdateSequenceNumber() const Q_DECL_OVERRIDE;
    virtual qint32 updateSequenceNumber() const Q_DECL_OVERRIDE;
    virtual void setUpdateSequenceNumber(const qint32 usn) Q_DECL_OVERRIDE;

    virtual bool checkParameters(QString & errorDescription) const Q_DECL_OVERRIDE;

    bool hasShareName() const;
    const QString & shareName() const;
    void setShareName(const QString & shareName);

    bool hasUsername() const;
    const QString & username() const;
    void setUsername(const QString & username);

    bool hasShardId() const;
    const QString & shardId() const;
    void setShardId(const QString & shardId);

    bool hasShareKey() const;
    const QString & shareKey() const;
    void setShareKey(const QString & shareKey);

    bool hasUri() const;
    const QString & uri() const;
    void setUri(const QString & uri);

    bool hasNoteStoreUrl() const;
    const QString & noteStoreUrl() const;
    void setNoteStoreUrl(const QString & noteStoreUrl);

    bool hasWebApiUrlPrefix() const;
    const QString & webApiUrlPrefix() const;
    void setWebApiUrlPrefix(const QString & webApiUrlPrefix);

    bool hasStack() const;
    const QString & stack() const;
    void setStack(const QString & stack);

    bool hasBusinessId() const;
    qint32 businessId() const;
    void setBusinessId(const qint32 businessId);

private:
    virtual QTextStream & Print(QTextStream & strm) const Q_DECL_OVERRIDE;

    // hide useless methods inherited from the base class from the public interface
    virtual const QString localGuid() const { return QString(); }
    virtual void setLocalGuid(const QString &) {}
    virtual void unsetLocalGuid() {}

    virtual bool isLocal() const { return false; }
    virtual void setLocal(const bool) {}

    QSharedDataPointer<LinkedNotebookData> d;
};

} // namespace qute_note

Q_DECLARE_METATYPE(qute_note::LinkedNotebook)

#endif // __LIB_QUTE_NOTE__TYPES__LINKED_NOTEBOOK_H