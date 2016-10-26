#include "NoteEditorWidget.h"
#include "../models/TagModel.h"

// Doh, Qt Designer's inability to work with namespaces in the expected way
// is deeply disappointing
#include "NoteTagsWidget.h"
#include "FindAndReplaceWidget.h"
#include "BasicXMLSyntaxHighlighter.h"
#include "../insert-table-tool-button/InsertTableToolButton.h"
#include "../insert-table-tool-button/TableSettingsDialog.h"
#include "../color-picker-tool-button/ColorPickerToolButton.h"
#include <quentier/note_editor/NoteEditor.h>
using quentier::FindAndReplaceWidget;
using quentier::NoteEditor;
using quentier::NoteTagsWidget;
#include "ui_NoteEditorWidget.h"

#include <quentier/local_storage/LocalStorageManagerThreadWorker.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Resource.h>
#include <QFontDatabase>
#include <QScopedPointer>

#define CHECK_NOTE_SET() \
    if (Q_UNLIKELY(m_pCurrentNote.isNull()) { \
        emit notifyError(QT_TR_NOOP("No note is set to the editor")); \
        return; \
    }

namespace quentier {

NoteEditorWidget::NoteEditorWidget(const Account & account, LocalStorageManagerThreadWorker & localStorageWorker,
                                   NoteCache & noteCache, NotebookCache & notebookCache, TagCache & tagCache,
                                   TagModel & tagModel, QUndoStack * pUndoStack, QWidget * parent) :
    QWidget(parent),
    m_pUi(new Ui::NoteEditorWidget),
    m_noteCache(noteCache),
    m_notebookCache(notebookCache),
    m_tagCache(tagCache),
    m_pCurrentNote(),
    m_pCurrentNotebook(),
    m_currentAccount(account),
    m_pUndoStack(pUndoStack),
    m_findCurrentNoteRequestId(),
    m_findCurrentNotebookRequestId(),
    m_updateNoteRequestIds(),
    m_lastFontSizeComboBoxIndex(-1),
    m_lastFontComboBoxFontFamily(),
    m_lastNoteEditorHtml(),
    m_lastSuggestedFontSize(-1),
    m_lastActualFontSize(-1),
    m_pendingEditorSpellChecker(false),
    m_currentNoteWasExpunged(false)
{
    m_pUi->setupUi(this);

    m_pUi->noteEditor->setAccount(m_currentAccount);
    m_pUi->noteEditor->setUndoStack(m_pUndoStack.data());
    m_pUi->findAndReplaceWidget->setHidden(true);
    m_pUi->noteSourceView->setHidden(true);

    checkIconThemeIconsAndSetFallbacks();

    m_pUi->fontSizeComboBox->clear();
    int numFontSizes = m_pUi->fontSizeComboBox->count();
    QNTRACE(QStringLiteral("fontSizeComboBox num items: ") << numFontSizes);
    for(int i = 0; i < numFontSizes; ++i) {
        QVariant value = m_pUi->fontSizeComboBox->itemData(i, Qt::UserRole);
        QNTRACE(QStringLiteral("Font size value for index[") << i << QStringLiteral("] = ") << value);
    }

    BasicXMLSyntaxHighlighter * highlighter = new BasicXMLSyntaxHighlighter(m_pUi->noteSourceView->document());
    Q_UNUSED(highlighter);

    m_pUi->tagNameLabelsContainer->setTagModel(&tagModel);
    m_pUi->tagNameLabelsContainer->setLocalStorageManagerThreadWorker(localStorageWorker);
    createConnections(localStorageWorker);
}

NoteEditorWidget::~NoteEditorWidget()
{
    delete m_pUi;
}

QString NoteEditorWidget::noteLocalUid() const
{
    if (m_pCurrentNote.isNull()) {
        return QString();
    }

    return m_pCurrentNote->localUid();
}

void NoteEditorWidget::setNoteLocalUid(const QString & noteLocalUid)
{
    QNDEBUG("NoteEditorWidget::setNoteLocalUid: " << noteLocalUid);

    if (!m_pCurrentNote.isNull() && (m_pCurrentNote->localUid() == noteLocalUid)) {
        QNDEBUG("This note is already set to the editor, nothing to do");
        return;
    }

    clear();

    if (Q_UNLIKELY(noteLocalUid.isEmpty())) {
        return;
    }

    const Note * pCachedNote = m_noteCache.get(noteLocalUid);

    // The cache might contain the note without resource binary data, need to check for this
    bool hasMissingResources = false;
    if (Q_LIKELY(pCachedNote))
    {
        QList<Resource> resources = pCachedNote->resources();
        if (!resources.isEmpty())
        {
            for(int i = 0, size = resources.size(); i < size; ++i)
            {
                const Resource & resource = resources[i];
                if (resource.hasDataHash() && !resource.hasDataBody()) {
                    hasMissingResources = true;
                    break;
                }
            }
        }
    }

    if (Q_UNLIKELY(!pCachedNote || hasMissingResources))
    {
        m_findCurrentNoteRequestId = QUuid::createUuid();
        Note dummy;
        dummy.setLocalUid(noteLocalUid);
        QNTRACE("Emitting the request to find the current note: local uid = " << noteLocalUid
                << ", request id = " << m_findCurrentNoteRequestId);
        emit findNote(dummy, /* with resource binary data = */ true, m_findCurrentNoteRequestId);
        return;
    }

    QNTRACE("Found the cached note");
    if (Q_UNLIKELY(!pCachedNote->hasNotebookLocalUid() && !pCachedNote->hasNotebookGuid())) {
        emit notifyError(QT_TR_NOOP("Can't set the note to the editor: the note has no linkage to any notebook"));
        return;
    }

    m_pCurrentNote.reset(new Note(*pCachedNote));

    const Notebook * pCachedNotebook = Q_NULLPTR;
    if (m_pCurrentNote->hasNotebookLocalUid()) {
        pCachedNotebook = m_notebookCache.get(m_pCurrentNote->notebookLocalUid());
    }

    if (Q_UNLIKELY(!pCachedNotebook))
    {
        m_findCurrentNotebookRequestId = QUuid::createUuid();
        Notebook dummy;
        if (m_pCurrentNote->hasNotebookLocalUid()) {
            dummy.setLocalUid(m_pCurrentNote->notebookLocalUid());
        }
        else {
            dummy.setLocalUid(QString());
            dummy.setGuid(m_pCurrentNote->notebookGuid());
        }

        QNTRACE("Emitting the request to find the current notebook: " << dummy
                << "\nRequest id = " << m_findCurrentNotebookRequestId);
        emit findNotebook(dummy, m_findCurrentNotebookRequestId);
        return;
    }

    m_pCurrentNotebook.reset(new Notebook(*pCachedNotebook));

    m_pUi->noteEditor->setNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
    m_pUi->tagNameLabelsContainer->setCurrentNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
}

bool NoteEditorWidget::isNoteSourceShown() const
{
    return m_pUi->noteSourceView->isVisible();
}

void NoteEditorWidget::showNoteSource()
{
    updateNoteSourceView(m_lastNoteEditorHtml);
    m_pUi->noteSourceView->setHidden(false);
}

void NoteEditorWidget::hideNoteSource()
{
    m_pUi->noteSourceView->setHidden(true);
}

void NoteEditorWidget::onEditorTextBoldToggled()
{
    m_pUi->noteEditor->textBold();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextItalicToggled()
{
    m_pUi->noteEditor->textItalic();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextUnderlineToggled()
{
    m_pUi->noteEditor->textUnderline();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextStrikethroughToggled()
{
    m_pUi->noteEditor->textStrikethrough();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextAlignLeftAction()
{
    if (m_pUi->formatJustifyLeftPushButton->isChecked()) {
        m_pUi->formatJustifyCenterPushButton->setChecked(false);
        m_pUi->formatJustifyRightPushButton->setChecked(false);
    }

    m_pUi->noteEditor->alignLeft();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextAlignCenterAction()
{
    if (m_pUi->formatJustifyCenterPushButton->isChecked()) {
        m_pUi->formatJustifyLeftPushButton->setChecked(false);
        m_pUi->formatJustifyRightPushButton->setChecked(false);
    }

    m_pUi->noteEditor->alignCenter();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextAlignRightAction()
{
    if (m_pUi->formatJustifyRightPushButton->isChecked()) {
        m_pUi->formatJustifyLeftPushButton->setChecked(false);
        m_pUi->formatJustifyCenterPushButton->setChecked(false);
    }

    m_pUi->noteEditor->alignRight();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextAddHorizontalLineAction()
{
    m_pUi->noteEditor->insertHorizontalLine();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextIncreaseFontSizeAction()
{
    m_pUi->noteEditor->increaseFontSize();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextDecreaseFontSizeAction()
{
    m_pUi->noteEditor->decreaseFontSize();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextHighlightAction()
{
    m_pUi->noteEditor->textHighlight();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextIncreaseIndentationAction()
{
    m_pUi->noteEditor->increaseIndentation();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextDecreaseIndentationAction()
{
    m_pUi->noteEditor->decreaseIndentation();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextInsertUnorderedListAction()
{
    m_pUi->noteEditor->insertBulletedList();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextInsertOrderedListAction()
{
    m_pUi->noteEditor->insertNumberedList();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextEditHyperlinkAction()
{
    m_pUi->noteEditor->editHyperlinkDialog();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextCopyHyperlinkAction()
{
    m_pUi->noteEditor->copyHyperlink();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorTextRemoveHyperlinkAction()
{
    m_pUi->noteEditor->removeHyperlink();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorChooseTextColor(QColor color)
{
    m_pUi->noteEditor->setFontColor(color);
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorChooseBackgroundColor(QColor color)
{
    m_pUi->noteEditor->setBackgroundColor(color);
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorSpellCheckStateChanged(int state)
{
    m_pUi->noteEditor->setSpellcheck(state != Qt::Unchecked);
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorInsertToDoCheckBoxAction()
{
    m_pUi->noteEditor->insertToDoCheckbox();
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorInsertTableDialogAction()
{
    onEditorTextInsertTableDialogRequested();
}

void NoteEditorWidget::onEditorInsertTable(int rows, int columns, double width, bool relativeWidth)
{
    rows = std::max(rows, 1);
    columns = std::max(columns, 1);
    width = std::max(width, 1.0);

    if (relativeWidth) {
        m_pUi->noteEditor->insertRelativeWidthTable(rows, columns, width);
    }
    else {
        m_pUi->noteEditor->insertFixedWidthTable(rows, columns, static_cast<int>(width));
    }

    QNTRACE("Inserted table: rows = " << rows << ", columns = " << columns
            << ", width = " << width << ", relative width = " << (relativeWidth ? "true" : "false"));
    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onUndoAction()
{
    m_pUi->noteEditor->undo();
}

void NoteEditorWidget::onRedoAction()
{
    m_pUi->noteEditor->redo();
}

void NoteEditorWidget::onUpdateNoteComplete(Note note, bool updateResources, bool updateTags, QUuid requestId)
{
    if (!m_pCurrentNote || (m_pCurrentNote->localUid() != note.localUid())) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onUpdateNoteComplete: note local uid = " << note.localUid()
            << ", request id = " << requestId << ", update resources = " << (updateResources ? "true" : "false")
            << ", update tags = " << (updateTags ? "true" : "false"));
    QNTRACE("Updated note: " << note);

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it != m_updateNoteRequestIds.end()) {
        Q_UNUSED(m_updateNoteRequestIds.erase(it))
    }

    QList<Resource> backupResources;
    if (!updateResources) {
        backupResources = m_pCurrentNote->resources();
    }

    QStringList backupTagLocalUids;
    bool hadTagLocalUids = m_pCurrentNote->hasTagLocalUids();
    if (!updateTags && hadTagLocalUids) {
        backupTagLocalUids = m_pCurrentNote->tagLocalUids();
    }

    QStringList backupTagGuids;
    bool hadTagGuids = m_pCurrentNote->hasTagGuids();
    if (!updateTags && hadTagGuids) {
        backupTagGuids = m_pCurrentNote->tagGuids();
    }

    *m_pCurrentNote = note;

    if (!updateResources) {
        m_pCurrentNote->setResources(backupResources);
    }

    if (!updateTags) {
        m_pCurrentNote->setTagLocalUids(backupTagLocalUids);
        m_pCurrentNote->setTagGuids(backupTagGuids);
    }

    if (Q_UNLIKELY(m_pCurrentNotebook.isNull()))
    {
        QNDEBUG("Current notebook is null - a bit unexpected at this point");

        if (!m_findCurrentNotebookRequestId.isNull()) {
            QNDEBUG("The request to find the current notebook is still active, waiting for it to finish");
            return;
        }

        const Notebook * pCachedNotebook = Q_NULLPTR;
        if (m_pCurrentNote->hasNotebookLocalUid()) {
            const QString & notebookLocalUid = m_pCurrentNote->notebookLocalUid();
            pCachedNotebook = m_notebookCache.get(notebookLocalUid);
        }

        if (pCachedNotebook)
        {
            m_pCurrentNotebook.reset(new Notebook(*pCachedNotebook));
        }
        else
        {
            if (Q_UNLIKELY(!m_pCurrentNote->hasNotebookLocalUid() && !m_pCurrentNote->hasNotebookGuid())) {
                QNLocalizedString error = QT_TR_NOOP("Note");
                error += " ";
                if (note.hasTitle()) {
                    error += "\"";
                    error += note.title();
                    error += "\"";
                }
                else {
                    error += QT_TR_NOOP("with local uid");
                    error += " ";
                    error += m_pCurrentNote->localUid();
                }

                error += " ";
                error += QT_TR_NOOP("has neither notebook local uid nor notebook guid");
                QNWARNING(error << ", note: " << *m_pCurrentNote);
                emit notifyError(error);
                clear();
                return;
            }

            m_findCurrentNotebookRequestId = QUuid::createUuid();
            Notebook dummy;
            if (m_pCurrentNote->hasNotebookLocalUid()) {
                dummy.setLocalUid(m_pCurrentNote->notebookLocalUid());
            }
            else {
                dummy.setLocalUid(QString());
                dummy.setGuid(m_pCurrentNote->notebookGuid());
            }

            QNTRACE("Emitting the request to find the current notebook: " << dummy
                    << "\nRequest id = " << m_findCurrentNotebookRequestId);
            emit findNotebook(dummy, m_findCurrentNotebookRequestId);
            return;
        }
    }

    m_pUi->noteEditor->setNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
    m_pUi->tagNameLabelsContainer->setCurrentNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
}

void NoteEditorWidget::onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                                          QNLocalizedString errorDescription, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNWARNING("NoteEditorWidget::onUpdateNoteFailed: " << note << ", update resoures = "
              << (updateResources ? "true" : "false") << ", update tags = " << (updateTags ? "true" : "false")
              << ", error description: " << errorDescription << "\nRequest id = " << requestId);

    QNLocalizedString error = QT_TR_NOOP("Failed to save the updated note");
    error += QStringLiteral(": ");
    error += errorDescription;
    emit notifyError(error);
    // NOTE: not clearing out the unsaved stuff because it may be of value to the user
}

void NoteEditorWidget::onFindNoteComplete(Note note, bool withResourceBinaryData, QUuid requestId)
{
    if (requestId != m_findCurrentNoteRequestId) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onFindNoteComplete: request id = " << requestId
            << ", with resource binary data = " << (withResourceBinaryData ? "true" : "false"));
    QNTRACE("Note: " << note);

    m_findCurrentNoteRequestId = QUuid();

    m_pCurrentNote.reset(new Note(note));

    const Notebook * pCachedNotebook = Q_NULLPTR;
    if (m_pCurrentNote->hasNotebookLocalUid()) {
        pCachedNotebook = m_notebookCache.get(m_pCurrentNote->notebookLocalUid());
    }

    if (Q_UNLIKELY(!pCachedNotebook))
    {
        if (!m_findCurrentNotebookRequestId.isNull()) {
            QNDEBUG("Couldn't find the notebook in the cache and the request to find it is already active");
            return;
        }

        m_findCurrentNotebookRequestId = QUuid::createUuid();
        Notebook dummy;
        if (m_pCurrentNote->hasNotebookLocalUid()) {
            dummy.setLocalUid(m_pCurrentNote->notebookLocalUid());
        }
        else {
            dummy.setLocalUid(QString());
            dummy.setGuid(m_pCurrentNote->notebookGuid());
        }

        QNTRACE("Emitting the request to find the current notebook: " << dummy
                << "\nRequest id = " << m_findCurrentNotebookRequestId);
        emit findNotebook(dummy, m_findCurrentNotebookRequestId);
        return;
    }

    m_pCurrentNotebook.reset(new Notebook(*pCachedNotebook));

    m_pUi->noteEditor->setNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
    m_pUi->tagNameLabelsContainer->setCurrentNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
}

void NoteEditorWidget::onFindNoteFailed(Note note, bool withResourceBinaryData, QNLocalizedString errorDescription,
                                        QUuid requestId)
{
    if (requestId != m_findCurrentNoteRequestId) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onFindNoteFailed: request id = " << requestId << ", with resource binary data = "
            << (withResourceBinaryData ? "true" : "false") << ", error description: " << errorDescription);
    QNTRACE("Note: " << note);

    m_findCurrentNoteRequestId = QUuid();

    clear();

    emit notifyError(QT_TR_NOOP("Can't find the note attempted to be selected in the note editor"));
}

void NoteEditorWidget::onExpungeNoteComplete(Note note, QUuid requestId)
{
    if (!m_pCurrentNote || (m_pCurrentNote->localUid() != note.localUid())) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onExpungeNoteComplete: note local uid = " << note.localUid()
            << ", request id = " << requestId);

    m_currentNoteWasExpunged = true;

    // TODO: ideally should allow the choice to either re-save the note or to drop it

    QNLocalizedString message = QT_TR_NOOP("The note loaded into the editor was expunged from the local storage");
    QNINFO(message << ", note: " << *m_pCurrentNote);
    emit notifyError(message);
}

void NoteEditorWidget::onUpdateNotebookComplete(Notebook notebook, QUuid requestId)
{
    if (!m_pCurrentNote || !m_pCurrentNotebook || (m_pCurrentNotebook->localUid() != notebook.localUid())) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onUpdateNotebookComplete: notebook = " << notebook << "\nRequest id = " << requestId);

    m_pUi->noteEditor->setNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
}

void NoteEditorWidget::onExpungeNotebookComplete(Notebook notebook, QUuid requestId)
{
    if (!m_pCurrentNotebook || (m_pCurrentNotebook->localUid() != notebook.localUid())) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onExpungeNotebookComplete: notebook = " << notebook
            << "\nRequest id = " << requestId);

    clear();
}

void NoteEditorWidget::onFindNotebookComplete(Notebook notebook, QUuid requestId)
{
    if (requestId != m_findCurrentNotebookRequestId) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onFindNotebookComplete: request id = " << requestId << ", notebook: " << notebook);

    m_findCurrentNotebookRequestId = QUuid();

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        QNDEBUG("Can't process the update of the notebook: no current note is set");
        clear();
        return;
    }

    m_pCurrentNotebook.reset(new Notebook(notebook));
    m_pUi->noteEditor->setNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
}

void NoteEditorWidget::onFindNotebookFailed(Notebook notebook, QNLocalizedString errorDescription, QUuid requestId)
{
    if (requestId != m_findCurrentNotebookRequestId) {
        return;
    }

    QNDEBUG("NoteEditorWidget::onFindNotebookFailed: request id = " << requestId
            << ", notebook: " << notebook << "\nError description = " << errorDescription);

    m_findCurrentNotebookRequestId = QUuid();
    clear();
    emit notifyError(QT_TR_NOOP("Can't find the note attempted to be selected in the note editor"));
}

void NoteEditorWidget::onEditorNoteUpdate(Note note)
{
    QNDEBUG("NoteEditorWidget::onEditorNoteUpdate: note local uid = " << note.localUid());
    QNTRACE("Note: " << note);

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        // That shouldn't really happen in normal circumstances but it could in theory be some old event
        // which has reached this object after the note has already been cleaned up
        QNDEBUG("No current note in the note editor widget! Ignoring the update from the note editor");
        return;
    }

    *m_pCurrentNote = note;

    if (Q_LIKELY(m_pCurrentNotebook)) {
        // Update the note within the tag names container just to avoid any potential race with updates from it
        // messing with other note's parts than tags
        m_pUi->tagNameLabelsContainer->setCurrentNoteAndNotebook(*m_pCurrentNote, *m_pCurrentNotebook);
    }

    QUuid requestId = QUuid::createUuid();
    Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
    QNTRACE("Emitting the request to update note: request id = " << requestId << ", note = " << *m_pCurrentNote);
    emit updateNote(*m_pCurrentNote, /* update resources = */ true, /* update tags = */ false, requestId);
}

void NoteEditorWidget::onEditorNoteUpdateFailed(QNLocalizedString error)
{
    QNDEBUG("NoteEditorWidget::onEditorNoteUpdateFailed: " << error);
    emit notifyError(error);
}

void NoteEditorWidget::onEditorTextBoldStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextBoldStateChanged: " << (state ? "enabled" : "disabled"));
    m_pUi->fontBoldPushButton->setChecked(state);
}

void NoteEditorWidget::onEditorTextItalicStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextItalicStateChanged: " << (state ? "enabled" : "disabled"));
    m_pUi->fontItalicPushButton->setChecked(state);
}

void NoteEditorWidget::onEditorTextUnderlineStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextUnderlineStateChanged: " << (state ? "enabled" : "disabled"));
    m_pUi->fontUnderlinePushButton->setChecked(state);
}

void NoteEditorWidget::onEditorTextStrikethroughStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextStrikethroughStateChanged: " << (state ? "enabled" : "disabled"));
    m_pUi->fontStrikethroughPushButton->setChecked(state);
}

void NoteEditorWidget::onEditorTextAlignLeftStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextAlignLeftStateChanged: " << (state ? "enabled" : "disabled"));

    m_pUi->formatJustifyLeftPushButton->setChecked(state);

    if (state) {
        m_pUi->formatJustifyCenterPushButton->setChecked(false);
        m_pUi->formatJustifyRightPushButton->setChecked(false);
    }
}

void NoteEditorWidget::onEditorTextAlignCenterStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextAlignCenterStateChanged: " << (state ? "enabled" : "disabled"));

    m_pUi->formatJustifyCenterPushButton->setChecked(state);

    if (state) {
        m_pUi->formatJustifyLeftPushButton->setChecked(false);
        m_pUi->formatJustifyRightPushButton->setChecked(false);
    }
}

void NoteEditorWidget::onEditorTextAlignRightStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextAlignRightStateChanged: " << (state ? "enabled" : "disabled"));

    m_pUi->formatJustifyRightPushButton->setChecked(state);

    if (state) {
        m_pUi->formatJustifyLeftPushButton->setChecked(false);
        m_pUi->formatJustifyCenterPushButton->setChecked(false);
    }
}

void NoteEditorWidget::onEditorTextInsideOrderedListStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextInsideOrderedListStateChanged: " << (state ? "enabled" : "disabled"));

    m_pUi->formatListOrderedPushButton->setChecked(state);

    if (state) {
        m_pUi->formatListUnorderedPushButton->setChecked(false);
    }
}

void NoteEditorWidget::onEditorTextInsideUnorderedListStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextInsideUnorderedListStateChanged: " << (state ? "enabled" : "disabled"));

    m_pUi->formatListUnorderedPushButton->setChecked(state);

    if (state) {
        m_pUi->formatListOrderedPushButton->setChecked(false);
    }
}

void NoteEditorWidget::onEditorTextInsideTableStateChanged(bool state)
{
    QNTRACE("NoteEditorWidget::onEditorTextInsideTableStateChanged: " << (state ? "enabled" : "disabled"));
    m_pUi->insertTableToolButton->setEnabled(!state);
}

void NoteEditorWidget::onEditorTextFontFamilyChanged(QString fontFamily)
{
    QNTRACE("NoteEditorWidget::onEditorTextFontFamilyChanged: " << fontFamily);

    if (m_lastFontComboBoxFontFamily == fontFamily) {
        QNTRACE("Font family didn't change");
        return;
    }

    m_lastFontComboBoxFontFamily = fontFamily;

    QFont currentFont(fontFamily);
    m_pUi->fontComboBox->setCurrentFont(currentFont);
    QNTRACE("Font family from combo box: " << m_pUi->fontComboBox->currentFont().family()
            << ", font family set by QFont's constructor from it: " << currentFont.family());

    QFontDatabase fontDatabase;
    QList<int> fontSizes = fontDatabase.pointSizes(currentFont.family(), currentFont.styleName());
    // NOTE: it is important to use currentFont.family() in the call above instead of fontFamily variable
    // because the two can be different by presence/absence of apostrophes around the font family name
    if (fontSizes.isEmpty()) {
        QNTRACE("Coulnd't find point sizes for font family " << currentFont.family() << ", will use standard sizes instead");
        fontSizes = fontDatabase.standardSizes();
    }

    m_lastFontSizeComboBoxIndex = 0;    // NOTE: clearing out font sizes combo box causes unwanted update of its index to 0, workarounding it
    m_pUi->fontSizeComboBox->clear();
    int numFontSizes = fontSizes.size();
    QNTRACE("Found " << numFontSizes << " font sizes for font family " << currentFont.family());

    for(int i = 0; i < numFontSizes; ++i) {
        m_pUi->fontSizeComboBox->addItem(QString::number(fontSizes[i]), QVariant(fontSizes[i]));
        QNTRACE("Added item " << fontSizes[i] << "pt for index " << i);
    }

    m_lastFontSizeComboBoxIndex = -1;
}

void NoteEditorWidget::onEditorTextFontSizeChanged(int fontSize)
{
    QNTRACE("NoteEditorWidget::onEditorTextFontSizeChanged: " << fontSize);

    if (m_lastSuggestedFontSize == fontSize) {
        QNTRACE("This font size has already been suggested previously");
        return;
    }

    m_lastSuggestedFontSize = fontSize;

    int fontSizeIndex = m_pUi->fontSizeComboBox->findData(QVariant(fontSize), Qt::UserRole);
    if (fontSizeIndex >= 0)
    {
        m_lastFontSizeComboBoxIndex = fontSizeIndex;
        m_lastActualFontSize = fontSize;

        if (m_pUi->fontSizeComboBox->currentIndex() != fontSizeIndex)
        {
            m_pUi->fontSizeComboBox->setCurrentIndex(fontSizeIndex);
            QNTRACE("fontSizeComboBox: set current index to " << fontSizeIndex << ", found font size = " << QVariant(fontSize));
        }

        return;
    }

    QNDEBUG("Can't find font size " << fontSize << " within those listed in font size combobox, "
            "will try to choose the closest one instead");

    const int numFontSizes = m_pUi->fontSizeComboBox->count();
    int currentSmallestDiscrepancy = 1e5;
    int currentClosestIndex = -1;
    int currentClosestFontSize = -1;
    for(int i = 0; i < numFontSizes; ++i)
    {
        QVariant value = m_pUi->fontSizeComboBox->itemData(i, Qt::UserRole);
        bool conversionResult = false;
        int valueInt = value.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING("Can't convert value from font size combo box to int: " << value);
            continue;
        }

        int discrepancy = abs(valueInt - fontSize);
        if (currentSmallestDiscrepancy > discrepancy) {
            currentSmallestDiscrepancy = discrepancy;
            currentClosestIndex = i;
            currentClosestFontSize = valueInt;
            QNTRACE("Updated current closest index to " << i << ": font size = " << valueInt);
        }
    }

    if (currentClosestIndex < 0) {
        QNDEBUG("Couldn't find closest font size to " << fontSize);
        return;
    }

    QNTRACE("Found closest current font size: " << currentClosestFontSize << ", index = " << currentClosestIndex);

    if ( (m_lastFontSizeComboBoxIndex == currentClosestIndex) &&
         (m_lastActualFontSize == currentClosestFontSize) )
    {
        QNTRACE("Neither the font size nor its index within the font combo box have changed");
        return;
    }

    m_lastFontSizeComboBoxIndex = currentClosestIndex;
    m_lastActualFontSize = currentClosestFontSize;

    if (m_pUi->fontSizeComboBox->currentIndex() != currentClosestIndex) {
        m_pUi->fontSizeComboBox->setCurrentIndex(currentClosestIndex);
    }
}

void NoteEditorWidget::onEditorTextInsertTableDialogRequested()
{
    QNTRACE("NoteEditorWidget::onEditorTextInsertTableDialogRequested");

    QScopedPointer<TableSettingsDialog> tableSettingsDialogHolder(new TableSettingsDialog(this));
    TableSettingsDialog * tableSettingsDialog = tableSettingsDialogHolder.data();
    if (tableSettingsDialog->exec() == QDialog::Rejected) {
        QNTRACE("Returned from TableSettingsDialog::exec: rejected");
        return;
    }

    QNTRACE("Returned from TableSettingsDialog::exec: accepted");
    int numRows = tableSettingsDialog->numRows();
    int numColumns = tableSettingsDialog->numColumns();
    double tableWidth = tableSettingsDialog->tableWidth();
    bool relativeWidth = tableSettingsDialog->relativeWidth();

    if (relativeWidth) {
        m_pUi->noteEditor->insertRelativeWidthTable(numRows, numColumns, tableWidth);
    }
    else {
        m_pUi->noteEditor->insertFixedWidthTable(numRows, numColumns, static_cast<int>(tableWidth));
    }

    m_pUi->noteEditor->setFocus();
}

void NoteEditorWidget::onEditorSpellCheckerNotReady()
{
    QNDEBUG("NoteEditorWidget::onEditorSpellCheckerNotReady");

    m_pendingEditorSpellChecker = true;
    emit notifyError(QNLocalizedString("Spell checker is loading dictionaries, please wait", this));
}

void NoteEditorWidget::onEditorSpellCheckerReady()
{
    QNDEBUG("NoteEditorWidget::onEditorSpellCheckerReady");

    if (!m_pendingEditorSpellChecker) {
        return;
    }

    m_pendingEditorSpellChecker = false;
    emit notifyError(QNLocalizedString());     // Send the empty message to remove the previous one about the non-ready spell checker
}

void NoteEditorWidget::onEditorHtmlUpdate(QString html)
{
    m_lastNoteEditorHtml = html;

    if (!m_pUi->noteSourceView->isVisible()) {
        return;
    }

    updateNoteSourceView(html);
}

void NoteEditorWidget::onFindInsideNoteAction()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindInsideNoteAction"));

    if (m_pUi->findAndReplaceWidget->isHidden())
    {
        QString textToFind = m_pUi->noteEditor->selectedText();
        if (textToFind.isEmpty()) {
            textToFind = m_pUi->findAndReplaceWidget->textToFind();
        }
        else {
            m_pUi->findAndReplaceWidget->setTextToFind(textToFind);
        }

        m_pUi->findAndReplaceWidget->setHidden(false);
        m_pUi->findAndReplaceWidget->show();
    }

    onFindNextInsideNote(m_pUi->findAndReplaceWidget->textToFind(),
                         m_pUi->findAndReplaceWidget->matchCase());
}

void NoteEditorWidget::onFindPreviousInsideNoteAction()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindPreviousInsideNoteAction"));

    if (m_pUi->findAndReplaceWidget->isHidden())
    {
        QString textToFind = m_pUi->noteEditor->selectedText();
        if (textToFind.isEmpty()) {
            textToFind = m_pUi->findAndReplaceWidget->textToFind();
        }
        else {
            m_pUi->findAndReplaceWidget->setTextToFind(textToFind);
        }

        m_pUi->findAndReplaceWidget->setHidden(false);
        m_pUi->findAndReplaceWidget->show();
    }

    onFindPreviousInsideNote(m_pUi->findAndReplaceWidget->textToFind(),
                             m_pUi->findAndReplaceWidget->matchCase());
}

void NoteEditorWidget::onReplaceInsideNoteAction()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onReplaceInsideNoteAction"));

    if (m_pUi->findAndReplaceWidget->isHidden() ||
        !m_pUi->findAndReplaceWidget->replaceEnabled())
    {
        QNTRACE(QStringLiteral("At least the replacement part of find and replace widget "
                               "is hidden, will only show it and do nothing else"));

        QString textToFind = m_pUi->noteEditor->selectedText();
        if (textToFind.isEmpty()) {
            textToFind = m_pUi->findAndReplaceWidget->textToFind();
        }
        else {
            m_pUi->findAndReplaceWidget->setTextToFind(textToFind);
        }

        m_pUi->findAndReplaceWidget->setHidden(false);
        m_pUi->findAndReplaceWidget->setReplaceEnabled(true);
        m_pUi->findAndReplaceWidget->show();
        return;
    }

    onReplaceInsideNote(m_pUi->findAndReplaceWidget->textToFind(),
                        m_pUi->findAndReplaceWidget->replacementText(),
                        m_pUi->findAndReplaceWidget->matchCase());
}

void NoteEditorWidget::onFindAndReplaceWidgetClosed()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindAndReplaceWidgetClosed"));
    onFindNextInsideNote(QString(), false);
}

void NoteEditorWidget::onTextToFindInsideNoteEdited(const QString & textToFind)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onTextToFindInsideNoteEdited: ") << textToFind);

    bool matchCase = m_pUi->findAndReplaceWidget->matchCase();
    onFindNextInsideNote(textToFind, matchCase);
}

#define CHECK_FIND_AND_REPLACE_WIDGET_STATE() \
    if (Q_UNLIKELY(m_pUi->findAndReplaceWidget->isHidden())) { \
        QNTRACE(QStringLiteral("Find and replace widget is not shown, nothing to do")); \
        return; \
    }

void NoteEditorWidget::onFindNextInsideNote(const QString & textToFind, const bool matchCase)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindNextInsideNote: text to find = ")
            << textToFind << QStringLiteral(", match case = ")
            << (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_FIND_AND_REPLACE_WIDGET_STATE()
    m_pUi->noteEditor->findNext(textToFind, matchCase);
}

void NoteEditorWidget::onFindPreviousInsideNote(const QString & textToFind, const bool matchCase)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindPreviousInsideNote: text to find = ")
            << textToFind << QStringLiteral(", match case = ")
            << (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_FIND_AND_REPLACE_WIDGET_STATE()
    m_pUi->noteEditor->findPrevious(textToFind, matchCase);
}

void NoteEditorWidget::onFindInsideNoteCaseSensitivityChanged(const bool matchCase)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onFindInsideNoteCaseSensitivityChanged: match case = ")
            << (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_FIND_AND_REPLACE_WIDGET_STATE()

    QString textToFind = m_pUi->findAndReplaceWidget->textToFind();
    m_pUi->noteEditor->findNext(textToFind, matchCase);
}

void NoteEditorWidget::onReplaceInsideNote(const QString & textToReplace,
                                           const QString & replacementText,
                                           const bool matchCase)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onReplaceInsideNote: text to replace = ")
            << textToReplace << QStringLiteral(", replacement text = ") << replacementText
            << QStringLiteral(", match case = ") << (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_FIND_AND_REPLACE_WIDGET_STATE()
    m_pUi->findAndReplaceWidget->setReplaceEnabled(true);

    m_pUi->noteEditor->replace(textToReplace, replacementText, matchCase);
}

void NoteEditorWidget::onReplaceAllInsideNote(const QString & textToReplace,
                                              const QString & replacementText,
                                              const bool matchCase)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::onReplaceAllInsideNote: text to replace = ")
            << textToReplace << QStringLiteral(", replacement text = ") << replacementText
            << QStringLiteral(", match case = ") << (matchCase ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_FIND_AND_REPLACE_WIDGET_STATE()
    m_pUi->findAndReplaceWidget->setReplaceEnabled(true);

    m_pUi->noteEditor->replaceAll(textToReplace, replacementText, matchCase);
}

#undef CHECK_FIND_AND_REPLACE_WIDGET_STATE

void NoteEditorWidget::createConnections(LocalStorageManagerThreadWorker & localStorageWorker)
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::createConnections"));

    // Local signals to localStorageWorker's slots
    QObject::connect(this, QNSIGNAL(NoteEditorWidget,updateNote,Note,bool,bool,QUuid),
                     &localStorageWorker, QNSLOT(LocalStorageManagerThreadWorker,onUpdateNoteRequest,Note,bool,bool,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorWidget,findNote,Note,bool,QUuid),
                     &localStorageWorker, QNSLOT(LocalStorageManagerThreadWorker,onFindNoteRequest,Note,bool,QUuid));
    QObject::connect(this, QNSIGNAL(NoteEditorWidget,findNotebook,Notebook,QUuid),
                     &localStorageWorker, QNSLOT(LocalStorageManagerThreadWorker,onFindNotebookRequest,Notebook,QUuid));

    // localStorageWorker's signals to local slots
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,updateNoteComplete,Note,bool,bool,QUuid),
                     this, QNSLOT(NoteEditorWidget,onUpdateNoteComplete,Note,bool,bool,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,updateNoteFailed,Note,bool,bool,QNLocalizedString,QUuid),
                     this, QNSLOT(NoteEditorWidget,onUpdateNoteFailed,Note,bool,bool,QNLocalizedString,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,findNoteComplete,Note,bool,QUuid),
                     this, QNSLOT(NoteEditorWidget,onFindNoteComplete,Note,bool,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,findNoteFailed,Note,bool,QNLocalizedString,QUuid),
                     this, QNSLOT(NoteEditorWidget,onFindNoteFailed,Note,bool,QNLocalizedString,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,expungeNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteEditorWidget,onExpungeNoteComplete,Note,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorWidget,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,findNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorWidget,onFindNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,findNotebookFailed,Notebook,QNLocalizedString,QUuid),
                     this, QNSLOT(NoteEditorWidget,onFindNotebookFailed,Notebook,QNLocalizedString,QUuid));
    QObject::connect(&localStorageWorker, QNSIGNAL(LocalStorageManagerThreadWorker,expungeNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NoteEditorWidget,onExpungeNotebookComplete,Notebook,QUuid));

    // Connect note editor's signals to local slots
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,convertedToNote,Note),
                     this, QNSLOT(NoteEditorWidget,onEditorNoteUpdate,Note));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,cantConvertToNote,QNLocalizedString),
                     this, QNSLOT(NoteEditorWidget,onEditorNoteUpdateFailed,QNLocalizedString));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,currentNoteChanged,Note),
                     this, QNSLOT(NoteEditorWidget,onEditorNoteUpdate,Note));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,noteEditorHtmlUpdated,QString),
                     this, QNSLOT(NoteEditorWIdget,onEditorHtmlUpdate,QString));

    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textBoldState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextBoldStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textItalicState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextItalicStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textUnderlineState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextUnderlineStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textUnderlineState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextUnderlineStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textStrikethroughState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextStrikethroughStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textAlignLeftState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignLeftStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textAlignCenterState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignCenterStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textAlignRightState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignRightStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textInsideOrderedListState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsideOrderedListStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textInsideUnorderedListState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsideUnorderedListStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textInsideTableState,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsideTableStateChanged,bool));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textFontFamilyChanged,QString),
                     this, QNSLOT(NoteEditorWidget,onEditorTextFontFamilyChanged,QString));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,textFontSizeChanged,int),
                     this, QNSLOT(NoteEditorWidget,onEditorTextFontSizeChanged,int));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,insertTableDialogRequested),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsertTableDialogRequested));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,spellCheckerNotReady),
                     this, QNSLOT(NoteEditorWidget,onEditorSpellCheckerNotReady));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,spellCheckerReady),
                     this, QNSLOT(NoteEditorWidget,onEditorSpellCheckerReady));
    QObject::connect(m_pUi->noteEditor, QNSIGNAL(NoteEditor,notifyError,QNLocalizedString),
                     this, QNSIGNAL(NoteEditorWidget,notifyError,QNLocalizedString));

    // Connect find and replace widget actions to local slots
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,closed),
                     this, QNSLOT(NoteEditorWidget,onFindAndReplaceWidgetClosed));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,textToFindEdited,const QString&),
                     this, QNSLOT(NoteEditorWidget,onTextToFindInsideNoteEdited,const QString&));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,findNext,const QString&,const bool),
                     this, QNSLOT(NoteEditorWidget,onFindNextInsideNote,const QString&,const bool));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,findPrevious,const QString&,const bool),
                     this, QNSLOT(NoteEditorWidget,onFindPreviousInsideNote,const QString&,const bool));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,searchCaseSensitivityChanged,const bool),
                     this, QNSLOT(NoteEditorWidget,onFindInsideNoteCaseSensitivityChanged,const bool));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,replace,const QString&,const QString&,const bool),
                     this, QNSLOT(NoteEditorWidget,onReplaceInsideNote,const QString&,const QString&,const bool));
    QObject::connect(m_pUi->findAndReplaceWidget, QNSIGNAL(FindAndReplaceWidget,replaceAll,const QString&,const QString&,const bool),
                     this, QNSLOT(NoteEditorWidget,onReplaceAllInsideNote,const QString&,const QString&,const bool));

    // Connect toolbar buttons actions to local slots
    QObject::connect(m_pUi->fontBoldPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextBoldToggled));
    QObject::connect(m_pUi->fontItalicPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextItalicToggled));
    QObject::connect(m_pUi->fontUnderlinePushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextUnderlineToggled));
    QObject::connect(m_pUi->fontStrikethroughPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextStrikethroughToggled));
    QObject::connect(m_pUi->formatJustifyLeftPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignLeftAction));
    QObject::connect(m_pUi->formatJustifyCenterPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignCenterAction));
    QObject::connect(m_pUi->formatJustifyRightPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAlignRightAction));
    QObject::connect(m_pUi->insertHorizontalLinePushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextAddHorizontalLineAction));
    QObject::connect(m_pUi->formatIndentMorePushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextIncreaseIndentationAction));
    QObject::connect(m_pUi->formatIndentLessPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextDecreaseIndentationAction));
    QObject::connect(m_pUi->formatListUnorderedPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsertUnorderedListAction));
    QObject::connect(m_pUi->formatListOrderedPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorTextInsertOrderedListAction));
    QObject::connect(m_pUi->chooseTextColorToolButton, QNSIGNAL(ColorPickerToolButton,colorSelected,QColor),
                     this, QNSLOT(NoteEditorWidget,onEditorChooseTextColor,QColor));
    QObject::connect(m_pUi->chooseBackgroundColorToolButton, QNSIGNAL(ColorPickerToolButton,colorSelected,QColor),
                     this, QNSLOT(NoteEditorWidget,onEditorChooseBackgroundColor,QColor));
    QObject::connect(m_pUi->spellCheckBox, QNSIGNAL(QCheckBox,stateChanged,int),
                     this, QNSLOT(NoteEditorWidget,onEditorSpellCheckStateChanged,int));
    QObject::connect(m_pUi->insertToDoCheckboxPushButton, QNSIGNAL(QPushButton,released),
                     this, QNSLOT(NoteEditorWidget,onEditorInsertToDoCheckBoxAction));
    QObject::connect(m_pUi->insertTableToolButton, QNSIGNAL(InsertTableToolButton,createdTable,int,int,double,bool),
                     this, QNSLOT(NoteEditorWidget,onEditorInsertTable,int,int,double,bool));

    // Connect toolbar button actions to editor slots
    QObject::connect(m_pUi->copyPushButton, QNSIGNAL(QPushButton,clicked),
                     m_pUi->noteEditor, QNSLOT(NoteEditor,copy));
    QObject::connect(m_pUi->cutPushButton, QNSIGNAL(QPushButton,clicked),
                     m_pUi->noteEditor, QNSLOT(NoteEditor,cut));
    QObject::connect(m_pUi->pastePushButton, QNSIGNAL(QPushButton,clicked),
                     m_pUi->noteEditor, QNSLOT(NoteEditor,paste));
    QObject::connect(m_pUi->undoPushButton, QNSIGNAL(QPushButton,clicked),
                     m_pUi->noteEditor, QNSLOT(NoteEditor,undo));
    QObject::connect(m_pUi->redoPushButton, QNSIGNAL(QPushButton,clicked),
                     m_pUi->noteEditor, QNSLOT(NoteEditor,redo));
}

