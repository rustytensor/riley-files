// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "browser.h"

#include "thumbnailprovider.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

using Riley::FolderModel;

namespace {

// Letters typed closer together than this make one name to find; a longer pause starts again.
constexpr qint64 findPauseMs = 1000;

const char *const viewKey = "browse/view";
const char *const treeKey = "browse/tree";
const char *const hiddenKey = "browse/showHidden";
const char *const detailsKey = "browse/details";
const char *const sidebarShownKey = "browse/sidebar";
const char *const sortKey = "browse/sort";
const char *const paneWidthsKey = "panes/mode";
// The view a new window starts in; empty: the one last used (viewKey).
const char *const defaultViewKey = "View/default";

QString viewToName(Browser::View view)
{
    switch (view) {
    case Browser::Grid:
        return QStringLiteral("grid");
    case Browser::Columns:
        return QStringLiteral("columns");
    case Browser::Graph:
        return QStringLiteral("graph");
    case Browser::List:
        break;
    }
    return QStringLiteral("list");
}

Browser::View viewFromName(const QString &name)
{
    if (name == QLatin1String("grid"))
        return Browser::Grid;
    if (name == QLatin1String("columns"))
        return Browser::Columns;
    if (name == QLatin1String("graph"))
        return Browser::Graph;
    return Browser::List;
}

QString clean(const QString &path)
{
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

Browser::Browser(const QString &settingsFile, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(settingsFile.isEmpty() ? defaultSettingsFile() : settingsFile,
                                             QSettings::IniFormat))
    , m_model(std::make_unique<FolderModel>())
    , m_ops(std::make_unique<FileOps>())
    , m_places(std::make_unique<Riley::PlacesModel>())
    , m_pinned(std::make_unique<Riley::PinnedModel>())
    , m_drives(std::make_unique<Riley::DrivesModel>())
    , m_network(std::make_unique<Riley::NetworkModel>())
    , m_trash(std::make_unique<Riley::TrashModel>())
    , m_recent(std::make_unique<Riley::RecentFiles>())
    , m_versions(std::make_unique<Riley::VersionModel>())
    , m_details(std::make_unique<FileDetails>())
    , m_preview(std::make_unique<Preview>())
    , m_actions(std::make_unique<Actions>())
    , m_panes(std::make_unique<PaneWidths>(m_settings.get()))
    , m_prefs(std::make_unique<Preferences>(m_settings.get()))
    , m_icons(std::make_unique<Riley::CustomIcons>())
    , m_chooser(std::make_unique<Chooser>())
    , m_search(std::make_unique<Riley::FileSearch>())
{
    connect(m_ops.get(), &FileOps::land, this, &Browser::aimCursor);
    // A chosen icon goes with its file through every rename and move made here.
    connect(m_ops.get(), &FileOps::moved, m_icons.get(), &Riley::CustomIcons::moved);
    connect(m_icons.get(), &Riley::CustomIcons::changed, this, [this] {
        ++m_iconsRevision;
        emit iconsChanged();
    });
    m_detailsShown = m_settings->value(detailsKey, false).toBool();
    m_sidebarShown = m_settings->value(sidebarShownKey, true).toBool();
    const QString panes = m_settings->value(paneWidthsKey).toString();
    if (panes == QLatin1String("automatic") || panes == QLatin1String("proportional"))
        m_paneWidths = panes;
    m_panes->setMode(PaneWidths::modeFromName(m_paneWidths));
    const int sort = m_settings->value(sortKey, int(FolderModel::SortByName)).toInt();
    if (sort >= int(FolderModel::SortByName) && sort <= int(FolderModel::SortByKind))
        m_model->setSortKey(FolderModel::Sort(sort));

    const QString startView = m_settings->value(defaultViewKey).toString();
    m_view = viewFromName(startView.isEmpty() ? m_settings->value(viewKey).toString() : startView);
    m_tree = m_settings->value(treeKey, false).toBool();
    m_model->setShowHidden(m_settings->value(hiddenKey, false).toBool());

    // The cursor follows its row through every change to the rows. These connections are made
    // before any view's, so a view always reads the cursor that fits the rows it is told about.
    FolderModel *model = m_model.get();
    connect(model, &QAbstractItemModel::modelReset, this, &Browser::followRows);
    connect(model, &QAbstractItemModel::rowsInserted, this, &Browser::followRows);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &Browser::followRows);
    connect(model, &QAbstractItemModel::rowsMoved, this, &Browser::followRows);
    connect(model, &QAbstractItemModel::layoutChanged, this, &Browser::followRows);
    connect(model, &FolderModel::loaded, this, [this] {
        m_rootLoaded = true;
        followRows();
    });
    connect(model, &FolderModel::showHiddenChanged, this, &Browser::showHiddenChanged);
    // The README is read again with the folder, and whenever its rows are read again.
    connect(model, &FolderModel::loaded, this, &Browser::refreshReadme);
    connect(this, &Browser::folderChanged, this, &Browser::refreshReadme);

    // The first column of the columns view is the folder itself, with the browser's cursor.
    auto *first = new FolderColumn(model, this);
    first->setReady(false);
    connect(this, &Browser::cursorChanged, first, [this, first] { first->mirrorCursor(m_cursor); });
    connect(model, &FolderModel::loaded, first, [first] { first->setReady(true); });
    m_columns.append(first);

    // The sidebar marks the place shown, and keeps its cursor on a row there is.
    connect(this, &Browser::folderChanged, this, &Browser::sidebarChanged);
    const auto sidebarRows = [this] {
        setSidebarCursor(m_sidebarCursor);
        emit sidebarChanged();
    };
    connect(m_places.get(), &QAbstractItemModel::modelReset, this, sidebarRows);
    connect(m_pinned.get(), &QAbstractItemModel::modelReset, this, sidebarRows);
    connect(m_pinned.get(), &QAbstractItemModel::rowsInserted, this, sidebarRows);
    connect(m_pinned.get(), &QAbstractItemModel::rowsRemoved, this, sidebarRows);
    connect(m_pinned.get(), &Riley::PinnedModel::countChanged, this, sidebarRows);
    connect(m_drives.get(), &QAbstractItemModel::modelReset, this, sidebarRows);
    connect(m_drives.get(), &Riley::DrivesModel::ejectFinished, this,
            [this](const QString &, bool, const QString &message) { m_ops->say(message); });
    connect(m_network.get(), &QAbstractItemModel::modelReset, this, sidebarRows);
    connect(m_network.get(), &Riley::NetworkModel::connectFinished, this,
            [this](const QString &, bool, const QString &message) { m_ops->say(message); });

    // The trash view reads the trashes again after every file operation, and the rows it
    // shows keep their cursor.
    connect(&m_ops->queue(), &Riley::JobQueue::jobFinished, this, [this] {
        if (m_place == QLatin1String("trash"))
            reloadTrash();
    });
    for (QAbstractItemModel *list : {static_cast<QAbstractItemModel *>(m_trash.get()),
                                     static_cast<QAbstractItemModel *>(m_recent.get())})
        connect(list, &QAbstractItemModel::modelReset, this, &Browser::followPlaceRows);
    connect(m_versions.get(), &QAbstractItemModel::modelReset, this, &Browser::followPlaceRows);
    // A file operation may have changed the file whose versions are known (a restore makes the
    // live file another one), so they are read again.
    connect(&m_ops->queue(), &Riley::JobQueue::jobFinished, this, [this] {
        if (!m_versions->path().isEmpty())
            m_versions->reload();
    });

    // The details and the preview follow the cursor, through every change to the rows.
    connect(this, &Browser::cursorChanged, this, &Browser::followFocus);
    connect(this, &Browser::folderChanged, this, &Browser::followFocus);
    connect(this, &Browser::activeColumnChanged, this, &Browser::followFocus);
    connect(model, &QAbstractItemModel::rowsInserted, this, &Browser::followFocus);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &Browser::followFocus);
    connect(model, &QAbstractItemModel::modelReset, this, &Browser::followFocus);
    connect(model, &QAbstractItemModel::dataChanged, this, [this] {
        m_followed.clear(); // the same row, changed: read it again
        followFocus();
    });

