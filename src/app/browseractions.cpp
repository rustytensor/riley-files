// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The browser's commands: each with its name, its key, when it can be used and what it does,
// and the menus that show them. The keys, the menu bar, the context menus and the list of
// shortcuts (F1) all come from what is registered here.

#include "browser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using Riley::FolderModel;
using Entry = Actions::Entry;
using namespace Qt::StringLiterals;

namespace {

Actions::Key key(int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return Actions::Key{k, mods};
}

constexpr Qt::KeyboardModifiers ctrl = Qt::ControlModifier;
constexpr Qt::KeyboardModifiers ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;
constexpr Qt::KeyboardModifiers alt = Qt::AltModifier;

} // namespace

void Browser::addActions()
{
    Actions &a = *m_actions;
    const auto hasTarget = [this] { return !targetPaths().isEmpty(); };
    const auto hasRow = [this] { return !activeModel()->pathAt(activeCursor()).isEmpty(); };

    // File
    a.add({u"new-window"_s, u"New Window"_s, key(Qt::Key_N, ctrl), {},
           [this] { emit newWindowAsked(folder()); }, {}, {}});
    a.add({u"new-folder"_s, u"New Folder"_s, key(Qt::Key_N, ctrlShift), {},
           [this] { m_ops->newFolder(workFolder()); }, {}, {}});
    a.add({u"open"_s, u"Open"_s, key(Qt::Key_Return), {key(Qt::Key_Enter), key(Qt::Key_Down, alt)},
           [this] { openUnderCursor(); }, hasRow, {}});
    a.add({u"preview"_s, u"Preview"_s, key(Qt::Key_Space), {},
           [this] {
               const QString path = activeModel()->pathAt(activeCursor());
               m_preview->setOpen(true);
               m_preview->show(path);
               m_followed = path;
           },
           hasRow, {}});
    a.add({u"rename"_s, u"Rename"_s, key(Qt::Key_F2), {},
           [this] { m_ops->startRename(activeModel()->pathAt(activeCursor())); }, hasRow, {}});
    a.add({u"trash"_s, u"Move to Trash"_s, key(Qt::Key_Delete), {},
           [this] { trashTargets(); }, hasTarget, {}});
    // Deleting for good is asked first, always; in the trash view it is the thing chosen there.
    a.add({u"delete-for-good"_s, u"Delete for Good"_s, key(Qt::Key_Delete, Qt::ShiftModifier), {},
           [this] { deleteTargetsForGood(); },
           [this] {
               return m_place == QLatin1String("trash") ? m_placeRow >= 0
                                                        : m_place.isEmpty() && !targetPaths().isEmpty();
           },
           {}});
    a.add({u"restore"_s, u"Restore from Trash"_s, {}, {}, [this] { restoreRow(m_placeRow); },
           [this] { return m_place == QLatin1String("trash") && m_placeRow >= 0; }, {}});
    a.add({u"empty-trash"_s, u"Empty Trash"_s, {}, {}, [this] { emptyTrash(); }, {}, {}});
    a.add({u"connect"_s, u"Connect to Server"_s, {}, {}, [this] { openConnect(); }, {}, {}});
    a.add({u"change-icon"_s, u"Change Icon"_s, {}, {},
           [this] { changeIcon(activeModel()->pathAt(activeCursor())); }, hasRow, {}});
    a.add({u"reset-icon"_s, u"Reset Icon"_s, {}, {},
           [this] { resetIcon(activeModel()->pathAt(activeCursor())); },
           [this] {
               const QString path = activeModel()->pathAt(activeCursor());
               return !path.isEmpty() && !customIcon(path).isEmpty();
           },
           {}});
    a.add({u"properties"_s, u"Properties"_s, {}, {}, [this] { setDetailsShown(true); }, {}, {}});
    a.add({u"close-window"_s, u"Close Window"_s, key(Qt::Key_W, ctrl), {},
           [this] { emit closeWindowAsked(); }, {}, {}});
    a.add({u"quit"_s, u"Quit"_s, key(Qt::Key_Q, ctrl), {}, [] { QCoreApplication::quit(); }, {}, {}});

    // Edit
    a.add({u"undo"_s, u"Undo"_s, key(Qt::Key_Z, ctrl), {}, [this] { m_ops->undo(); },
           [this] { return m_ops->canUndo(); }, {}});
    a.add({u"redo"_s, u"Redo"_s, key(Qt::Key_Z, ctrlShift), {key(Qt::Key_Y, ctrl)},
           [this] { m_ops->redo(); }, [this] { return m_ops->canRedo(); }, {}});
    a.add({u"cut"_s, u"Cut"_s, key(Qt::Key_X, ctrl), {}, [this] { m_ops->copy(targetPaths(), true); },
           hasTarget, {}});
    a.add({u"copy"_s, u"Copy"_s, key(Qt::Key_C, ctrl), {}, [this] { m_ops->copy(targetPaths(), false); },
           hasTarget, {}});
    a.add({u"paste"_s, u"Paste"_s, key(Qt::Key_V, ctrl), {},
           [this] {
               if (!m_ops->paste(workFolder()))
                   m_ops->say(QStringLiteral("There are no files on the clipboard to paste."));
           },
           {}, {}});
    a.add({u"select-all"_s, u"Select All"_s, key(Qt::Key_A, ctrl), {}, [this] { activeModel()->selectAll(); },
           [this] { return activeModel()->count() > 0; }, {}});
    a.add({u"find"_s, u"Find"_s, key(Qt::Key_F, ctrl), {key(Qt::Key_Slash), key(Qt::Key_Slash, Qt::ShiftModifier)},
           [this] { openSearch(false); }, {}, {}});
    // Again while searching inside files, it goes back to names.
    a.add({u"find-contents"_s, u"Find in Files"_s, key(Qt::Key_F, ctrlShift), {},
           [this] { openSearch(!(m_searchShown && m_searchContents)); }, {}, {}});
    a.add({u"preferences"_s, u"Preferences"_s, key(Qt::Key_Comma, ctrl), {},
           [this] { m_prefs->setShown(true); }, {}, {}});

    // View
    const auto view = [this](View v) { return [this, v] { setView(v); }; };
    const auto isView = [this](View v) { return [this, v] { return m_view == v; }; };
    a.add({u"view-grid"_s, u"Grid"_s, key(Qt::Key_1, ctrl), {}, view(Grid), {}, isView(Grid)});
    a.add({u"view-list"_s, u"List"_s, key(Qt::Key_2, ctrl), {}, view(List), {}, isView(List)});
    a.add({u"view-columns"_s, u"Columns"_s, key(Qt::Key_3, ctrl), {}, view(Columns), {}, isView(Columns)});
    a.add({u"tree"_s, u"Tree"_s, key(Qt::Key_T), {}, [this] { setTree(!m_tree); },
           [this] { return m_view == List; }, [this] { return m_tree; }});
    a.add({u"hidden"_s, u"Hidden Files"_s, key(Qt::Key_H, ctrl), {}, [this] { setShowHidden(!showHidden()); },
           {}, [this] { return showHidden(); }});
    a.add({u"sidebar"_s, u"Sidebar"_s, key(Qt::Key_F9), {}, [this] { setSidebarShown(!m_sidebarShown); }, {},
           [this] { return m_sidebarShown; }});
    a.add({u"details"_s, u"Details"_s, key(Qt::Key_I, ctrl), {}, [this] { setDetailsShown(!m_detailsShown); },
           {}, [this] { return m_detailsShown; }});
    const auto sortBy = [this](FolderModel::Sort s) { return [this, s] { setSortKey(s); }; };
    const auto isSort = [this](FolderModel::Sort s) { return [this, s] { return m_model->sortKey() == s; }; };
    a.add({u"sort-name"_s, u"Name"_s, {}, {}, sortBy(FolderModel::SortByName), {}, isSort(FolderModel::SortByName)});
    a.add({u"sort-size"_s, u"Size"_s, {}, {}, sortBy(FolderModel::SortBySize), {}, isSort(FolderModel::SortBySize)});
    a.add({u"sort-modified"_s, u"Modified"_s, {}, {}, sortBy(FolderModel::SortByModified), {},
           isSort(FolderModel::SortByModified)});
    a.add({u"sort-kind"_s, u"Kind"_s, {}, {}, sortBy(FolderModel::SortByKind), {}, isSort(FolderModel::SortByKind)});
    const auto panes = [this](const QString &m) { return [this, m] { setPaneWidths(m); }; };
    const auto isPanes = [this](const QString &m) { return [this, m] { return m_paneWidths == m; }; };
    a.add({u"panes-fixed"_s, u"Fixed"_s, {}, {}, panes(u"fixed"_s), {}, isPanes(u"fixed"_s)});
    a.add({u"panes-automatic"_s, u"Automatic"_s, {}, {}, panes(u"automatic"_s), {}, isPanes(u"automatic"_s)});
    a.add({u"panes-proportional"_s, u"Proportional"_s, {}, {}, panes(u"proportional"_s), {},
           isPanes(u"proportional"_s)});
    a.add({u"reload"_s, u"Reload"_s, key(Qt::Key_R, ctrl), {},
           [this] {
               if (m_place == QLatin1String("trash"))
                   reloadTrash();
               else if (m_place == QLatin1String("recent"))
                   m_recent->reload();
               else
                   m_model->reload();
           },
           {}, {}});

    // Go
    a.add({u"back"_s, u"Back"_s, key(Qt::Key_Left, alt), {key(Qt::Key_Back)}, [this] { goBack(); },
           [this] { return canGoBack() || !m_place.isEmpty(); }, {}});
    a.add({u"forward"_s, u"Forward"_s, key(Qt::Key_Right, alt), {key(Qt::Key_Forward)}, [this] { goForward(); },
           [this] { return canGoForward(); }, {}});
    a.add({u"up"_s, u"Up"_s, key(Qt::Key_Up, alt), {key(Qt::Key_Backspace)}, [this] { goUp(); },
           [this] { return canGoUp(); }, {}});
    a.add({u"home"_s, u"Home"_s, key(Qt::Key_Home, alt), {}, [this] { open(QDir::homePath()); }, {}, {}});
    a.add({u"recent"_s, u"Recent Files"_s, {}, {}, [this] { showRecent(); }, {}, {}});
    a.add({u"trash-place"_s, u"Trash"_s, {}, {}, [this] { showTrash(); }, {}, {}});
    a.add({u"location"_s, u"Location"_s, key(Qt::Key_L, ctrl), {}, [this] { editLocation(); }, {}, {}});
    a.add({u"pin"_s, u"Pin to Sidebar"_s, key(Qt::Key_D, ctrl), {}, [this] { togglePin(); },
           [this] { return !folder().isEmpty(); }, [this] { return folderPinned(); }});

    // Help
    a.add({u"shortcuts"_s, u"Keyboard Shortcuts"_s, key(Qt::Key_F1), {}, [this] { setShortcutsShown(true); },
           {}, {}});
    a.add({u"about"_s, u"About"_s, {}, {}, [this] { showAbout(); }, {}, {}});

    const auto item = [](const char16_t *id) { return Entry::action(QString::fromUtf16(id)); };
    const Entry sep = Entry::separator();
    const Entry openWith = Entry::submenu(u"open-with"_s, u"Open With"_s);
    const Entry sortMenu = Entry::submenu(u"sort"_s, u"Sort By"_s,
                                          {item(u"sort-name"), item(u"sort-size"), item(u"sort-modified"),
                                           item(u"sort-kind")});
    const Entry panesMenu = Entry::submenu(u"panes"_s, u"Pane Widths"_s,
                                           {item(u"panes-fixed"), item(u"panes-automatic"),
                                            item(u"panes-proportional")});

    a.setMenus({
        {u"File"_s,
         {item(u"new-window"), item(u"new-folder"), sep, item(u"open"), openWith, item(u"preview"), sep,
          item(u"rename"), item(u"trash"), item(u"delete-for-good"), item(u"restore"), item(u"empty-trash"), sep,
          item(u"change-icon"), item(u"reset-icon"), sep, item(u"connect"), sep, item(u"close-window"),
          item(u"quit")}},
        {u"Edit"_s,
         {item(u"undo"), item(u"redo"), sep, item(u"cut"), item(u"copy"), item(u"paste"), sep,
          item(u"select-all"), sep, item(u"find"), item(u"find-contents"), sep, item(u"preferences")}},
        {u"View"_s,
         {item(u"view-grid"), item(u"view-list"), item(u"view-columns"), sep, item(u"tree"), item(u"hidden"),
          sep, item(u"sidebar"), item(u"details"), sep, sortMenu, panesMenu, sep, item(u"reload")}},
        {u"Go"_s,
         {item(u"back"), item(u"forward"), item(u"up"), item(u"home"), sep, item(u"recent"), item(u"trash-place"),
          sep, item(u"location"), item(u"pin")}},
        {u"Help"_s, {item(u"shortcuts"), item(u"about")}},
    });
    a.setContextMenus(
        {item(u"open"), openWith, sep, item(u"cut"), item(u"copy"), sep, item(u"rename"), item(u"trash"), sep,
         item(u"change-icon"), item(u"properties")},
        {item(u"new-folder"), item(u"paste"), sep, item(u"hidden"), sortMenu});

    // Open With lists the applications for the file under the cursor, each time it opens.
    a.setSubmenuFill(u"open-with"_s, [this] { return openWithEntries(); });
}

