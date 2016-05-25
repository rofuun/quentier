#include "SavedSearchModelTestHelper.h"
#include "../../models/SavedSearchModel.h"
#include "modeltest.h"
#include "Macros.h"
#include <qute_note/utility/SysInfo.h>
#include <qute_note/logging/QuteNoteLogger.h>

namespace qute_note {

SavedSearchModelTestHelper::SavedSearchModelTestHelper(LocalStorageManagerThreadWorker * pLocalStorageManagerThreadWorker,
                                                       QObject * parent) :
    QObject(parent),
    m_pLocalStorageManagerThreadWorker(pLocalStorageManagerThreadWorker)
{
    QObject::connect(pLocalStorageManagerThreadWorker, QNSIGNAL(LocalStorageManagerThreadWorker,addSavedSearchFailed,SavedSearch,QString,QUuid),
                     this, QNSLOT(SavedSearchModelTestHelper,onAddSavedSearchFailed,SavedSearch,QString,QUuid));
    QObject::connect(pLocalStorageManagerThreadWorker, QNSIGNAL(LocalStorageManagerThreadWorker,updateSavedSearchFailed,SavedSearch,QString,QUuid),
                     this, QNSLOT(SavedSearchModelTestHelper,onUpdateSavedSearchFailed,SavedSearch,QString,QUuid));
    QObject::connect(pLocalStorageManagerThreadWorker, QNSIGNAL(LocalStorageManagerThreadWorker,findSavedSearchFailed,SavedSearch,QString,QUuid),
                     this, QNSLOT(SavedSearchModelTestHelper,onFindSavedSearchFailed,SavedSearch,QString,QUuid));
    QObject::connect(pLocalStorageManagerThreadWorker, QNSIGNAL(LocalStorageManagerThreadWorker,listSavedSearchesFailed,
                                                                LocalStorageManager::ListObjectsOptions,
                                                                size_t,size_t,LocalStorageManager::ListSavedSearchesOrder::type,
                                                                LocalStorageManager::OrderDirection::type,QString,QUuid),
                     this, QNSLOT(SavedSearchModelTestHelper,onListSavedSearchesFailed,LocalStorageManager::ListObjectsOptions,
                                  size_t,size_t,LocalStorageManager::ListSavedSearchesOrder::type,LocalStorageManager::OrderDirection::type,
                                  QString,QUuid));
    QObject::connect(pLocalStorageManagerThreadWorker, QNSIGNAL(LocalStorageManagerThreadWorker,expungeSavedSearchFailed,SavedSearch,QString,QUuid),
                     this, QNSLOT(SavedSearchModelTestHelper,onExpungeSavedSearchFailed,SavedSearch,QString,QUuid));
}

void SavedSearchModelTestHelper::test()
{
    QNDEBUG("SavedSearchModelTestHelper::test");

    try {
        SavedSearch first;
        first.setName("First search");
        first.setQuery("First search query");
        first.setLocal(true);
        first.setDirty(true);

        SavedSearch second;
        second.setName("Second search");
        second.setQuery("Second search query");
        second.setLocal(true);
        second.setDirty(false);

        SavedSearch third;
        third.setName("Third search");
        third.setQuery("Third search query");
        third.setLocal(false);
        third.setDirty(true);

        SavedSearch fourth;
        fourth.setName("Fourth search");
        fourth.setQuery("Fourth search query");
        fourth.setLocal(false);
        fourth.setDirty(false);

        // NOTE: exploiting the direct connection used in the current test environment:
        // after the following lines the local storage would be filled with the test objects
        m_pLocalStorageManagerThreadWorker->onAddSavedSearchRequest(first, QUuid());
        m_pLocalStorageManagerThreadWorker->onAddSavedSearchRequest(second, QUuid());
        m_pLocalStorageManagerThreadWorker->onAddSavedSearchRequest(third, QUuid());
        m_pLocalStorageManagerThreadWorker->onAddSavedSearchRequest(fourth, QUuid());

        SavedSearchCache cache(20);

        SavedSearchModel * model = new SavedSearchModel(*m_pLocalStorageManagerThreadWorker, cache, this);
        ModelTest t1(model);
        Q_UNUSED(t1)

        // Should not be able to change the dirty flag manually
        QModelIndex secondIndex = model->indexForLocalUid(second.localUid());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for local uid");
        }

        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Dirty, QModelIndex());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for dirty column");
        }

        bool res = model->setData(secondIndex, QVariant(true), Qt::EditRole);
        if (res) {
            FAIL("Was able to change the dirty flag in saved search model manually which is not intended");
        }

        QVariant data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the state of dirty flag");
        }

        if (data.toBool()) {
            FAIL("The dirty state appears to have changed after setData in saved search model even though the method returned false");
        }

        // Should be able to make the non-synchronizable (local) item synchronizable (non-local)
        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Synchronizable, QModelIndex());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for synchronizable column");
        }

        res = model->setData(secondIndex, QVariant(true), Qt::EditRole);
        if (!res) {
            FAIL("Can't change the synchronizable flag from false to true for saved search model");
        }

        data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the state of synchronizable flag");
        }

        if (!data.toBool()) {
            FAIL("The synchronizable state appears to have not changed after setData in saved search model even though the method returned true");
        }

        // Verify the dirty flag has changed as a result of makind the item synchronizable
        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Dirty, QModelIndex());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for dirty column");
        }

        data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the state of dirty flag");
        }

        if (!data.toBool()) {
            FAIL("The dirty state hasn't changed after making the saved search model item synchronizable while it was expected to have changed");
        }

        // Should not be able to make the synchronizable (non-local) item non-synchronizable (local)
        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Synchronizable, QModelIndex());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for synchronizable column");
        }

        res = model->setData(secondIndex, QVariant(false), Qt::EditRole);
        if (res) {
            FAIL("Was able to change the synchronizable flag in saved search model from true to false which is not intended");
        }

        data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the state of synchronizable flag");
        }

        if (!data.toBool()) {
            FAIL("The synchronizable state appears to have changed after setData in saved search model even though the method returned false");
        }

        // Should be able to change name and query columns
        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Name);
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for name column");
        }

        QString newName = "second (name modified)";
        res = model->setData(secondIndex, QVariant(newName), Qt::EditRole);
        if (!res) {
            FAIL("Can't change the name of the saved search model item");
        }

        data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the name of the saved search item");
        }

        if (data.toString() != newName) {
            FAIL("The name of the saved search item returned by the model does not match the name just set to this item: "
                 "received " << data.toString() << ", expected " << newName);
        }

        secondIndex = model->index(secondIndex.row(), SavedSearchModel::Columns::Query, QModelIndex());
        if (!secondIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for query column");
        }

        QString newQuery = "second query (modified)";
        res = model->setData(secondIndex, QVariant(newQuery), Qt::EditRole);
        if (!res) {
            FAIL("Can't change the query of the saved search model item");
        }

        data = model->data(secondIndex, Qt::EditRole);
        if (data.isNull()) {
            FAIL("Null data was returned by the saved search model while expected to get the query of the saved search item");
        }

        if (data.toString() != newQuery) {
            FAIL("The query of the saved search item returned by the model does not match the query just set to this item: "
                 "received " << data.toString() << ", expected " << newQuery);
        }

        // Should not be able to remove the row with a synchronizable (non-local) saved search
        res = model->removeRow(secondIndex.row(), QModelIndex());
        if (res) {
            FAIL("Was able to remove the row with a synchronizable saved search which is not intended");
        }

        QModelIndex secondIndexAfterFailedRemoval = model->indexForLocalUid(second.localUid());
        if (!secondIndexAfterFailedRemoval.isValid()) {
            FAIL("Can't get the valid saved search item model index after the failed row removal attempt");
        }

        if (secondIndexAfterFailedRemoval.row() != secondIndex.row()) {
            FAIL("Saved search model returned item index with a different row after the failed row removal attempt");
        }

        // Should be able to remove the row with a non-synchronizable (local) saved search
        QModelIndex firstIndex = model->indexForLocalUid(first.localUid());
        if (!firstIndex.isValid()) {
            FAIL("Can't get the valid saved search item model index for local uid");
        }

        res = model->removeRow(firstIndex.row(), QModelIndex());
        if (!res) {
            FAIL("Can't remove the row with a non-synchronizable saved search item from the model");
        }

        QModelIndex firstIndexAfterRemoval = model->indexForLocalUid(first.localUid());
        if (firstIndexAfterRemoval.isValid()) {
            FAIL("Was able to get the valid model index for the removed saved search item by local uid which is not intended");
        }

        // Check the sorting for saved search items: by default should sort by name in ascending order
        res = checkSorting(*model);
        if (!res) {
            FAIL("Sorting check failed for the saved search model for ascending order");
        }

        // Change the sort order and check the sorting again
        model->sort(SavedSearchModel::Columns::Name, Qt::DescendingOrder);

        res = checkSorting(*model);
        if (!res) {
            FAIL("Sorting check failed for the saved search model for descending order");
        }

        emit success();
        return;
    }
    catch(const IQuteNoteException & exception) {
        SysInfo & sysInfo = SysInfo::GetSingleton();
        QNWARNING("Caught QuteNote exception: " + exception.errorMessage() +
                  ", what: " + QString(exception.what()) + "; stack trace: " + sysInfo.GetStackTrace());
    }
    catch(const std::exception & exception) {
        SysInfo & sysInfo = SysInfo::GetSingleton();
        QNWARNING("Caught std::exception: " + QString(exception.what()) + "; stack trace: " + sysInfo.GetStackTrace());
    }
    catch(...) {
        SysInfo & sysInfo = SysInfo::GetSingleton();
        QNWARNING("Caught some unknown exception; stack trace: " + sysInfo.GetStackTrace());
    }

    emit failure();
}

