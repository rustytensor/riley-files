// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "actions.h"

#include <QKeySequence>
#include <QVariantMap>

#include <utility>

Actions::Entry Actions::Entry::action(const QString &id)
{
    Entry entry;
    entry.id = id;
    return entry;
}

Actions::Entry Actions::Entry::separator()
{
    Entry entry;
    entry.kind = Separator;
    return entry;
}

Actions::Entry Actions::Entry::submenu(const QString &id, const QString &label, const QList<Entry> &children)
{
    Entry entry;
    entry.kind = Submenu;
    entry.id = id;
    entry.label = label;
    entry.children = children;
    return entry;
}

Actions::Entry Actions::Entry::oneOff(const QString &label, std::function<void()> run, bool enabled)
{
    Entry entry;
    entry.label = label;
    entry.run = std::move(run);
    entry.enabled = enabled;
    return entry;
}

Actions::Actions(QObject *parent)
    : QObject(parent)
{
}

Actions::~Actions() = default;

void Actions::add(Action action)
{
    const QString id = action.id;
    if (!m_actions.contains(id))
        m_order.append(id);
    m_actions.insert(id, std::move(action));
    emit menusChanged();
}

void Actions::setMenus(const QList<std::pair<QString, QList<Entry>>> &menus)
{
    closeMenu();
    m_menus = menus;
    emit menusChanged();
}

void Actions::setContextMenus(const QList<Entry> &onFile, const QList<Entry> &onEmpty)
{
    closeMenu();
    m_fileMenu = onFile;
    m_emptyMenu = onEmpty;
}

void Actions::setSubmenuFill(const QString &submenuId, std::function<QList<Entry>()> fill)
{
    m_fills.insert(submenuId, std::move(fill));
}

QString Actions::idForKey(int key, Qt::KeyboardModifiers mods) const
{
    if (key == 0)
        return {};
    for (const QString &id : m_order) {
        const Action &action = m_actions[id];
        if (action.key.key == key && action.key.mods == mods)
            return id;
        for (const Key &other : action.others) {
            if (other.key == key && other.mods == mods)
                return id;
        }
    }
    return {};
}

bool Actions::trigger(const QString &id)
{
    const auto it = m_actions.constFind(id);
    if (it == m_actions.cend() || !isEnabled(id) || !it->run)
        return false;
    // A copy: the action may change the registry while it runs.
    const std::function<void()> run = it->run;
    run();
    return true;
}

bool Actions::isEnabled(const QString &id) const
{
    const auto it = m_actions.constFind(id);
    if (it == m_actions.cend())
        return false;
    return !it->enabled || it->enabled();
}

bool Actions::isCheckable(const QString &id) const
{
    const auto it = m_actions.constFind(id);
    return it != m_actions.cend() && bool(it->checked);
}

bool Actions::isChecked(const QString &id) const
{
    const auto it = m_actions.constFind(id);
    return it != m_actions.cend() && it->checked && it->checked();
}

QString Actions::label(const QString &id) const
{
    const auto it = m_actions.constFind(id);
    return it == m_actions.cend() ? QString() : it->label;
}

QString Actions::shortcut(const QString &id) const
{
    const auto it = m_actions.constFind(id);
    return it == m_actions.cend() ? QString() : keyText(it->key);
}

QString Actions::keyText(const Key &key)
{
    if (key.key == 0)
        return {};
    QString text;
    if (key.mods & Qt::ControlModifier)
        text += QStringLiteral("Ctrl+");
    if (key.mods & Qt::ShiftModifier)
        text += QStringLiteral("Shift+");
    if (key.mods & Qt::AltModifier)
        text += QStringLiteral("Alt+");
    if (key.mods & Qt::MetaModifier)
        text += QStringLiteral("Meta+");

    const int k = key.key;
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return text + QChar(u'A' + (k - Qt::Key_A));
    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return text + QChar(u'0' + (k - Qt::Key_0));
    if (k >= Qt::Key_F1 && k <= Qt::Key_F35)
        return text + QStringLiteral("F%1").arg(k - Qt::Key_F1 + 1);
    switch (k) {
    case Qt::Key_Return:
        return text + QStringLiteral("Return");
    case Qt::Key_Enter:
        return text + QStringLiteral("Enter");
    case Qt::Key_Delete:
        return text + QStringLiteral("Delete");
    case Qt::Key_Backspace:
        return text + QStringLiteral("Backspace");
    case Qt::Key_Space:
        return text + QStringLiteral("Space");
    case Qt::Key_Escape:
        return text + QStringLiteral("Escape");
    case Qt::Key_Left:
        return text + QStringLiteral("Left");
    case Qt::Key_Right:
        return text + QStringLiteral("Right");
    case Qt::Key_Up:
        return text + QStringLiteral("Up");
    case Qt::Key_Down:
        return text + QStringLiteral("Down");
    case Qt::Key_Home:
        return text + QStringLiteral("Home");
    case Qt::Key_End:
        return text + QStringLiteral("End");
    case Qt::Key_Comma:
        return text + QStringLiteral(",");
    case Qt::Key_Period:
        return text + QStringLiteral(".");
    case Qt::Key_Back:
        return text + QStringLiteral("Back");
    case Qt::Key_Forward:
        return text + QStringLiteral("Forward");
    case Qt::Key_Menu:
        return text + QStringLiteral("Menu");
    default:
        return text + QKeySequence(k).toString(QKeySequence::PortableText);
    }
}

