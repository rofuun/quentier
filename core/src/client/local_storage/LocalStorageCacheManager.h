#ifndef __QUTE_NOTE__CORE__CLIENT__LOCAL_STORAGE__LOCAL_STORAGE_CACHE_MANAGER_H
#define __QUTE_NOTE__CORE__CLIENT__LOCAL_STORAGE__LOCAL_STORAGE_CACHE_MANAGER_H

#include <QScopedPointer>
#include <functional>

namespace qute_note {

QT_FORWARD_DECLARE_CLASS(Note)

QT_FORWARD_DECLARE_CLASS(LocalStorageCacheManagerPrivate)
class LocalStorageCacheManager
{
public:
    LocalStorageCacheManager();
    ~LocalStorageCacheManager();

    typedef std::function<bool(const LocalStorageCacheManager &)> CacheExpiryFunction;

    size_t numCachedNotes() const;
    void cacheNote(const Note & note);

    enum WhichGuid
    {
        LocalGuid,
        Guid
    };

    const Note * findNote(const QString & guid, const WhichGuid wg) const;

    void installCacheExpiryFunction(const CacheExpiryFunction & function);

private:
    LocalStorageCacheManager(const LocalStorageCacheManager & other) = delete;
    LocalStorageCacheManager(LocalStorageCacheManager && other) = delete;
    LocalStorageCacheManager & operator=(const LocalStorageCacheManager & other) = delete;
    LocalStorageCacheManager & operator=(LocalStorageCacheManager && other) = delete;

    LocalStorageCacheManagerPrivate *   d_ptr;
    Q_DECLARE_PRIVATE(LocalStorageCacheManager)
};

} // namespace qute_note

#endif // __QUTE_NOTE__CORE__CLIENT__LOCAL_STORAGE__LOCAL_STORAGE_CACHE_MANAGER_H
