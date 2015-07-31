#include <qute_note/exception/IQuteNoteException.h>
#include <ostream>

namespace qute_note {

IQuteNoteException::IQuteNoteException(const QString & message) :
    Printable(),
    m_message(message)
{}

#ifdef _MSC_VER
IQuteNoteException::~IQuteNoteException()
#elif __cplusplus >= 201103L
IQuteNoteException::~IQuteNoteException() noexcept
#else
IQuteNoteException::~IQuteNoteException() throw()
#endif
{}

const QString IQuteNoteException::errorMessage() const
{
    return m_message;
}

#if defined(_MSC_VER)
const char * IQuteNoteException::what() const
#elif __cplusplus >= 201103L
const char * IQuteNoteException::what() const noexcept
#else
const char * IQuteNoteException::what() const throw()
#endif
{
    return qPrintable(m_message);
}

QTextStream & IQuteNoteException::Print(QTextStream & strm) const
{
    strm << "\n" << " " << "<" << exceptionDisplayName() << ">";
    strm << "\n" << " " << " message: " << m_message;
    return strm;
}

IQuteNoteException::IQuteNoteException(const IQuteNoteException & other) :
    Printable(other),
    m_message(other.m_message)
{}

IQuteNoteException & IQuteNoteException::operator =(const IQuteNoteException & other)
{
    if (this != &other) {
        m_message = other.m_message;
    }

    return *this;
}



} // namespace qute_note