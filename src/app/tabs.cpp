// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "tabs.h"

#include "preferences.h"

#include <QDir>
#include <QQmlEngine>

#include <algorithm>
#include <utility>

namespace {

// How many closed tabs can be opened again.
constexpr qsizetype keptClosed = 10;

} // namespace

Tabs::Tabs(const QString &settingsFile, QObject *parent)
    : QAbstractListModel(parent)
    , m_settingsFile(settingsFile)
{
}

Tabs::~Tabs()
{
    // Children of this object; deleted here, before the model goes, so no signal of theirs
    // reaches a model half gone.
    for (const Closed &closed : std::as_const(m_closed))
        delete closed.browser;
    m_closed.clear();
    const QList<Browser *> tabs = m_tabs;
    m_tabs.clear();
    m_current = -1;
    qDeleteAll(tabs);
}

int Tabs::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_tabs.size());
}

QVariant Tabs::data(const QModelIndex &index, int role) const
{
    const Browser *browser = at(index.row());
    if (!browser || index.parent().isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return titleOf(browser);
    case FolderRole:
        return browser->folder();
    case ActiveRole:
        return index.row() == m_current;
    default:
        return {};
    }
}

QHash<int, QByteArray> Tabs::roleNames() const
{
    return {{TitleRole, "title"}, {FolderRole, "folder"}, {ActiveRole, "active"}};
}

QStringList Tabs::titles() const
{
    QStringList titles;
    for (const Browser *browser : m_tabs)
        titles.append(titleOf(browser));
    return titles;
}

QString Tabs::titleOf(const Browser *browser) const
{
    if (browser->place().isEmpty()) {
        const QString folder = browser->folder();
        if (!folder.isEmpty() && QDir::cleanPath(folder) == QDir::cleanPath(QDir::homePath()))
            return tr("Home");
    }
    return browser->folderName();
}

Browser *Tabs::make()
{
    auto *browser = new Browser(m_settingsFile, this);
    // QML only shows it; the tabs decide when it goes.
    QQmlEngine::setObjectOwnership(browser, QQmlEngine::CppOwnership);
    connect(browser, &Browser::folderChanged, this, [this, browser] { titleChanged(browser); });
    connect(browser, &Browser::placeChanged, this, [this, browser] { titleChanged(browser); });
    connect(browser, &Browser::newWindowAsked, this, &Tabs::newWindowAsked);
    connect(browser, &Browser::newTabAsked, this, [this, browser](const QString &location, bool background) {
        // Next to the tab that asked, which is the active one unless it is in the background.
        const qsizetype asking = m_tabs.indexOf(browser);
        if (asking < 0)
            return;
        Browser *tab = make();
        tab->openLocation(location);
        insert(tab, int(asking) + 1, !background);
    });
    connect(browser, &Browser::closeTabAsked, this, [this, browser] { close(int(m_tabs.indexOf(browser))); });
    connect(browser, &Browser::reopenTabAsked, this, &Tabs::reopen);
    connect(browser, &Browser::stepTabAsked, this, &Tabs::step);
    connect(browser, &Browser::goToTabAsked, this, &Tabs::activate);
    // The tabs are one window: an option set in one tab's Preferences holds in every tab, open or closed.
    connect(browser->prefs(), &Preferences::changed, this, [this, browser](const QString &id, const QString &value) {
        if (m_sharing)
            return;
        m_sharing = true;
        for (Browser *tab : std::as_const(m_tabs))
            if (tab != browser)
                tab->prefs()->follow(id, value);
        for (const Closed &closed : std::as_const(m_closed))
            if (closed.browser != browser)
                closed.browser->prefs()->follow(id, value);
        m_sharing = false;
    });
    return browser;
}

Browser *Tabs::open(const QString &location, bool background, int row)
{
    Browser *browser = make();
    browser->openLocation(location);
    insert(browser, row < 0 ? m_current + 1 : row, !background || m_current < 0);
    return browser;
}

void Tabs::insert(Browser *browser, int row, bool activate)
{
    row = std::clamp(row, 0, count());
    beginInsertRows(QModelIndex(), row, row);
    m_tabs.insert(row, browser);
    endInsertRows();
    if (m_current >= row) {
        // The same tab, one place further right.
        ++m_current;
        emit currentChanged();
    }
    tellCount();
    if (activate || m_current < 0)
        setCurrent(row);
}

void Tabs::activate(int row)
{
    if (row >= 0 && row < count())
        setCurrent(row);
}

void Tabs::setCurrent(int row)
{
    if (row == m_current)
        return;
    const int was = m_current;
    m_current = row;
    // Whatever the tab left was doing in front of the files (a menu) closes with it.
    if (Browser *left = at(was)) {
        left->actions()->closeMenu();
        // A location half typed is not kept: the tab comes back showing where it is.
        if (left->locationEditing())
            left->cancelLocation();
    }
    emit currentChanged();
    if (was >= 0 && was < count())
        emit dataChanged(index(was), index(was), {ActiveRole});
    emit dataChanged(index(row), index(row), {ActiveRole});
}

void Tabs::close(int row)
{
    if (count() <= 1 || row < 0 || row >= count())
        return;
    Browser *browser = m_tabs.at(row);
    // The window moves to the tab to the right, or to the left at the end, before this one goes.
    if (row == m_current)
        setCurrent(row + 1 < count() ? row + 1 : row - 1);
    beginRemoveRows(QModelIndex(), row, row);
    m_tabs.removeAt(row);
    endRemoveRows();
    if (m_current > row) {
        --m_current;
        emit currentChanged();
    }
    browser->actions()->closeMenu();
    m_closed.append({browser, row});
    if (m_closed.size() > keptClosed)
        m_closed.takeFirst().browser->deleteLater();
    tellCount();
}

void Tabs::reopen()
{
    if (m_closed.isEmpty())
        return;
    const Closed closed = m_closed.takeLast();
    insert(closed.browser, closed.row, true);
}

void Tabs::step(int by)
{
    const int n = count();
    if (n < 2)
        return;
    setCurrent(((m_current + by) % n + n) % n);
}

void Tabs::tellCount()
{
    for (Browser *browser : std::as_const(m_tabs))
        browser->setTabCount(count());
    emit countChanged();
}

void Tabs::titleChanged(Browser *browser)
{
    const qsizetype row = m_tabs.indexOf(browser);
    if (row >= 0)
        emit dataChanged(index(int(row)), index(int(row)), {TitleRole, FolderRole, Qt::DisplayRole});
}