void SavedSearchModelTestHelper::onAddSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId)
{
    QNDEBUG("SavedSearchModelTestHelper::onAddSavedSearchFailed: search = " << search << ", error description = "
            << errorDescription << ", request id = " << requestId);

    emit failure();
}

void SavedSearchModelTestHelper::onUpdateSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId)
{
    QNDEBUG("SavedSearchModelTestHelper::onUpdateSavedSearchFailed: search = " << search << ", error description = "
            << errorDescription << ", request id = " << requestId);

    emit failure();
}

void SavedSearchModelTestHelper::onFindSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId)
{
    QNDEBUG("SavedSearchModelTestHelper::onFindSavedSearchFailed: search = " << search << ", error description = "
            << errorDescription << ", request id = " << requestId);

    emit failure();
}

void SavedSearchModelTestHelper::onListSavedSearchesFailed(LocalStorageManager::ListObjectsOptions flag,
                                                           size_t limit, size_t offset,
                                                           LocalStorageManager::ListSavedSearchesOrder::type order,
                                                           LocalStorageManager::OrderDirection::type orderDirection,
                                                           QString errorDescription, QUuid requestId)
{
    QNDEBUG("SavedSearchModelTestHelper::onListSavedSearchesFailed: flag = " << flag << ", limit = " << limit
            << ", offset = " << offset << ", order = " << order << ", direction = " << orderDirection
            << ", error description = " << errorDescription << ", request id = " << requestId);

    emit failure();
}