void Browser::openUnderCursor()
{
    FolderModel *model = activeModel();
    const int row = activeCursor();
    const QString path = model->pathAt(row);
    if (path.isEmpty())
        return;
    if (model->isDirAt(row)) {
        open(path);
        return;
    }
    openFiles({path});
}

void Browser::contextMenuFromKeys()
{
    const bool onFile = !activeModel()->pathAt(activeCursor()).isEmpty();
    m_actions->openContextMenu(onFile, QPointF(-1, -1));
}

void Browser::contextMenuAt(int row, qreal x, qreal y)
{
    if (m_ops->asking() || m_preview->isOpen() || m_chooser->shown())
        return;
    const bool onFile = row >= 0 && row < m_model->count();
    if (onFile) {
        // A row outside the selection becomes what the menu acts on, alone.
        if (!m_model->isSelected(row))
            m_model->clearSelection();
        if (m_view == Columns) {
            closeColumnsAfter(0);
            setActiveColumn(0);
        }
        setCursor(row);
    }
    m_actions->openContextMenu(onFile, QPointF(x, y));
}

void Browser::editLocation()
{
    if (m_locationEditing)
        return;
    m_locationEditing = true;
    emit locationEditingChanged();
}

void Browser::cancelLocation()
{
    if (!m_locationEditing)
        return;
    m_locationEditing = false;
    emit locationEditingChanged();
}

void Browser::openTypedLocation(const QString &text)
{
    cancelLocation();
    QString path = text.trimmed();
    if (path.isEmpty())
        return;
    if (path == QLatin1String("~"))
        path = QDir::homePath();
    else if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    else if (!path.contains(QLatin1String("://")) && QDir::isRelativePath(path))
        path = QDir(folder()).filePath(path);
    if (!path.contains(QLatin1String("://")) && !QFileInfo::exists(path)) {
        m_ops->say(QStringLiteral("There is no folder “%1”.").arg(text.trimmed()));
        return;
    }
    openLocation(path);
}

void Browser::showAbout()
{
    const QString version = QCoreApplication::applicationVersion();
    m_ops->say(QStringLiteral("Riley Files%1, the file manager of the Riley desktop. Free software under the "
                              "GNU General Public License, version 3 or later.")
                   .arg(version.isEmpty() ? QString() : QStringLiteral(" ") + version));
}

bool Browser::shortcutsKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods == Qt::NoModifier
        && (key == Qt::Key_Escape || key == Qt::Key_F1 || key == Qt::Key_Return || key == Qt::Key_Enter))
        setShortcutsShown(false);
    // The list is in front; the other keys wait until it closes.
    return true;
}
