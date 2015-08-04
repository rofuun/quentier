#ifndef __LIB_QUTE_NOTE__EXCEPTION__SHARED_NOTEBOOK_ADAPTER_ACCESS_EXCEPTION_H
#define __LIB_QUTE_NOTE__EXCEPTION__SHARED_NOTEBOOK_ADAPTER_ACCESS_EXCEPTION_H

#include <qute_note/exception/IQuteNoteException.h>

namespace qute_note {

class QUTE_NOTE_EXPORT SharedNotebookAdapterAccessException: public IQuteNoteException
{
public:
    explicit SharedNotebookAdapterAccessException(const QString & message);

private:
    virtual const QString exceptionDisplayName() const;
};

} // namespace qute_note

#endif // __LIB_QUTE_NOTE__EXCEPTION__SHARED_NOTEBOOK_ADAPTER_ACCESS_EXCEPTION_H
