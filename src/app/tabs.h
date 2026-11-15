// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "browser.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

// The tabs of one window, left to right. Each tab is a browser of its own, with its own folder,
// history, view, cursor, hidden files, search and graph; the window shows the active one. The
// browsers ask for what only the tabs can do (a new tab, closing, going to another) through
// their signals, and the decisions about it are made here.
class Tabs : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The window makes its tabs; QML only shows them.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    // The active tab's browser: what the window shows.
    Q_PROPERTY(Browser *browser READ browser NOTIFY currentChanged)

public:
    enum Role { TitleRole = Qt::UserRole + 1, FolderRole, ActiveRole };

    // `settingsFile` as for Browser: empty means the user's usual settings.
    explicit Tabs(const QString &settingsFile = QString(), QObject *parent = nullptr);
    ~Tabs() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_tabs.size()); }
    int current() const { return m_current; }
    Browser *browser() const { return m_current >= 0 ? m_tabs.at(m_current) : nullptr; }
    Browser *at(int row) const { return row >= 0 && row < m_tabs.size() ? m_tabs.at(row) : nullptr; }
    // What each tab's label says: the folder's name, "Home" for the home folder.
    QStringList titles() const;

    // A new tab on `location` (as Browser::openLocation takes it) at `row`, or right after the
    // active one when row is -1, made active unless it opens in the background. The first tab
    // is always made active.
    Browser *open(const QString &location, bool background = false, int row = -1);
    // Makes this tab the active one.
    Q_INVOKABLE void activate(int row);
    // Closes this tab; the last one is never closed here (closing it closes the window).
    Q_INVOKABLE void close(int row);
    // Opens the tab closed last again, where it was, with everything it had.
    void reopen();
    // The next tab (by 1) or the one before (by -1), going round at the ends.
    void step(int by);

signals:
    void countChanged();
    void currentChanged();
    // File > New Window, from any tab.
    void newWindowAsked(const QString &folder);

private:
    struct Closed
    {
        Browser *browser = nullptr;
        int row = 0;
    };

    Browser *make();
    void insert(Browser *browser, int row, bool activate);
    void setCurrent(int row);
    void tellCount();
    void titleChanged(Browser *browser);
    QString titleOf(const Browser *browser) const;

    QString m_settingsFile;
    QList<Browser *> m_tabs;
    int m_current = -1;
    // The tabs closed, the last closed at the end; kept whole so they come back as they were.
    QList<Closed> m_closed;
    // True while one tab's changed option is handed to the others, so it is not handed back.
    bool m_sharing = false;
};
