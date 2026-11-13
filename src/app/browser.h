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
#include "foldermodel.h"
#include "panewidths.h"
#include "pinnedfolders.h"
#include "places.h"
#include "preferences.h"
#include "preview.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
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

public:
    enum View { List, Grid, Columns };
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
    // Change Icon: the chooser of theme icons (or an image file's path, typed) for `path`.
    void changeIcon(const QString &path);
    // Reset Icon: `path` gets its usual icon again.
    void resetIcon(const QString &path);

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
    // "list", "grid" or "columns": also the objectName of the item that shows the view.
    QString viewName() const;

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
    // Points the details and the preview at what the keys are on now.
    void followFocus();

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

    QString m_findText;
    QElapsedTimer m_findTimer;

    std::unique_ptr<Riley::PlacesModel> m_places;
    std::unique_ptr<Riley::PinnedModel> m_pinned;
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
};
