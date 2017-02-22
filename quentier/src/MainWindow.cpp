/*
 * Copyright 2016 Dmitry Ivanov
 *
 * This file is part of Quentier.
 *
 * Quentier is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Quentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Quentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MainWindow.h"
#include "EditNoteDialogsManager.h"
#include "NoteEditorTabsAndWindowsCoordinator.h"
#include "AccountToKey.h"
#include "NoteFiltersManager.h"
#include "models/NoteFilterModel.h"
#include "color-picker-tool-button/ColorPickerToolButton.h"
#include "insert-table-tool-button/InsertTableToolButton.h"
#include "insert-table-tool-button/TableSettingsDialog.h"
#include "tests/ManualTestingHelper.h"
#include "widgets/FindAndReplaceWidget.h"
#include "delegates/NotebookItemDelegate.h"
#include "delegates/SynchronizableColumnDelegate.h"
#include "delegates/DirtyColumnDelegate.h"
#include "delegates/FavoriteItemDelegate.h"
#include "delegates/FromLinkedNotebookColumnDelegate.h"
#include "delegates/NoteItemDelegate.h"
#include "delegates/TagItemDelegate.h"
#include "delegates/DeletedNoteItemDelegate.h"
#include "dialogs/AddOrEditNotebookDialog.h"
#include "dialogs/AddOrEditTagDialog.h"
#include "dialogs/AddOrEditSavedSearchDialog.h"
#include "models/ColumnChangeRerouter.h"
#include "views/ItemView.h"
#include "views/DeletedNoteItemView.h"
#include "views/NotebookItemView.h"
#include "views/NoteListView.h"
#include "views/TagItemView.h"
#include "views/SavedSearchItemView.h"
#include "views/FavoriteItemView.h"

#include "widgets/TabWidget.h"
using quentier::TabWidget;

#include "widgets/FilterByNotebookWidget.h"
using quentier::FilterByNotebookWidget;

#include "widgets/FilterByTagWidget.h"
using quentier::FilterByTagWidget;

#include "widgets/FilterBySavedSearchWidget.h"
using quentier::FilterBySavedSearchWidget;

#include "widgets/NotebookModelItemInfoWidget.h"
#include "widgets/TagModelItemInfoWidget.h"
#include "widgets/SavedSearchModelItemInfoWidget.h"

#include <quentier/note_editor/NoteEditor.h>
#include "ui_MainWindow.h"
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/DesktopServices.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/logging/QuentierLogger.h>
#include <cmath>
#include <QPushButton>
#include <QIcon>
#include <QLabel>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextList>
#include <QColorDialog>
#include <QFile>
#include <QScopedPointer>
#include <QMessageBox>
#include <QtDebug>
#include <QFontDatabase>
#include <QKeySequence>
#include <QUndoStack>
#include <QCryptographicHash>
#include <QXmlStreamWriter>
#include <QDateTime>
#include <QCheckBox>
#include <QPalette>
#include <QToolTip>
#include <QResizeEvent>
#include <QMenu>

#define NOTIFY_ERROR(error) \
    QNWARNING(error); \
    onSetStatusBarText(error)

#define FILTERS_VIEW_STATUS_KEY QStringLiteral("ViewExpandedStatus")
#define NOTE_SORTING_MODE_KEY QStringLiteral("NoteSortingMode")

using namespace quentier;

MainWindow::MainWindow(QWidget * pParentWidget) :
    QMainWindow(pParentWidget),
    m_pUI(new Ui::MainWindow),
    m_currentStatusBarChildWidget(Q_NULLPTR),
    m_lastNoteEditorHtml(),
    m_nativeIconThemeName(),
    m_pAvailableAccountsActionGroup(new QActionGroup(this)),
    m_pAvailableAccountsSubMenu(Q_NULLPTR),
    m_pAccountManager(new AccountManager(this)),
    m_pAccount(),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_pLocalStorageManager(Q_NULLPTR),
    m_lastLocalStorageSwitchUserRequest(),
    m_pSynchronizationManagerThread(Q_NULLPTR),
    m_pSynchronizationManager(Q_NULLPTR),
    m_synchronizationManagerHost(),
    m_notebookCache(),
    m_tagCache(),
    m_savedSearchCache(),
    m_noteCache(),
    m_pNotebookModel(Q_NULLPTR),
    m_pTagModel(Q_NULLPTR),
    m_pSavedSearchModel(Q_NULLPTR),
    m_pNoteModel(Q_NULLPTR),
    m_pNotebookModelColumnChangeRerouter(new ColumnChangeRerouter(NotebookModel::Columns::NumNotesPerNotebook,
                                                                  NotebookModel::Columns::Name, this)),
    m_pTagModelColumnChangeRerouter(new ColumnChangeRerouter(TagModel::Columns::NumNotesPerTag,
                                                             TagModel::Columns::Name, this)),
    m_pNoteModelColumnChangeRerouter(new ColumnChangeRerouter(NoteModel::Columns::PreviewText,
                                                              NoteModel::Columns::Title, this)),
    m_pDeletedNotesModel(Q_NULLPTR),
    m_pFavoritesModel(Q_NULLPTR),
    m_blankModel(),
    m_pNoteFilterModel(Q_NULLPTR),
    m_pNoteFiltersManager(Q_NULLPTR),
    m_pNoteEditorTabsAndWindowsCoordinator(Q_NULLPTR),
    m_pEditNoteDialogsManager(Q_NULLPTR),
    m_testNotebook(),
    m_testNote(),
    m_pUndoStack(new QUndoStack(this)),
    m_noteSearchQueryValidated(false),
    m_styleSheetInfo(),
    m_currentPanelStyle(),
    m_shortcutManager(this),
    m_filtersViewExpanded(false)
{
    QNTRACE(QStringLiteral("MainWindow constructor"));

    setupThemeIcons();

    m_pUI->setupUi(this);

    if (m_nativeIconThemeName.isEmpty()) {
        m_pUI->ActionIconsNative->setVisible(false);
        m_pUI->ActionIconsNative->setDisabled(true);
    }

    collectBaseStyleSheets();
    setupPanelOverlayStyleSheets();

    m_pAvailableAccountsActionGroup->setExclusive(true);

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    fixupQt4StyleSheets();
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
    m_pUI->searchQueryLineEdit->setClearButtonEnabled(true);
#endif

    setupAccountManager();
    m_pAccount.reset(new Account(m_pAccountManager->currentAccount()));
    setWindowTitleForAccount(*m_pAccount);

    setupLocalStorageManager();
    setupModels();
    setupViews();
    setupNoteFilters();

    setupNoteEditorTabWidgetManager();

    setupShowHideStartupSettings();

    setupDefaultShortcuts();
    setupUserShortcuts();

    addMenuActionsToMainWindow();

    connectActionsToSlots();
    connectViewButtonsToSlots();
    connectNoteSearchActionsToSlots();
    connectToolbarButtonsToSlots();

    // Stuff primarily for manual testing
    QObject::connect(m_pUI->ActionShowNoteSource, QNSIGNAL(QAction, triggered),
                     this, QNSLOT(MainWindow, onShowNoteSource));
    QObject::connect(m_pUI->ActionSetTestNoteWithEncryptedData, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSetTestNoteWithEncryptedData));
    QObject::connect(m_pUI->ActionSetTestNoteWithResources, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSetTestNoteWithResources));
    QObject::connect(m_pUI->ActionSetTestReadOnlyNote, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSetTestReadOnlyNote));
    QObject::connect(m_pUI->ActionSetTestInkNote, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSetInkNote));

    QString consumerKey, consumerSecret;
    setupConsumerKeyAndSecret(consumerKey, consumerSecret);
}

MainWindow::~MainWindow()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
    }

    delete m_pUI;
}

void MainWindow::show()
{
    QWidget::show();

    // sadly, can't get it to work any other way :(
    if (!m_filtersViewExpanded) {
        foldFiltersView();
    }
}

void MainWindow::connectActionsToSlots()
{
    QNDEBUG(QStringLiteral("MainWindow::connectActionsToSlots"));

    QObject::connect(m_pUI->ActionFindInsideNote, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onFindInsideNoteAction));
    QObject::connect(m_pUI->ActionFindNext, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onFindInsideNoteAction));
    QObject::connect(m_pUI->ActionFindPrevious, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onFindPreviousInsideNoteAction));
    QObject::connect(m_pUI->ActionReplaceInNote, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onReplaceInsideNoteAction));

    // Undo/redo actions
    QObject::connect(m_pUI->ActionUndo, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onUndoAction));
    QObject::connect(m_pUI->ActionRedo, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onRedoAction));
    // Copy/cut/paste actions
    QObject::connect(m_pUI->ActionCopy, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onCopyAction));
    QObject::connect(m_pUI->ActionCut, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onCutAction));
    QObject::connect(m_pUI->ActionPaste, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onPasteAction));
    // Select all action
    QObject::connect(m_pUI->ActionSelectAll, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextSelectAllToggled));
    // Font actions
    QObject::connect(m_pUI->ActionFontBold, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextBoldToggled));
    QObject::connect(m_pUI->ActionFontItalic, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextItalicToggled));
    QObject::connect(m_pUI->ActionFontUnderlined, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextUnderlineToggled));
    QObject::connect(m_pUI->ActionFontStrikethrough, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextStrikethroughToggled));
    QObject::connect(m_pUI->ActionIncreaseFontSize, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextIncreaseFontSizeAction));
    QObject::connect(m_pUI->ActionDecreaseFontSize, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextDecreaseFontSizeAction));
    QObject::connect(m_pUI->ActionFontHighlight, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextHighlightAction));
    // Spell checking
    QObject::connect(m_pUI->ActionSpellCheck, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextSpellCheckToggled));
    // Text format actions
    QObject::connect(m_pUI->ActionAlignLeft, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextAlignLeftAction));
    QObject::connect(m_pUI->ActionAlignCenter, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextAlignCenterAction));
    QObject::connect(m_pUI->ActionAlignRight, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextAlignRightAction));
    QObject::connect(m_pUI->ActionAlignFull, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextAlignFullAction));
    QObject::connect(m_pUI->ActionInsertHorizontalLine, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextAddHorizontalLineAction));
    QObject::connect(m_pUI->ActionIncreaseIndentation, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextIncreaseIndentationAction));
    QObject::connect(m_pUI->ActionDecreaseIndentation, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextDecreaseIndentationAction));
    QObject::connect(m_pUI->ActionInsertBulletedList, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextInsertUnorderedListAction));
    QObject::connect(m_pUI->ActionInsertNumberedList, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextInsertOrderedListAction));
    QObject::connect(m_pUI->ActionInsertTable, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextInsertTableDialogAction));
    QObject::connect(m_pUI->ActionEditHyperlink, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextEditHyperlinkAction));
    QObject::connect(m_pUI->ActionCopyHyperlink, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextCopyHyperlinkAction));
    QObject::connect(m_pUI->ActionRemoveHyperlink, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onNoteTextRemoveHyperlinkAction));
    QObject::connect(m_pUI->ActionSaveNote, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSaveNoteAction));
    // Toggle view actions
    QObject::connect(m_pUI->ActionShowSidePanel, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowSidePanelActionToggled,bool));
    QObject::connect(m_pUI->ActionShowFavorites, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowFavoritesActionToggled,bool));
    QObject::connect(m_pUI->ActionShowNotebooks, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowNotebooksActionToggled,bool));
    QObject::connect(m_pUI->ActionShowTags, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowTagsActionToggled,bool));
    QObject::connect(m_pUI->ActionShowSavedSearches, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowSavedSearchesActionToggled,bool));
    QObject::connect(m_pUI->ActionShowDeletedNotes, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowDeletedNotesActionToggled,bool));
    QObject::connect(m_pUI->ActionShowNotesList, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowNoteListActionToggled,bool));
    QObject::connect(m_pUI->ActionShowToolbar, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowToolbarActionToggled,bool));
    QObject::connect(m_pUI->ActionShowStatusBar, QNSIGNAL(QAction,toggled,bool),
                     this, QNSLOT(MainWindow,onShowStatusBarActionToggled,bool));
    // Look and feel actions
    QObject::connect(m_pUI->ActionIconsNative, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchIconsToNativeAction));
    QObject::connect(m_pUI->ActionIconsOxygen, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchIconsToOxygenAction));
    QObject::connect(m_pUI->ActionIconsTango, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchIconsToTangoAction));
    QObject::connect(m_pUI->ActionPanelStyleBuiltIn, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchPanelStyleToBuiltIn));
    QObject::connect(m_pUI->ActionPanelStyleLighter, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchPanelStyleToLighter));
    QObject::connect(m_pUI->ActionPanelStyleDarker, QNSIGNAL(QAction,triggered),
                     this, QNSLOT(MainWindow,onSwitchPanelStyleToDarker));
}

void MainWindow::connectViewButtonsToSlots()
{
    QNDEBUG(QStringLiteral("MainWindow::connectViewButtonsToSlots"));

    QObject::connect(m_pUI->addNotebookButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onCreateNotebookButtonPressed));
    QObject::connect(m_pUI->removeNotebookButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onRemoveNotebookButtonPressed));
    QObject::connect(m_pUI->notebookInfoButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onNotebookInfoButtonPressed));

    QObject::connect(m_pUI->addTagButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onCreateTagButtonPressed));
    QObject::connect(m_pUI->removeTagButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onRemoveTagButtonPressed));
    QObject::connect(m_pUI->tagInfoButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onTagInfoButtonPressed));

    QObject::connect(m_pUI->addSavedSearchButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onCreateSavedSearchButtonPressed));
    QObject::connect(m_pUI->removeSavedSearchButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onRemoveSavedSearchButtonPressed));
    QObject::connect(m_pUI->savedSearchInfoButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onSavedSearchInfoButtonPressed));

    QObject::connect(m_pUI->unfavoritePushButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onUnfavoriteItemButtonPressed));
    QObject::connect(m_pUI->favoriteInfoButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onFavoritedItemInfoButtonPressed));

    QObject::connect(m_pUI->restoreDeletedNoteButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onRestoreDeletedNoteButtonPressed));
    QObject::connect(m_pUI->eraseDeletedNoteButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onDeleteNotePermanentlyButtonPressed));
    QObject::connect(m_pUI->deletedNoteInfoButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onDeletedNoteInfoButtonPressed));

    QObject::connect(m_pUI->filtersViewTogglePushButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onFiltersViewTogglePushButtonPressed));
}

void MainWindow::connectNoteSearchActionsToSlots()
{
    QNDEBUG(QStringLiteral("MainWindow::connectNoteSearchActionsToSlots"));

    QObject::connect(m_pUI->searchQueryLineEdit, QNSIGNAL(QLineEdit,textChanged,const QString&),
                     this, QNSLOT(MainWindow,onNoteSearchQueryChanged,const QString&));
    QObject::connect(m_pUI->searchQueryLineEdit, QNSIGNAL(QLineEdit,returnPressed),
                     this, QNSLOT(MainWindow,onNoteSearchQueryReady));
    QObject::connect(m_pUI->saveSearchPushButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onSaveNoteSearchQueryButtonPressed));
}

void MainWindow::connectToolbarButtonsToSlots()
{
    QNDEBUG(QStringLiteral("MainWindow::connectToolbarButtonsToSlots"));

    QObject::connect(m_pUI->addNotePushButton, QNSIGNAL(QPushButton,clicked),
                     this, QNSLOT(MainWindow,onNewNoteButtonPressed));
}

void MainWindow::addMenuActionsToMainWindow()
{
    QNDEBUG(QStringLiteral("MainWindow::addMenuActionsToMainWindow"));

    // NOTE: adding the actions from the menu bar's menus is required for getting the shortcuts
    // of these actions to work properly; action shortcuts only fire when the menu is shown
    // which is not really the purpose behind those shortcuts
    QList<QMenu*> menus = m_pUI->menuBar->findChildren<QMenu*>();
    const int numMenus = menus.size();
    for(int i = 0; i < numMenus; ++i)
    {
        QMenu * menu = menus[i];

        QList<QAction*> actions = menu->actions();
        const int numActions = actions.size();
        for(int j = 0; j < numActions; ++j) {
            QAction * action = actions[j];
            addAction(action);
        }
    }

    m_pAvailableAccountsSubMenu = new QMenu(tr("Switch account"));
    QAction * separatorAction = m_pUI->menuFile->insertSeparator(m_pUI->ActionQuit);

    QAction * switchAccountSubMenuAction =
            m_pUI->menuFile->insertMenu(separatorAction, m_pAvailableAccountsSubMenu);
    Q_UNUSED(m_pUI->menuFile->insertSeparator(switchAccountSubMenuAction));

    updateSubMenuWithAvailableAccounts();
}

void MainWindow::updateSubMenuWithAvailableAccounts()
{
    QNDEBUG(QStringLiteral("MainWindow::updateSubMenuWithAvailableAccounts"));

    if (Q_UNLIKELY(!m_pAvailableAccountsSubMenu)) {
        QNDEBUG(QStringLiteral("No available accounts sub-menu"));
        return;
    }

    delete m_pAvailableAccountsActionGroup;
    m_pAvailableAccountsActionGroup = new QActionGroup(this);
    m_pAvailableAccountsActionGroup->setExclusive(true);

    m_pAvailableAccountsSubMenu->clear();

    const QVector<Account> & availableAccounts = m_pAccountManager->availableAccounts();

    int numAvailableAccounts = availableAccounts.size();
    for(int i = 0; i < numAvailableAccounts; ++i)
    {
        const Account & availableAccount = availableAccounts[i];
        QString availableAccountRepresentationName = availableAccount.name();
        if (availableAccount.type() == Account::Type::Local) {
            availableAccountRepresentationName += QStringLiteral(" (");
            availableAccountRepresentationName += tr("local");
            availableAccountRepresentationName += QStringLiteral(")");
        }

        QAction * pAccountAction = new QAction(availableAccountRepresentationName, Q_NULLPTR);
        m_pAvailableAccountsSubMenu->addAction(pAccountAction);

        pAccountAction->setData(i);
        pAccountAction->setCheckable(true);

        if (!m_pAccount.isNull() && (*m_pAccount == availableAccount)) {
            pAccountAction->setChecked(true);
        }

        addAction(pAccountAction);
        QObject::connect(pAccountAction, QNSIGNAL(QAction,toggled,bool),
                         this, QNSLOT(MainWindow,onSwitchAccountActionToggled,bool));

        m_pAvailableAccountsActionGroup->addAction(pAccountAction);
    }

    if (Q_LIKELY(numAvailableAccounts != 0)) {
        Q_UNUSED(m_pAvailableAccountsSubMenu->addSeparator())
    }

    QAction * pAddAccountAction = m_pAvailableAccountsSubMenu->addAction(tr("Add account"));
    addAction(pAddAccountAction);
    QObject::connect(pAddAccountAction, QNSIGNAL(QAction,triggered,bool),
                     this, QNSLOT(MainWindow,onAddAccountActionTriggered,bool));

    QAction * pManageAccountsAction = m_pAvailableAccountsSubMenu->addAction(tr("Manage accounts"));
    addAction(pManageAccountsAction);
    QObject::connect(pManageAccountsAction, QNSIGNAL(QAction,triggered,bool),
                     this, QNSLOT(MainWindow,onManageAccountsActionTriggered,bool));
}

void MainWindow::setWindowTitleForAccount(const Account & account)
{
    QNDEBUG(QStringLiteral("MainWindow::setWindowTitleForAccount: ") << account);

    QString username = account.name();
    QString displayName = account.displayName();

    QString title = qApp->applicationName() + QStringLiteral(": ");
    if (!displayName.isEmpty()) {
        title += displayName;
        title += QStringLiteral(" (");
        title += username;
        title += QStringLiteral(")");
    }
    else {
        title += username;
    }

    setWindowTitle(title);
}

NoteEditorWidget * MainWindow::currentNoteEditor()
{
    QNDEBUG(QStringLiteral("MainWindow::currentNoteEditor"));

    if (Q_UNLIKELY(m_pUI->noteEditorsTabWidget->count() == 0)) {
        QNTRACE(QStringLiteral("No open note editors"));
        return Q_NULLPTR;
    }

    int currentIndex = m_pUI->noteEditorsTabWidget->currentIndex();
    if (Q_UNLIKELY(currentIndex < 0)) {
        QNTRACE(QStringLiteral("No current note editor"));
        return Q_NULLPTR;
    }

    QWidget * currentWidget = m_pUI->noteEditorsTabWidget->widget(currentIndex);
    if (Q_UNLIKELY(!currentWidget)) {
        QNTRACE(QStringLiteral("No current widget"));
        return Q_NULLPTR;
    }

    NoteEditorWidget * noteEditorWidget = qobject_cast<NoteEditorWidget*>(currentWidget);
    if (Q_UNLIKELY(!noteEditorWidget)) {
        QNWARNING(QStringLiteral("Can't cast current note tag widget's widget to note editor widget"));
        return Q_NULLPTR;
    }

    return noteEditorWidget;
}

void MainWindow::connectSynchronizationManager()
{
    QNDEBUG(QStringLiteral("MainWindow::connectSynchronizationManager"));

    if (Q_UNLIKELY(!m_pSynchronizationManager)) {
        QNDEBUG(QStringLiteral("No synchronization manager"));
        return;
    }

    // Connect local signals to SynchronizationManager slots
    QObject::connect(this, QNSIGNAL(MainWindow,authenticate),
                     m_pSynchronizationManager, QNSLOT(SynchronizationManager,authenticate));

    // Connect SynchronizationManager signals to local slots
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,failed,ErrorString),
                     this, QNSLOT(MainWindow,onSynchronizationManagerFailure,ErrorString));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,finished,Account),
                     this, QNSLOT(MainWindow,onSynchronizationFinished,Account));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,authenticationFinished,bool,ErrorString,Account),
                     this, QNSLOT(MainWindow,onAuthenticationFinished,bool,ErrorString,Account));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,authenticationRevoked,bool,ErrorString,qevercloud::UserID),
                     this, QNSLOT(MainWindow,onAuthenticationRevoked,bool,ErrorString,qevercloud::UserID));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,remoteToLocalSyncStopped),
                     this, QNSLOT(MainWindow,onRemoteToLocalSyncStopped));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,sendLocalChangesStopped),
                     this, QNSLOT(MainWindow,onSendLocalChangesStopped));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,rateLimitExceeded,qint32),
                     this, QNSLOT(MainWindow,onRateLimitExceeded,qint32));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,remoteToLocalSyncDone),
                     this, QNSLOT(MainWindow,onRemoteToLocalSyncDone));
    QObject::connect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,progress,QString,double),
                     this, QNSLOT(MainWindow,onSynchronizationProgressUpdate,QString,double));
}

void MainWindow::disconnectSynchronizationManager()
{
    QNDEBUG(QStringLiteral("MainWindow::disconnectSynchronizationManager"));

    if (Q_UNLIKELY(!m_pSynchronizationManager)) {
        QNDEBUG(QStringLiteral("No synchronization manager"));
        return;
    }

    // Disconnect local signals from SynchronizationManager slots
    QObject::disconnect(this, QNSIGNAL(MainWindow,authenticate),
                        m_pSynchronizationManager, QNSLOT(SynchronizationManager,authenticate));

    // Disonnect SynchronizationManager signals from local slots
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,failed,ErrorString),
                        this, QNSLOT(MainWindow,onSynchronizationManagerFailure,ErrorString));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,finished,Account),
                        this, QNSLOT(MainWindow,onSynchronizationFinished,Account));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,authenticationFinished,bool,ErrorString,Account),
                        this, QNSLOT(MainWindow,onAuthenticationFinished,bool,ErrorString,Account));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,authenticationRevoked,bool,ErrorString,qevercloud::UserID),
                        this, QNSLOT(MainWindow,onAuthenticationRevoked,bool,ErrorString,qevercloud::UserID));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,remoteToLocalSyncStopped),
                        this, QNSLOT(MainWindow,onRemoteToLocalSyncStopped));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,sendLocalChangesStopped),
                        this, QNSLOT(MainWindow,onSendLocalChangesStopped));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,rateLimitExceeded,qint32),
                        this, QNSLOT(MainWindow,onRateLimitExceeded,qint32));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,remoteToLocalSyncDone),
                        this, QNSLOT(MainWindow,onRemoteToLocalSyncDone));
    QObject::disconnect(m_pSynchronizationManager, QNSIGNAL(SynchronizationManager,progress,QString,double),
                        this, QNSLOT(MainWindow,onSynchronizationProgressUpdate,QString,double));
}

void MainWindow::onSyncStopped()
{
    onSetStatusBarText(tr("Synchronization was stopped"));
}

bool MainWindow::checkNoteSearchQuery(const QString & noteSearchQuery)
{
    QNDEBUG(QStringLiteral("MainWindow::checkNoteSearchQuery: ") << noteSearchQuery);

    if (m_noteSearchQueryValidated) {
        QNTRACE(QStringLiteral("Note search query validated"));
        return true;
    }

    NoteSearchQuery query;
    ErrorString errorDescription;
    bool res = query.setQueryString(noteSearchQuery, errorDescription);
    if (!res) {
        QToolTip::showText(m_pUI->searchQueryLineEdit->mapToGlobal(QPoint(0, m_pUI->searchQueryLineEdit->height())),
                           errorDescription.localizedString(), m_pUI->searchQueryLineEdit);
        m_noteSearchQueryValidated = false;
        return false;
    }

    m_noteSearchQueryValidated = true;
    return true;
}

void MainWindow::prepareTestNoteWithResources()
{
    QNDEBUG(QStringLiteral("MainWindow::prepareTestNoteWithResources"));

    m_testNote = Note();

    QString noteContent = test::ManualTestingHelper::noteContentWithResources();
    m_testNote.setContent(noteContent);

    // Read the first result from qrc
    QFile resourceFile(QStringLiteral(":/test_notes/Architecture_whats_the_architecture.png"));
    Q_UNUSED(resourceFile.open(QIODevice::ReadOnly))
    QByteArray resourceData = resourceFile.readAll();
    resourceFile.close();

    // Assemble the first resource data
    quentier::Resource resource;
    resource.setLocalUid(QStringLiteral("{e2f201df-8718-499b-ac92-4c9970170cba}"));
    resource.setNoteLocalUid(m_testNote.localUid());
    resource.setDataBody(resourceData);
    resource.setDataSize(resourceData.size());
    resource.setDataHash(QCryptographicHash::hash(resource.dataBody(), QCryptographicHash::Md5));
    resource.setNoteLocalUid(m_testNote.localUid());
    resource.setMime(QStringLiteral("image/png"));

    qevercloud::ResourceAttributes resourceAttributes;
    resourceAttributes.fileName = QStringLiteral("Architecture_whats_the_architecture.png");

    resource.setResourceAttributes(resourceAttributes);

    // Gather the recognition data as well
    resourceFile.setFileName(QStringLiteral(":/test_notes/Architecture_whats_the_architecture_recognition_data.xml"));
    resourceFile.open(QIODevice::ReadOnly);
    QByteArray resourceRecognitionData = resourceFile.readAll();
    resourceFile.close();

    resource.setRecognitionDataBody(resourceRecognitionData);
    resource.setRecognitionDataSize(resourceRecognitionData.size());
    resource.setRecognitionDataHash(QCryptographicHash::hash(resourceRecognitionData, QCryptographicHash::Md5).toHex());

    // Add the first resource to the note
    m_testNote.addResource(resource);

    // Read the second resource from qrc
    resourceFile.setFileName(QStringLiteral(":/test_notes/cDock_v8.2.zip"));
    resourceFile.open(QIODevice::ReadOnly);
    resourceData = resourceFile.readAll();
    resourceFile.close();

    // Assemble the second resource data
    resource = quentier::Resource();
    resource.setLocalUid(QStringLiteral("{c3acdcba-d6a4-407d-a85f-5fc3c15126df}"));
    resource.setNoteLocalUid(m_testNote.localUid());
    resource.setDataBody(resourceData);
    resource.setDataSize(resourceData.size());
    resource.setDataHash(QCryptographicHash::hash(resource.dataBody(), QCryptographicHash::Md5));
    resource.setMime(QStringLiteral("application/zip"));

    resourceAttributes = qevercloud::ResourceAttributes();
    resourceAttributes.fileName = QStringLiteral("cDock_v8.2.zip");

    resource.setResourceAttributes(resourceAttributes);

    // Add the second resource to the note
    m_testNote.addResource(resource);

    // Read the third resource from qrc
    resourceFile.setFileName(QStringLiteral(":/test_notes/GrowlNotify.pkg"));
    resourceFile.open(QIODevice::ReadOnly);
    resourceData = resourceFile.readAll();
    resourceFile.close();

    // Assemble the third resource data
    resource = quentier::Resource();
    resource.setLocalUid(QStringLiteral("{d44d85f4-d4e2-4788-a172-4d477741b233}"));
    resource.setNoteLocalUid(m_testNote.localUid());
    resource.setDataBody(resourceData);
    resource.setDataSize(resourceData.size());
    resource.setDataHash(QCryptographicHash::hash(resource.dataBody(), QCryptographicHash::Md5));
    resource.setMime(QStringLiteral("application/octet-stream"));

    resourceAttributes = qevercloud::ResourceAttributes();
    resourceAttributes.fileName = QStringLiteral("GrowlNotify.pkg");

    resource.setResourceAttributes(resourceAttributes);

    // Add the third resource to the note
    m_testNote.addResource(resource);
}

void MainWindow::prepareTestInkNote()
{
    QNDEBUG(QStringLiteral("MainWindow::prepareTestInkNote"));

    m_testNote = Note();
    m_testNote.setGuid(QStringLiteral("a458d0ac-5c0b-446d-bc39-5b069148f66a"));

    // Read the ink note image data from qrc
    QFile inkNoteImageQrc(QStringLiteral(":/test_notes/inknoteimage.png"));
    Q_UNUSED(inkNoteImageQrc.open(QIODevice::ReadOnly))
    QByteArray inkNoteImageData = inkNoteImageQrc.readAll();
    inkNoteImageQrc.close();

    quentier::Resource resource;
    resource.setGuid(QStringLiteral("6bdf808c-7bd9-4a39-bef8-20b84779956e"));
    resource.setDataBody(QByteArray("aaa"));
    resource.setDataHash(QByteArray("2e0f79af4ca47b473e5105156a18c7cb"));
    resource.setMime(QStringLiteral("application/vnd.evernote.ink"));
    resource.setHeight(308);
    resource.setWidth(602);
    resource.setNoteGuid(m_testNote.guid());

    m_testNote.addResource(resource);

    QString inkNoteImageFilePath = quentier::applicationPersistentStoragePath() + QStringLiteral("/NoteEditorPage/inkNoteImages");
    QDir inkNoteImageFileDir(inkNoteImageFilePath);
    if (!inkNoteImageFileDir.exists())
    {
        if (Q_UNLIKELY(!inkNoteImageFileDir.mkpath(inkNoteImageFilePath))) {
            onSetStatusBarText(QStringLiteral("Can't set test ink note to the editor: can't create folder to hold the ink note resource images"));
            return;
        }
    }

    inkNoteImageFilePath += QStringLiteral("/") + resource.guid() + QStringLiteral(".png");
    QFile inkNoteImageFile(inkNoteImageFilePath);
    if (Q_UNLIKELY(!inkNoteImageFile.open(QIODevice::WriteOnly))) {
        onSetStatusBarText(QStringLiteral("Can't set test ink note to the editor: can't open file meant to hold the ink note resource image for writing"));
        return;
    }

    inkNoteImageFile.write(inkNoteImageData);
    inkNoteImageFile.close();
}

void MainWindow::persistChosenIconTheme(const QString & iconThemeName)
{
    QNDEBUG(QStringLiteral("MainWindow::persistChosenIconTheme: ") << iconThemeName);

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("LookAndFeel"));
    appSettings.setValue(QStringLiteral("iconTheme"), iconThemeName);
    appSettings.endGroup();
}

void MainWindow::refreshChildWidgetsThemeIcons()
{
    QNDEBUG(QStringLiteral("MainWindow::refreshChildWidgetsThemeIcons"));

    refreshThemeIcons<QAction>();
    refreshThemeIcons<QPushButton>();
    refreshThemeIcons<QCheckBox>();
    refreshThemeIcons<ColorPickerToolButton>();
    refreshThemeIcons<InsertTableToolButton>();
}

void MainWindow::showHideViewColumnsForAccountType(const Account::Type::type accountType)
{
    QNDEBUG(QStringLiteral("MainWindow::showHideViewColumnsForAccountType: ") << accountType);

    bool isLocal = (accountType == Account::Type::Local);

    NotebookItemView * notebooksTreeView = m_pUI->notebooksTreeView;
    notebooksTreeView->setColumnHidden(NotebookModel::Columns::Published, isLocal);
    notebooksTreeView->setColumnHidden(NotebookModel::Columns::FromLinkedNotebook, isLocal);
    notebooksTreeView->setColumnHidden(NotebookModel::Columns::Dirty, isLocal);

    ItemView * tagsTreeView = m_pUI->tagsTreeView;
    tagsTreeView->setColumnHidden(TagModel::Columns::FromLinkedNotebook, isLocal);
    tagsTreeView->setColumnHidden(TagModel::Columns::Dirty, isLocal);

    SavedSearchItemView * savedSearchesTableView = m_pUI->savedSearchesTableView;
    savedSearchesTableView->setColumnHidden(SavedSearchModel::Columns::Dirty, isLocal);

    DeletedNoteItemView * deletedNotesTableView = m_pUI->deletedNotesTableView;
    deletedNotesTableView->setColumnHidden(NoteModel::Columns::Dirty, isLocal);
}

void MainWindow::expandFiltersView()
{
    QNDEBUG(QStringLiteral("MainWindow::expandFiltersView"));

    m_pUI->filtersViewTogglePushButton->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    m_pUI->filterBodyFrame->show();
    m_pUI->filterFrameBottomBoundary->hide();
    m_pUI->filterFrame->adjustSize();
}

void MainWindow::foldFiltersView()
{
    QNDEBUG(QStringLiteral("MainWindow::foldFiltersView"));

    m_pUI->filtersViewTogglePushButton->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    m_pUI->filterBodyFrame->hide();
    m_pUI->filterFrameBottomBoundary->show();

    adjustNoteListAndFiltersSplitterSizes();
}

void MainWindow::adjustNoteListAndFiltersSplitterSizes()
{
    QNDEBUG(QStringLiteral("MainWindow::adjustNoteListAndFiltersSplitterSizes"));

    QList<int> splitterSizes = m_pUI->noteListAndFiltersSplitter->sizes();
    int count = splitterSizes.count();
    if (Q_UNLIKELY(count != 2)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't properly resize "
                                            "the splitter after folding the filter view: "
                                            "wrong number of sizes within the splitter"));
        QNWARNING(error << QStringLiteral("Sizes count: ") << count);
        onSetStatusBarText(error.localizedString());
        return;
    }

    int filterHeaderPanelHeight = m_pUI->noteFilterHeaderPanel->height();
    int heightDiff = std::max(splitterSizes[0] - filterHeaderPanelHeight, 0);
    splitterSizes[0] = filterHeaderPanelHeight;
    splitterSizes[1] = splitterSizes[1] + heightDiff;
    m_pUI->noteListAndFiltersSplitter->setSizes(splitterSizes);

    // Need to schedule the repaint because otherwise the actions above
    // seem to have no effect
    m_pUI->noteListAndFiltersSplitter->update();
}

void MainWindow::collectBaseStyleSheets()
{
    QNDEBUG(QStringLiteral("MainWindow::collectBaseStyleSheets"));

    QList<QWidget*> childWidgets = findChildren<QWidget*>();
    for(auto it = childWidgets.constBegin(), end = childWidgets.constEnd(); it != end; ++it)
    {
        QWidget * widget = *it;
        if (Q_UNLIKELY(!widget)) {
            continue;
        }

        QString styleSheet = widget->styleSheet();
        if (styleSheet.isEmpty()) {
            continue;
        }

        StyleSheetInfo & info = m_styleSheetInfo[widget];
        info.m_baseStyleSheet = styleSheet;
        info.m_targetWidget = QPointer<QWidget>(widget);
    }
}

void MainWindow::setupPanelOverlayStyleSheets()
{
    QNDEBUG(QStringLiteral("MainWindow::setupPanelOverlayStyleSheets"));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("LookAndFeel"));
    QString panelStyle = appSettings.value(QStringLiteral("panelStyle")).toString();
    appSettings.endGroup();

    if (panelStyle.isEmpty()) {
        QNDEBUG(QStringLiteral("No last chosen panel style"));
        return;
    }

    StyleSheetProperties properties;
    getPanelStyleSheetProperties(panelStyle, properties);
    if (Q_UNLIKELY(properties.isEmpty())) {
        return;
    }

    m_currentPanelStyle = panelStyle;

    setPanelsOverlayStyleSheet(properties);
}

void MainWindow::getPanelStyleSheetProperties(const QString & panelStyleOption,
                                              StyleSheetProperties & properties) const
{
    properties.clear();
    properties.reserve(4);

    QString backgroundColorName, colorName, backgroundColorButtonHoverName,
            backgroundColorButtonPressedName;

    if (panelStyleOption == QStringLiteral("Lighter"))
    {
        QColor backgroundColor = palette().color(QPalette::Light);
        backgroundColorName = backgroundColor.name();

        QColor color = palette().color(QPalette::WindowText).name();
        colorName = color.name();

        QColor backgroundColorButtonHover = backgroundColor.darker(200);
        backgroundColorButtonHoverName = backgroundColorButtonHover.name();

        QColor backgroundColorButtonPressed = backgroundColor.lighter(150);
        backgroundColorButtonPressedName = backgroundColorButtonPressed.name();
    }
    else if (panelStyleOption == QStringLiteral("Darker"))
    {
        QColor backgroundColor = palette().color(QPalette::Dark);
        backgroundColorName = backgroundColor.name();

        QColor color = palette().color(QPalette::BrightText);
        colorName = color.name();

        QColor backgroundColorButtonHover = backgroundColor.lighter(150);
        backgroundColorButtonHoverName = backgroundColorButtonHover.name();

        QColor backgroundColorButtonPressed = backgroundColor.darker(200);
        backgroundColorButtonPressedName = backgroundColorButtonPressed.name();
    }
    else
    {
        QNDEBUG(QStringLiteral("Unidentified panel style name: ") << panelStyleOption);
        return;
    }

    properties.push_back(StyleSheetProperty(StyleSheetProperty::Target::None,
                                            "background-color", backgroundColorName));
    properties.push_back(StyleSheetProperty(StyleSheetProperty::Target::None,
                                            "color", colorName));
    properties.push_back(StyleSheetProperty(StyleSheetProperty::Target::ButtonHover,
                                            "background-color", backgroundColorButtonHoverName));
    properties.push_back(StyleSheetProperty(StyleSheetProperty::Target::ButtonPressed,
                                            "background-color", backgroundColorButtonPressedName));

    if (QuentierIsLogLevelActive(LogLevel::TraceLevel))
    {
        QNTRACE("Computed stylesheet properties: ");
        for(auto it = properties.constBegin(), end = properties.constEnd(); it != end; ++it)
        {
            const StyleSheetProperty & property = *it;
            QNTRACE("Property: target = " << property.m_targetType << ", property name = " << property.m_name
                    << ", property value = " << property.m_value);
        }
    }

    return;
}

void MainWindow::setPanelsOverlayStyleSheet(const StyleSheetProperties & properties)
{
    for(auto it = m_styleSheetInfo.begin(); it != m_styleSheetInfo.end(); )
    {
        StyleSheetInfo & info = it.value();

        const QPointer<QWidget> & widget = info.m_targetWidget;
        if (Q_UNLIKELY(widget.isNull())) {
            it = m_styleSheetInfo.erase(it);
            continue;
        }

        ++it;

        QString widgetName = widget->objectName();
        if (!widgetName.contains(QStringLiteral("Panel"))) {
            continue;
        }

        if (properties.isEmpty()) {
            widget->setStyleSheet(info.m_baseStyleSheet);
            continue;
        }

        QString overlayStyleSheet = alterStyleSheet(info.m_baseStyleSheet, properties);
        if (Q_UNLIKELY(overlayStyleSheet.isEmpty())) {
            widget->setStyleSheet(info.m_baseStyleSheet);
            continue;
        }

        widget->setStyleSheet(overlayStyleSheet);
    }
}

QString MainWindow::alterStyleSheet(const QString & originalStyleSheet,
                                    const StyleSheetProperties & properties)
{
    QNDEBUG(QStringLiteral("MainWindow::alterStyleSheet: original stylesheet = ")
            << originalStyleSheet);

    QString result = originalStyleSheet;

#define REPORT_ERROR() \
    ErrorString errorDescription(QT_TRANSLATE_NOOP("", "Can't alter the stylesheet: stylesheet parsing failed")); \
    QNINFO(errorDescription << QStringLiteral(", original stylesheet: ") \
           << originalStyleSheet << QStringLiteral(", stylesheet modified so far: ") \
           << result << QStringLiteral(", property index = ") \
           << propertyIndex); \
    onSetStatusBarText(errorDescription.localizedString())

    for(auto it = properties.constBegin(), end = properties.constEnd(); it != end; ++it)
    {
        const StyleSheetProperty & property = *it;

        QString propertyName = QString::fromUtf8(property.m_name);

        int propertyIndex = -1;
        while(true)
        {
            if (propertyIndex >= result.size()) {
                break;
            }

            propertyIndex = result.indexOf(propertyName, propertyIndex + 1);
            if (propertyIndex < 0) {
                break;
            }

            if (propertyIndex > 0)
            {
                // Check that what we've found is the start of a word
                QChar previousChar = result[propertyIndex - 1];
                QString previousCharString(previousChar);
                if (!previousCharString.trimmed().isEmpty()) {
                    continue;
                }
            }

            bool error = false;
            bool insideHover = isInsideStyleBlock(result, QStringLiteral(":hover:!pressed"), propertyIndex, error);
            if (Q_UNLIKELY(error)) {
                REPORT_ERROR();
                return QString();
            }

            error = false;
            bool insidePressed = isInsideStyleBlock(result, QStringLiteral(":pressed"), propertyIndex, error);
            if (Q_UNLIKELY(error)) {
                REPORT_ERROR();
                return QString();
            }

            error = false;
            bool insidePseudoButtonHover = isInsideStyleBlock(result, QStringLiteral("pseudoPushButton:hover:!pressed"),
                                                              propertyIndex, error);
            if (Q_UNLIKELY(error)) {
                REPORT_ERROR();
                return QString();
            }

            error = false;
            bool insidePseudoButtonPressed = isInsideStyleBlock(result, QStringLiteral("pseudoPushButton:hover:pressed"),
                                                                propertyIndex, error);
            if (Q_UNLIKELY(error)) {
                REPORT_ERROR();
                return QString();
            }

            int propertyEndIndex = result.indexOf(QStringLiteral(";"), propertyIndex + 1);
            if (Q_UNLIKELY(propertyEndIndex < 0)) {
                ErrorString error(QT_TRANSLATE_NOOP("", "Can't alter the stylesheet: stylesheet parsing failed"));
                QNINFO(error << QStringLiteral(", original stylesheet: ")
                       << originalStyleSheet << QStringLiteral(", stylesheet modified so far: ")
                       << result << QStringLiteral(", property index = ")
                       << propertyIndex);
                onSetStatusBarText(error.localizedString());
                return QString();
            }

            QString replacement = propertyName + QStringLiteral(": ") + property.m_value;

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#define REPLACE() \
            QNDEBUG(QStringLiteral("Replacing substring ") \
                    << result.midRef(propertyIndex, propertyEndIndex - propertyIndex) \
                    << QStringLiteral(" with ") << replacement); \
            result.replace(propertyIndex, propertyEndIndex - propertyIndex, replacement); \
            QNDEBUG(QStringLiteral("Stylesheet after the replacement: ") << result)
#else
#define REPLACE() \
            QNDEBUG(QStringLiteral("Replacing substring ") \
                    << result.mid(propertyIndex, propertyEndIndex - propertyIndex) \
                    << QStringLiteral(" with ") << replacement); \
            result.replace(propertyIndex, propertyEndIndex - propertyIndex, replacement); \
            QNDEBUG(QStringLiteral("Stylesheet after the replacement: ") << result)
#endif

            if (property.m_targetType == StyleSheetProperty::Target::None)
            {
                if (insideHover && !insidePseudoButtonHover) {
                    continue;
                }

                if (insidePressed && !insidePseudoButtonPressed) {
                    continue;
                }

                REPLACE();
            }
            else if (property.m_targetType == StyleSheetProperty::Target::ButtonHover)
            {
                if (insidePressed) {
                    continue;
                }

                if (!insideHover || insidePseudoButtonHover) {
                    continue;
                }

                REPLACE();
            }
            else if (property.m_targetType == StyleSheetProperty::Target::ButtonPressed)
            {
                if (!insidePressed || insidePseudoButtonPressed) {
                    continue;
                }

                REPLACE();
            }
        }
    }

    QNDEBUG(QStringLiteral("Altered stylesheet: ") << result);
    return result;
}

bool MainWindow::isInsideStyleBlock(const QString & styleSheet, const QString & styleBlockStartSearchString,
                                    const int currentIndex, bool & error) const
{
    error = false;

    int blockStartIndex = styleSheet.lastIndexOf(styleBlockStartSearchString, currentIndex);
    if (blockStartIndex < 0) {
        // No block start before the current index => not inside the style block of this kind
        return false;
    }

    int blockEndIndex = styleSheet.lastIndexOf(QStringLiteral("}"), currentIndex);
    if (blockEndIndex > blockStartIndex) {
        // Block end was found after the last block start before the current index => the current index is not within the style block
        return false;
    }

    // There's no block end between block start and the current index, need to look for the block end after the current index
    blockEndIndex = styleSheet.indexOf(QStringLiteral("}"), currentIndex + 1);
    if (Q_UNLIKELY(blockEndIndex < 0)) {
        QNDEBUG(QStringLiteral("Can't parse stylesheet: can't find block end for style block search string ")
                << styleBlockStartSearchString);
        error = true;
        return false;
    }

    // Found first style block end after the current index while the block start is before the current index =>
    // the current index is inside the style block
    return true;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
void MainWindow::fixupQt4StyleSheets()
{
    QNDEBUG(QStringLiteral("MainWindow::fixupQt4StyleSheets"));

    QString alternateCentralWidgetStylesheet(
                "QSplitter::handle {"
                "   background-color: black;"
                "}"
                "QSplitter::handle:horizontal {"
                "   width: 1px;"
                "}"
                "QSplitter::handle:vertical {"
                "   height: 1px;"
                "}"
                "#noteEditorVerticalSplitter::handle {"
                "   background: none;"
                "}");
    m_pUI->centralWidget->setStyleSheet(alternateCentralWidgetStylesheet);

    QString alternateNoteListFrameStylesheet(
                "#notesListAndFiltersFrame {"
                "   border: none;"
                "}");
    m_pUI->notesListAndFiltersFrame->setStyleSheet(alternateNoteListFrameStylesheet);

    QString alternateFilterBodyFrameStylesheet(
                "#filterBodyFrame {"
                "padding: 0px;"
                "margin: 0px;"
                "border: none;"
                "border-top: 1px solid black;"
                "}");
    m_pUI->filterBodyFrame->setStyleSheet(alternateFilterBodyFrameStylesheet);

    QString alternateViewStylesheetBase(
                "{"
                "   border: none;"
                "   margin-top: 1px;"
                "   background-color: transparent;"
                "}");

    QString alternateFavoritesTableViewStylesheet = QString("#favoritesTableView ") +
                                                    alternateViewStylesheetBase;
    m_pUI->favoritesTableView->setStyleSheet(alternateFavoritesTableViewStylesheet);

    QString alternateNotebooksTreeViewStylesheet = QString("#notebooksTreeView ") +
                                                    alternateViewStylesheetBase;
    m_pUI->notebooksTreeView->setStyleSheet(alternateNotebooksTreeViewStylesheet);

    QString alternateTagsTreeViewStylesheet = QString("#tagsTreeView ") +
                                                   alternateViewStylesheetBase;
    m_pUI->tagsTreeView->setStyleSheet(alternateTagsTreeViewStylesheet);

    QString alternateSavedSearchTableViewStylesheet = QString("#savedSearchesTableView ") +
                                                      alternateViewStylesheetBase;
    m_pUI->savedSearchesTableView->setStyleSheet(alternateSavedSearchTableViewStylesheet);

    QString alternateDeletedNotesTableViewStylesheet = QString("#deletedNotesTableView ") +
                                                       alternateViewStylesheetBase;
    m_pUI->deletedNotesTableView->setStyleSheet(alternateDeletedNotesTableViewStylesheet);

    QString alternateSidePanelSplitterStylesheet(
                "QFrame {"
                "   border: none;"
                "}"
                "#sidePanelSplitter {"
                "   border-bottom: 1px solid black;"
                "}");
    m_pUI->sidePanelSplitter->setStyleSheet(alternateSidePanelSplitterStylesheet);

    QString alternateNoteListWidgetHeaderPanelStylesheet = m_pUI->noteListWidgetHeaderPanel->styleSheet();
    int index = alternateNoteListWidgetHeaderPanelStylesheet.indexOf("QLabel");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of note list widget header panel: "
                               "no QLabel within the stylesheet"));
        return;
    }

    index = alternateNoteListWidgetHeaderPanelStylesheet.indexOf("border-right", index);
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of note list widget header panel: "
                               "no border-right property for QLabel within the stylesheet"));
        return;
    }

    int propertyEndIndex = alternateNoteListWidgetHeaderPanelStylesheet.indexOf(";", index);
    if (Q_UNLIKELY(propertyEndIndex < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of note list widget header panel: "
                               "no closing \";\" for border-right property for QLabel "
                               "within the stylesheet"));
        return;
    }

    alternateNoteListWidgetHeaderPanelStylesheet.remove(index, propertyEndIndex - index + 1);
    QNDEBUG(QStringLiteral("alternateNoteListWidgetHeaderPanelStylesheet: ")
            << alternateNoteListWidgetHeaderPanelStylesheet);
    m_pUI->noteListWidgetHeaderPanel->setStyleSheet(alternateNoteListWidgetHeaderPanelStylesheet);

    // Add bottom border to the header panel widgets
    QString alternativeFavoritesHeaderPanelStylesheet = m_pUI->favoritesHeaderPanel->styleSheet();
    index = alternativeFavoritesHeaderPanelStylesheet.indexOf("}");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of favorites header panel: no first closing curly brace "
                               "found within the stylesheet"));
        return;
    }
    alternativeFavoritesHeaderPanelStylesheet.insert(index, "border-bottom: 1px solid black;");
    m_pUI->favoritesHeaderPanel->setStyleSheet(alternativeFavoritesHeaderPanelStylesheet);
    m_pUI->favoritesHeaderPanel->setMinimumHeight(23);
    m_pUI->favoritesHeaderPanel->setMaximumHeight(23);

    QString alternativeNotebooksHeaderPanelStylesheet = m_pUI->notebooksHeaderPanel->styleSheet();
    index = alternativeNotebooksHeaderPanelStylesheet.indexOf("}");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of notebooks header panel: no first closing curly brace "
                               "found within the stylesheet"));
        return;
    }
    alternativeNotebooksHeaderPanelStylesheet.insert(index, "border-bottom: 1px solid black;");
    m_pUI->notebooksHeaderPanel->setStyleSheet(alternativeNotebooksHeaderPanelStylesheet);
    m_pUI->notebooksHeaderPanel->setMinimumHeight(23);
    m_pUI->notebooksHeaderPanel->setMaximumHeight(23);

    QString alternativeTagsHeaderPanelStylesheet = m_pUI->tagsHeaderPanel->styleSheet();
    index = alternativeTagsHeaderPanelStylesheet.indexOf("}");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of tags header panel: no first closing curly brace "
                               "found within the stylesheet"));
        return;
    }
    alternativeTagsHeaderPanelStylesheet.insert(index, "border-bottom: 1px solid black;");
    m_pUI->tagsHeaderPanel->setStyleSheet(alternativeTagsHeaderPanelStylesheet);
    m_pUI->tagsHeaderPanel->setMinimumHeight(23);
    m_pUI->tagsHeaderPanel->setMaximumHeight(23);

    QString alternativeSavedSearchesHeaderPanelStylesheet = m_pUI->savedSearchesHeaderPanel->styleSheet();
    index = alternativeSavedSearchesHeaderPanelStylesheet.indexOf("}");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of saved searches header panel: no first closing curly brace "
                               "found within the stylesheet"));
        return;
    }
    alternativeSavedSearchesHeaderPanelStylesheet.insert(index, "border-bottom: 1px solid black;");
    m_pUI->savedSearchesHeaderPanel->setStyleSheet(alternativeSavedSearchesHeaderPanelStylesheet);
    m_pUI->savedSearchesHeaderPanel->setMinimumHeight(23);
    m_pUI->savedSearchesHeaderPanel->setMaximumHeight(23);

    QString alternativeDeletedNotesHeaderPanelStylesheet = m_pUI->deletedNotesHeaderPanel->styleSheet();
    index = alternativeDeletedNotesHeaderPanelStylesheet.indexOf("}");
    if (Q_UNLIKELY(index < 0)) {
        QNDEBUG(QStringLiteral("Can't fixup the stylesheet of deleted notes header panel: no first closing curly brace "
                               "found within the stylesheet"));
        return;
    }
    alternativeDeletedNotesHeaderPanelStylesheet.insert(index, "border-bottom: 1px solid black;");
    m_pUI->deletedNotesHeaderPanel->setStyleSheet(alternativeDeletedNotesHeaderPanelStylesheet);
    m_pUI->deletedNotesHeaderPanel->setMinimumHeight(23);
    m_pUI->deletedNotesHeaderPanel->setMaximumHeight(23);
}
#endif

void MainWindow::onSetStatusBarText(QString message, const int duration)
{
    QStatusBar * pStatusBar = m_pUI->statusBar;

    if (m_currentStatusBarChildWidget != Q_NULLPTR) {
        pStatusBar->removeWidget(m_currentStatusBarChildWidget);
        m_currentStatusBarChildWidget = Q_NULLPTR;
    }

    if (duration == 0) {
        m_currentStatusBarChildWidget = new QLabel(message);
        pStatusBar->addWidget(m_currentStatusBarChildWidget);
    }
    else {
        pStatusBar->showMessage(message, duration);
    }
}

#define DISPATCH_TO_NOTE_EDITOR(MainWindowMethod, NoteEditorMethod) \
    void MainWindow::MainWindowMethod() \
    { \
        QNDEBUG(QStringLiteral("MainWindow::" #MainWindowMethod)); \
        \
        NoteEditorWidget * noteEditorWidget = currentNoteEditor(); \
        if (!noteEditorWidget) { \
            return; \
        } \
        \
        noteEditorWidget->NoteEditorMethod(); \
    }

DISPATCH_TO_NOTE_EDITOR(onUndoAction, onUndoAction)
DISPATCH_TO_NOTE_EDITOR(onRedoAction, onRedoAction)
DISPATCH_TO_NOTE_EDITOR(onCopyAction, onCopyAction)
DISPATCH_TO_NOTE_EDITOR(onCutAction, onCutAction)
DISPATCH_TO_NOTE_EDITOR(onPasteAction, onPasteAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextSelectAllToggled, onSelectAllAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextBoldToggled, onEditorTextBoldToggled)
DISPATCH_TO_NOTE_EDITOR(onNoteTextItalicToggled, onEditorTextItalicToggled)
DISPATCH_TO_NOTE_EDITOR(onNoteTextUnderlineToggled, onEditorTextUnderlineToggled)
DISPATCH_TO_NOTE_EDITOR(onNoteTextStrikethroughToggled, onEditorTextStrikethroughToggled)
DISPATCH_TO_NOTE_EDITOR(onNoteTextAlignLeftAction, onEditorTextAlignLeftAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextAlignCenterAction, onEditorTextAlignCenterAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextAlignRightAction, onEditorTextAlignRightAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextAlignFullAction, onEditorTextAlignFullAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextAddHorizontalLineAction, onEditorTextAddHorizontalLineAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextIncreaseFontSizeAction, onEditorTextIncreaseFontSizeAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextDecreaseFontSizeAction, onEditorTextDecreaseFontSizeAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextHighlightAction, onEditorTextHighlightAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextIncreaseIndentationAction, onEditorTextIncreaseIndentationAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextDecreaseIndentationAction, onEditorTextDecreaseIndentationAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextInsertUnorderedListAction, onEditorTextInsertUnorderedListAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextInsertOrderedListAction, onEditorTextInsertOrderedListAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextInsertTableDialogAction, onEditorTextInsertTableDialogRequested)
DISPATCH_TO_NOTE_EDITOR(onNoteTextEditHyperlinkAction, onEditorTextEditHyperlinkAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextCopyHyperlinkAction, onEditorTextCopyHyperlinkAction)
DISPATCH_TO_NOTE_EDITOR(onNoteTextRemoveHyperlinkAction, onEditorTextRemoveHyperlinkAction)
DISPATCH_TO_NOTE_EDITOR(onFindInsideNoteAction, onFindInsideNoteAction)
DISPATCH_TO_NOTE_EDITOR(onFindPreviousInsideNoteAction, onFindPreviousInsideNoteAction)
DISPATCH_TO_NOTE_EDITOR(onReplaceInsideNoteAction, onReplaceInsideNoteAction)

#undef DISPATCH_TO_NOTE_EDITOR

void MainWindow::onSynchronizationManagerFailure(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("MainWindow::onSynchronizationManagerFailure: ") << errorDescription);
    onSetStatusBarText(errorDescription.localizedString());
}

void MainWindow::onSynchronizationFinished(Account account)
{
    QNDEBUG(QStringLiteral("MainWindow::onSynchronizationFinished: ") << account);

    onSetStatusBarText(tr("Synchronization finished!"));

    QNINFO(QStringLiteral("Synchronization finished for user ") << account.name()
           << QStringLiteral(", id ") << account.id());

    // TODO: figure out what to do with the account object now: should anything be done with it?
}

void MainWindow::onAuthenticationFinished(bool success, ErrorString errorDescription,
                                          Account account)
{
    QNDEBUG(QStringLiteral("MainWindow::onAuthenticationFinished: success = ")
            << (success ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", account = ") << account);

    if (!success) {
        onSetStatusBarText(tr("Couldn't authenticate the Evernote user") + QStringLiteral(": ") +
                           errorDescription.localizedString());
        return;
    }

    m_pAccountManager->switchAccount(account);
}

void MainWindow::onAuthenticationRevoked(bool success, ErrorString errorDescription,
                                         qevercloud::UserID userId)
{
    QNDEBUG(QStringLiteral("MainWindow::onAuthenticationRevoked: success = ")
            << (success ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", user id = ") << userId);

    if (!success) {
        onSetStatusBarText(tr("Couldn't revoke the authentication") + QStringLiteral(": ") +
                           errorDescription.localizedString());
        return;
    }

    QNINFO(QStringLiteral("Revoked authentication for user with id ") << userId);
}

void MainWindow::onRateLimitExceeded(qint32 secondsToWait)
{
    QNDEBUG(QStringLiteral("MainWindow::onRateLimitExceeded: seconds to wait = ")
            << secondsToWait);

    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    qint64 futureTimestamp = currentTimestamp + secondsToWait * 1000;
    QDateTime futureDateTime;
    futureDateTime.setMSecsSinceEpoch(futureTimestamp);

    QDateTime today = QDateTime::currentDateTime();
    bool includeDate = (today.date() != futureDateTime.date());

    QString dateTimeToShow = (includeDate
                              ? futureDateTime.toString(QStringLiteral("dd.MM.yyyy hh:mm:ss"))
                              : futureDateTime.toString(QStringLiteral("hh:mm:ss")));

    onSetStatusBarText(tr("The synchronization has reached the Evernote API rate limit, "
                          "it will continue automatically at approximately") + QStringLiteral(" ") +
                       dateTimeToShow);

    QNINFO(QStringLiteral("Evernote API rate limit exceeded, need to wait for ")
           << secondsToWait << QStringLiteral(" seconds, the synchronization will continue at ")
           << dateTimeToShow);
}

void MainWindow::onRemoteToLocalSyncDone()
{
    QNDEBUG(QStringLiteral("MainWindow::onRemoteToLocalSyncDone"));

    onSetStatusBarText(tr("Received all updates from Evernote servers, sending local changes"));
    QNINFO(QStringLiteral("Remote to local sync done"));
}

void MainWindow::onSynchronizationProgressUpdate(QString message, double workDonePercentage)
{
    QNDEBUG(QStringLiteral("MainWindow::onSynchronizationProgressUpdate: message = ")
            << message << QStringLiteral(", work done percentage = ") << workDonePercentage);

    if (Q_UNLIKELY(message.isEmpty())) {
        return;
    }

    bool showProgress = true;
    if (Q_UNLIKELY((workDonePercentage < 0.0) || (workDonePercentage >= 1.0))) {
        showProgress = false;
    }

    workDonePercentage *= 1.0e4;
    workDonePercentage = std::floor(workDonePercentage + 0.5);
    workDonePercentage *= 1.0e-2;

    QString messageToShow = message;
    if (showProgress) {
        messageToShow += QStringLiteral(", ");
        messageToShow += tr("progress");
        messageToShow += QStringLiteral(": ");
        messageToShow += QString::number(workDonePercentage);
        messageToShow += QStringLiteral("%");
    }

    onSetStatusBarText(messageToShow);
}

void MainWindow::onRemoteToLocalSyncStopped()
{
    QNDEBUG(QStringLiteral("MainWindow::onRemoteToLocalSyncStopped"));
    onSyncStopped();
}

void MainWindow::onSendLocalChangesStopped()
{
    QNDEBUG(QStringLiteral("MainWindow::onSendLocalChangesStopped"));
    onSyncStopped();
}

void MainWindow::onEvernoteAccountAuthenticationRequested(QString host)
{
    QNDEBUG(QStringLiteral("MainWindow::onEvernoteAccountAuthenticationRequested: host = ") << host);

    if ((host != m_synchronizationManagerHost) || !m_pSynchronizationManager) {
        m_synchronizationManagerHost = host;
        setupSynchronizationManager();
    }

    emit authenticate();
}

void MainWindow::onNoteTextSpellCheckToggled()
{
    QNDEBUG(QStringLiteral("MainWindow::onNoteTextSpellCheckToggled"));

    NoteEditorWidget * noteEditorWidget = currentNoteEditor();
    if (!noteEditorWidget) {
        return;
    }

    if (noteEditorWidget->isSpellCheckEnabled()) {
        noteEditorWidget->onEditorSpellCheckStateChanged(Qt::Unchecked);
    }
    else {
        noteEditorWidget->onEditorSpellCheckStateChanged(Qt::Checked);
    }
}

void MainWindow::onShowNoteSource()
{
    QNDEBUG(QStringLiteral("MainWindow::onShowNoteSource"));

    NoteEditorWidget * pNoteEditorWidget = currentNoteEditor();
    if (!pNoteEditorWidget) {
        return;
    }

    if (!pNoteEditorWidget->isNoteSourceShown()) {
        pNoteEditorWidget->showNoteSource();
    }
    else {
        pNoteEditorWidget->hideNoteSource();
    }
}

void MainWindow::onSaveNoteAction()
{
    QNDEBUG(QStringLiteral("MainWindow::onSaveNoteAction"));

    NoteEditorWidget * pNoteEditorWidget = currentNoteEditor();
    if (!pNoteEditorWidget) {
        return;
    }

    pNoteEditorWidget->onSaveNoteAction();
}

void MainWindow::onCreateNotebookButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onCreateNotebookButtonPressed"));

    if (Q_UNLIKELY(!m_pNotebookModel)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Can't create a new notebook: no notebook model is set up"));
        QNWARNING(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QScopedPointer<AddOrEditNotebookDialog> pAddNotebookDialog(new AddOrEditNotebookDialog(m_pNotebookModel, this));
    pAddNotebookDialog->setWindowModality(Qt::WindowModal);
    Q_UNUSED(pAddNotebookDialog->exec())
}

void MainWindow::onRemoveNotebookButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onRemoveNotebookButtonPressed"));
    m_pUI->notebooksTreeView->deleteSelectedItem();
}

void MainWindow::onNotebookInfoButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onNotebookInfoButtonPressed"));

    QModelIndex index = m_pUI->notebooksTreeView->currentlySelectedItemIndex();
    NotebookModelItemInfoWidget * pNotebookModelItemInfoWidget = new NotebookModelItemInfoWidget(index, this);
    showInfoWidget(pNotebookModelItemInfoWidget);
}

void MainWindow::onCreateTagButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onCreateTagButtonPressed"));

    if (Q_UNLIKELY(!m_pTagModel)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Can't create a new tag: no tag model is set up"));
        QNWARNING(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QScopedPointer<AddOrEditTagDialog> pAddTagDialog(new AddOrEditTagDialog(m_pTagModel, this));
    pAddTagDialog->setWindowModality(Qt::WindowModal);
    Q_UNUSED(pAddTagDialog->exec())
}

void MainWindow::onRemoveTagButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onRemoveTagButtonPressed"));
    m_pUI->tagsTreeView->deleteSelectedItem();
}

void MainWindow::onTagInfoButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onTagInfoButtonPressed"));

    QModelIndex index = m_pUI->tagsTreeView->currentlySelectedItemIndex();
    TagModelItemInfoWidget * pTagModelItemInfoWidget = new TagModelItemInfoWidget(index, this);
    showInfoWidget(pTagModelItemInfoWidget);
}

void MainWindow::onCreateSavedSearchButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onCreateSavedSearchButtonPressed"));

    if (Q_UNLIKELY(!m_pSavedSearchModel)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Can't create a new saved search: no saved search model is set up"));
        QNWARNING(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QScopedPointer<AddOrEditSavedSearchDialog> pAddSavedSearchDialog(new AddOrEditSavedSearchDialog(m_pSavedSearchModel, this));
    pAddSavedSearchDialog->setWindowModality(Qt::WindowModal);
    Q_UNUSED(pAddSavedSearchDialog->exec())
}

void MainWindow::onRemoveSavedSearchButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onRemoveSavedSearchButtonPressed"));
    m_pUI->savedSearchesTableView->deleteSelectedItem();
}

void MainWindow::onSavedSearchInfoButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onSavedSearchInfoButtonPressed"));

    QModelIndex index = m_pUI->savedSearchesTableView->currentlySelectedItemIndex();
    SavedSearchModelItemInfoWidget * pSavedSearchModelItemInfoWidget = new SavedSearchModelItemInfoWidget(index, this);
    showInfoWidget(pSavedSearchModelItemInfoWidget);
}

void MainWindow::onUnfavoriteItemButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onUnfavoriteItemButtonPressed"));
    m_pUI->favoritesTableView->deleteSelectedItems();
}

void MainWindow::onFavoritedItemInfoButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onFavoritedItemInfoButtonPressed"));
    QModelIndex index = m_pUI->favoritesTableView->currentlySelectedItemIndex();
    if (!index.isValid()) {
        Q_UNUSED(informationMessageBox(this, tr("Not exactly one favorited item is selected"),
                                       tr("Please select the only one favorited item to "
                                          "see its detailed info")));
        return;
    }

    FavoritesModel * pFavoritesModel = qobject_cast<FavoritesModel*>(m_pUI->favoritesTableView->model());
    if (Q_UNLIKELY(!pFavoritesModel)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Failed to cast the favorited table "
                                                  "view's model to favorites model")))
        return;
    }

    const FavoritesModelItem * pItem = pFavoritesModel->itemAtRow(index.row());
    if (Q_UNLIKELY(!pItem)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Favorites model returned null pointer "
                                                  "to favorited item for the selected index")))
        return;
    }

    switch(pItem->type())
    {
    case FavoritesModelItem::Type::Note:
        // TODO: show note info widget when it's done
        break;
    case FavoritesModelItem::Type::Notebook:
        {
            if (Q_LIKELY(m_pNotebookModel)) {
                QModelIndex notebookIndex = m_pNotebookModel->indexForLocalUid(pItem->localUid());
                NotebookModelItemInfoWidget * pNotebookItemInfoWidget = new NotebookModelItemInfoWidget(notebookIndex, this);
                showInfoWidget(pNotebookItemInfoWidget);
            }
            else {
                Q_UNUSED(internalErrorMessageBox(this, tr("No notebook model exists at the moment")))
            }
            break;
        }
    case FavoritesModelItem::Type::SavedSearch:
        {
            if (Q_LIKELY(m_pSavedSearchModel)) {
                QModelIndex savedSearchIndex = m_pSavedSearchModel->indexForLocalUid(pItem->localUid());
                SavedSearchModelItemInfoWidget * pSavedSearchItemInfoWidget = new SavedSearchModelItemInfoWidget(savedSearchIndex, this);
                showInfoWidget(pSavedSearchItemInfoWidget);
            }
            else {
                Q_UNUSED(internalErrorMessageBox(this, tr("No saved search model exists at the moment"));)
            }
            break;
        }
    case FavoritesModelItem::Type::Tag:
        {
            if (Q_LIKELY(m_pTagModel)) {
                QModelIndex tagIndex = m_pTagModel->indexForLocalUid(pItem->localUid());
                TagModelItemInfoWidget * pTagItemInfoWidget = new TagModelItemInfoWidget(tagIndex, this);
                showInfoWidget(pTagItemInfoWidget);
            }
            else {
                Q_UNUSED(internalErrorMessageBox(this, tr("No tag model exists at the moment"));)
            }
            break;
        }
    default:
        Q_UNUSED(internalErrorMessageBox(this, tr("Incorrect favorited item type") +
                                         QStringLiteral(": ") + QString::number(pItem->type())))
        break;
    }
}

void MainWindow::onRestoreDeletedNoteButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onRestoreDeletedNoteButtonPressed"));
    m_pUI->deletedNotesTableView->restoreCurrentlySelectedNote();
}

void MainWindow::onDeleteNotePermanentlyButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onDeleteNotePermanentlyButtonPressed"));
    m_pUI->deletedNotesTableView->deleteCurrentlySelectedNotePermanently();
}

void MainWindow::onDeletedNoteInfoButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onDeletedNoteInfoButtonPressed"));
    m_pUI->deletedNotesTableView->showCurrentlySelectedNoteInfo();
}

void MainWindow::showInfoWidget(QWidget * pWidget)
{
    pWidget->setAttribute(Qt::WA_DeleteOnClose, true);
    pWidget->setWindowModality(Qt::WindowModal);
    pWidget->adjustSize();
#ifndef Q_OS_MAC
    // Center the widget relative to the main window
    const QRect & geometryRect = geometry();
    const QRect & dialogGeometryRect = pWidget->geometry();
    if (geometryRect.isValid() && dialogGeometryRect.isValid()) {
        const QPoint center = geometryRect.center();
        int x = center.x() - dialogGeometryRect.width() / 2;
        int y = center.y() - dialogGeometryRect.height() / 2;
        pWidget->move(x, y);
    }
#endif
    pWidget->show();
}

void MainWindow::onFiltersViewTogglePushButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onFiltersViewTogglePushButtonPressed"));

    m_filtersViewExpanded = !m_filtersViewExpanded;

    if (m_filtersViewExpanded) {
        expandFiltersView();
    }
    else {
        foldFiltersView();
    }

    QString accountKey = accountToKey(*m_pAccount);
    if (Q_UNLIKELY(accountKey.isEmpty())) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't save "
                                            "the filters view expanded state, "
                                            "can't convert the last used account "
                                            "to the string key"));
        QNWARNING(error << QStringLiteral(", account: ") << *m_pAccount);
        onSetStatusBarText(error.localizedString());
        return;
    }

    ApplicationSettings appSettings;
    appSettings.beginGroup(accountKey + QStringLiteral("/FiltersView"));
    appSettings.setValue(FILTERS_VIEW_STATUS_KEY, QVariant(m_filtersViewExpanded));
    appSettings.endGroup();
}

void MainWindow::onNoteSortingModeChanged(int index)
{
    QNDEBUG(QStringLiteral("MainWindow::onNoteSortingModeChanged: index = ") << index);

    persistChosenNoteSortingMode(index);

    if (Q_UNLIKELY(!m_pNoteModel)) {
        QNDEBUG(QStringLiteral("No note model, ignoring the change"));
        return;
    }

    switch(index)
    {
    case NoteSortingModes::CreatedAscending:
        m_pNoteModel->sort(NoteModel::Columns::CreationTimestamp, Qt::AscendingOrder);
        break;
    case NoteSortingModes::CreatedDescending:
        m_pNoteModel->sort(NoteModel::Columns::CreationTimestamp, Qt::DescendingOrder);
        break;
    case NoteSortingModes::ModifiedAscending:
        m_pNoteModel->sort(NoteModel::Columns::ModificationTimestamp, Qt::AscendingOrder);
        break;
    case NoteSortingModes::ModifiedDescending:
        m_pNoteModel->sort(NoteModel::Columns::ModificationTimestamp, Qt::DescendingOrder);
        break;
    case NoteSortingModes::TitleAscending:
        m_pNoteModel->sort(NoteModel::Columns::Title, Qt::AscendingOrder);
        break;
    case NoteSortingModes::TitleDescending:
        m_pNoteModel->sort(NoteModel::Columns::Title, Qt::DescendingOrder);
        break;
    case NoteSortingModes::SizeAscending:
        m_pNoteModel->sort(NoteModel::Columns::Size, Qt::AscendingOrder);
        break;
    case NoteSortingModes::SizeDescending:
        m_pNoteModel->sort(NoteModel::Columns::Size, Qt::DescendingOrder);
        break;
    default:
        {
            ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: got unknown note sorting order, fallback to the default"));
            QNWARNING(error << QStringLiteral(", sorting mode index = ") << index);
            onSetStatusBarText(error.localizedString());

            m_pNoteModel->sort(NoteModel::Columns::CreationTimestamp, Qt::AscendingOrder);
            break;
        }
    }
}

void MainWindow::onNewNoteButtonPressed()
{
    QNDEBUG(QStringLiteral("MainWindow::onNewNoteButtonPressed"));

    if (Q_UNLIKELY(!m_pNoteEditorTabsAndWindowsCoordinator)) {
        QNDEBUG(QStringLiteral("No note editor tab widget manager, probably the button was pressed too quickly on startup, skipping"));
        return;
    }

    if (Q_UNLIKELY(!m_pNoteModel)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Can't create a new note: note model is unexpectedly null")))
        return;
    }

    if (Q_UNLIKELY(!m_pNotebookModel)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Can't create a new note: notebook model is unexpectedly null")))
        return;
    }

    QModelIndex currentNotebookIndex = m_pUI->notebooksTreeView->currentlySelectedItemIndex();
    if (Q_UNLIKELY(!currentNotebookIndex.isValid())) {
        Q_UNUSED(informationMessageBox(this, tr("No notebook is selected"),
                                       tr("Please select the notebook in which you want to create the note; "
                                          "if you don't have any notebooks yet, create one")))
        return;
    }

    const NotebookModelItem * pNotebookModelItem = m_pNotebookModel->itemForIndex(currentNotebookIndex);
    if (Q_UNLIKELY(!pNotebookModelItem)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Can't create a new note: can't find the notebook model item "
                                                  "corresponding to the currently selected notebook")))
        return;
    }

    if (Q_UNLIKELY(pNotebookModelItem->type() != NotebookModelItem::Type::Notebook)) {
        Q_UNUSED(informationMessageBox(this, tr("No notebook is selected"),
                                       tr("Please select the notebook in which you want to create the note (currently "
                                          "the notebook stack seems to be selected)")))
        return;
    }

    const NotebookItem * pNotebookItem = pNotebookModelItem->notebookItem();
    if (Q_UNLIKELY(!pNotebookItem)) {
        Q_UNUSED(internalErrorMessageBox(this, tr("Can't create a new note: the notebook model item has notebook type "
                                                  "but null pointer to the actual notebook item")))
        return;
    }

    m_pNoteEditorTabsAndWindowsCoordinator->createNewNote(pNotebookItem->localUid(), pNotebookItem->guid());
}

void MainWindow::onCurrentNoteInListChanged(QString noteLocalUid)
{
    QNDEBUG(QStringLiteral("MainWindow::onCurrentNoteInListChanged: ") << noteLocalUid);
    m_pNoteEditorTabsAndWindowsCoordinator->addNote(noteLocalUid);
}

void MainWindow::onOpenNoteInSeparateWindow(QString noteLocalUid)
{
    QNDEBUG(QStringLiteral("MainWindow::onOpenNoteInSeparateWindow: ") << noteLocalUid);
    m_pNoteEditorTabsAndWindowsCoordinator->addNote(noteLocalUid, NoteEditorTabsAndWindowsCoordinator::NoteEditorMode::Window);
}

void MainWindow::onNoteSearchQueryChanged(const QString & query)
{
    QNDEBUG(QStringLiteral("MainWindow::onNoteSearchQueryChanged: ") << query);
    m_noteSearchQueryValidated = false;
}

void MainWindow::onNoteSearchQueryReady()
{
    QString noteSearchQuery = m_pUI->searchQueryLineEdit->text();
    QNDEBUG(QStringLiteral("MainWindow::onNoteSearchQueryReady: query = ") << noteSearchQuery);

    if (noteSearchQuery.isEmpty()) {
        // TODO: remove search query from NoteFilterModel
        return;
    }

    bool res = checkNoteSearchQuery(noteSearchQuery);
    if (!res) {
        // TODO: remove search query from NoteFilterModel
        return;
    }

    // TODO: set the search query to the NoteFilterModel
}

void MainWindow::onSaveNoteSearchQueryButtonPressed()
{
    QString noteSearchQuery = m_pUI->searchQueryLineEdit->text();
    QNDEBUG(QStringLiteral("MainWindow::onSaveNoteSearchQueryButtonPressed, query = ")
            << noteSearchQuery);

    bool res = checkNoteSearchQuery(noteSearchQuery);
    if (!res) {
        // TODO: remove search query from NoteFilterModel
        return;
    }

    QScopedPointer<AddOrEditSavedSearchDialog> pAddSavedSearchDialog(new AddOrEditSavedSearchDialog(m_pSavedSearchModel, this));
    pAddSavedSearchDialog->setQuery(noteSearchQuery);
    Q_UNUSED(pAddSavedSearchDialog->exec())
}

void MainWindow::onSetTestNoteWithEncryptedData()
{
    QNDEBUG(QStringLiteral("MainWindow::onSetTestNoteWithEncryptedData"));

    m_testNote = Note();
    m_testNote.setLocalUid(QStringLiteral("{7ae26137-9b62-4c30-85a9-261b435f6db3}"));

    QString noteContent = test::ManualTestingHelper::noteContentWithEncryption();
    m_testNote.setContent(noteContent);

    // TODO: set this note to editor, in new tab or replace existing tab (if any)
}

void MainWindow::onSetTestNoteWithResources()
{
    QNDEBUG(QStringLiteral("MainWindow::onSetTestNoteWithResources"));

    prepareTestNoteWithResources();

    m_testNote.setLocalUid(QStringLiteral("{ce8e5ea1-28fc-4842-a726-0d4a78dfcbe6}"));
    m_testNotebook.setCanUpdateNotes(true);

    // TODO: set this note to editor, in new tab or replace existing tab (if any)
}

void MainWindow::onSetTestReadOnlyNote()
{
    prepareTestNoteWithResources();

    m_testNote.setLocalUid(QStringLiteral("{ce8e5ea1-28fc-4842-a726-0d4a78dfcbe5}"));
    m_testNotebook.setCanUpdateNotes(false);

    // TODO: set this note to editor, in new tab or replace existing tab (if any)
}

void MainWindow::onSetInkNote()
{
    prepareTestInkNote();

    m_testNote.setLocalUid(QStringLiteral("{96c747e2-7bdc-4805-a704-105cbfcc7fbe}"));
    m_testNotebook.setCanUpdateNotes(true);

    // TODO: set this note to editor, in new tab or replace existing tab (if any)
}

void MainWindow::onNoteEditorError(ErrorString error)
{
    QNINFO(QStringLiteral("MainWindow::onNoteEditorError: ") << error);
    onSetStatusBarText(error.localizedString(), 20000);
}

void MainWindow::onModelViewError(ErrorString error)
{
    QNINFO(QStringLiteral("MainWindow::onModelViewError: ") << error);
    onSetStatusBarText(error.localizedString(), 20000);
}

void MainWindow::onNoteEditorSpellCheckerNotReady()
{
    QNDEBUG(QStringLiteral("MainWindow::onNoteEditorSpellCheckerNotReady"));

    NoteEditorWidget * noteEditor = qobject_cast<NoteEditorWidget*>(sender());
    if (!noteEditor) {
        QNTRACE(QStringLiteral("Can't cast caller to note editor widget, skipping"));
        return;
    }

    NoteEditorWidget * currentEditor = currentNoteEditor();
    if (!currentEditor || (currentEditor != noteEditor)) {
        QNTRACE(QStringLiteral("Not an update from current note editor, skipping"));
        return;
    }

    onSetStatusBarText(tr("Spell checker is loading dictionaries, please wait"));
}

void MainWindow::onNoteEditorSpellCheckerReady()
{
    QNDEBUG(QStringLiteral("MainWindow::onNoteEditorSpellCheckerReady"));

    NoteEditorWidget * noteEditor = qobject_cast<NoteEditorWidget*>(sender());
    if (!noteEditor) {
        QNTRACE(QStringLiteral("Can't cast caller to note editor widget, skipping"));
        return;
    }

    NoteEditorWidget * currentEditor = currentNoteEditor();
    if (!currentEditor || (currentEditor != noteEditor)) {
        QNTRACE(QStringLiteral("Not an update from current note editor, skipping"));
        return;
    }

    onSetStatusBarText(QString());
}

void MainWindow::onAddAccountActionTriggered(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onAddAccountActionTriggered"));

    Q_UNUSED(checked)

    m_pAccountManager->raiseAddAccountDialog();
}

void MainWindow::onManageAccountsActionTriggered(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onManageAccountsActionTriggered"));

    Q_UNUSED(checked)

    m_pAccountManager->raiseManageAccountsDialog();
}

void MainWindow::onSwitchAccountActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchAccountActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    if (!checked) {
        QNTRACE(QStringLiteral("Ignoring the unchecking of account"));
        return;
    }

    QAction * action = qobject_cast<QAction*>(sender());
    if (Q_UNLIKELY(!action)) {
        NOTIFY_ERROR(QT_TRANSLATE_NOOP("", "Internal error: account switching action is unexpectedly null"));
        return;
    }

    QVariant indexData = action->data();
    bool conversionResult = false;
    int index = indexData.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        NOTIFY_ERROR(QT_TRANSLATE_NOOP("", "Internal error: can't get identification data "
                                       "from the account switching action"));
        return;
    }

    const QVector<Account> & availableAccounts = m_pAccountManager->availableAccounts();
    const int numAvailableAccounts = availableAccounts.size();

    if ((index < 0) || (index >= numAvailableAccounts)) {
        NOTIFY_ERROR(QT_TRANSLATE_NOOP("", "Internal error: wrong index into available accounts "
                                       "in account switching action"));
        return;
    }

    const Account & availableAccount = availableAccounts[index];
    m_pAccountManager->switchAccount(availableAccount);
    // Will continue in slot connected to AccountManager's switchedAccount signal
}

void MainWindow::onAccountSwitched(Account account)
{
    QNDEBUG(QStringLiteral("MainWindow::onAccountSwitched: ") << account);

    // Now need to ask the local stortage manager to switch the account
    m_lastLocalStorageSwitchUserRequest = QUuid::createUuid();
    emit localStorageSwitchUserRequest(account, /* start from scratch = */ false,
                                       m_lastLocalStorageSwitchUserRequest);
}