QStringList Actions::menuTitles() const
{
    QStringList titles;
    for (const auto &menu : m_menus)
        titles.append(menu.first);
    return titles;
}

QList<Actions::Entry> Actions::childrenOf(const Entry &submenu) const
{
    const auto fill = m_fills.constFind(submenu.id);
    if (fill != m_fills.cend() && *fill)
        return (*fill)();
    return submenu.children;
}

bool Actions::entryEnabled(const Entry &entry) const
{
    switch (entry.kind) {
    case Entry::Separator:
        return false;
    case Entry::Submenu:
        return true;
    case Entry::Item:
        break;
    }
    if (entry.run)
        return entry.enabled;
    return isEnabled(entry.id);
}

bool Actions::selectable(const Entry &entry) const
{
    return entry.kind != Entry::Separator;
}

QVariantList Actions::levels() const
{
    QVariantList list;
    for (const Level &level : m_levels) {
        QVariantList items;
        for (const Entry &entry : level.entries) {
            QVariantMap item;
            const bool own = entry.kind != Entry::Item || bool(entry.run);
            item.insert(QStringLiteral("label"), own ? entry.label : label(entry.id));
            item.insert(QStringLiteral("shortcut"), own ? QString() : shortcut(entry.id));
            item.insert(QStringLiteral("enabled"), entryEnabled(entry));
            item.insert(QStringLiteral("checkable"), !own && isCheckable(entry.id));
            item.insert(QStringLiteral("checked"), !own && isChecked(entry.id));
            item.insert(QStringLiteral("separator"), entry.kind == Entry::Separator);
            item.insert(QStringLiteral("submenu"), entry.kind == Entry::Submenu);
            items.append(item);
        }
        QVariantMap map;
        map.insert(QStringLiteral("items"), items);
        map.insert(QStringLiteral("current"), level.current);
        map.insert(QStringLiteral("parentRow"), level.parentRow);
        list.append(map);
    }
    return list;
}

void Actions::collect(const QList<Entry> &entries, const QString &menu, QVariantList &into,
                      QStringList &seen) const
{
    for (const Entry &entry : entries) {
        if (entry.kind == Entry::Submenu) {
            collect(entry.children, menu, into, seen);
            continue;
        }
        if (entry.kind != Entry::Item || entry.run || seen.contains(entry.id))
            continue;
        const auto it = m_actions.constFind(entry.id);
        if (it == m_actions.cend() || it->key.key == 0)
            continue;
        seen.append(entry.id);
        QStringList others;
        for (const Key &other : it->others)
            others.append(keyText(other));
        QVariantMap row;
        row.insert(QStringLiteral("menu"), menu);
        row.insert(QStringLiteral("label"), it->label);
        row.insert(QStringLiteral("shortcut"), keyText(it->key));
        row.insert(QStringLiteral("others"), others.join(QStringLiteral(", ")));
        into.append(row);
    }
}

QVariantList Actions::shortcutList() const
{
    QVariantList list;
    QStringList seen;
    for (const auto &menu : m_menus)
        collect(menu.second, menu.first, list, seen);
    // An action with a key that no menu holds is still listed, so no key is ever hidden.
    QList<Entry> rest;
    for (const QString &id : m_order) {
        if (!seen.contains(id))
            rest.append(Entry::action(id));
    }
    collect(rest, QStringLiteral("Other"), list, seen);
    return list;
}

void Actions::openLevel(const QList<Entry> &entries, int parentRow)
{
    Level level;
    level.entries = entries;
    level.parentRow = parentRow;
    for (int i = 0; i < entries.size(); ++i) {
        if (selectable(entries.at(i))) {
            level.current = i;
            break;
        }
    }
    m_levels.append(level);
}

void Actions::openBarMenu(int index)
{
    if (index < 0 || index >= m_menus.size())
        return;
    if (m_barMenu == index) {
        closeMenu();
        return;
    }
    m_levels.clear();
    m_mouseSeen = false;
    m_barMenu = index;
    m_menuPoint = QPointF(-1, -1);
    openLevel(m_menus.at(index).second, -1);
    emit menuChanged();
}