    // The graph view's graph is built while it is shown, of the folder shown.
    m_graph = std::make_unique<Riley::GraphBuilder>();
    connect(m_graph.get(), &Riley::GraphBuilder::built, this, [this] {
        const int node = m_graph->indexOf(m_graphPath);
        m_graphHover = -1;
        setGraphNode(node >= 0 ? node : (m_graph->count() > 0 ? 0 : -1));
        emit graphNodeChanged();
    });
    connect(this, &Browser::folderChanged, this, [this] {
        if (m_view == Graph)
            startGraph();
    });
    connect(model, &FolderModel::showHiddenChanged, this, [this] {
        if (m_view == Graph)
            startGraph();
    });

    addActions();
    addPreferences();
    addIndexPage();
    m_model->setFoldersFirst(m_prefs->isOn(QStringLiteral("foldersFirst")));
    // The list view's table shows what git says about a folder in a repository.
    m_model->setGitEnabled(true);
    connect(m_prefs.get(), &Preferences::changed, this, &Browser::applyPreference);

    // The automatic pane widths follow the names shown.
    connect(m_places.get(), &QAbstractItemModel::modelReset, this, &Browser::measureSidebar);
    connect(m_pinned.get(), &QAbstractItemModel::modelReset, this, &Browser::measureSidebar);
    connect(m_pinned.get(), &QAbstractItemModel::rowsInserted, this, &Browser::measureSidebar);
    connect(m_pinned.get(), &QAbstractItemModel::rowsRemoved, this, &Browser::measureSidebar);
    connect(m_drives.get(), &QAbstractItemModel::modelReset, this, &Browser::measureSidebar);
    connect(m_network.get(), &QAbstractItemModel::modelReset, this, &Browser::measureSidebar);
    connect(model, &FolderModel::loaded, this, &Browser::measureColumns);
    connect(this, &Browser::columnsChanged, this, &Browser::measureColumns);
    connect(this, &Browser::viewChanged, this, &Browser::measureColumns);
    measureSidebar();