void MainWindow::onAccountUpdated(Account account)
{
    QNDEBUG(QStringLiteral("MainWindow::onAccountUpdated: ") << account);

    if (!m_pAccount) {
        QNDEBUG(QStringLiteral("No account is current at the moment"));
        return;
    }

    if (m_pAccount->type() != account.type()) {
        QNDEBUG(QStringLiteral("Not an update for the current account: it has another type"));
        QNTRACE(*m_pAccount);
        return;
    }

    bool isLocal = (m_pAccount->type() == Account::Type::Local);

    if (isLocal && (m_pAccount->name() != account.name())) {
        QNDEBUG(QStringLiteral("Not an update for the current account: it has another name"));
        QNTRACE(*m_pAccount);
        return;
    }

    if (!isLocal &&
        ((m_pAccount->id() != account.id()) || (m_pAccount->name() != account.name())))
    {
        QNDEBUG(QStringLiteral("Not an update for the current account: either id or name don't match"));
        QNTRACE(*m_pAccount);
        return;
    }

    *m_pAccount = account;
    setWindowTitleForAccount(account);
}

void MainWindow::onAccountAdded(Account account)
{
    QNDEBUG(QStringLiteral("MainWindow::onAccountAdded: ") << account);
    updateSubMenuWithAvailableAccounts();
}