void NoteEditorWidget::clear()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::clear: note ") << (m_pCurrentNote ? m_pCurrentNote->localUid() : QStringLiteral("<null>")));

    m_pCurrentNote.reset(Q_NULLPTR);
    m_pCurrentNotebook.reset(Q_NULLPTR);
    m_pUi->noteEditor->clear();
    m_pUi->tagNameLabelsContainer->clear();

    m_findCurrentNoteRequestId = QUuid();
    m_findCurrentNotebookRequestId = QUuid();
    m_updateNoteRequestIds.clear();

    m_pendingEditorSpellChecker = false;
    m_currentNoteWasExpunged = false;
}

void NoteEditorWidget::checkIconThemeIconsAndSetFallbacks()
{
    QNDEBUG(QStringLiteral("NoteEditorWidget::checkIconThemeIconsAndSetFallbacks"));

    if (!QIcon::hasThemeIcon(QStringLiteral("checkbox"))) {
        m_pUi->insertToDoCheckboxPushButton->setIcon(QIcon(QStringLiteral(":/fallback_icons/png/checkbox-2.png")));
        QNTRACE(QStringLiteral("set fallback checkbox icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("edit-copy"))) {
        QIcon editCopyIcon(QStringLiteral(":/fallback_icons/png/edit-copy-6.png"));
        m_pUi->copyPushButton->setIcon(editCopyIcon);
        QNTRACE(QStringLiteral("set fallback edit-copy icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("edit-cut"))) {
        QIcon editCutIcon(QStringLiteral(":/fallback_icons/png/edit-cut-6.png"));
        m_pUi->cutPushButton->setIcon(editCutIcon);
        QNTRACE(QStringLiteral("set fallback edit-cut icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("edit-paste"))) {
        QIcon editPasteIcon(QStringLiteral(":/fallback_icons/png/edit-paste-6.png"));
        m_pUi->pastePushButton->setIcon(editPasteIcon);
        QNTRACE(QStringLiteral("set fallback edit-paste icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("edit-redo"))) {
        QIcon editRedoIcon(QStringLiteral(":/fallback_icons/png/edit-redo-7.png"));
        m_pUi->redoPushButton->setIcon(editRedoIcon);
        QNTRACE(QStringLiteral("set fallback edit-redo icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("edit-undo"))) {
        QIcon editUndoIcon(QStringLiteral(":/fallback_icons/png/edit-undo-7.png"));
        m_pUi->undoPushButton->setIcon(editUndoIcon);
        QNTRACE(QStringLiteral("set fallback edit-undo icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-indent-less"))) {
        QIcon formatIndentLessIcon(QStringLiteral(":/fallback_icons/png/format-indent-less-5.png"));
        m_pUi->formatIndentLessPushButton->setIcon(formatIndentLessIcon);
        QNTRACE(QStringLiteral("set fallback format-indent-less icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-indent-more"))) {
        QIcon formatIndentMoreIcon(QStringLiteral(":/fallback_icons/png/format-indent-more-5.png"));
        m_pUi->formatIndentMorePushButton->setIcon(formatIndentMoreIcon);
        QNTRACE(QStringLiteral("set fallback format-indent-more icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-justify-center"))) {
        QIcon formatJustifyCenterIcon(QStringLiteral(":/fallback_icons/png/format-justify-center-5.png"));
        m_pUi->formatJustifyCenterPushButton->setIcon(formatJustifyCenterIcon);
        QNTRACE(QStringLiteral("set fallback format-justify-center icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-justify-left"))) {
        QIcon formatJustifyLeftIcon(QStringLiteral(":/fallback_icons/png/format-justify-left-5.png"));
        m_pUi->formatJustifyLeftPushButton->setIcon(formatJustifyLeftIcon);
        QNTRACE(QStringLiteral("set fallback format-justify-left icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-justify-right"))) {
        QIcon formatJustifyRightIcon(QStringLiteral(":/fallback_icons/png/format-justify-right-5.png"));
        m_pUi->formatJustifyRightPushButton->setIcon(formatJustifyRightIcon);
        QNTRACE(QStringLiteral("set fallback format-justify-right icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-list-ordered"))) {
        QIcon formatListOrderedIcon(QStringLiteral(":/fallback_icons/png/format-list-ordered.png"));
        m_pUi->formatListOrderedPushButton->setIcon(formatListOrderedIcon);
        QNTRACE(QStringLiteral("set fallback format-list-ordered icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-list-unordered"))) {
        QIcon formatListUnorderedIcon(QStringLiteral(":/fallback_icons/png/format-list-unordered.png"));
        m_pUi->formatListUnorderedPushButton->setIcon(formatListUnorderedIcon);
        QNTRACE(QStringLiteral("set fallback format-list-unordered icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-text-bold"))) {
        QIcon formatTextBoldIcon(QStringLiteral(":/fallback_icons/png/format-text-bold-4.png"));
        m_pUi->fontBoldPushButton->setIcon(formatTextBoldIcon);
        QNTRACE(QStringLiteral("set fallback format-text-bold icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-text-color"))) {
        QIcon formatTextColorIcon(QStringLiteral(":/fallback_icons/png/format-text-color.png"));
        m_pUi->chooseTextColorToolButton->setIcon(formatTextColorIcon);
        QNTRACE(QStringLiteral("set fallback format-text-color icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("color-fill"))) {
        QIcon colorFillIcon(QStringLiteral(":/fallback_icons/png/color-fill.png"));
        m_pUi->chooseBackgroundColorToolButton->setIcon(colorFillIcon);
        QNTRACE(QStringLiteral("set fallback color-fill icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-text-italic"))) {
        QIcon formatTextItalicIcon(QStringLiteral(":/fallback_icons/png/format-text-italic-4.png"));
        m_pUi->fontItalicPushButton->setIcon(formatTextItalicIcon);
        QNTRACE(QStringLiteral("set fallback format-text-italic icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-text-strikethrough"))) {
        QIcon formatTextStrikethroughIcon(QStringLiteral(":/fallback_icons/png/format-text-strikethrough-3.png"));
        m_pUi->fontStrikethroughPushButton->setIcon(formatTextStrikethroughIcon);
        QNTRACE(QStringLiteral("set fallback format-text-strikethrough icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("format-text-underline"))) {
        QIcon formatTextUnderlineIcon(QStringLiteral(":/fallback_icons/png/format-text-underline-4.png"));
        m_pUi->fontUnderlinePushButton->setIcon(formatTextUnderlineIcon);
        QNTRACE(QStringLiteral("set fallback format-text-underline icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("insert-horizontal-rule"))) {
        QIcon insertHorizontalRuleIcon(QStringLiteral(":/fallback_icons/png/insert-horizontal-rule.png"));
        m_pUi->insertHorizontalLinePushButton->setIcon(insertHorizontalRuleIcon);
        QNTRACE(QStringLiteral("set fallback insert-horizontal-rule icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("insert-table"))) {
        QIcon insertTableIcon(QStringLiteral(":/fallback_icons/png/insert-table.png"));
        m_pUi->insertTableToolButton->setIcon(insertTableIcon);
        QNTRACE(QStringLiteral("set fallback insert-table icon"));
    }

    if (!QIcon::hasThemeIcon(QStringLiteral("tools-check-spelling"))) {
        QIcon spellcheckIcon(QStringLiteral(":/fallback_icons/png/tools-check-spelling-5.png"));
        m_pUi->spellCheckBox->setIcon(spellcheckIcon);
        QNTRACE(QStringLiteral("set fallback tools-check-spelling icon"));
    }
}

void NoteEditorWidget::updateNoteSourceView(const QString & html)
{
    m_pUi->noteSourceView->setPlainText(html);
}

} // namespace quentier