void SavedSearchModelTestHelper::onExpungeSavedSearchFailed(SavedSearch search, QString errorDescription, QUuid requestId)
{
    QNDEBUG("SavedSearchModelTestHelper::onExpungeSavedSearchFailed: search = " << search << ", error description = "
            << errorDescription << ", request id = " << requestId);

    emit failure();
}

bool SavedSearchModelTestHelper::checkSorting(const SavedSearchModel & model) const
{
    int numRows = model.rowCount(QModelIndex());

    QStringList names;
    names.reserve(numRows);
    for(int i = 0; i < numRows; ++i) {
        QModelIndex index = model.index(i, SavedSearchModel::Columns::Name, QModelIndex());
        QString name = model.data(index, Qt::EditRole).toString();
        names << name;
    }

    QStringList sortedNames = names;
    if (model.sortOrder() == Qt::AscendingOrder) {
        std::sort(sortedNames.begin(), sortedNames.end(), LessByName());
    }
    else {
        std::sort(sortedNames.begin(), sortedNames.end(), GreaterByName());
    }

    return (names == sortedNames);
}

bool SavedSearchModelTestHelper::LessByName::operator()(const QString & lhs, const QString & rhs) const
{
    return (lhs.toUpper().localeAwareCompare(rhs.toUpper()) <= 0);
}

bool SavedSearchModelTestHelper::GreaterByName::operator()(const QString & lhs, const QString & rhs) const
{
    return (lhs.toUpper().localeAwareCompare(rhs.toUpper()) > 0);
}

} // namespace qute_note