void MainWindow::onAccountManagerError(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("MainWindow::onAccountManagerError: ") << errorDescription);
    onSetStatusBarText(errorDescription.localizedString());
}

void MainWindow::onShowSidePanelActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowSidePanelActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowSidePanel"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->sidePanelSplitter->show();
    }
    else {
        m_pUI->sidePanelSplitter->hide();
    }
}

void MainWindow::onShowFavoritesActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowFavoritesActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowFavorites"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->favoritesWidget->show();
    }
    else {
        m_pUI->favoritesWidget->hide();
    }
}

void MainWindow::onShowNotebooksActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowNotebooksActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowNotebooks"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->notebooksWidget->show();
    }
    else {
        m_pUI->notebooksWidget->hide();
    }
}

void MainWindow::onShowTagsActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowTagsActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowTags"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->tagsWidget->show();
    }
    else {
        m_pUI->tagsWidget->hide();
    }
}

void MainWindow::onShowSavedSearchesActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowSavedSearchesActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowSavedSearches"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->savedSearchesWidget->show();
    }
    else {
        m_pUI->savedSearchesWidget->hide();
    }
}

void MainWindow::onShowDeletedNotesActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowDeletedNotesActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowDeletedNotes"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->deletedNotesWidget->show();
    }
    else {
        m_pUI->deletedNotesWidget->hide();
    }
}