    // The search's cursor follows its result as rows come in, and its line of status follows
    // the search.
    Riley::FileSearch *search = m_search.get();
    connect(search, &QAbstractItemModel::modelReset, this, &Browser::followSearchRows);
    connect(search, &QAbstractItemModel::rowsInserted, this, &Browser::followSearchRows);
    connect(search, &Riley::FileSearch::started, this, &Browser::searchStatusChanged);
    connect(search, &Riley::FileSearch::runningChanged, this, &Browser::searchStatusChanged);
    connect(search, &Riley::FileSearch::countChanged, this, &Browser::searchStatusChanged);
    connect(search, &Riley::FileSearch::noteChanged, this, &Browser::searchStatusChanged);
    connect(search, &Riley::FileSearch::messageChanged, this, &Browser::searchStatusChanged);
}

QString Browser::defaultSettingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/riley-files/riley-files.conf");
}

Browser::~Browser() = default;

QString Browser::folderName() const
{
    if (m_place == QLatin1String("trash"))
        return tr("Trash");
    if (m_place == QLatin1String("recent"))
        return tr("Recent");
    if (m_place == QLatin1String("versions"))
        return tr("Previous Versions");
    const QString path = folder();
    if (path == QLatin1String("/"))
        return path;
    if (path == QDir::homePath())
        return tr("Home");
    return QFileInfo(path).fileName();
}

void Browser::setCursor(int row)
{
    m_cursorWaiting = false;
    m_aimPath.clear();
    placeCursor(row);
}

bool Browser::canGoUp() const
{
    const QString path = folder();
    return !path.isEmpty() && path != QLatin1String("/");
}

void Browser::setView(View view)
{
    if (view == m_view)
        return;
    // The tree is the list view's: folders open in place close when the list goes.
    if (m_view == List)
        flattenTree();
    // Columns opened in the columns view close with it.
    if (m_view == Columns) {
        closeColumnsAfter(0);
        setActiveColumn(0);
    }
    // The graph is built while its view is shown, and let go when it goes.
    if (m_view == Graph) {
        m_graph->clear();
        m_graphPath.clear();
        m_graphNode = -1;
        m_graphHover = -1;
        emit graphNodeChanged();
    }
    m_view = view;
    m_settings->setValue(viewKey, viewToName(view));
    m_settings->sync();
    if (m_view == Graph)
        startGraph();
    emit viewChanged();
}

QString Browser::viewName() const
{
    return viewToName(m_view);
}

void Browser::setTree(bool on)
{
    if (on == m_tree)
        return;
    m_tree = on;
    if (!on)
        flattenTree();
    m_settings->setValue(treeKey, on);
    m_settings->sync();
    emit treeChanged();
}

bool Browser::showHidden() const
{
    return m_model->showHidden();
}

void Browser::flattenTree()
{
    if (!m_model->hasOpenFolders())
        return;
    // The cursor moves out of the folders about to close, to the row they are shown under.
    int row = m_cursor;
    for (int up = m_model->parentRow(row); up >= 0; up = m_model->parentRow(row))
        row = up;
    if (row != m_cursor)
        setCursor(row);
    m_model->collapseAll();
}

void Browser::setShowHidden(bool show)
{
    if (show == m_model->showHidden())
        return;
    m_settings->setValue(hiddenKey, show);
    m_settings->sync();
    m_model->setShowHidden(show); // emits showHiddenChanged through the model
    for (qsizetype i = 1; i < m_columns.size(); ++i)
        m_columns.at(i)->model()->setShowHidden(show);
    if (m_searchShown)
        runSearch();
    m_prefs->refresh();
}

void Browser::setPageRows(int rows)
{
    rows = std::max(1, rows);
    if (rows == m_pageRows)
        return;
    m_pageRows = rows;
    emit pageRowsChanged();
}

void Browser::setGridColumns(int tiles)
{
    tiles = std::max(1, tiles);
    if (tiles == m_gridColumns)
        return;
    m_gridColumns = tiles;
    emit gridColumnsChanged();
}

QList<QObject *> Browser::columns() const
{
    QList<QObject *> list;
    list.reserve(m_columns.size());
    for (FolderColumn *column : m_columns)
        list.append(column);
    return list;
}

void Browser::openLocation(const QString &location)
{
    QString path = location;
    if (path.isEmpty()) {
        // The start folder of the Preferences, when there is one; else the home folder.
        const QString start = m_prefs->value(QStringLiteral("startFolder"));
        const QString expanded = start == QLatin1String("~") || start.startsWith(QLatin1String("~/"))
                                     ? QDir::homePath() + start.mid(1)
                                     : start;
        path = !expanded.isEmpty() && QFileInfo(expanded).isDir() ? expanded : QDir::homePath();
    } else if (path.contains(QLatin1String("://"))) {
        const QUrl url(path);
        path = url.isLocalFile() ? url.toLocalFile() : QDir::homePath();
    }
    path = clean(path);

    const QFileInfo info(path);
    if (info.exists() && !info.isDir()) {
        open(info.absolutePath(), path);
        return;
    }
    // A folder that does not exist is still shown, so its error says why it is empty.
    open(path);
}

