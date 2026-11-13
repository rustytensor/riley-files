// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "actions.h"
#include "chooser.h"
#include "customicons.h"
#include "details.h"
#include "fileops.h"
#include "foldercolumn.h"
#include "desktopentry.h"
#include "drives.h"
#include "filesearch.h"
#include "foldermodel.h"
#include "graphbuilder.h"
#include "networkplaces.h"
#include "panewidths.h"
#include "pinnedfolders.h"
#include "places.h"
#include "preferences.h"
#include "preview.h"
#include "recentfiles.h"
#include "trashmodel.h"
#include "versionmodel.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

class QSettings;

// The window's state and every decision about it: which folder is shown, where the cursor is,
// the way back and forward, the view and the tree. QML shows what this says and passes the keys
// it gets to handleKey(); it decides nothing itself.
class Browser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The program makes the browser; QML only shows it.")

    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderChanged)
    Q_PROPERTY(Riley::FolderModel *model READ model CONSTANT)
    Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY cursorChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY folderChanged)
    Q_PROPERTY(View view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString viewName READ viewName NOTIFY viewChanged)
    Q_PROPERTY(bool tree READ tree WRITE setTree NOTIFY treeChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int pageRows READ pageRows WRITE setPageRows NOTIFY pageRowsChanged)
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY gridColumnsChanged)
    Q_PROPERTY(QList<QObject *> columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(int activeColumn READ activeColumn NOTIFY activeColumnChanged)
    Q_PROPERTY(FileOps *ops READ ops CONSTANT)

    // The graph view (Ctrl+4): the folder shown and everything below it, built off the UI thread
    // while the view is shown. graphNode is the node the keys are on (0, the folder itself, when
    // a graph is built; -1 before), graphHover the one under the mouse (-1 for none), and
    // graphLabel what is said about the hovered node, or else the selected one, "a · 1 file".
    Q_PROPERTY(Riley::GraphBuilder *graph READ graph CONSTANT)
    Q_PROPERTY(int graphNode READ graphNode NOTIFY graphNodeChanged)
    Q_PROPERTY(int graphHover READ graphHover NOTIFY graphNodeChanged)
    Q_PROPERTY(int graphLabelNode READ graphLabelNode NOTIFY graphNodeChanged)
    Q_PROPERTY(QString graphLabel READ graphLabel NOTIFY graphNodeChanged)

    // The sidebar: places, then pinned folders. The keyboard is in it or in the files (Tab).
    // Its cursor counts the places first, then the pinned folders after them.
    Q_PROPERTY(Riley::PlacesModel *places READ places CONSTANT)
    Q_PROPERTY(Riley::PinnedModel *pinned READ pinned CONSTANT)
    Q_PROPERTY(bool sidebarFocus READ sidebarFocus NOTIFY sidebarChanged)
    Q_PROPERTY(int sidebarCursor READ sidebarCursor NOTIFY sidebarChanged)
    // The place and the pinned folder that are the folder shown, or -1.
    Q_PROPERTY(int markedPlace READ markedPlace NOTIFY sidebarChanged)
    Q_PROPERTY(int markedPin READ markedPin NOTIFY sidebarChanged)
    Q_PROPERTY(bool folderPinned READ folderPinned NOTIFY sidebarChanged)
    // The drives the person mounted, after the pinned folders in the sidebar (and in its
    // cursor); the section shows only when there is one. markedDrive is the one shown, or -1.
    Q_PROPERTY(Riley::DrivesModel *drives READ drives CONSTANT)
    Q_PROPERTY(int markedDrive READ markedDrive NOTIFY sidebarChanged)
    // The servers connected through GVfs, after the drives (Connect to Server in the File menu).
    Q_PROPERTY(Riley::NetworkModel *network READ network CONSTANT)
    Q_PROPERTY(int markedNetwork READ markedNetwork NOTIFY sidebarChanged)
    // The Recent row (shown among the places, before the Trash; in the keys' order after the
    // pinned folders): the files used lately.
    Q_PROPERTY(bool markedRecent READ markedRecent NOTIFY sidebarChanged)

    // A place that is not a folder, shown in the folder's place: "trash" (everything in every
    // trash, with Restore, Delete for good and Empty Trash) or "recent"; empty for a folder.
    // placeRow is the row its keys are on, or -1.
    Q_PROPERTY(QString place READ place NOTIFY placeChanged)
    Q_PROPERTY(int placeRow READ placeRow WRITE setPlaceRow NOTIFY placeRowChanged)
    Q_PROPERTY(Riley::TrashModel *trash READ trash CONSTANT)
    Q_PROPERTY(Riley::RecentFiles *recent READ recent CONSTANT)
    // The previous versions of the file or folder the keys are on, from the snapshots above it.
    // Its path follows the keys; while they are listed (the place "versions", File > Previous
    // Versions, Alt+V) it stays on the one they are of, versionsOf.
    Q_PROPERTY(Riley::VersionModel *versions READ versions CONSTANT)
    Q_PROPERTY(QString versionsOf READ versionsOf NOTIFY placeChanged)
    // The Connect to Server field, in front of the window.
    Q_PROPERTY(bool connectShown READ connectShown NOTIFY connectShownChanged)

    // The details pane on the right (Ctrl+I), about the row under the cursor.
    Q_PROPERTY(bool detailsShown READ detailsShown WRITE setDetailsShown NOTIFY detailsShownChanged)
    Q_PROPERTY(FileDetails *details READ details CONSTANT)
    // The preview overlay (Space).
    Q_PROPERTY(Preview *preview READ preview CONSTANT)

    // Every command, its key and the menus that hold it (see Actions).
    Q_PROPERTY(Actions *actions READ actions CONSTANT)
    // The sidebar can be hidden (F9).
    Q_PROPERTY(bool sidebarShown READ sidebarShown WRITE setSidebarShown NOTIFY sidebarShownChanged)
    // The folder's path being typed in the header (Ctrl+L).
    Q_PROPERTY(bool locationEditing READ locationEditing NOTIFY locationEditingChanged)
    // The list of every action and its key, over the window (F1).
    Q_PROPERTY(bool shortcutsShown READ shortcutsShown WRITE setShortcutsShown NOTIFY shortcutsShownChanged)
    // How the panes get their widths: "fixed", "automatic" or "proportional".
    Q_PROPERTY(QString paneWidths READ paneWidths WRITE setPaneWidths NOTIFY paneWidthsChanged)
    // The widths themselves, and the dividers that drag them.
    Q_PROPERTY(PaneWidths *panes READ panes CONSTANT)
    // The Preferences panel (Ctrl+,) and the options it sets.
    Q_PROPERTY(Preferences *prefs READ prefs CONSTANT)
    // A question before something is done (moving to the trash, when the person asked for it):
    // its text, or empty when none is asked. Return says yes, Escape no.
    Q_PROPERTY(QString question READ question NOTIFY questionChanged)
    // The searchable list in front of the window: an icon (Change Icon) or an application
    // (Open With > Other Application).
    Q_PROPERTY(Chooser *chooser READ chooser CONSTANT)
    // Goes up by one whenever a chosen icon changes; the rows pass it to customIconSource() so
    // they ask again.
    Q_PROPERTY(int iconsRevision READ iconsRevision NOTIFY iconsChanged)

    // Search below the folder shown ('/', Ctrl+F for names, Ctrl+Shift+F inside files). While it
    // is shown, its results take the folder's place and its field in the header takes the typing.
    Q_PROPERTY(Riley::FileSearch *search READ search CONSTANT)
    Q_PROPERTY(bool searchShown READ searchShown NOTIFY searchShownChanged)
    Q_PROPERTY(bool searchContents READ searchContents WRITE setSearchContents NOTIFY searchModeChanged)
    Q_PROPERTY(QString searchText READ searchText NOTIFY searchTextChanged)
    // The result the keys are on, or -1.
    Q_PROPERTY(int searchCursor READ searchCursor WRITE setSearchCursor NOTIFY searchCursorChanged)
    // One line about the search: how many were found, that it is still looking, or why it cannot.
    Q_PROPERTY(QString searchStatus READ searchStatus NOTIFY searchStatusChanged)
    // What the field shows while nothing is typed.
    Q_PROPERTY(QString searchHint READ searchHint NOTIFY searchModeChanged)

public:
    enum View { List, Grid, Columns, Graph };
    Q_ENUM(View)

    // Reads and keeps its choices in `settingsFile` (an INI file), or when it is empty in
    // $XDG_CONFIG_HOME/riley-files/riley-files.conf (see defaultSettingsFile()).
    explicit Browser(const QString &settingsFile = QString(), QObject *parent = nullptr);
    ~Browser() override;

    static QString defaultSettingsFile();

    QString folder() const { return m_model->path(); }
    QString folderName() const;
    Riley::FolderModel *model() const { return m_model.get(); }
    // Copy, paste, rename, trash, undo: what the file keys do.
    FileOps *ops() const { return m_ops.get(); }

    Riley::PlacesModel *places() const { return m_places.get(); }
    Riley::PinnedModel *pinned() const { return m_pinned.get(); }
    Riley::DrivesModel *drives() const { return m_drives.get(); }
    int markedDrive() const;
    // Unmounts and powers off the drive on that row of the drives (its eject mark); what went
    // wrong is said in the message bar, in udisksctl's words.
    Q_INVOKABLE void ejectDrive(int row);
    Riley::NetworkModel *network() const { return m_network.get(); }
    int markedNetwork() const;
    bool markedRecent() const { return m_place == QLatin1String("recent"); }

    // The trash view and the recent files (browserplaces.cpp).
    QString place() const { return m_place; }
    int placeRow() const { return m_placeRow; }
    void setPlaceRow(int row);
    Riley::TrashModel *trash() const { return m_trash.get(); }
    Riley::RecentFiles *recent() const { return m_recent.get(); }
    // Shows everything in the trash, read again; or the recent files; or the folder again.
    Q_INVOKABLE void showTrash();
    Q_INVOKABLE void showRecent();
    Q_INVOKABLE void leavePlace();
    // A click on a row of the place shown puts its keys there; a double-click opens it (a
    // recent file) or restores it (a thing in the trash).
    Q_INVOKABLE void clickPlaceRow(int row);
    Q_INVOKABLE void openPlaceRow(int row);
    // The trash view's buttons: the thing on that row put back where it came from (a taken
    // name is asked about), deleted for good after asking, and every thing deleted for good
    // after asking.
    Q_INVOKABLE void restoreRow(int row);
    Q_INVOKABLE void deleteRowForGood(int row);
    Q_INVOKABLE void emptyTrash();
    // Previous versions (browserversions.cpp). The list of them for what the keys are on, in
    // the folder's place: Return restores the one chosen after asking (a folder's is opened, to
    // look through it read-only), Escape closes the list.
    Riley::VersionModel *versions() const { return m_versions.get(); }
    QString versionsOf() const { return m_versionsOf; }
    Q_INVOKABLE void showVersions();
    // Asks "Restore this version?" and on yes puts that row's copy in the live one's place (the
    // live one goes to the trash first); asCopy copies it beside the live one, without asking.
    Q_INVOKABLE void restoreVersion(int row, bool asCopy);
    // The context menu of a row of the versions: Restore, Restore as Copy, Open.
    Q_INVOKABLE void versionMenuAt(int row, qreal x, qreal y);
    // Every trash folder there is: the home trash and each drive's own.
    QStringList trashFolders() const;
    // The home trash's files/, the folder the Trash place stands for.
    static QString trashPlacePath();

    // Connect to Server: the field, and what is typed in it (an sftp, smb, ftp, dav or davs
    // address, given to "gio mount").
    bool connectShown() const { return m_connectShown; }
    Q_INVOKABLE void openConnect();
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void connectToServer(const QString &uri);

    bool sidebarFocus() const { return m_sidebarFocus; }
    // Moves the keyboard into the sidebar (its cursor on the place shown, if any) or back.
    Q_INVOKABLE void setSidebarFocus(bool on);
    int sidebarCursor() const { return m_sidebarCursor; }
    int markedPlace() const;
    int markedPin() const;
    bool folderPinned() const;
    // Opens a sidebar row (places first, then pinned folders) and gives the keys back to the
    // files.
    Q_INVOKABLE void openSidebarRow(int row);
    // Pins the folder shown, or unpins it when it is pinned (Ctrl+D).
    Q_INVOKABLE void togglePin();

    bool detailsShown() const { return m_detailsShown; }
    void setDetailsShown(bool shown);
    FileDetails *details() const { return m_details.get(); }
    Preview *preview() const { return m_preview.get(); }
    // The path the keys are on: the row under the active cursor, or else the folder shown.
    QString focusPath() const;

    Actions *actions() const { return m_actions.get(); }
    bool sidebarShown() const { return m_sidebarShown; }
    void setSidebarShown(bool shown);
    bool locationEditing() const { return m_locationEditing; }
    // Opens what was typed in the header: a folder, or a file's folder with the cursor on it;
    // "~" is the home folder and a relative path starts at the folder shown.
    Q_INVOKABLE void openTypedLocation(const QString &text);
    Q_INVOKABLE void cancelLocation();
    bool shortcutsShown() const { return m_shortcutsShown; }
    void setShortcutsShown(bool shown);
    QString paneWidths() const { return m_paneWidths; }
    void setPaneWidths(const QString &mode);
    PaneWidths *panes() const { return m_panes.get(); }
    Preferences *prefs() const { return m_prefs.get(); }
    QString question() const { return m_question; }
    // Answers the question asked: yes does what it was about.
    Q_INVOKABLE void answer(bool yes);

    Chooser *chooser() const { return m_chooser.get(); }
    int iconsRevision() const { return m_iconsRevision; }
    // The icon chosen for `path` (a theme icon's name or an image file), or empty; and the
    // Image source that shows it, or empty. `revision` is iconsRevision, for the binding.
    QString customIcon(const QString &path) const;
    Q_INVOKABLE QString customIconSource(const QString &path, int revision = 0) const;
    // What a file holds, by its name, for its icon ("folder" for a folder); the rows outside the
    // folder (search results, the trash, the recent files) use it.
    Q_INVOKABLE QString categoryOf(const QString &name, bool isDir) const
    {
        return Riley::FolderModel::categoryOfName(name, isDir);
    }
    // Change Icon: the chooser of theme icons (or an image file's path, typed) for `path`.
    void changeIcon(const QString &path);
    // Reset Icon: `path` gets its usual icon again.
    void resetIcon(const QString &path);

    Riley::FileSearch *search() const { return m_search.get(); }
    bool searchShown() const { return m_searchShown; }
    bool searchContents() const { return m_searchContents; }
    void setSearchContents(bool contents);
    QString searchText() const { return m_searchText; }
    int searchCursor() const { return m_searchCursor; }
    void setSearchCursor(int row);
    QString searchStatus() const;
    QString searchHint() const;
    // Opens the search field (names, or inside files) with nothing typed yet; when it is open
    // already, switches it to that mode.
    void openSearch(bool contents);
    // Closes the search and shows the folder again, as it was.
    Q_INVOKABLE void closeSearch();
    // What is typed in the search field; each change starts the search again.
    Q_INVOKABLE void setSearchText(const QString &text);
    // A key pressed in the search field. True when it was used; false leaves it to the field.
    Q_INVOKABLE bool searchFieldKey(int key, int modifiers, const QString &text);
    // A click on a result, and a double-click: the cursor goes there; a double-click goes to the
    // result's folder with the cursor on it.
    Q_INVOKABLE void clickSearchRow(int row);
    Q_INVOKABLE void openSearchRow(int row);
    // A matching line as styled text: every character escaped, the matches (start and length
    // pairs) in bold and in `color` (a #rrggbb colour).
    Q_INVOKABLE static QString markMatches(const QString &text, const QVariantList &matches,
                                           const QString &color);

    // Opens files with their default application (the first one for their type).
    void openFiles(const QStringList &paths);

    // A click on a row of a view, and a double-click: the cursor goes there, and the row opens
    // on a double-click, or on one click when the Preferences say so.
    Q_INVOKABLE void clickRow(int row);
    Q_INVOKABLE void doubleClickRow(int row);

    // The context menu for a row of the folder shown (it gets the cursor) or, for row -1, for
    // the empty space around the rows. `x` and `y` are where it opens, in the window.
    Q_INVOKABLE void contextMenuAt(int row, qreal x, qreal y);
    // The context menu from the keyboard (the Menu key, Shift+F10): for the row under the
    // cursor, or for the folder when there is none.
    void contextMenuFromKeys();
    // The row under the active cursor, as Return opens it: a folder is shown, a file is opened
    // with its default application.
    void openUnderCursor();

    // The row the keys act on; -1 when the folder has no rows (yet).
    int cursor() const { return m_cursor; }
    void setCursor(int row);
    Q_INVOKABLE QString cursorPath() const { return m_cursorPath; }

    bool canGoBack() const { return m_historyAt > 0; }
    bool canGoForward() const { return m_historyAt + 1 < m_history.size(); }
    bool canGoUp() const;

    View view() const { return m_view; }
    void setView(View view);
    // "list", "grid", "columns" or "graph": also the objectName of the item that shows the view.
    QString viewName() const;

    // The graph view (browsergraph.cpp).
    Riley::GraphBuilder *graph() const { return m_graph.get(); }
    int graphNode() const { return m_graphNode; }
    int graphHover() const { return m_graphHover; }
    int graphLabelNode() const { return m_graphHover >= 0 ? m_graphHover : m_graphNode; }
    QString graphLabel() const;
    // The mouse over a node of the graph (-1: over none), a click on one (the keys go there) and
    // a double-click (it opens, as Return does).
    Q_INVOKABLE void hoverGraphNode(int node);
    Q_INVOKABLE void clickGraphNode(int node);
    Q_INVOKABLE void openGraphNode(int node);
    // Whether the graph can be drawn in `window` (a QQuickWindow): its scene is drawn through a
    // 3D graphics API, not in software. RILEY_FILES_NO_3D set in the environment says no, so
    // the view's other face can be seen and tested.
    Q_INVOKABLE bool graphCanDraw(QObject *window) const;

    // Folders open in place in the list view. Off by default; turning it off closes them.
    bool tree() const { return m_tree; }
    void setTree(bool on);

    bool showHidden() const;
    void setShowHidden(bool show);

    // How many rows Page Up and Page Down move; the view sets it from its height.
    int pageRows() const { return m_pageRows; }
    void setPageRows(int rows);

    // How many tiles fit in a row of the grid view; the grid sets it from its width.
    int gridColumns() const { return m_gridColumns; }
    void setGridColumns(int tiles);

    // The columns view: the folder first, then each folder opened from it, left to right
    // (FolderColumn objects). The keys act on the active column.
    QList<QObject *> columns() const;
    int activeColumn() const { return m_activeColumn; }

    // Opens what the program was given to start with: a folder, a file (its folder, with the
    // cursor on it) or a file:// URL. Empty means the home folder.
    Q_INVOKABLE void openLocation(const QString &location);

    // Shows `path` as a new step in the history. When `cursorOn` is a row's path, the cursor
    // goes there once the rows are in; otherwise to the first row.
    Q_INVOKABLE void open(const QString &path, const QString &cursorOn = QString());
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    // The folder that holds this one, with the cursor on the folder just left.
    Q_INVOKABLE void goUp();
    // Opens the row: a folder is shown, a file opened with its default application.
    Q_INVOKABLE void activate(int row);

    // The Image source of a row's thumbnail, or empty for a folder (folders keep their icon).
    Q_INVOKABLE QString thumbnailSource(const QString &path, bool isDir, const QDateTime &modified) const;

    // One key press from the window. Returns true when it was used.
    Q_INVOKABLE bool handleKey(int key, int modifiers, const QString &text);

    // What the file keys act on: the selection of the active folder, or else the row under its
    // cursor. Empty when there is neither.
    QStringList targetPaths() const;
    // The folder pasted into and where new folders are made: the one the keys are in.
    QString workFolder() const;

    // Puts the cursor on path as soon as its row is shown (now, or once the folder has read it);
    // with edit, its name is then edited. Nothing when path is not in a folder shown here.
    void aimCursor(const QString &path, bool edit = false);

signals:
    void folderChanged();
    void cursorChanged();
    void historyChanged();
    void viewChanged();
    void treeChanged();
    void showHiddenChanged();
    void pageRowsChanged();
    void gridColumnsChanged();
    void columnsChanged();
    void activeColumnChanged();
    void sidebarChanged();
    void detailsShownChanged();
    void sidebarShownChanged();
    void locationEditingChanged();
    void shortcutsShownChanged();
    void paneWidthsChanged();
    void questionChanged();
    void iconsChanged();
    void searchShownChanged();
    void searchModeChanged();
    void searchTextChanged();
    void searchCursorChanged();
    void searchStatusChanged();
    void placeChanged();
    void placeRowChanged();
    void connectShownChanged();
    void graphNodeChanged();

    // Asks for what the window, not the browser, does: another window on `folder`, and closing
    // this one.
    void newWindowAsked(const QString &folder);
    void closeWindowAsked();

private:
    struct Step
    {
        QString path;
        QString cursorPath; // where the cursor was when this folder was left
    };

    void show(const QString &path, const QString &cursorOn);
    void rememberCursor();
    void placeCursor(int row);
    void followRows();

    bool listKey(int key, Qt::KeyboardModifiers mods);
    bool gridKey(int key);
    bool columnsKey(int key);
    bool graphKey(int key);
    void startGraph();
    void setGraphNode(int node);
    bool columnsRight();
    bool columnsLeft();
    void previewUnderCursor();
    void appendColumn(const QString &path);
    void closeColumnsAfter(int column);
    void setActiveColumn(int column);
    void flattenTree();

    // The model and cursor the keys act on: the folder's own, or the active column's.
    Riley::FolderModel *activeModel() const;
    int activeCursor() const;
    void setActiveCursor(int row);
    bool treeRight();
    bool treeLeft();
    bool typeToFind(const QString &text);
    // Registers every command with its key and builds the menus (browseractions.cpp).
    void addActions();
    // Registers every option of the Preferences and what each changes (browserprefs.cpp).
    void addPreferences();
    void applyPreference(const QString &id, const QString &value);
    // Open With: the applications for the file under the cursor, the default first, then
    // Always Use for This Type and Other Application (browserapps.cpp).
    QList<Actions::Entry> openWithEntries();
    void launch(const Riley::DesktopEntry &app, const QStringList &paths);
    void chooseOtherApp(const QStringList &paths);
    // Moves the targets to the trash, after asking when the Preferences say so.
    void trashTargets();
    void ask(const QString &text, std::function<void()> yes);
    // The automatic pane widths measure the rows' names.
    void measureSidebar();
    void measureColumns();
    void setSortKey(Riley::FolderModel::Sort key);
    void editLocation();
    void showAbout();
    bool shortcutsKey(int key, Qt::KeyboardModifiers mods);
    bool questionKey(int key, Qt::KeyboardModifiers mods);
    bool landAim();
    bool sidebarKey(int key, Qt::KeyboardModifiers mods);
    bool previewKey(int key, Qt::KeyboardModifiers mods);
    void stepPreview(int by);
    void setSidebarCursor(int row);
    // How many rows the sidebar's cursor goes through: places, pinned folders, Recent, drives,
    // network places.
    int sidebarRowCount() const;
    // Points the details and the preview at what the keys are on now.
    void followFocus();
    // Search (browsersearch.cpp).
    void runSearch();
    void followSearchRows();
    bool searchKey(int key, Qt::KeyboardModifiers mods);
    bool searchAllows(const QString &actionId) const;
    bool somethingInFront() const;
    // The trash view and the recent files (browserplaces.cpp).
    void setPlace(const QString &place);
    void reloadTrash();
    void followPlaceRows();
    bool placeKey(int key, Qt::KeyboardModifiers mods);
    bool placeAllows(const QString &actionId) const;
    int placeCount() const;
    QString placePathAt(int row) const;
    // Deletes the targets for good (Shift+Delete), after asking.
    void deleteTargetsForGood();
    void askDeleteForGood(const QStringList &paths, const QString &name);
    // True (after saying why) when the action would change something inside a snapshot.
    bool refusedInSnapshot(const QString &actionId);
    bool inSnapshot() const;
    void openVersionRow(int row);
    QList<Actions::Entry> versionMenu() const;
    // Where the sidebar's sections start in its cursor.
    int recentSidebarRow() const;
    int drivesSidebarRow() const { return recentSidebarRow() + 1; }
    int networkSidebarRow() const;

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<Riley::FolderModel> m_model;
    std::unique_ptr<FileOps> m_ops;

    int m_cursor = -1;
    QString m_cursorPath;     // the path the cursor is on, or is waiting for
    bool m_cursorWaiting = false;
    bool m_rootLoaded = false;
    QString m_aimPath;        // a row the cursor goes to once it is shown
    bool m_aimEdit = false;   // and whose name is then edited

    QList<Step> m_history;
    qsizetype m_historyAt = -1;

    View m_view = List;
    bool m_tree = false;
    int m_pageRows = 10;
    int m_gridColumns = 1;

    QList<FolderColumn *> m_columns; // the first one shows m_model
    int m_activeColumn = 0;

    std::unique_ptr<Riley::GraphBuilder> m_graph;
    int m_graphNode = -1;
    int m_graphHover = -1;
    QString m_graphPath; // the path the graph's keys are on, kept when the graph is built again

    QString m_findText;
    QElapsedTimer m_findTimer;

    std::unique_ptr<Riley::PlacesModel> m_places;
    std::unique_ptr<Riley::PinnedModel> m_pinned;
    std::unique_ptr<Riley::DrivesModel> m_drives;
    std::unique_ptr<Riley::NetworkModel> m_network;
    std::unique_ptr<Riley::TrashModel> m_trash;
    std::unique_ptr<Riley::RecentFiles> m_recent;
    QString m_place;
    int m_placeRow = -1;
    QString m_placeRowPath; // the path the place's keys are on, kept as its rows change
    std::unique_ptr<Riley::VersionModel> m_versions;
    QString m_versionsOf;
    bool m_connectShown = false;
    bool m_sidebarFocus = false;
    int m_sidebarCursor = 0;

    std::unique_ptr<FileDetails> m_details;
    std::unique_ptr<Preview> m_preview;
    bool m_detailsShown = false;
    QString m_followed; // the path the details and preview were last pointed at

    std::unique_ptr<Actions> m_actions;
    bool m_sidebarShown = true;
    bool m_locationEditing = false;
    bool m_shortcutsShown = false;
    QString m_paneWidths = QStringLiteral("fixed");
    std::unique_ptr<PaneWidths> m_panes;
    std::unique_ptr<Preferences> m_prefs;
    QString m_question;
    std::function<void()> m_onYes;
    std::unique_ptr<Riley::CustomIcons> m_icons;
    std::unique_ptr<Chooser> m_chooser;
    int m_iconsRevision = 0;

    std::unique_ptr<Riley::FileSearch> m_search;
    bool m_searchShown = false;
    bool m_searchContents = false;
    QString m_searchText;
    int m_searchCursor = -1;
    QString m_searchCursorPath; // the result the cursor is on (path and line), kept as rows come in
    int m_searchCursorLine = 0;
};