void MainWindow::onShowNoteListActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowNoteListActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowNotesList"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->notesListAndFiltersFrame->show();
    }
    else {
        m_pUI->notesListAndFiltersFrame->hide();
    }
}

void MainWindow::onShowToolbarActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowToolbarActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowToolbar"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->upperBarPanel->show();
    }
    else {
        m_pUI->upperBarPanel->hide();
    }
}

void MainWindow::onShowStatusBarActionToggled(bool checked)
{
    QNDEBUG(QStringLiteral("MainWindow::onShowStatusBarActionToggled: checked = ")
            << (checked ? QStringLiteral("true") : QStringLiteral("false")));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));
    appSettings.setValue(QStringLiteral("ShowStatusBar"), checked);
    appSettings.endGroup();

    if (checked) {
        m_pUI->statusBar->show();
    }
    else {
        m_pUI->statusBar->hide();
    }
}

void MainWindow::onSwitchIconsToNativeAction()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchIconsToNativeAction"));

    if (m_nativeIconThemeName.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "No native icon theme is available"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    if (QIcon::themeName() == m_nativeIconThemeName) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using the native icon theme"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QIcon::setThemeName(m_nativeIconThemeName);
    persistChosenIconTheme(m_nativeIconThemeName);
    refreshChildWidgetsThemeIcons();
}