void Browser::open(const QString &path, const QString &cursorOn)
{
    const QString target = clean(path);
    if (target.isEmpty())
        return;
    // The trash's own folder is shown as the trash view, with every trash in it.
    if (target == trashPlacePath()) {
        showTrash();
        return;
    }
    if (target == folder() && !m_place.isEmpty())
        leavePlace();
    if (target == folder()) {
        const int row = cursorOn.isEmpty() ? -1 : m_model->rowOfPath(cursorOn);
        if (row >= 0)
            setCursor(row);
        return;
    }

    rememberCursor();
    if (m_historyAt + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyAt + 1, m_history.end());
    m_history.append({target, QString()});
    m_historyAt = m_history.size() - 1;
    show(target, clean(cursorOn));
    emit historyChanged();
}

void Browser::goBack()
{
    // Back from the trash view or the recent files is the folder they were shown over.
    if (!m_place.isEmpty()) {
        leavePlace();
        return;
    }
    if (!canGoBack())
        return;
    rememberCursor();
    --m_historyAt;
    const Step step = m_history.at(m_historyAt);
    show(step.path, step.cursorPath);
    emit historyChanged();
}

void Browser::goForward()
{
    if (!canGoForward())
        return;
    rememberCursor();
    ++m_historyAt;
    const Step step = m_history.at(m_historyAt);
    show(step.path, step.cursorPath);
    emit historyChanged();
}

void Browser::goUp()
{
    if (!canGoUp())
        return;
    const QString here = folder();
    open(QFileInfo(here).absolutePath(), here);
}

void Browser::activate(int row)
{
    const QString path = m_model->pathAt(row);
    if (path.isEmpty())
        return;
    if (m_model->isDirAt(row))
        open(path);
    else
        openFiles({path});
}

bool Browser::handleKey(int key, int modifiers, const QString &text)
{
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;

    // While a name clash is asked, the keys answer it and do nothing else.
    if (m_ops->asking())
        return m_ops->clashKey(key, mods);
    // An open menu is in front of everything else.
    if (m_actions->menuOpen())
        return m_actions->menuKey(key, mods);
    if (!m_question.isEmpty())
        return questionKey(key, mods);
    // The Connect to Server field takes its own typing; the other keys wait until it closes.
    if (m_connectShown) {
        if (key == Qt::Key_Escape)
            cancelConnect();
        return true;
    }
    if (m_chooser->shown())
        return m_chooser->key(key, mods, text);
    if (m_shortcutsShown)
        return shortcutsKey(key, mods);
    // The Preferences take every key while they are open; a text being edited in them takes
    // its own typing before the keys come here.
    if (m_prefs->shown())
        return m_prefs->key(key, mods);
    // While the preview is open, the keys move it along the files or close it.
    if (m_preview->isOpen())
        return previewKey(key, mods);
    // While the search is shown, it takes the keys; only the commands about the window work.
    if (m_searchShown) {
        if (searchKey(key, mods))
            return true;
        if (key == Qt::Key_F10 && mods == Qt::NoModifier) {
            m_actions->openBarMenu(0);
            return true;
        }
        const QString id = m_actions->idForKey(key, mods);
        if (!id.isEmpty() && searchAllows(id))
            m_actions->trigger(id);
        return true;
    }
    // The trash view and the recent files take the keys the same way.
    if (!m_place.isEmpty()) {
        if (key == Qt::Key_F10 && mods == Qt::NoModifier) {
            m_actions->openBarMenu(0);
            return true;
        }
        if ((key == Qt::Key_Tab && mods == Qt::NoModifier) || key == Qt::Key_Backtab) {
            setSidebarFocus(!m_sidebarFocus);
            return true;
        }
        if (m_sidebarFocus && sidebarKey(key, mods))
            return true;
        if (placeKey(key, mods))
            return true;
        const QString id = m_actions->idForKey(key, mods);
        if (!id.isEmpty() && placeAllows(id))
            m_actions->trigger(id);
        return true;
    }

    // The menus from the keyboard: F10 opens the menu bar's first menu, the Menu key and
    // Shift+F10 the context menu.
    if (key == Qt::Key_F10 && mods == Qt::NoModifier) {
        m_actions->openBarMenu(0);
        return true;
    }
    if ((key == Qt::Key_Menu && mods == Qt::NoModifier) || (key == Qt::Key_F10 && mods == Qt::ShiftModifier)) {
        contextMenuFromKeys();
        return true;
    }
    // Tab moves the keys between the sidebar and the files.
    if ((key == Qt::Key_Tab && mods == Qt::NoModifier) || key == Qt::Key_Backtab) {
        setSidebarFocus(!m_sidebarFocus);
        return true;
    }
    if (m_sidebarFocus && sidebarKey(key, mods))
        return true;
    // The graph view moves through the graph with the arrows, and Return and Escape are its own.
    if (m_view == Graph && mods == Qt::NoModifier && graphKey(key))
        return true;

    const bool finding = !m_findText.isEmpty() && m_findTimer.isValid()
                         && m_findTimer.elapsed() < findPauseMs;

    // The commands, from the one registry the menus show. A plain letter or Space in the middle
    // of typing a name is part of the name; a command that cannot be used now leaves the key to
    // the cursor and to typing.
    const QString id = m_actions->idForKey(key, mods);
    const bool typingKey = mods == Qt::NoModifier && !text.isEmpty() && text.at(0).isPrint();
    if (!id.isEmpty() && !(typingKey && finding) && refusedInSnapshot(id))
        return true;
    if (!id.isEmpty() && !(typingKey && finding) && m_actions->trigger(id))
        return true;

    if (mods == Qt::NoModifier) {
        if (key == Qt::Key_Escape) {
            m_ops->dismissMessage();
            return true;
        }
        // Each view moves the cursor its own way.
        const bool used = m_view == Grid      ? gridKey(key)
                          : m_view == Columns ? columnsKey(key)
                                              : listKey(key, mods);
        if (used)
            return true;
    }

    if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
        return typeToFind(text);
    return false;
}

