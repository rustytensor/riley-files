// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "browser.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#include <algorithm>

using Riley::FolderModel;

namespace {

// Letters typed closer together than this make one name to find; a longer pause starts again.
constexpr qint64 findPauseMs = 1000;

const char *const viewKey = "browse/view";
const char *const treeKey = "browse/tree";
const char *const hiddenKey = "browse/showHidden";

QString viewToName(Browser::View view)
{
    switch (view) {
    case Browser::Grid:
        return QStringLiteral("grid");
    case Browser::Columns:
        return QStringLiteral("columns");
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
    , m_settings(settingsFile.isEmpty() ? std::make_unique<QSettings>()
                                        : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat))
    , m_model(std::make_unique<FolderModel>())
{
    m_view = viewFromName(m_settings->value(viewKey).toString());
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

    // The first column of the columns view is the folder itself, with the browser's cursor.
    auto *first = new FolderColumn(model, this);
    first->setReady(false);
    connect(this, &Browser::cursorChanged, first, [this, first] { first->mirrorCursor(m_cursor); });
    connect(model, &FolderModel::loaded, first, [first] { first->setReady(true); });
    m_columns.append(first);
}

Browser::~Browser() = default;

QString Browser::folderName() const
{
    const QString path = folder();
    if (path == QLatin1String("/"))
        return path;
    if (path == QDir::homePath())
        return QStringLiteral("Home");
    return QFileInfo(path).fileName();
}

void Browser::setCursor(int row)
{
    m_cursorWaiting = false;
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
    m_view = view;
    m_settings->setValue(viewKey, viewToName(view));
    m_settings->sync();
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
        path = QDir::homePath();
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
    if (m_model->isDirAt(row))
        open(m_model->pathAt(row));
    // Opening files with their application comes with the default apps milestone.
}

bool Browser::handleKey(int key, int modifiers, const QString &text)
{
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;

    if (key == Qt::Key_Back) {
        goBack();
        return true;
    }
    if (key == Qt::Key_Forward) {
        goForward();
        return true;
    }

    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_1:
            setView(Grid);
            return true;
        case Qt::Key_2:
            setView(List);
            return true;
        case Qt::Key_3:
            setView(Columns);
            return true;
        case Qt::Key_H:
            setShowHidden(!showHidden());
            return true;
        case Qt::Key_R:
            m_model->reload();
            return true;
        default:
            return false;
        }
    }

    if (mods == Qt::AltModifier) {
        switch (key) {
        case Qt::Key_Left:
            goBack();
            return true;
        case Qt::Key_Right:
            goForward();
            return true;
        case Qt::Key_Up:
            goUp();
            return true;
        case Qt::Key_Down:
            activate(m_cursor);
            return true;
        default:
            return false;
        }
    }

    const bool finding = !m_findText.isEmpty() && m_findTimer.isValid()
                         && m_findTimer.elapsed() < findPauseMs;

    if (mods == Qt::NoModifier) {
        if (key == Qt::Key_Backspace) {
            goUp();
            return true;
        }
        // T is the tree switch, except in the middle of typing a name.
        if (key == Qt::Key_T && !finding && m_view == List) {
            setTree(!m_tree);
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
    case Qt::Key_Return:
    case Qt::Key_Enter:
        activate(m_cursor);
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
    case Qt::Key_Return:
    case Qt::Key_Enter:
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
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (model->isDirAt(row))
            open(model->pathAt(row));
        return true;
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
    m_columns.append(new FolderColumn(path, m_model->showHidden(), this));
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