void MainWindow::onSwitchIconsToTangoAction()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchIconsToTangoAction"));

    QString tango = QStringLiteral("tango");

    if (QIcon::themeName() == tango) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using tango icon theme"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QIcon::setThemeName(tango);
    persistChosenIconTheme(tango);
    refreshChildWidgetsThemeIcons();
}

void MainWindow::onSwitchIconsToOxygenAction()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchIconsToOxygenAction"));

    QString oxygen = QStringLiteral("oxygen");

    if (QIcon::themeName() == oxygen) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using oxygen icon theme"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    QIcon::setThemeName(oxygen);
    persistChosenIconTheme(oxygen);
    refreshChildWidgetsThemeIcons();
}

void MainWindow::onSwitchPanelStyleToBuiltIn()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchPanelStyleToBuiltIn"));

    if (m_currentPanelStyle.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using the built-in panel style"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    m_currentPanelStyle.clear();

    ApplicationSettings appSettings;
    appSettings.beginGroup("LookAndFeel");
    appSettings.remove(QStringLiteral("panelStyle"));
    appSettings.endGroup();

    setPanelsOverlayStyleSheet(StyleSheetProperties());
}

void MainWindow::onSwitchPanelStyleToLighter()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchPanelStyleToLighter"));

    if (m_currentPanelStyle == QStringLiteral("Lighter")) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using the lighter panel style"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    m_currentPanelStyle = QStringLiteral("Lighter");

    ApplicationSettings appSettings;
    appSettings.beginGroup("LookAndFeel");
    appSettings.setValue(QStringLiteral("panelStyle"), m_currentPanelStyle);
    appSettings.endGroup();

    StyleSheetProperties properties;
    getPanelStyleSheetProperties(m_currentPanelStyle, properties);
    setPanelsOverlayStyleSheet(properties);
}