void Browser::show(const QString &path, const QString &cursorOn)
{
    // Going anywhere ends a search, and leaves the trash view or the recent files.
    closeSearch();
    setPlace(QString());
    m_aimPath.clear();
    m_ops->cancelRename();
    m_cursorPath = cursorOn;
    m_cursorWaiting = true;
    m_rootLoaded = false;
    m_findText.clear();
    closeColumnsAfter(0);
    setActiveColumn(0);
    m_columns.first()->setReady(false);
    if (m_cursor != -1) {
        m_cursor = -1;
        emit cursorChanged();
    }
    m_model->setPath(path);
    emit folderChanged();
}

void Browser::rememberCursor()
{
    if (m_historyAt >= 0 && m_historyAt < m_history.size())
        m_history[m_historyAt].cursorPath = m_cursorPath;
}

void Browser::placeCursor(int row)
{
    const int n = m_model->count();
    row = n == 0 ? -1 : std::clamp(row, 0, n - 1);
    const QString path = m_model->pathAt(row);
    if (row == m_cursor && path == m_cursorPath)
        return;
    m_cursor = row;
    m_cursorPath = path;
    emit cursorChanged();
}

// After any change to the rows: put the cursor back on its path, or, when that row is gone,
// keep it about where it was.
void Browser::followRows()
{
    if (landAim())
        return;
    const int row = m_cursorPath.isEmpty() ? -1 : m_model->rowOfPath(m_cursorPath);
    if (row >= 0) {
        m_cursorWaiting = false;
        placeCursor(row);
        return;
    }
    if (m_cursorWaiting && !m_rootLoaded) {
        // The rows it waits for have not come in yet.
        if (m_cursor != -1) {
            m_cursor = -1;
            emit cursorChanged();
        }
        return;
    }
    m_cursorWaiting = false;
    placeCursor(std::max(m_cursor, 0));
}

bool Browser::listKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods != Qt::NoModifier)
        return false;
    const int n = m_model->count();
    switch (key) {
    case Qt::Key_Up:
        if (n > 0)
            setCursor(m_cursor < 0 ? 0 : m_cursor - 1);
        return true;
    case Qt::Key_Down:
        if (n > 0)
            setCursor(m_cursor + 1);
        return true;
    case Qt::Key_Home:
        setCursor(0);
        return true;
    case Qt::Key_End:
        setCursor(n - 1);
        return true;
    case Qt::Key_PageUp:
        setCursor(m_cursor - m_pageRows);
        return true;
    case Qt::Key_PageDown:
        setCursor(m_cursor + m_pageRows);
        return true;
    case Qt::Key_Right:
        if (m_tree)
            return treeRight();
        activate(m_cursor);
        return true;
    case Qt::Key_Left:
        if (m_tree)
            return treeLeft();
        goUp();
        return true;
    default:
        return false;
    }
}

// Right in the tree: a closed folder opens in place; an open one takes the cursor to its first
// row, once its contents are in.
bool Browser::treeRight()
{
    const int row = m_cursor;
    if (!m_model->isDirAt(row))
        return true;
    if (!m_model->isOpenAt(row)) {
        m_model->expand(row);
        return true;
    }
    if (m_model->isLoadedAt(row) && m_model->parentRow(row + 1) == row)
        setCursor(row + 1);
    return true;
}

// Left in the tree: an open folder closes; a row inside a folder takes the cursor to that
// folder; a closed row at the top goes up to the folder that holds this one.
bool Browser::treeLeft()
{
    const int row = m_cursor;
    if (row < 0) {
        goUp();
        return true;
    }
    if (m_model->isOpenAt(row)) {
        m_model->collapse(row);
        return true;
    }
    const int parent = m_model->parentRow(row);
    if (parent >= 0)
        setCursor(parent);
    else
        goUp();
    return true;
}

