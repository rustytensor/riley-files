// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "chooser.h"

#include <QVariantMap>

#include <algorithm>

Chooser::Chooser(QObject *parent)
    : QObject(parent)
{
}

int Chooser::open(const QString &title, const QString &hint, const QList<Item> &items, bool loading, Extra extra,
                  Chosen chosen)
{
    m_shown = true;
    m_title = title;
    m_hint = hint;
    m_query.clear();
    m_items = items;
    m_loading = loading;
    m_extra = std::move(extra);
    m_chosen = std::move(chosen);
    filter();
    emit shownChanged();
    return ++m_ticket;
}

void Chooser::setItems(int ticket, const QList<Item> &items)
{
    if (!m_shown || ticket != m_ticket)
        return;
    m_items = items;
    m_loading = false;
    filter();
}

void Chooser::close()
{
    if (!m_shown)
        return;
    m_shown = false;
    ++m_ticket;
    m_items.clear();
    m_matches.clear();
    m_query.clear();
    m_cursor = -1;
    m_extra = {};
    m_chosen = {};
    emit rowsChanged();
    emit shownChanged();
}

QVariantList Chooser::rows() const
{
    QVariantList list;
    for (const Item &item : m_matches) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), item.label);
        row.insert(QStringLiteral("detail"), item.detail);
        row.insert(QStringLiteral("icon"), item.icon);
        list.append(row);
    }
    return list;
}

void Chooser::setCursor(int row)
{
    const int clamped = m_matches.isEmpty() ? -1 : std::clamp(row, 0, int(m_matches.size()) - 1);
    if (clamped == m_cursor)
        return;
    m_cursor = clamped;
    emit rowsChanged();
}

void Chooser::filter()
{
    QList<Item> starts;
    QList<Item> holds;
    const QString query = m_query.trimmed();
    for (const Item &item : std::as_const(m_items)) {
        if (starts.size() >= maxRows)
            break;
        if (query.isEmpty() || item.label.startsWith(query, Qt::CaseInsensitive))
            starts.append(item);
        else if (holds.size() < maxRows && item.label.contains(query, Qt::CaseInsensitive))
            holds.append(item);
    }
    m_matches = m_extra && !query.isEmpty() ? m_extra(query) : QList<Item>();
    m_matches.append(starts);
    m_matches.append(holds);
    if (m_matches.size() > maxRows)
        m_matches.resize(maxRows);
    m_cursor = m_matches.isEmpty() ? -1 : 0;
    emit rowsChanged();
}

void Chooser::choose(int row)
{
    if (row < 0 || row >= m_matches.size())
        return;
    const Item item = m_matches.at(row);
    const Chosen chosen = m_chosen;
    close();
    if (chosen)
        chosen(item);
}

bool Chooser::key(int key, Qt::KeyboardModifiers mods, const QString &text)
{
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return true;
    switch (key) {
    case Qt::Key_Escape:
        close();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        choose(m_cursor);
        return true;
    case Qt::Key_Up:
        setCursor(m_cursor - 1);
        return true;
    case Qt::Key_Down:
        setCursor(m_cursor + 1);
        return true;
    case Qt::Key_PageUp:
        setCursor(m_cursor - 10);
        return true;
    case Qt::Key_PageDown:
        setCursor(m_cursor + 10);
        return true;
    case Qt::Key_Backspace:
        if (!m_query.isEmpty()) {
            m_query.chop(1);
            filter();
        }
        return true;
    default:
        break;
    }
    if (!text.isEmpty() && text.at(0).isPrint()) {
        m_query += text;
        filter();
    }
    return true;
}