void MainWindow::onSwitchPanelStyleToDarker()
{
    QNDEBUG(QStringLiteral("MainWindow::onSwitchPanelStyleToDarker"));

    if (m_currentPanelStyle == QStringLiteral("Darker")) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Already using the darker panel style"));
        QNDEBUG(error);
        onSetStatusBarText(error.localizedString());
        return;
    }

    m_currentPanelStyle = QStringLiteral("Darker");

    ApplicationSettings appSettings;
    appSettings.beginGroup("LookAndFeel");
    appSettings.setValue(QStringLiteral("panelStyle"), m_currentPanelStyle);
    appSettings.endGroup();

    StyleSheetProperties properties;
    getPanelStyleSheetProperties(m_currentPanelStyle, properties);
    setPanelsOverlayStyleSheet(properties);
}

void MainWindow::onLocalStorageSwitchUserRequestComplete(Account account, QUuid requestId)
{
    QNDEBUG(QStringLiteral("MainWindow::onLocalStorageSwitchUserRequestComplete: account = ")
            << account << QStringLiteral(", request id = ") << requestId);

    bool expected = (m_lastLocalStorageSwitchUserRequest == requestId);
    m_lastLocalStorageSwitchUserRequest = QUuid();

    if (!expected) {
        NOTIFY_ERROR(QT_TRANSLATE_NOOP("", "Local storage user was switched without explicit user action"));
        // Trying to undo it
        m_pAccountManager->switchAccount(*m_pAccount); // This should trigger the switch in local storage as well
        return;
    }

    m_notebookCache.clear();
    m_tagCache.clear();
    m_savedSearchCache.clear();
    m_noteCache.clear();

    *m_pAccount = account;
    setWindowTitleForAccount(account);

    if (m_pAccount->type() == Account::Type::Local)
    {
        clearSynchronizationManager();
    }
    else
    {
        if (m_synchronizationManagerHost == m_pAccount->evernoteHost())
        {
            if (!m_pSynchronizationManager) {
                setupSynchronizationManager();
            }
        }
        else
        {
            m_synchronizationManagerHost = m_pAccount->evernoteHost();
            setupSynchronizationManager();
        }

        m_pSynchronizationManager->setAccount(*m_pAccount);
    }

    setupModels();

    if (m_pNoteEditorTabsAndWindowsCoordinator) {
        m_pNoteEditorTabsAndWindowsCoordinator->switchAccount(*m_pAccount, *m_pTagModel);
    }

    m_pUI->filterByNotebooksWidget->switchAccount(*m_pAccount, m_pNotebookModel);
    m_pUI->filterByTagsWidget->switchAccount(*m_pAccount, m_pTagModel);
    m_pUI->filterBySavedSearchComboBox->switchAccount(*m_pAccount, m_pSavedSearchModel);

    setupViews();
}

void MainWindow::onLocalStorageSwitchUserRequestFailed(Account account, ErrorString errorDescription, QUuid requestId)
{
    bool expected = (m_lastLocalStorageSwitchUserRequest == requestId);
    if (!expected) {
        return;
    }

    QNDEBUG(QStringLiteral("MainWindow::onLocalStorageSwitchUserRequestFailed: ") << account << QStringLiteral("\nError description: ")
            << errorDescription << QStringLiteral(", request id = ") << requestId);

    m_lastLocalStorageSwitchUserRequest = QUuid();

    onSetStatusBarText(tr("Could not switch account") + QStringLiteral(": ") + errorDescription.localizedString());

    if (!m_pAccount) {
        // If there was no any account set previously, nothing to do
        return;
    }

    const QVector<Account> & availableAccounts = m_pAccountManager->availableAccounts();
    const int numAvailableAccounts = availableAccounts.size();

    // Trying to restore the previously selected account as the current one in the UI
    QList<QAction*> availableAccountActions = m_pAvailableAccountsActionGroup->actions();
    for(auto it = availableAccountActions.constBegin(), end = availableAccountActions.constEnd(); it != end; ++it)
    {
        QAction * action = *it;
        if (Q_UNLIKELY(!action)) {
            QNDEBUG(QStringLiteral("Found null pointer to action within the available accounts action group"));
            continue;
        }

        QVariant actionData = action->data();
        bool conversionResult = false;
        int index = actionData.toInt(&conversionResult);
        if (Q_UNLIKELY(!conversionResult)) {
            QNDEBUG(QStringLiteral("Can't convert available account's user data to int: ") << actionData);
            continue;
        }

        if (Q_UNLIKELY((index < 0) || (index >= numAvailableAccounts))) {
            QNDEBUG(QStringLiteral("Available account's index is beyond the range of available accounts: index = ")
                    << index << QStringLiteral(", num available accounts = ") << numAvailableAccounts);
            continue;
        }

        const Account & actionAccount = availableAccounts.at(index);
        if (actionAccount == *m_pAccount) {
            QNDEBUG(QStringLiteral("Restoring the current account in UI: index = ") << index << QStringLiteral(", account = ")
                    << actionAccount);
            action->setChecked(true);
            return;
        }
    }

    // If we got here, it means we haven't found the proper previous account
    QNDEBUG(QStringLiteral("Couldn't find the action corresponding to the previous available account: ") << *m_pAccount);
}

void MainWindow::resizeEvent(QResizeEvent * pEvent)
{
    QMainWindow::resizeEvent(pEvent);

    if (m_pUI->filterBodyFrame->isHidden()) {
        // NOTE: without this the splitter seems to take a wrong guess
        // about the size of note filters header panel and that doesn't look good
        adjustNoteListAndFiltersSplitterSizes();
    }
}

void MainWindow::closeEvent(QCloseEvent * pEvent)
{
    QNDEBUG(QStringLiteral("MainWindow::closeEvent"));

    if (m_pNoteEditorTabsAndWindowsCoordinator) {
        m_pNoteEditorTabsAndWindowsCoordinator->clear();
    }

    QMainWindow::closeEvent(pEvent);
}

void MainWindow::setupThemeIcons()
{
    QNTRACE(QStringLiteral("MainWindow::setupThemeIcons"));

    m_nativeIconThemeName = QIcon::themeName();
    QNDEBUG(QStringLiteral("Native icon theme name: ") << m_nativeIconThemeName);

    if (!QIcon::hasThemeIcon(QStringLiteral("document-open"))) {
        QNDEBUG(QStringLiteral("There seems to be no native icon theme available: "
                               "document-open icon is not present within the theme"));
        m_nativeIconThemeName.clear();
    }

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("LookAndFeel"));
    QString iconThemeName = appSettings.value(QStringLiteral("iconTheme")).toString();
    appSettings.endGroup();

    if (!iconThemeName.isEmpty()) {
        QNDEBUG(QStringLiteral("Last chosen icon theme: ") << iconThemeName);
    }
    else {
        QNDEBUG(QStringLiteral("No last chosen icon theme, fallback to oxygen"));
        iconThemeName = QStringLiteral("oxygen");
    }

    QIcon::setThemeName(iconThemeName);
}

