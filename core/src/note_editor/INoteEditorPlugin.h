#ifndef __QUTE_NOTE__CORE__NOTE_EDITOR__NOTE_EDITOR_PLUGIN_H
#define __QUTE_NOTE__CORE__NOTE_EDITOR__NOTE_EDITOR_PLUGIN_H

#include <QWidget>
#include <QUrl>

namespace qute_note {

/**
 * @brief The INoteEditorPlugin class is the interface for any class wishing to implement
 * the display for any custom object embedded inside the note editor,
 * such as embedded pdf viewer, embedded video viewer etc.
 */
class INoteEditorPlugin: public QWidget
{
protected:
    explicit INoteEditorPlugin(QWidget * parent = nullptr);

public:
    /**
     * @brief initialize - the method used to initialize the note editor plugin
     * @param mimeType - mime type of the data meant to be displayed by the plugin
     * @param url - url of the content to be displayed by the plugin
     * @param parameterNames - names of string parameters stored in HTML <object> tag for the plugin
     * @param parameterValues - values of string parameters stored in HTML <obejct> tag for the plugin
     * @param errorDescription - error description in case the plugin can't be initialized properly
     * with this set of parameters
     * @return true if initialization was successful, false otherwise
     */
    virtual bool initialize(const QString & mimeType, const QUrl & url,
                            const QStringList & parameterNames,
                            const QStringList & parameterValues,
                            QString & errorDescription) = 0;

    /**
     * @brief mimeTypes - the method telling which mime types the plugin can work with
     * @return the list of mime types the plugin supports
     */
    virtual QStringList mimeTypes() const = 0;

    /**
     * @brief fileExtensions - the method telling which file extensions of the data the plugin should be able to handle
     * @return the list of file extensions the plugin supports
     */
    virtual QStringList fileExtensions() const = 0;

    /**
     * @brief description - the optional method
     * @return plugin's description
     */
    virtual QString description() const { return QString(); }
};

} // namespace qute_note

#endif // __QUTE_NOTE__CORE__NOTE_EDITOR__NOTE_EDITOR_PLUGIN_H