bool Actions::mouseMoved(QPointF at)
{
    // Only a change of place counts. A menu redrawn under a resting mouse reports the same place
    // again, and must not reopen the submenu just closed with Escape.
    const bool moved = m_mouseSeen && at != m_lastMouse;
    m_mouseSeen = true;
    m_lastMouse = at;
    return moved;
}

void Actions::hoverBarMenu(int index, qreal x, qreal y)
{
    if (!mouseMoved(QPointF(x, y)))
        return;
    if (m_barMenu >= 0 && index != m_barMenu) {
        openBarMenu(index);
        // The mouse is where it was; it is not a new menu's resting place.
        m_mouseSeen = true;
        m_lastMouse = QPointF(x, y);
    }
}

void Actions::openContextMenu(bool onFile, QPointF at)
{
    openMenu(onFile ? m_fileMenu : m_emptyMenu, at);
}

void Actions::openMenu(const QList<Entry> &entries, QPointF at)
{
    m_levels.clear();
    m_mouseSeen = false;
    m_barMenu = -1;
    m_menuPoint = at;
    openLevel(entries, -1);
    emit menuChanged();
}

void Actions::closeMenu()
{
    if (m_levels.isEmpty() && m_barMenu < 0)
        return;
    m_levels.clear();
    m_barMenu = -1;
    m_menuPoint = QPointF(-1, -1);
    emit menuChanged();
}

void Actions::hover(int level, int row, qreal x, qreal y)
{
    if (!mouseMoved(QPointF(x, y)))
        return;
    if (level < 0 || level >= m_levels.size())
        return;
    const QList<Entry> &entries = m_levels.at(level).entries;
    if (row < 0 || row >= entries.size() || !selectable(entries.at(row)))
        return;
    if (m_levels.at(level).current == row && m_levels.size() == level + 1 + (entries.at(row).kind == Entry::Submenu))
        return;
    m_levels.resize(level + 1);
    m_levels[level].current = row;
    if (entries.at(row).kind == Entry::Submenu)
        openLevel(childrenOf(entries.at(row)), row);
    emit menuChanged();
}

void Actions::choose(int level, int row)
{
    if (level < 0 || level >= m_levels.size())
        return;
    const QList<Entry> entries = m_levels.at(level).entries;
    if (row < 0 || row >= entries.size())
        return;
    const Entry entry = entries.at(row);
    if (!entryEnabled(entry))
        return;
    if (entry.kind == Entry::Submenu) {
        m_levels.resize(level + 1);
        m_levels[level].current = row;
        openLevel(childrenOf(entry), row);
        emit menuChanged();
        return;
    }
    // The menu closes before the action runs, so what it opens (a name to edit, a dialog)
    // is in front.
    closeMenu();
    if (entry.run)
        entry.run();
    else
        trigger(entry.id);
}

void Actions::step(int by)
{
    Level &level = m_levels.last();
    const int n = int(level.entries.size());
    if (n == 0)
        return;
    int row = level.current;
    for (int tries = 0; tries < n; ++tries) {
        row = row < 0 ? (by > 0 ? 0 : n - 1) : (row + by + n) % n;
        if (selectable(level.entries.at(row))) {
            level.current = row;
            emit menuChanged();
            return;
        }
    }
}

bool Actions::menuKey(int key, Qt::KeyboardModifiers mods)
{
    if (m_levels.isEmpty())
        return false;
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        // Alt alone or with another key closes the menu, as the menu bar's own key would.
        if (key == Qt::Key_Alt)
            closeMenu();
        return true;
    }
    const int deepest = int(m_levels.size()) - 1;
    const Level &level = m_levels.last();
    const bool onRow = level.current >= 0 && level.current < level.entries.size();
    switch (key) {
    case Qt::Key_Escape:
        if (deepest > 0) {
            m_levels.removeLast();
            emit menuChanged();
        } else {
            closeMenu();
        }
        return true;
    case Qt::Key_F10:
        closeMenu();
        return true;
    case Qt::Key_Up:
        step(-1);
        return true;
    case Qt::Key_Down:
        step(+1);
        return true;
    case Qt::Key_Home:
        m_levels.last().current = -1;
        step(+1);
        return true;
    case Qt::Key_End:
        m_levels.last().current = -1;
        step(-1);
        return true;
    case Qt::Key_Right:
        if (onRow && level.entries.at(level.current).kind == Entry::Submenu) {
            choose(deepest, level.current);
        } else if (m_barMenu >= 0) {
            openBarMenu((m_barMenu + 1) % int(m_menus.size()));
        }
        return true;
    case Qt::Key_Left:
        if (deepest > 0) {
            m_levels.removeLast();
            emit menuChanged();
        } else if (m_barMenu >= 0) {
            const int n = int(m_menus.size());
            openBarMenu((m_barMenu - 1 + n) % n);
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (onRow)
            choose(deepest, level.current);
        return true;
    default:
        return true;
    }
}
