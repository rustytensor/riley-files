// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// A small searchable list in front of the window: an icon for Change Icon, an application for
// Open With > Other Application. What is typed narrows the list (names that start with it
// first, then names that hold it); Up and Down move, Return chooses, Escape closes. The keys
// come from Browser::handleKey; QML only shows what this says.
class Chooser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the chooser; QML only shows it.")

    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(QString title READ title NOTIFY shownChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY shownChanged)
    Q_PROPERTY(QString query READ query NOTIFY rowsChanged)
    // The rows shown: [{label, detail, icon}], icon an Image source or empty.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY rowsChanged)
    // The list is still being gathered.
    Q_PROPERTY(bool loading READ loading NOTIFY rowsChanged)

public:
    struct Item
    {
        QString label;
        QString value;
        QString detail;
        QString icon;
    };
    using Extra = std::function<QList<Item>(const QString &query)>;
    using Chosen = std::function<void(const Item &item)>;

    // At most this many rows are shown; typing narrows the rest.
    static constexpr int maxRows = 200;

    explicit Chooser(QObject *parent = nullptr);

    // Opens with `items` (or none yet, while `loading`). `extra` gives rows for what is typed
    // that are not in the list (an image file's path), shown first; `chosen` gets the row
    // chosen, after the chooser closed. Returns a number that names this opening, for
    // setItems().
    int open(const QString &title, const QString &hint, const QList<Item> &items, bool loading, Extra extra,
             Chosen chosen);
    // The items of the opening `ticket`, once gathered; nothing when it was closed since.
    void setItems(int ticket, const QList<Item> &items);
    Q_INVOKABLE void close();

    bool shown() const { return m_shown; }
    QString title() const { return m_title; }
    QString hint() const { return m_hint; }
    QString query() const { return m_query; }
    QVariantList rows() const;
    int cursor() const { return m_cursor; }
    void setCursor(int row);
    bool loading() const { return m_loading; }

    Q_INVOKABLE void choose(int row);
    // A key while the chooser is shown; every key is used.
    bool key(int key, Qt::KeyboardModifiers mods, const QString &text);

signals:
    void shownChanged();
    void rowsChanged();

private:
    void filter();

    bool m_shown = false;
    int m_ticket = 0;
    QString m_title;
    QString m_hint;
    QString m_query;
    QList<Item> m_items;
    QList<Item> m_matches;
    int m_cursor = -1;
    bool m_loading = false;
    Extra m_extra;
    Chosen m_chosen;
};