void MainWindow::setupAccountManager()
{
    QNDEBUG(QStringLiteral("MainWindow::setupAccountManager"));

    QObject::connect(m_pAccountManager, QNSIGNAL(AccountManager,evernoteAccountAuthenticationRequested,QString),
                     this, QNSLOT(MainWindow,onEvernoteAccountAuthenticationRequested,QString));
    QObject::connect(m_pAccountManager, QNSIGNAL(AccountManager,switchedAccount,Account),
                     this, QNSLOT(MainWindow,onAccountSwitched,Account));
    QObject::connect(m_pAccountManager, QNSIGNAL(AccountManager,accountUpdated,Account),
                     this, QNSLOT(MainWindow,onAccountUpdated,Account));
    QObject::connect(m_pAccountManager, QNSIGNAL(AccountManager,accountAdded,Account),
                     this, QNSLOT(MainWindow,onAccountAdded,Account));
    QObject::connect(m_pAccountManager, QNSIGNAL(AccountManager,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onAccountManagerError,ErrorString));
}

void MainWindow::setupLocalStorageManager()
{
    QNDEBUG(QStringLiteral("MainWindow::setupLocalStorageManager"));

    m_pLocalStorageManagerThread = new QThread;
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));
    m_pLocalStorageManagerThread->start();

    m_pLocalStorageManager = new LocalStorageManagerThreadWorker(*m_pAccount, /* start from scratch = */ false,
                                                                 /* override lock = */ false);
    m_pLocalStorageManager->init();
    m_pLocalStorageManager->moveToThread(m_pLocalStorageManagerThread);

    QObject::connect(this, QNSIGNAL(MainWindow,localStorageSwitchUserRequest,Account,bool,QUuid),
                     m_pLocalStorageManager, QNSLOT(LocalStorageManagerThreadWorker,onSwitchUserRequest,Account,bool,QUuid));
    QObject::connect(m_pLocalStorageManager, QNSIGNAL(LocalStorageManagerThreadWorker,switchUserComplete,Account,QUuid),
                     this, QNSLOT(MainWindow,onLocalStorageSwitchUserRequestComplete,Account,QUuid));
    QObject::connect(m_pLocalStorageManager, QNSIGNAL(LocalStorageManagerThreadWorker,switchUserFailed,Account,ErrorString,QUuid),
                     this, QNSLOT(MainWindow,onLocalStorageSwitchUserRequestFailed,Account,ErrorString,QUuid));
}

void MainWindow::setupModels()
{
    QNDEBUG(QStringLiteral("MainWindow::setupModels"));

    clearModels();

    m_pFavoritesModel = new FavoritesModel(*m_pAccount, *m_pLocalStorageManager, m_noteCache,
                                           m_notebookCache, m_tagCache, m_savedSearchCache, this);
    m_pNotebookModel = new NotebookModel(*m_pAccount, *m_pLocalStorageManager,
                                         m_notebookCache, this);
    m_pTagModel = new TagModel(*m_pAccount, *m_pLocalStorageManager, m_tagCache, this);
    m_pSavedSearchModel = new SavedSearchModel(*m_pAccount, *m_pLocalStorageManager,
                                               m_savedSearchCache, this);
    m_pNoteModel = new NoteModel(*m_pAccount, *m_pLocalStorageManager, m_noteCache,
                                 m_notebookCache, this, NoteModel::IncludedNotes::NonDeleted);
    m_pDeletedNotesModel = new NoteModel(*m_pAccount, *m_pLocalStorageManager, m_noteCache,
                                         m_notebookCache, this, NoteModel::IncludedNotes::Deleted);

    m_pNoteFilterModel = new NoteFilterModel(this);
    m_pNoteFilterModel->setSourceModel(m_pNoteModel);

    m_pNoteFiltersManager = new NoteFiltersManager(*m_pUI->filterByTagsWidget,
                                                   *m_pUI->filterByNotebooksWidget,
                                                   *m_pNoteFilterModel,
                                                   *m_pUI->filterBySavedSearchComboBox,
                                                   *m_pUI->searchQueryLineEdit,
                                                   *m_pLocalStorageManager, this);

    m_pUI->favoritesTableView->setModel(m_pFavoritesModel);
    m_pUI->notebooksTreeView->setModel(m_pNotebookModel);
    m_pUI->tagsTreeView->setModel(m_pTagModel);
    m_pUI->savedSearchesTableView->setModel(m_pSavedSearchModel);
    m_pUI->noteListView->setModel(m_pNoteFilterModel);
    m_pUI->deletedNotesTableView->setModel(m_pDeletedNotesModel);

    m_pNotebookModelColumnChangeRerouter->setModel(m_pNotebookModel);
    m_pTagModelColumnChangeRerouter->setModel(m_pTagModel);
    m_pNoteModelColumnChangeRerouter->setModel(m_pNoteFilterModel);

    if (m_pEditNoteDialogsManager) {
        m_pEditNoteDialogsManager->setNotebookModel(m_pNotebookModel);
    }
}

void MainWindow::clearModels()
{
    QNDEBUG(QStringLiteral("MainWindow::clearModels"));

    clearViews();

    if (m_pNotebookModel) {
        delete m_pNotebookModel;
        m_pNotebookModel = Q_NULLPTR;
    }

    if (m_pTagModel) {
        delete m_pTagModel;
        m_pTagModel = Q_NULLPTR;
    }

    if (m_pSavedSearchModel) {
        delete m_pSavedSearchModel;
        m_pSavedSearchModel = Q_NULLPTR;
    }

    if (m_pNoteModel) {
        delete m_pNoteModel;
        m_pNoteModel = Q_NULLPTR;
    }

    if (m_pDeletedNotesModel) {
        delete m_pDeletedNotesModel;
        m_pDeletedNotesModel = Q_NULLPTR;
    }

    if (m_pFavoritesModel) {
        delete m_pFavoritesModel;
        m_pFavoritesModel = Q_NULLPTR;
    }
}

void MainWindow::setupShowHideStartupSettings()
{
    QNDEBUG(QStringLiteral("MainWindow::setupShowHideStartupSettings"));

    ApplicationSettings appSettings;
    appSettings.beginGroup(QStringLiteral("MainWindow"));

#define CHECK_AND_SET_SHOW_SETTING(name, action, widget) \
    { \
        QVariant showSetting = appSettings.value(name); \
        if (showSetting.isNull()) { \
            showSetting = m_pUI->Action##action->isChecked(); \
        } \
        if (showSetting.toBool()) {\
            m_pUI->widget->show(); \
        } \
        else { \
            m_pUI->widget->hide(); \
        } \
        m_pUI->Action##action->setChecked(showSetting.toBool()); \
    }

    CHECK_AND_SET_SHOW_SETTING("ShowSidePanel", ShowSidePanel, sidePanelSplitter)
    CHECK_AND_SET_SHOW_SETTING("ShowFavorites", ShowFavorites, favoritesWidget)
    CHECK_AND_SET_SHOW_SETTING("ShowNotebooks", ShowNotebooks, notebooksWidget)
    CHECK_AND_SET_SHOW_SETTING("ShowTags", ShowTags, tagsWidget)
    CHECK_AND_SET_SHOW_SETTING("ShowSavedSearches", ShowSavedSearches, savedSearchesWidget)
    CHECK_AND_SET_SHOW_SETTING("ShowDeletedNotes", ShowDeletedNotes, deletedNotesWidget)
    CHECK_AND_SET_SHOW_SETTING("ShowNotesList", ShowNotesList, notesListAndFiltersFrame)
    CHECK_AND_SET_SHOW_SETTING("ShowToolbar", ShowToolbar, upperBarPanel)
    CHECK_AND_SET_SHOW_SETTING("ShowStatusBar", ShowStatusBar, upperBarPanel)

#undef CHECK_AND_SET_SHOW_SETTING

    appSettings.endGroup();
}

void MainWindow::setupViews()
{
    QNDEBUG(QStringLiteral("MainWindow::setupViews"));

    // NOTE: only a few columns would be shown for each view because otherwise there are problems finding space for everyting
    // TODO: in future should implement the persistent setting of which columns to show or not to show

    FavoriteItemView * pFavoritesTableView = m_pUI->favoritesTableView;
    FavoriteItemDelegate * favoriteItemDelegate = new FavoriteItemDelegate(pFavoritesTableView);
    pFavoritesTableView->setItemDelegate(favoriteItemDelegate);
    pFavoritesTableView->setColumnHidden(FavoritesModel::Columns::NumNotesTargeted, true);   // This column's values would be displayed along with the favorite item's name
    pFavoritesTableView->setColumnWidth(FavoritesModel::Columns::Type, favoriteItemDelegate->sideSize());

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    pFavoritesTableView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
    pFavoritesTableView->header()->setResizeMode(QHeaderView::ResizeToContents);
#endif

    QObject::connect(pFavoritesTableView, QNSIGNAL(FavoriteItemView,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onModelViewError,ErrorString));
    QObject::connect(pFavoritesTableView, QNSIGNAL(FavoriteItemView,favoritedItemInfoRequested),
                     this, QNSLOT(MainWindow,onFavoritedItemInfoButtonPressed));

    NotebookItemView * pNotebooksTreeView = m_pUI->notebooksTreeView;
    NotebookItemDelegate * notebookItemDelegate = new NotebookItemDelegate(pNotebooksTreeView);
    pNotebooksTreeView->setItemDelegate(notebookItemDelegate);
    pNotebooksTreeView->setColumnHidden(NotebookModel::Columns::NumNotesPerNotebook, true);    // This column's values would be displayed along with the notebook's name
    pNotebooksTreeView->setColumnHidden(NotebookModel::Columns::Synchronizable, true);
    pNotebooksTreeView->setColumnHidden(NotebookModel::Columns::LastUsed, true);
    DirtyColumnDelegate * notebookTreeViewDirtyColumnDelegate =
            new DirtyColumnDelegate(pNotebooksTreeView);
    pNotebooksTreeView->setItemDelegateForColumn(NotebookModel::Columns::Dirty,
                                                notebookTreeViewDirtyColumnDelegate);
    pNotebooksTreeView->setColumnWidth(NotebookModel::Columns::Dirty,
                                      notebookTreeViewDirtyColumnDelegate->sideSize());
    FromLinkedNotebookColumnDelegate * notebookTreeViewFromLinkedNotebookColumnDelegate =
            new FromLinkedNotebookColumnDelegate(pNotebooksTreeView);
    pNotebooksTreeView->setItemDelegateForColumn(NotebookModel::Columns::FromLinkedNotebook,
                                                notebookTreeViewFromLinkedNotebookColumnDelegate);
    pNotebooksTreeView->setColumnWidth(NotebookModel::Columns::FromLinkedNotebook,
                                      notebookTreeViewFromLinkedNotebookColumnDelegate->sideSize());
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    pNotebooksTreeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    QObject::connect(m_pNotebookModelColumnChangeRerouter, QNSIGNAL(ColumnChangeRerouter,dataChanged,const QModelIndex&,const QModelIndex&,const QVector<int>&),
                     pNotebooksTreeView, QNSLOT(NotebookItemView,dataChanged,const QModelIndex&,const QModelIndex&,const QVector<int>&));
#else
    pNotebooksTreeView->header()->setResizeMode(QHeaderView::ResizeToContents);
    QObject::connect(m_pNotebookModelColumnChangeRerouter, QNSIGNAL(ColumnChangeRerouter,dataChanged,const QModelIndex&,const QModelIndex&),
                     pNotebooksTreeView, QNSLOT(NotebookItemView,dataChanged,const QModelIndex&,const QModelIndex&));
#endif

    QObject::connect(pNotebooksTreeView, QNSIGNAL(NotebookItemView,newNotebookCreationRequested),
                     this, QNSLOT(MainWindow,onCreateNotebookButtonPressed));
    QObject::connect(pNotebooksTreeView, QNSIGNAL(NotebookItemView,notebookInfoRequested),
                     this, QNSLOT(MainWindow,onNotebookInfoButtonPressed));
    QObject::connect(pNotebooksTreeView, QNSIGNAL(NotebookItemView,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onModelViewError,ErrorString));

    TagItemView * pTagsTreeView = m_pUI->tagsTreeView;
    pTagsTreeView->setColumnHidden(TagModel::Columns::NumNotesPerTag, true); // This column's values would be displayed along with the notebook's name
    pTagsTreeView->setColumnHidden(TagModel::Columns::Synchronizable, true);
    DirtyColumnDelegate * tagsTreeViewDirtyColumnDelegate =
            new DirtyColumnDelegate(pTagsTreeView);
    pTagsTreeView->setItemDelegateForColumn(TagModel::Columns::Dirty,
                                           tagsTreeViewDirtyColumnDelegate);
    pTagsTreeView->setColumnWidth(TagModel::Columns::Dirty,
                                 tagsTreeViewDirtyColumnDelegate->sideSize());
    FromLinkedNotebookColumnDelegate * tagsTreeViewFromLinkedNotebookColumnDelegate =
            new FromLinkedNotebookColumnDelegate(pTagsTreeView);
    pTagsTreeView->setItemDelegateForColumn(TagModel::Columns::FromLinkedNotebook,
                                           tagsTreeViewFromLinkedNotebookColumnDelegate);
    pTagsTreeView->setColumnWidth(TagModel::Columns::FromLinkedNotebook,
                                 tagsTreeViewFromLinkedNotebookColumnDelegate->sideSize());
    TagItemDelegate * tagsTreeViewNameColumnDelegate = new TagItemDelegate(pTagsTreeView);
    pTagsTreeView->setItemDelegateForColumn(TagModel::Columns::Name, tagsTreeViewNameColumnDelegate);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    pTagsTreeView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    QObject::connect(m_pTagModelColumnChangeRerouter, QNSIGNAL(ColumnChangeRerouter,dataChanged,const QModelIndex&,const QModelIndex&,const QVector<int>&),
                     pTagsTreeView, QNSLOT(TagItemView,dataChanged,const QModelIndex&,const QModelIndex&,const QVector<int>&));
#else
    pTagsTreeView->header()->setResizeMode(QHeaderView::ResizeToContents);
    QObject::connect(m_pTagModelColumnChangeRerouter, QNSIGNAL(ColumnChangeRerouter,dataChanged,const QModelIndex&,const QModelIndex&),
                     pTagsTreeView, QNSLOT(TagItemView,dataChanged,const QModelIndex&,const QModelIndex&));
#endif

    QObject::connect(pTagsTreeView, QNSIGNAL(TagItemView,newTagCreationRequested),
                     this, QNSLOT(MainWindow,onCreateTagButtonPressed));
    QObject::connect(pTagsTreeView, QNSIGNAL(TagItemView,tagInfoRequested),
                     this, QNSLOT(MainWindow,onTagInfoButtonPressed));
    QObject::connect(pTagsTreeView, QNSIGNAL(TagItemView,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onModelViewError,ErrorString));

    SavedSearchItemView * pSavedSearchesTableView = m_pUI->savedSearchesTableView;
    pSavedSearchesTableView->setColumnHidden(SavedSearchModel::Columns::Query, true);
    pSavedSearchesTableView->setColumnHidden(SavedSearchModel::Columns::Synchronizable, true);
    DirtyColumnDelegate * savedSearchesTableViewDirtyColumnDelegate =
            new DirtyColumnDelegate(pSavedSearchesTableView);
    pSavedSearchesTableView->setItemDelegateForColumn(SavedSearchModel::Columns::Dirty,
                                                     savedSearchesTableViewDirtyColumnDelegate);
    pSavedSearchesTableView->setColumnWidth(SavedSearchModel::Columns::Dirty,
                                           savedSearchesTableViewDirtyColumnDelegate->sideSize());
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    pSavedSearchesTableView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
    pSavedSearchesTableView->header()->setResizeMode(QHeaderView::ResizeToContents);
#endif

    QObject::connect(pSavedSearchesTableView, QNSIGNAL(SavedSearchItemView,savedSearchInfoRequested),
                     this, QNSLOT(MainWindow,onSavedSearchInfoButtonPressed));
    QObject::connect(pSavedSearchesTableView, QNSIGNAL(SavedSearchItemView,newSavedSearchCreationRequested),
                     this, QNSLOT(MainWindow,onCreateSavedSearchButtonPressed));
    QObject::connect(pSavedSearchesTableView, QNSIGNAL(SavedSearchItemView,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onModelViewError,ErrorString));

    NoteListView * pNoteListView = m_pUI->noteListView;
    pNoteListView->setModelColumn(NoteModel::Columns::Title);
    pNoteListView->setItemDelegate(new NoteItemDelegate(pNoteListView));
    pNoteListView->setNotebookItemView(pNotebooksTreeView);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QObject::connect(m_pNoteModelColumnChangeRerouter, &ColumnChangeRerouter::dataChanged,
                     pNoteListView, &NoteListView::dataChanged);
#else
    QObject::connect(m_pNoteModelColumnChangeRerouter, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
                     pNoteListView, SLOT(dataChanged(QModelIndex,QModelIndex)));
#endif
    QObject::connect(pNoteListView, QNSIGNAL(NoteListView,currentNoteChanged,QString),
                     this, QNSLOT(MainWindow,onCurrentNoteInListChanged,QString));
    QObject::connect(pNoteListView, QNSIGNAL(NoteListView,openNoteInSeparateWindowRequested,QString),
                     this, QNSLOT(MainWindow,onOpenNoteInSeparateWindow,QString));

    QStringList noteSortingModes;
    noteSortingModes.reserve(8);
    noteSortingModes << tr("Created (ascending)");
    noteSortingModes << tr("Created (descending)");
    noteSortingModes << tr("Modified (ascending)");
    noteSortingModes << tr("Modified (descending)");
    noteSortingModes << tr("Title (ascending)");
    noteSortingModes << tr("Title (descending)");
    noteSortingModes << tr("Size (ascending)");
    noteSortingModes << tr("Size (descending)");

    QStringListModel * pNoteSortingModeModel = new QStringListModel(this);
    pNoteSortingModeModel->setStringList(noteSortingModes);

    m_pUI->noteSortingModeComboBox->setModel(pNoteSortingModeModel);
    QObject::connect(m_pUI->noteSortingModeComboBox, SIGNAL(currentIndexChanged(int)),
                     this, SLOT(onNoteSortingModeChanged(int)));

    restoreNoteSortingMode();
    if (m_pUI->noteSortingModeComboBox->currentIndex() < 0) {
        // Couldn't restore, fallback to the default
        m_pUI->noteSortingModeComboBox->setCurrentIndex(0);
    }

    DeletedNoteItemView * pDeletedNotesTableView = m_pUI->deletedNotesTableView;
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::CreationTimestamp, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::ModificationTimestamp, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::PreviewText, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::ThumbnailImageFilePath, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::TagNameList, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::Size, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::Synchronizable, true);
    pDeletedNotesTableView->setColumnHidden(NoteModel::Columns::NotebookName, true);
    DeletedNoteItemDelegate * pDeletedNoteItemDelegate = new DeletedNoteItemDelegate(pDeletedNotesTableView);
    pDeletedNotesTableView->setItemDelegate(pDeletedNoteItemDelegate);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    pDeletedNotesTableView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
    pDeletedNotesTableView->header()->setResizeMode(QHeaderView::ResizeToContents);
