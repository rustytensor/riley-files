// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// Every command of the window in one place: its name, its key, whether it can be used now,
// whether it is on, and what it does. The keys, the menu bar, the context menus and the list of
// keyboard shortcuts all read this one registry, so they never disagree.
//
// It also keeps the state of the menu that is open (from the menu bar or a context menu) and
// answers the keys while one is open. QML draws what it says.
class Actions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the actions; QML only shows them.")

    // The menu bar's menus, left to right.
    Q_PROPERTY(QStringList menuTitles READ menuTitles NOTIFY menusChanged)
    // The menu bar's menu that is open, or -1 (none, or a context menu).
    Q_PROPERTY(int barMenu READ barMenu NOTIFY menuChanged)
    Q_PROPERTY(bool menuOpen READ menuOpen NOTIFY menuChanged)
    // Where a context menu opens, in window coordinates; x < 0 when the window chooses (a menu
    // opened from the keyboard).
    Q_PROPERTY(QPointF menuPoint READ menuPoint NOTIFY menuChanged)
    // The open menu and the submenus open from it, outermost first. Each is a map:
    //   items: [{label, shortcut, enabled, checkable, checked, separator, submenu}], current: row,
    //   parentRow: the row of the level before that opened it (-1 for the first).
    Q_PROPERTY(QVariantList levels READ levels NOTIFY menuChanged)
    // Every action that has a key, menu by menu: [{menu, label, shortcut, others}].
    Q_PROPERTY(QVariantList shortcutList READ shortcutList NOTIFY menusChanged)

public:
    struct Key
    {
        int key = 0;
        Qt::KeyboardModifiers mods = Qt::NoModifier;
    };

    struct Action
    {
        QString id;
        QString label;
        Key key;              // the key shown in menus; key 0 when it has none
        QList<Key> others;    // more keys that do the same (Ctrl+Y for redo, ...)
        std::function<void()> run;
        std::function<bool()> enabled; // empty: always
        std::function<bool()> checked; // empty: not a switch
    };

    struct Entry
    {
        enum Kind { Item, Separator, Submenu };
        Kind kind = Item;
        QString id;    // the action's id, or the submenu's
        QString label; // the submenu's label, or a one-off item's
        std::function<void()> run; // a one-off item's, made when the menu opens (an app ...)
        bool enabled = true;       // a one-off item's
        QList<Entry> children;     // a submenu's; see setSubmenuFill for ones made on opening

        static Entry action(const QString &id);
        static Entry separator();
        static Entry submenu(const QString &id, const QString &label, const QList<Entry> &children = {});
        static Entry oneOff(const QString &label, std::function<void()> run, bool enabled = true);
    };

    explicit Actions(QObject *parent = nullptr);
    ~Actions() override;

    void add(Action action);
    bool has(const QString &id) const { return m_actions.contains(id); }
    QStringList ids() const { return m_order; }

    // The menu bar, as (title, entries) pairs, and the two context menus.
    void setMenus(const QList<std::pair<QString, QList<Entry>>> &menus);
    void setContextMenus(const QList<Entry> &onFile, const QList<Entry> &onEmpty);
    // A submenu whose entries are made each time it opens (Open With lists the apps for the
    // file under the cursor). Replaces any children it was given.
    void setSubmenuFill(const QString &submenuId, std::function<QList<Entry>()> fill);

    // The action a key press is for (its own key or one of its others), or empty.
    QString idForKey(int key, Qt::KeyboardModifiers mods) const;
    // Runs the action when it can be used now. True when it ran.
    bool trigger(const QString &id);
    bool isEnabled(const QString &id) const;
    bool isCheckable(const QString &id) const;
    bool isChecked(const QString &id) const;
    QString label(const QString &id) const;
    QString shortcut(const QString &id) const;

    // "Ctrl+Shift+N", "Alt+Left", "Delete", "F1", "Ctrl+,".
    static QString keyText(const Key &key);

    QStringList menuTitles() const;
    int barMenu() const { return m_barMenu; }
    bool menuOpen() const { return !m_levels.isEmpty(); }
    QPointF menuPoint() const { return m_menuPoint; }
    QVariantList levels() const;
    QVariantList shortcutList() const;

    // Opens a menu of the menu bar, or closes it when it is the one open.
    Q_INVOKABLE void openBarMenu(int index);
    // While a menu of the bar is open, the mouse moving onto another title opens that one.
    // `x` and `y` are the mouse in the window: see hover().
    Q_INVOKABLE void hoverBarMenu(int index, qreal x, qreal y);
    void openContextMenu(bool onFile, QPointF at);
    Q_INVOKABLE void closeMenu();
    // The mouse on a row of an open level: it becomes the current row, and a submenu opens.
    // Only a mouse that moves counts: a menu that opens under a resting mouse (from the keys,
    // or pushed back inside the window) keeps its current row. `x` and `y` are the mouse in
    // the window.
    Q_INVOKABLE void hover(int level, int row, qreal x, qreal y);
    // A row chosen (clicked, or Return): a submenu opens; an action runs and the menu closes.
    Q_INVOKABLE void choose(int level, int row);
    // A key while a menu is open. Every key is used: the menu is in front.
    bool menuKey(int key, Qt::KeyboardModifiers mods);

signals:
    void menusChanged();
    void menuChanged();

private:
    struct Level
    {
        QList<Entry> entries;
        int current = -1;
        int parentRow = -1;
    };

    QList<Entry> childrenOf(const Entry &submenu) const;
    bool entryEnabled(const Entry &entry) const;
    bool selectable(const Entry &entry) const;
    void openLevel(const QList<Entry> &entries, int parentRow);
    void step(int by);
    // True when the mouse is somewhere else than when it was last seen. The first place seen
    // after a menu opens never counts.
    bool mouseMoved(QPointF at);
    void collect(const QList<Entry> &entries, const QString &menu, QVariantList &into,
                 QStringList &seen) const;

    QHash<QString, Action> m_actions;
    QStringList m_order;
    QList<std::pair<QString, QList<Entry>>> m_menus;
    QList<Entry> m_fileMenu;
    QList<Entry> m_emptyMenu;
    QHash<QString, std::function<QList<Entry>()>> m_fills;

    QList<Level> m_levels;
    int m_barMenu = -1;
    QPointF m_menuPoint{-1, -1};
    QPointF m_lastMouse;
    bool m_mouseSeen = false;
};