bool Browser::typeToFind(const QString &text)
{
    if (text.isEmpty() || !text.at(0).isPrint())
        return false;
    if (m_findText.isEmpty() && text.at(0).isSpace())
        return false;
    if (!m_findTimer.isValid() || m_findTimer.elapsed() >= findPauseMs)
        m_findText.clear();
    m_findText += text;
    m_findTimer.restart();

    // The same letter again and again steps through the names that start with it.
    const bool oneLetter = std::all_of(m_findText.cbegin(), m_findText.cend(),
                                       [this](QChar c) { return c == m_findText.front(); });
    const QString prefix = oneLetter ? m_findText.left(1) : m_findText;
    FolderModel *model = activeModel();
    const int cursor = activeCursor();
    const int from = oneLetter ? cursor + 1 : std::max(cursor, 0);
    const int row = model->findByPrefix(prefix, from >= model->count() ? 0 : from);
    if (row >= 0 && row != cursor) {
        setActiveCursor(row);
        if (m_view == Columns)
            previewUnderCursor();
    }
    return true;
}

// The grid: Left and Right move one tile, Up and Down a row of tiles. Down from a row above
// the last goes to the last tile when there is none right below.
bool Browser::gridKey(int key)
{
    const int n = m_model->count();
    const int per = m_gridColumns;
    const int row = m_cursor;
    switch (key) {
    case Qt::Key_Left:
        if (n > 0)
            setCursor(row <= 0 ? 0 : row - 1);
        return true;
    case Qt::Key_Right:
        if (n > 0)
            setCursor(row + 1);
        return true;
    case Qt::Key_Up:
        if (row < 0 && n > 0)
            setCursor(0);
        else if (row >= per)
            setCursor(row - per);
        return true;
    case Qt::Key_Down:
        if (row < 0) {
            if (n > 0)
                setCursor(0);
        } else if (row + per < n) {
            setCursor(row + per);
        } else if (row / per < (n - 1) / per) {
            setCursor(n - 1);
        }
        return true;
    case Qt::Key_PageUp:
        setCursor(row - m_pageRows * per);
        return true;
    case Qt::Key_PageDown:
        setCursor(row + m_pageRows * per);
        return true;
    case Qt::Key_Home:
    case Qt::Key_End:
        return listKey(key, Qt::NoModifier);
    default:
        return false;
    }
}

FolderModel *Browser::activeModel() const
{
    return m_columns.at(m_activeColumn)->model();
}

int Browser::activeCursor() const
{
    return m_activeColumn == 0 ? m_cursor : m_columns.at(m_activeColumn)->cursor();
}

void Browser::setActiveCursor(int row)
{
    if (m_activeColumn == 0)
        setCursor(row);
    else
        m_columns.at(m_activeColumn)->setCursor(row);
}

void Browser::setActiveColumn(int column)
{
    if (column == m_activeColumn)
        return;
    m_activeColumn = column;
    emit activeColumnChanged();
}

// The columns view. Up and Down move in the active column, and a folder the cursor moves onto
// shows in the next column. Right on a folder shows it in the next column; Right again moves
// into that column. Left moves back a column, onto the folder the column it left shows, and
// from the first column it goes up a folder.
bool Browser::columnsKey(int key)
{
    FolderModel *model = activeModel();
    const int n = model->count();
    const int row = activeCursor();
    int to = row;
    switch (key) {
    case Qt::Key_Up:
        to = row < 0 ? 0 : row - 1;
        break;
    case Qt::Key_Down:
        to = row + 1;
        break;
    case Qt::Key_Home:
        to = 0;
        break;
    case Qt::Key_End:
        to = n - 1;
        break;
    case Qt::Key_PageUp:
        to = row - m_pageRows;
        break;
    case Qt::Key_PageDown:
        to = row + m_pageRows;
        break;
    case Qt::Key_Right:
        return columnsRight();
    case Qt::Key_Left:
        return columnsLeft();
    default:
        return false;
    }
    if (n > 0)
        setActiveCursor(to);
    if (activeCursor() != row)
        previewUnderCursor();
    return true;
}

bool Browser::columnsRight()
{
    FolderModel *model = activeModel();
    const int row = activeCursor();
    if (!model->isDirAt(row))
        return true;
    const QString path = model->pathAt(row);
    const int next = m_activeColumn + 1;
    if (next < m_columns.size() && m_columns.at(next)->path() == path) {
        FolderColumn *column = m_columns.at(next);
        // The cursor goes in once there are rows to go to.
        if (!column->isReady() || column->model()->count() == 0)
            return true;
        if (column->cursor() < 0)
            column->setCursor(0);
        setActiveColumn(next);
        previewUnderCursor();
        return true;
    }
    closeColumnsAfter(m_activeColumn);
    appendColumn(path);
    return true;
}

bool Browser::columnsLeft()
{
    // As in Finder: the cursor goes back to the folder this column shows, and the column it
    // left stays open beside it. Anything further right closes.
    if (m_activeColumn > 0) {
        const int back = m_activeColumn - 1;
        closeColumnsAfter(back + 1);
        setActiveColumn(back);
        return true;
    }
    goUp();
    return true;
}

// The folder under the active column's cursor, shown in the next column.
void Browser::previewUnderCursor()
{
    FolderModel *model = activeModel();
    const int row = activeCursor();
    const QString path = model->isDirAt(row) ? model->pathAt(row) : QString();
    const int next = m_activeColumn + 1;
    if (!path.isEmpty() && next == m_columns.size() - 1 && m_columns.at(next)->path() == path)
        return;
    closeColumnsAfter(m_activeColumn);
    if (!path.isEmpty())
        appendColumn(path);
}