#endif

    if (!m_pEditNoteDialogsManager)
    {
        m_pEditNoteDialogsManager = new EditNoteDialogsManager(*m_pLocalStorageManager, m_noteCache, m_pNotebookModel, this);
        QObject::connect(pNoteListView, QNSIGNAL(NoteListView,editNoteDialogRequested,QString),
                         m_pEditNoteDialogsManager, QNSLOT(EditNoteDialogsManager,onEditNoteDialogRequested,QString));
        QObject::connect(pNoteListView, QNSIGNAL(NoteListView,noteInfoDialogRequested,QString),
                         m_pEditNoteDialogsManager, QNSLOT(EditNoteDialogsManager,onNoteInfoDialogRequested,QString));
        QObject::connect(pDeletedNotesTableView, QNSIGNAL(DeletedNoteItemView,deletedNoteInfoRequested,QString),
                         m_pEditNoteDialogsManager, QNSLOT(EditNoteDialogsManager,onNoteInfoDialogRequested,QString));
    }

    Account::Type::type currentAccountType = Account::Type::Local;
    if (m_pAccount) {
        currentAccountType = m_pAccount->type();
    }

    showHideViewColumnsForAccountType(currentAccountType);
}

void MainWindow::clearViews()
{
    QNDEBUG(QStringLiteral("MainWindow::clearViews"));

    m_pUI->favoritesTableView->setModel(&m_blankModel);
    m_pUI->notebooksTreeView->setModel(&m_blankModel);
    m_pUI->tagsTreeView->setModel(&m_blankModel);
    m_pUI->savedSearchesTableView->setModel(&m_blankModel);

    m_pUI->noteListView->setModel(&m_blankModel);
    m_pUI->deletedNotesTableView->setModel(&m_blankModel);
}

void MainWindow::setupNoteFilters()
{
    QNDEBUG(QStringLiteral("MainWindow::setupNoteFilters"));

    m_pUI->filterFrameBottomBoundary->hide();

    m_pUI->filterByNotebooksWidget->setLocalStorageManager(*m_pLocalStorageManager);
    m_pUI->filterByTagsWidget->setLocalStorageManager(*m_pLocalStorageManager);

    m_pUI->filterByNotebooksWidget->switchAccount(*m_pAccount, m_pNotebookModel);
    m_pUI->filterByTagsWidget->switchAccount(*m_pAccount, m_pTagModel);
    m_pUI->filterBySavedSearchComboBox->switchAccount(*m_pAccount, m_pSavedSearchModel);

    m_pUI->filterStatusBarLabel->hide();

    // TODO: set up the object which would watch for changes in these widgets and
    // set stuff to the NoteFilterModel accordingly

    QString accountKey = accountToKey(*m_pAccount);
    if (Q_UNLIKELY(accountKey.isEmpty())) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't restore "
                                            "the filters view expanded state, "
                                            "can't convert the last used account "
                                            "to the string key"));
        QNWARNING(error << QStringLiteral(", account: ") << *m_pAccount);
        onSetStatusBarText(error.localizedString());
        return;
    }

    ApplicationSettings appSettings;
    appSettings.beginGroup(accountKey + QStringLiteral("/FiltersView"));
    m_filtersViewExpanded = appSettings.value(FILTERS_VIEW_STATUS_KEY).toBool();
    appSettings.endGroup();

    // NOTE: won't apply this setting immediately: if m_filtersViewExpanded is true,
    // the default settings from the loaded UI are just what's needed. Otherwise
    // the necessary size adjustments would be done in MainWindow::show() method.
    // Why so? Because it seems trying to adjust the sizes of QSplitter before
    // the widget is shown just silently fail for unknown reason.
}

void MainWindow::setupNoteEditorTabWidgetManager()
{
    QNDEBUG(QStringLiteral("MainWindow::setupNoteEditorTabWidgetManager"));

    delete m_pNoteEditorTabsAndWindowsCoordinator;
    m_pNoteEditorTabsAndWindowsCoordinator = new NoteEditorTabsAndWindowsCoordinator(*m_pAccount, *m_pLocalStorageManager,
                                                                                     m_noteCache, m_notebookCache,
                                                                                     m_tagCache, *m_pTagModel,
                                                                                     m_pUI->noteEditorsTabWidget, this);
    QObject::connect(m_pNoteEditorTabsAndWindowsCoordinator, QNSIGNAL(NoteEditorTabsAndWindowsCoordinator,notifyError,ErrorString),
                     this, QNSLOT(MainWindow,onNoteEditorError,ErrorString));
    QObject::connect(m_pNoteEditorTabsAndWindowsCoordinator, QNSIGNAL(NoteEditorTabsAndWindowsCoordinator,currentNoteChanged,QString),
                     m_pUI->noteListView, QNSLOT(NoteListView,onCurrentNoteChanged,QString));
}

void MainWindow::setupSynchronizationManager()
{
    QNDEBUG(QStringLiteral("MainWindow::setupSynchronizationManager"));

    clearSynchronizationManager();

    if (m_synchronizationManagerHost.isEmpty()) {
        QNDEBUG(QStringLiteral("Host is empty"));
        return;
    }

    QString consumerKey, consumerSecret;
    setupConsumerKeyAndSecret(consumerKey, consumerSecret);

    if (Q_UNLIKELY(consumerKey.isEmpty())) {
        QNDEBUG(QStringLiteral("Consumer key is empty"));
        return;
    }

    if (Q_UNLIKELY(consumerSecret.isEmpty())) {
        QNDEBUG(QStringLiteral("Consumer secret is empty"));
        return;
    }

    m_pSynchronizationManagerThread = new QThread;
    QObject::connect(m_pSynchronizationManagerThread, QNSIGNAL(QThread,finished),
                     m_pSynchronizationManagerThread, QNSLOT(QThread,deleteLater));
    m_pSynchronizationManagerThread->start();

    m_pSynchronizationManager = new SynchronizationManager(consumerKey, consumerSecret,
                                                           m_synchronizationManagerHost,
                                                           *m_pLocalStorageManager);
    m_pSynchronizationManager->moveToThread(m_pSynchronizationManagerThread);
    connectSynchronizationManager();
}

void MainWindow::clearSynchronizationManager()
{
    QNDEBUG(QStringLiteral("MainWindow::clearSynchronizationManager"));

    disconnectSynchronizationManager();

    if (m_pSynchronizationManager) {
        m_pSynchronizationManager->deleteLater();
        m_pSynchronizationManager = Q_NULLPTR;
    }

    if (m_pSynchronizationManagerThread) {
        m_pSynchronizationManagerThread->quit();    // The thread would delete itself after it's finished
        m_pSynchronizationManagerThread = Q_NULLPTR;
    }
}

void MainWindow::setupDefaultShortcuts()
{
    QNDEBUG(QStringLiteral("MainWindow::setupDefaultShortcuts"));

    using quentier::ShortcutManager;

#define PROCESS_ACTION_SHORTCUT(action, key, ...) \
    { \
        QKeySequence shortcut = m_pUI->Action##action->shortcut(); \
        if (shortcut.isEmpty()) { \
            QNTRACE(QStringLiteral("No shortcut was found for action " #action)); \
        } \
        else { \
            m_shortcutManager.setDefaultShortcut(key, shortcut, *m_pAccount, QStringLiteral("" #__VA_ARGS__)); \
        } \
    }

#define PROCESS_NON_STANDARD_ACTION_SHORTCUT(action, ...) \
    { \
        QKeySequence shortcut = m_pUI->Action##action->shortcut(); \
        if (shortcut.isEmpty()) { \
            QNTRACE(QStringLiteral("No shortcut was found for action " #action)); \
        } \
        else { \
            m_shortcutManager.setNonStandardDefaultShortcut(#action, shortcut, *m_pAccount, QStringLiteral("" #__VA_ARGS__)); \
        } \
    }

#include "ActionShortcuts.inl"

#undef PROCESS_NON_STANDARD_ACTION_SHORTCUT
#undef PROCESS_ACTION_SHORTCUT
}

void MainWindow::setupUserShortcuts()
{
    QNDEBUG(QStringLiteral("MainWindow::setupUserShortcuts"));

#define PROCESS_ACTION_SHORTCUT(action, key, ...) \
    { \
        QKeySequence shortcut = m_shortcutManager.shortcut(key, *m_pAccount, QStringLiteral("" #__VA_ARGS__)); \
        if (shortcut.isEmpty()) { \
            QNTRACE(QStringLiteral("No shortcut was found for action " #action)); \
        } \
        else { \
            m_pUI->Action##action->setShortcut(shortcut); \
            m_pUI->Action##action->setShortcutContext(Qt::WidgetWithChildrenShortcut); \
        } \
    }

#define PROCESS_NON_STANDARD_ACTION_SHORTCUT(action, ...) \
    { \
        QKeySequence shortcut = m_shortcutManager.shortcut(#action, *m_pAccount, QStringLiteral("" #__VA_ARGS__)); \
        if (shortcut.isEmpty()) { \
            QNTRACE(QStringLiteral("No shortcut was found for action " #action)); \
        } \
        else { \
            m_pUI->Action##action->setShortcut(shortcut); \
            m_pUI->Action##action->setShortcutContext(Qt::WidgetWithChildrenShortcut); \
        } \
    }

#include "ActionShortcuts.inl"

#undef PROCESS_NON_STANDARD_ACTION_SHORTCUT
#undef PROCESS_ACTION_SHORTCUT
}

void MainWindow::setupConsumerKeyAndSecret(QString & consumerKey, QString & consumerSecret)
{
    const char key[10] = "e3zA914Ol";

    QByteArray consumerKeyObf = QByteArray::fromBase64(QStringLiteral("ZVYsYCHKtSuDnK0g0swrUYAHzYy1m1UeVw==").toUtf8());
    char lastChar = 0;
    int size = consumerKeyObf.size();
    for(int i = 0; i < size; ++i) {
        char currentChar = consumerKeyObf[i];
        consumerKeyObf[i] = consumerKeyObf[i] ^ lastChar ^ key[i % 8];
        lastChar = currentChar;
    }

    consumerKeyObf = qUncompress(consumerKeyObf);
    consumerKey = QString::fromUtf8(consumerKeyObf.constData(), consumerKeyObf.size());

    QByteArray consumerSecretObf = QByteArray::fromBase64(QStringLiteral("ZVYsfTzX0KqA+jbDsjC0T2ZnKiRT0+Os7AN9uQ==").toUtf8());
    lastChar = 0;
    size = consumerSecretObf.size();
    for(int i = 0; i < size; ++i) {
        char currentChar = consumerSecretObf[i];
        consumerSecretObf[i] = consumerSecretObf[i] ^ lastChar ^ key[i % 8];
        lastChar = currentChar;
    }

    consumerSecretObf = qUncompress(consumerSecretObf);
    consumerSecret = QString::fromUtf8(consumerSecretObf.constData(), consumerSecretObf.size());
}

void MainWindow::persistChosenNoteSortingMode(int index)
{
    QNDEBUG(QStringLiteral("MainWindow::persistChosenNoteSortingMode: index = ") << index);

    QString accountKey = accountToKey(*m_pAccount);
    if (Q_UNLIKELY(accountKey.isEmpty())) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't persist "
                                            "the selected note sorting mode, "
                                            "can't convert the current account "
                                            "to the string key"));
        QNWARNING(error << QStringLiteral(", account: ") << *m_pAccount);
        onSetStatusBarText(error.localizedString());
        return;
    }

    ApplicationSettings appSettings;
    appSettings.beginGroup(accountKey + QStringLiteral("/NoteListView"));
    appSettings.setValue(NOTE_SORTING_MODE_KEY, index);
    appSettings.endGroup();
}

void MainWindow::restoreNoteSortingMode()
{
    QNDEBUG(QStringLiteral("MainWindow::restoreNoteSortingMode"));

    QString accountKey = accountToKey(*m_pAccount);
    if (Q_UNLIKELY(accountKey.isEmpty())) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't restore "
                                            "the last used note sorting mode, "
                                            "can't convert the current account "
                                            "to the string key"));
        QNWARNING(error << QStringLiteral(", account: ") << *m_pAccount);
        onSetStatusBarText(error.localizedString());
        return;
    }

    ApplicationSettings appSettings;
    appSettings.beginGroup(accountKey + QStringLiteral("/NoteListView"));
    QVariant data = appSettings.value(NOTE_SORTING_MODE_KEY);
    appSettings.endGroup();

    if (data.isNull()) {
        QNDEBUG(QStringLiteral("No persisted note sorting mode, nothing to restore"));
        return;
    }

    bool conversionResult = false;
    int index = data.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: can't restore "
                                            "the last used note sorting mode, "
                                            "can't convert the persisted setting "
                                            "to the integer index"));
        QNWARNING(error << QStringLiteral(", persisted data: ") << data);
        onSetStatusBarText(error.localizedString());
        return;
    }

    m_pUI->noteSortingModeComboBox->setCurrentIndex(index);
}

template <class T>
void MainWindow::refreshThemeIcons()
{
    QList<T*> objects = findChildren<T*>();
    QNDEBUG(QStringLiteral("Found ") << objects.size() << QStringLiteral(" child objects"));

    for(auto it = objects.constBegin(), end = objects.constEnd(); it != end; ++it)
    {
        T * object = *it;
        if (Q_UNLIKELY(!object)) {
            continue;
        }

        QIcon icon = object->icon();
        if (icon.isNull()) {
            continue;
        }

        QString iconName = icon.name();
        if (iconName.isEmpty()) {
            continue;
        }

        QIcon newIcon;
        if (QIcon::hasThemeIcon(iconName)) {
            newIcon = QIcon::fromTheme(iconName);
        }
        else {
            newIcon.addFile(QStringLiteral("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        object->setIcon(newIcon);
    }
}
