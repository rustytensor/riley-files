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
    // Nothing inside a snapshot can be changed: those commands are greyed out there, and their
    // keys say why (see refusedInSnapshot).
    const auto changeable = [this] { return !inSnapshot(); };
    const auto onVersion = [this] { return m_place == QLatin1String("versions") && m_placeRow >= 0; };

    // File
    a.add({u"new-window"_s, tr("New Window"), key(Qt::Key_N, ctrl), {},
           [this] { emit newWindowAsked(folder()); }, {}, {}});
    a.add({u"new-folder"_s, tr("New Folder"), key(Qt::Key_N, ctrlShift), {},
           [this] { m_ops->newFolder(workFolder()); }, changeable, {}});
    a.add({u"open"_s, tr("Open"), key(Qt::Key_Return), {key(Qt::Key_Enter), key(Qt::Key_Down, alt)},
           [this] { openUnderCursor(); }, hasRow, {}});
    a.add({u"preview"_s, tr("Preview"), key(Qt::Key_Space), {},
           [this] {
               const QString path = activeModel()->pathAt(activeCursor());
               m_preview->setOpen(true);
               m_preview->show(path);
               m_followed = path;
           },
           hasRow, {}});
    a.add({u"rename"_s, tr("Rename"), key(Qt::Key_F2), {},
           [this] { m_ops->startRename(activeModel()->pathAt(activeCursor())); },
           [=] { return hasRow() && changeable(); }, {}});
    a.add({u"trash"_s, tr("Move to Trash"), key(Qt::Key_Delete), {},
           [this] { trashTargets(); }, [=] { return hasTarget() && changeable(); }, {}});
    // Deleting for good is asked first, always; in the trash view it is the thing chosen there.
    a.add({u"delete-for-good"_s, tr("Delete for Good"), key(Qt::Key_Delete, Qt::ShiftModifier), {},
           [this] { deleteTargetsForGood(); },
           [this] {
               return m_place == QLatin1String("trash")
                          ? m_placeRow >= 0
                          : m_place.isEmpty() && !targetPaths().isEmpty() && !inSnapshot();
           },
           {}});
    a.add({u"restore"_s, tr("Restore from Trash"), {}, {}, [this] { restoreRow(m_placeRow); },
           [this] { return m_place == QLatin1String("trash") && m_placeRow >= 0; }, {}});
    a.add({u"empty-trash"_s, tr("Empty Trash"), {}, {}, [this] { emptyTrash(); }, {}, {}});
    // Previous versions, from the snapshots above what the keys are on; offered only when there
    // are snapshots there.
    a.add({u"previous-versions"_s, tr("Previous Versions"), key(Qt::Key_V, alt), {}, [this] { showVersions(); },
           [this] {
               return m_place.isEmpty() && !m_searchShown && m_versions->isAvailable()
                      && m_versions->path() == focusPath();
           },
           {}});
    a.add({u"restore-version"_s, tr("Restore This Version"), {}, {},
           [this] { restoreVersion(m_placeRow, false); }, onVersion, {}});
    a.add({u"restore-version-copy"_s, tr("Restore as Copy"), {}, {},
           [this] { restoreVersion(m_placeRow, true); }, onVersion, {}});
    a.add({u"open-version"_s, tr("Open"), {}, {},
           [this] {
               const QString copy = m_versions->copyPathAt(m_placeRow);
               if (copy.isEmpty())
                   return;
               if (QFileInfo(copy).isDir())
                   open(copy);
               else
                   openFiles({copy});
           },
           onVersion, {}});
    a.add({u"connect"_s, tr("Connect to Server"), {}, {}, [this] { openConnect(); }, {}, {}});
    a.add({u"change-icon"_s, tr("Change Icon"), {}, {},
           [this] { changeIcon(activeModel()->pathAt(activeCursor())); }, hasRow, {}});
    a.add({u"reset-icon"_s, tr("Reset Icon"), {}, {},
           [this] { resetIcon(activeModel()->pathAt(activeCursor())); },
           [this] {
               const QString path = activeModel()->pathAt(activeCursor());
               return !path.isEmpty() && !customIcon(path).isEmpty();
           },
           {}});
    a.add({u"properties"_s, tr("Properties"), {}, {}, [this] { setDetailsShown(true); }, {}, {}});
    a.add({u"close-window"_s, tr("Close Window"), key(Qt::Key_W, ctrl), {},
           [this] { emit closeWindowAsked(); }, {}, {}});
    a.add({u"quit"_s, tr("Quit"), key(Qt::Key_Q, ctrl), {}, [] { QCoreApplication::quit(); }, {}, {}});

    // Edit
    a.add({u"undo"_s, tr("Undo"), key(Qt::Key_Z, ctrl), {}, [this] { m_ops->undo(); },
           [this] { return m_ops->canUndo(); }, {}});
    a.add({u"redo"_s, tr("Redo"), key(Qt::Key_Z, ctrlShift), {key(Qt::Key_Y, ctrl)},
           [this] { m_ops->redo(); }, [this] { return m_ops->canRedo(); }, {}});
    a.add({u"cut"_s, tr("Cut"), key(Qt::Key_X, ctrl), {}, [this] { m_ops->copy(targetPaths(), true); },
           [=] { return hasTarget() && changeable(); }, {}});
    a.add({u"copy"_s, tr("Copy"), key(Qt::Key_C, ctrl), {}, [this] { m_ops->copy(targetPaths(), false); },
           hasTarget, {}});
    a.add({u"paste"_s, tr("Paste"), key(Qt::Key_V, ctrl), {},
           [this] {
               if (!m_ops->paste(workFolder()))
                   m_ops->say(tr("There are no files on the clipboard to paste."));
           },
           changeable, {}});
    a.add({u"select-all"_s, tr("Select All"), key(Qt::Key_A, ctrl), {}, [this] { activeModel()->selectAll(); },
           [this] { return activeModel()->count() > 0; }, {}});
    a.add({u"find"_s, tr("Find"), key(Qt::Key_F, ctrl), {key(Qt::Key_Slash), key(Qt::Key_Slash, Qt::ShiftModifier)},
           [this] { openSearch(false); }, {}, {}});
    // Again while searching inside files, it goes back to names.
    a.add({u"find-contents"_s, tr("Find in Files"), key(Qt::Key_F, ctrlShift), {},
           [this] { openSearch(!(m_searchShown && m_searchContents)); }, {}, {}});
    a.add({u"preferences"_s, tr("Preferences"), key(Qt::Key_Comma, ctrl), {},
           [this] { m_prefs->setShown(true); }, {}, {}});

    // View
    const auto view = [this](View v) { return [this, v] { setView(v); }; };
    const auto isView = [this](View v) { return [this, v] { return m_view == v; }; };
    a.add({u"view-grid"_s, tr("Grid"), key(Qt::Key_1, ctrl), {}, view(Grid), {}, isView(Grid)});
    a.add({u"view-list"_s, tr("List"), key(Qt::Key_2, ctrl), {}, view(List), {}, isView(List)});
    a.add({u"view-columns"_s, tr("Columns"), key(Qt::Key_3, ctrl), {}, view(Columns), {}, isView(Columns)});
    a.add({u"tree"_s, tr("Tree"), key(Qt::Key_T), {}, [this] { setTree(!m_tree); },
           [this] { return m_view == List; }, [this] { return m_tree; }});
    a.add({u"hidden"_s, tr("Hidden Files"), key(Qt::Key_H, ctrl), {}, [this] { setShowHidden(!showHidden()); },
           {}, [this] { return showHidden(); }});
    a.add({u"sidebar"_s, tr("Sidebar"), key(Qt::Key_F9), {}, [this] { setSidebarShown(!m_sidebarShown); }, {},
           [this] { return m_sidebarShown; }});
    a.add({u"details"_s, tr("Details"), key(Qt::Key_I, ctrl), {}, [this] { setDetailsShown(!m_detailsShown); },
           {}, [this] { return m_detailsShown; }});
    const auto sortBy = [this](FolderModel::Sort s) { return [this, s] { setSortKey(s); }; };
    const auto isSort = [this](FolderModel::Sort s) { return [this, s] { return m_model->sortKey() == s; }; };
    a.add({u"sort-name"_s, tr("Name"), {}, {}, sortBy(FolderModel::SortByName), {}, isSort(FolderModel::SortByName)});
    a.add({u"sort-size"_s, tr("Size"), {}, {}, sortBy(FolderModel::SortBySize), {}, isSort(FolderModel::SortBySize)});
    a.add({u"sort-modified"_s, tr("Modified"), {}, {}, sortBy(FolderModel::SortByModified), {},
           isSort(FolderModel::SortByModified)});
    a.add({u"sort-kind"_s, tr("Kind"), {}, {}, sortBy(FolderModel::SortByKind), {}, isSort(FolderModel::SortByKind)});
    const auto panes = [this](const QString &m) { return [this, m] { setPaneWidths(m); }; };
    const auto isPanes = [this](const QString &m) { return [this, m] { return m_paneWidths == m; }; };
    a.add({u"panes-fixed"_s, tr("Fixed"), {}, {}, panes(u"fixed"_s), {}, isPanes(u"fixed"_s)});
    a.add({u"panes-automatic"_s, tr("Automatic"), {}, {}, panes(u"automatic"_s), {}, isPanes(u"automatic"_s)});
    a.add({u"panes-proportional"_s, tr("Proportional"), {}, {}, panes(u"proportional"_s), {},
           isPanes(u"proportional"_s)});
    a.add({u"reload"_s, tr("Reload"), key(Qt::Key_R, ctrl), {},
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
    a.add({u"back"_s, tr("Back"), key(Qt::Key_Left, alt), {key(Qt::Key_Back)}, [this] { goBack(); },
           [this] { return canGoBack() || !m_place.isEmpty(); }, {}});
    a.add({u"forward"_s, tr("Forward"), key(Qt::Key_Right, alt), {key(Qt::Key_Forward)}, [this] { goForward(); },
           [this] { return canGoForward(); }, {}});
    a.add({u"up"_s, tr("Up"), key(Qt::Key_Up, alt), {key(Qt::Key_Backspace)}, [this] { goUp(); },
           [this] { return canGoUp(); }, {}});
    a.add({u"home"_s, tr("Home"), key(Qt::Key_Home, alt), {}, [this] { open(QDir::homePath()); }, {}, {}});
    a.add({u"recent"_s, tr("Recent Files"), {}, {}, [this] { showRecent(); }, {}, {}});
    a.add({u"trash-place"_s, tr("Trash"), {}, {}, [this] { showTrash(); }, {}, {}});
    a.add({u"location"_s, tr("Location"), key(Qt::Key_L, ctrl), {}, [this] { editLocation(); }, {}, {}});
    a.add({u"pin"_s, tr("Pin to Sidebar"), key(Qt::Key_D, ctrl), {}, [this] { togglePin(); },
           [this] { return !folder().isEmpty(); }, [this] { return folderPinned(); }});

    // Help
    a.add({u"shortcuts"_s, tr("Keyboard Shortcuts"), key(Qt::Key_F1), {}, [this] { setShortcutsShown(true); },
           {}, {}});
    a.add({u"about"_s, tr("About"), {}, {}, [this] { showAbout(); }, {}, {}});

    const auto item = [](const char16_t *id) { return Entry::action(QString::fromUtf16(id)); };
    const Entry sep = Entry::separator();
    const Entry openWith = Entry::submenu(u"open-with"_s, tr("Open With"));
    const Entry sortMenu = Entry::submenu(u"sort"_s, tr("Sort By"),
                                          {item(u"sort-name"), item(u"sort-size"), item(u"sort-modified"),
                                           item(u"sort-kind")});
    const Entry panesMenu = Entry::submenu(u"panes"_s, tr("Pane Widths"),
                                           {item(u"panes-fixed"), item(u"panes-automatic"),
                                            item(u"panes-proportional")});

    a.setMenus({
        {tr("File"),
         {item(u"new-window"), item(u"new-folder"), sep, item(u"open"), openWith, item(u"preview"), sep,
          item(u"rename"), item(u"trash"), item(u"delete-for-good"), item(u"restore"), item(u"empty-trash"), sep,
          item(u"previous-versions"), sep, item(u"change-icon"), item(u"reset-icon"), sep, item(u"connect"), sep,
          item(u"close-window"),
          item(u"quit")}},
        {tr("Edit"),
         {item(u"undo"), item(u"redo"), sep, item(u"cut"), item(u"copy"), item(u"paste"), sep,
          item(u"select-all"), sep, item(u"find"), item(u"find-contents"), sep, item(u"preferences")}},
        {tr("View"),
         {item(u"view-grid"), item(u"view-list"), item(u"view-columns"), sep, item(u"tree"), item(u"hidden"),
          sep, item(u"sidebar"), item(u"details"), sep, sortMenu, panesMenu, sep, item(u"reload")}},
        {tr("Go"),
         {item(u"back"), item(u"forward"), item(u"up"), item(u"home"), sep, item(u"recent"), item(u"trash-place"),
          sep, item(u"location"), item(u"pin")}},
        {tr("Help"), {item(u"shortcuts"), item(u"about")}},
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
        m_ops->say(tr("There is no folder “%1”.").arg(text.trimmed()));
        return;
    }
    openLocation(path);
}

void Browser::showAbout()
{
    const QString version = QCoreApplication::applicationVersion();
    m_ops->say(version.isEmpty()
                   ? tr("Riley Files, the file manager of the Riley desktop. Free software under the GNU General "
                        "Public License, version 3 or later.")
                   : tr("Riley Files %1, the file manager of the Riley desktop. Free software under the GNU "
                        "General Public License, version 3 or later.")
                         .arg(version));
}

bool Browser::shortcutsKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods == Qt::NoModifier
        && (key == Qt::Key_Escape || key == Qt::Key_F1 || key == Qt::Key_Return || key == Qt::Key_Enter))
        setShortcutsShown(false);
    // The list is in front; the other keys wait until it closes.
    return true;
}