void Browser::appendColumn(const QString &path)
{
    auto *column = new FolderColumn(path, m_model->showHidden(), this);
    column->model()->setSortKey(m_model->sortKey());
    column->model()->setFoldersFirst(m_model->foldersFirst());
    connect(column->model(), &FolderModel::loaded, this, &Browser::measureColumns);
    connect(column, &FolderColumn::cursorChanged, this, &Browser::followFocus);
    m_columns.append(column);
    emit columnsChanged();
}

void Browser::closeColumnsAfter(int column)
{
    if (column + 1 >= m_columns.size())
        return;
    const QList<FolderColumn *> gone = m_columns.mid(column + 1);
    m_columns.resize(column + 1);
    // The view lets go of the columns first; they go once it has.
    emit columnsChanged();
    for (FolderColumn *old : gone)
        old->deleteLater();
}

QStringList Browser::targetPaths() const
{
    FolderModel *model = activeModel();
    QStringList paths = model->selectedPaths();
    if (paths.isEmpty()) {
        const QString path = model->pathAt(activeCursor());
        if (!path.isEmpty())
            paths.append(path);
    }
    return paths;
}

QString Browser::workFolder() const
{
    return activeModel()->path();
}

void Browser::aimCursor(const QString &path, bool edit)
{
    const QString parent = QFileInfo(path).absolutePath();
    if (parent != folder() && m_model->rowOfPath(parent) < 0)
        return;
    m_aimPath = path;
    m_aimEdit = edit;
    landAim();
}

bool Browser::landAim()
{
    if (m_aimPath.isEmpty())
        return false;
    const int row = m_model->rowOfPath(m_aimPath);
    if (row < 0)
        return false;
    const QString path = m_aimPath;
    const bool edit = m_aimEdit;
    m_aimPath.clear();
    m_cursorWaiting = false;
    if (m_view == Columns) {
        closeColumnsAfter(0);
        setActiveColumn(0);
    }
    placeCursor(row);
    if (edit)
        m_ops->startRename(path);
    return true;
}

QString Browser::thumbnailSource(const QString &path, bool isDir, const QDateTime &modified) const
{
    if (isDir || path.isEmpty())
        return QString();
    return ThumbnailProvider::sourceFor(path, modified);
}

// The sidebar: places, then pinned folders.

int Browser::markedPlace() const
{
    if (m_place == QLatin1String("trash"))
        return m_places->rowOf(trashPlacePath());
    return m_place.isEmpty() ? m_places->rowOf(folder()) : -1;
}

int Browser::markedPin() const
{
    return m_place.isEmpty() ? m_pinned->rowOf(folder()) : -1;
}

bool Browser::folderPinned() const
{
    return !folder().isEmpty() && m_pinned->isPinned(folder());
}

int Browser::sidebarRowCount() const
{
    return networkSidebarRow() + m_network->count();
}

int Browser::recentSidebarRow() const
{
    return m_places->count() + m_pinned->count();
}

int Browser::networkSidebarRow() const
{
    return drivesSidebarRow() + m_drives->count();
}

int Browser::markedDrive() const
{
    return m_place.isEmpty() ? m_drives->rowOf(folder()) : -1;
}

int Browser::markedNetwork() const
{
    return m_place.isEmpty() ? m_network->rowOf(folder()) : -1;
}

void Browser::ejectDrive(int row)
{
    QString why;
    if (!m_drives->eject(row, &why))
        m_ops->say(why);
}

void Browser::setSidebarCursor(int row)
{
    const int rows = sidebarRowCount();
    row = rows == 0 ? 0 : std::clamp(row, 0, rows - 1);
    if (row == m_sidebarCursor)
        return;
    m_sidebarCursor = row;
    emit sidebarChanged();
}

void Browser::setSidebarFocus(bool on)
{
    if (on && (!m_sidebarShown || sidebarRowCount() == 0))
        on = false;
    if (on == m_sidebarFocus)
        return;
    m_sidebarFocus = on;
    if (on) {
        // The cursor starts on the place shown, when there is one.
        const int place = markedPlace();
        const int pin = markedPin();
        const int drive = markedDrive();
        const int server = markedNetwork();
        if (place >= 0)
            m_sidebarCursor = place;
        else if (pin >= 0)
            m_sidebarCursor = m_places->count() + pin;
        else if (markedRecent())
            m_sidebarCursor = recentSidebarRow();
        else if (drive >= 0)
            m_sidebarCursor = drivesSidebarRow() + drive;
        else if (server >= 0)
            m_sidebarCursor = networkSidebarRow() + server;
        setSidebarCursor(m_sidebarCursor);
    }
    emit sidebarChanged();
}

void Browser::openSidebarRow(int row)
{
    const int places = m_places->count();
    const int pins = m_pinned->count();
    if (row == recentSidebarRow()) {
        setSidebarCursor(row);
        setSidebarFocus(false);
        showRecent();
        return;
    }
    const QString path = row < places               ? m_places->pathAt(row)
                         : row < places + pins      ? m_pinned->pathAt(row - places)
                         : row < networkSidebarRow() ? m_drives->pathAt(row - drivesSidebarRow())
                                                     : m_network->pathAt(row - networkSidebarRow());
    if (path.isEmpty())
        return;
    setSidebarCursor(row);
    setSidebarFocus(false);
    if (clean(path) == trashPlacePath()) {
        showTrash();
        return;
    }
    if (!QFileInfo(path).isDir()) {
        m_ops->say(tr("“%1” is not there any more.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (clean(path) != folder())
        open(path);
    else
        leavePlace();
}

void Browser::togglePin()
{
    const QString path = folder();
    if (path.isEmpty())
        return;
    const bool was = m_pinned->isPinned(path);
    const bool now = m_pinned->toggle(path);
    const QString name = folderName();
    if (now == was)
        m_ops->say(m_pinned->error().isEmpty() ? tr("The pinned folders could not be changed.")
                                               : m_pinned->error());
    else
        m_ops->say(now ? tr("Pinned “%1” to the sidebar.").arg(name)
                       : tr("Unpinned “%1”.").arg(name));
    emit sidebarChanged();
}

bool Browser::sidebarKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods != Qt::NoModifier)
        return false;
    const int rows = sidebarRowCount();
    switch (key) {
    case Qt::Key_Up:
        setSidebarCursor(m_sidebarCursor - 1);
        return true;
    case Qt::Key_Down:
        setSidebarCursor(m_sidebarCursor + 1);
        return true;
    case Qt::Key_Home:
    case Qt::Key_PageUp:
        setSidebarCursor(0);
        return true;
    case Qt::Key_End:
    case Qt::Key_PageDown:
        setSidebarCursor(rows - 1);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Right:
    case Qt::Key_Space:
        openSidebarRow(m_sidebarCursor);
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Left:
        setSidebarFocus(false);
        return true;
    default:
        // Other keys belong to the files (Backspace, Delete, F2 ...), not to typing here.
        return key != Qt::Key_Backspace && key != Qt::Key_Delete && key != Qt::Key_F2;
    }
}

// The details pane and the preview.

void Browser::setDetailsShown(bool shown)
{
    if (shown == m_detailsShown)
        return;
    m_detailsShown = shown;
    m_settings->setValue(detailsKey, shown);
    m_settings->sync();
    if (shown) {
        m_followed.clear();
        followFocus();
    }
    emit detailsShownChanged();
}

QString Browser::focusPath() const
{
    const QString path = activeModel()->pathAt(activeCursor());
    return path.isEmpty() ? folder() : path;
}

void Browser::followFocus()
{
    const QString path = focusPath();
    // The versions follow the keys, except while the versions of one thing are listed.
    if (m_place != QLatin1String("versions"))
        m_versions->setPath(path);
    if (path == m_followed)
        return;
    m_followed = path;
    // A pane that is not shown reads nothing.
    if (m_detailsShown)
        m_details->setPath(path, showHidden());
    if (m_preview->isOpen())
        m_preview->show(activeModel()->pathAt(activeCursor()));
}

void Browser::stepPreview(int by)
{
    FolderModel *model = activeModel();
    const int n = model->count();
    if (n == 0)
        return;
    const int row = std::clamp(activeCursor() + by, 0, n - 1);
    if (row != activeCursor()) {
        setActiveCursor(row);
        if (m_view == Columns)
            previewUnderCursor();
    }
    followFocus();
}

bool Browser::previewKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods != Qt::NoModifier)
        return true;
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_Escape:
        m_preview->setOpen(false);
        m_followed.clear();
        followFocus();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Left:
        stepPreview(-1);
        return true;
    case Qt::Key_Down:
    case Qt::Key_Right:
        stepPreview(+1);
        return true;
    default:
        // The preview is in front of everything; the other keys wait until it closes.
        return true;
    }
}

// The window's switches the menus hold.

void Browser::setSidebarShown(bool shown)
{
    if (shown == m_sidebarShown)
        return;
    m_sidebarShown = shown;
    m_settings->setValue(sidebarShownKey, shown);
    m_settings->sync();
    if (!shown)
        setSidebarFocus(false);
    emit sidebarShownChanged();
}

void Browser::setPaneWidths(const QString &mode)
{
    if (mode != QLatin1String("fixed") && mode != QLatin1String("automatic")
        && mode != QLatin1String("proportional"))
        return;
    if (mode == m_paneWidths)
        return;
    m_paneWidths = mode;
    m_settings->setValue(paneWidthsKey, mode);
    m_settings->sync();
    m_panes->setMode(PaneWidths::modeFromName(mode));
    m_prefs->refresh();
    measureColumns();
    emit paneWidthsChanged();
}

void Browser::setSortKey(FolderModel::Sort key)
{
    m_settings->setValue(sortKey, int(key));
    m_settings->sync();
    m_model->setSortKey(key);
    for (qsizetype i = 1; i < m_columns.size(); ++i)
        m_columns.at(i)->model()->setSortKey(key);
}

void Browser::setShortcutsShown(bool shown)
{
    if (shown == m_shortcutsShown)
        return;
    m_shortcutsShown = shown;
    emit shortcutsShownChanged();
}
