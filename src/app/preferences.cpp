// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "preferences.h"

#include <QSettings>

#include <algorithm>

Preferences::Preferences(QSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
    , m_pages{tr("Options")}
{
}

Preferences::~Preferences() = default;

void Preferences::add(const Option &option)
{
    m_options.append(option);
    if (option.page != m_page)
        return;
    const int row = int(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.append(int(m_options.size()) - 1);
    endInsertRows();
}

int Preferences::addPage(const QString &title)
{
    m_pages.append(title);
    emit pagesChanged();
    return int(m_pages.size()) - 1;
}

void Preferences::setPageOptions(int page, const QList<Option> &options)
{
    const bool shown = page == m_page;
    if (shown)
        beginResetModel();
    // The page's rows stay together, where its first one was (or at the end).
    qsizetype at = -1;
    for (qsizetype i = m_options.size() - 1; i >= 0; --i) {
        if (m_options.at(i).page == page) {
            m_options.removeAt(i);
            at = i;
        }
    }
    if (at < 0)
        at = m_options.size();
    for (Option option : options) {
        option.page = page;
        m_options.insert(at++, option);
    }
    if (shown) {
        showRows();
        endResetModel();
        if (m_cursor >= m_rows.size()) {
            m_cursor = int(m_rows.size()) - 1;
            emit cursorChanged();
        }
        emit valuesChanged();
    }
}

void Preferences::showRows()
{
    m_rows.clear();
    for (qsizetype i = 0; i < m_options.size(); ++i) {
        if (m_options.at(i).page == m_page)
            m_rows.append(int(i));
    }
}

void Preferences::setPage(int page)
{
    page = std::clamp(page, 0, int(m_pages.size()) - 1);
    if (page == m_page)
        return;
    cancelEditing();
    beginResetModel();
    m_page = page;
    showRows();
    endResetModel();
    emit pageChanged();
    // The keys stay on the tabs, so the next page can be chosen the same way.
    if (m_cursor != -1) {
        m_cursor = -1;
        emit cursorChanged();
    }
}

int Preferences::optionOf(const QString &id) const
{
    for (qsizetype i = 0; i < m_options.size(); ++i) {
        if (m_options.at(i).id == id)
            return int(i);
    }
    return -1;
}

int Preferences::optionAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : -1;
}

int Preferences::rowOfOption(int option) const
{
    return int(m_rows.indexOf(option));
}

QString Preferences::valueOf(int option) const
{
    if (option < 0 || option >= m_options.size())
        return {};
    const Option &o = m_options.at(option);
    if (o.get)
        return o.get();
    if (o.key.isEmpty())
        return o.fallback;
    const QString kept = m_settings->value(o.key, o.fallback).toString();
    // A value the file holds that is not one of the choices reads as the default.
    if (o.kind == Choice && !o.choiceValues.contains(kept))
        return o.fallback;
    if (o.kind == Toggle && kept != QLatin1String("true") && kept != QLatin1String("false"))
        return o.fallback;
    return kept;
}

QString Preferences::value(const QString &id) const
{
    return valueOf(optionOf(id));
}

void Preferences::setValue(const QString &id, const QString &value)
{
    store(optionOf(id), value);
}

void Preferences::store(int option, const QString &value)
{
    if (option < 0 || option >= m_options.size() || valueOf(option) == value)
        return;
    // A copy: what `set` does may replace the page's options.
    const Option o = m_options.at(option);
    if (o.set) {
        o.set(value);
    } else {
        m_settings->setValue(o.key, value);
        m_settings->sync();
    }
    const int row = rowOfOption(optionOf(o.id));
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit valuesChanged();
    emit changed(o.id, value);
}

int Preferences::thumbnailSize() const
{
    const int size = value(QStringLiteral("thumbnailSize")).toInt();
    return size > 0 ? size : 44;
}

QStringList Preferences::sections() const
{
    QStringList list;
    for (int option : m_rows) {
        const QString &section = m_options.at(option).section;
        if (!list.contains(section))
            list.append(section);
    }
    return list;
}

void Preferences::setShown(bool shown)
{
    if (shown == m_shown)
        return;
    cancelEditing();
    m_shown = shown;
    if (shown)
        refresh();
    emit shownChanged();
}

void Preferences::setCursor(int row)
{
    if (m_rows.isEmpty())
        row = -1;
    row = std::clamp(row, -1, int(m_rows.size()) - 1);
    if (row == m_cursor)
        return;
    m_cursor = row;
    emit cursorChanged();
}

void Preferences::toggle(int row)
{
    const int option = optionAt(row);
    if (option < 0 || m_options.at(option).kind != Toggle)
        return;
    setCursor(row);
    store(option, valueOf(option) == QLatin1String("true") ? QStringLiteral("false") : QStringLiteral("true"));
}

void Preferences::choose(int row, int choice)
{
    const int option = optionAt(row);
    if (option < 0 || m_options.at(option).kind != Choice)
        return;
    setCursor(row);
    const QStringList &values = m_options.at(option).choiceValues;
    if (choice >= 0 && choice < values.size())
        store(option, values.at(choice));
}

void Preferences::press(int row)
{
    const int option = optionAt(row);
    if (option < 0 || m_options.at(option).kind != Button)
        return;
    setCursor(row);
    const Option o = m_options.at(option);
    if (o.set)
        o.set(QString());
    emit changed(o.id, QString());
}

void Preferences::remove(int row)
{
    const int option = optionAt(row);
    if (option < 0 || !m_options.at(option).remove)
        return;
    setCursor(row);
    const std::function<void()> run = m_options.at(option).remove;
    run();
}

void Preferences::step(int row, int by)
{
    const int option = optionAt(row);
    const Option &o = m_options.at(option);
    const int at = int(o.choiceValues.indexOf(valueOf(option)));
    const int count = int(o.choiceValues.size());
    choose(row, std::clamp(at + by, 0, count - 1));
}

void Preferences::editText(int row)
{
    const int option = optionAt(row);
    if (option < 0 || m_options.at(option).kind != Text)
        return;
    setCursor(row);
    if (m_editing == row)
        return;
    m_editing = row;
    emit editingChanged();
}

void Preferences::setText(int row, const QString &text)
{
    const int option = optionAt(row);
    cancelEditing();
    if (option >= 0 && m_options.at(option).kind == Text)
        store(option, text.trimmed());
}

void Preferences::cancelEditing()
{
    if (m_editing < 0)
        return;
    m_editing = -1;
    emit editingChanged();
}

bool Preferences::key(int key, Qt::KeyboardModifiers mods)
{
    if ((key == Qt::Key_Escape && mods == Qt::NoModifier) || (key == Qt::Key_Comma && mods == Qt::ControlModifier)) {
        setShown(false);
        return true;
    }
    // Ctrl+Page Down and Ctrl+Page Up turn the pages from anywhere.
    if (mods == Qt::ControlModifier && (key == Qt::Key_PageDown || key == Qt::Key_PageUp)) {
        const int count = int(m_pages.size());
        setPage((m_page + (key == Qt::Key_PageDown ? 1 : count - 1)) % count);
        return true;
    }
    // Tab and Shift+Tab go round the page tabs and every option, from the last back to the
    // tabs, so the keys reach each one.
    const int count = int(m_rows.size()) + 1; // the tabs are -1
    if (key == Qt::Key_Backtab || (key == Qt::Key_Tab && mods == Qt::ShiftModifier)) {
        setCursor((m_cursor + 1 - 1 + count) % count - 1);
        return true;
    }
    if (mods != Qt::NoModifier)
        return true;
    if (key == Qt::Key_Tab) {
        setCursor((m_cursor + 1 + 1) % count - 1);
        return true;
    }
    if (m_cursor < 0) {
        // On the page tabs.
        switch (key) {
        case Qt::Key_Left:
            setPage(m_page - 1);
            break;
        case Qt::Key_Right:
            setPage(m_page + 1);
            break;
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            setPage((m_page + 1) % int(m_pages.size()));
            break;
        case Qt::Key_Down:
            setCursor(0);
            break;
        default:
            break;
        }
        return true;
    }
    const int option = optionAt(m_cursor);
    if (option < 0)
        return true;
    const Option &o = m_options.at(option);
    switch (key) {
    case Qt::Key_Up:
        setCursor(m_cursor - 1);
        break;
    case Qt::Key_Down:
        setCursor(m_cursor + 1);
        break;
    case Qt::Key_Home:
        setCursor(0);
        break;
    case Qt::Key_End:
        setCursor(int(m_rows.size()) - 1);
        break;
    case Qt::Key_Left:
        if (o.kind == Choice)
            step(m_cursor, -1);
        break;
    case Qt::Key_Right:
        if (o.kind == Choice)
            step(m_cursor, 1);
        break;
    case Qt::Key_Delete:
        remove(m_cursor);
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (o.kind == Toggle)
            toggle(m_cursor);
        else if (o.kind == Text)
            editText(m_cursor);
        else if (o.kind == Button)
            press(m_cursor);
        else if (o.kind == Choice)
            choose(m_cursor, (int(o.choiceValues.indexOf(valueOf(option))) + 1) % int(o.choiceValues.size()));
        break;
    default:
        break;
    }
    return true;
}

void Preferences::refresh()
{
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1));
    emit valuesChanged();
}

void Preferences::follow(const QString &id, const QString &value)
{
    const int option = optionOf(id);
    if (option < 0)
        return;
    m_settings->sync();
    // A copy: what `set` does may replace the page's options.
    const Option o = m_options.at(option);
    if (o.set)
        o.set(value);
    const int row = rowOfOption(optionOf(o.id));
    if (row >= 0)
        emit dataChanged(index(row), index(row));
    emit valuesChanged();
    emit changed(o.id, value);
}

void Preferences::refresh(const QString &id)
{
    const int row = rowOfOption(optionOf(id));
    if (row >= 0)
        emit dataChanged(index(row), index(row));
}

int Preferences::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant Preferences::data(const QModelIndex &index, int role) const
{
    const int option = index.isValid() ? optionAt(index.row()) : -1;
    if (option < 0)
        return {};
    const Option &o = m_options.at(option);
    switch (role) {
    case SectionRole:
        return o.section;
    case IdRole:
        return o.id;
    case Qt::DisplayRole:
    case LabelRole:
        return o.label;
    case HintRole:
        return o.getHint ? o.getHint() : o.hint;
    case KindRole:
        return int(o.kind);
    case ChoicesRole:
        return o.choiceLabels;
    case ChoiceRole:
        return int(o.choiceValues.indexOf(valueOf(option)));
    case OnRole:
        return valueOf(option) == QLatin1String("true");
    case TextRole:
        return valueOf(option);
    case PlaceholderRole:
        return o.placeholder;
    case RemovableRole:
        return bool(o.remove);
    case ButtonRole:
        return o.choiceLabels.value(0, o.label);
    default:
        return {};
    }
}

QHash<int, QByteArray> Preferences::roleNames() const
{
    return {
        {SectionRole, "section"}, {IdRole, "optionId"},          {LabelRole, "label"},
        {HintRole, "hint"},       {KindRole, "kind"},            {ChoicesRole, "choices"},
        {ChoiceRole, "choice"},   {OnRole, "on"},                {TextRole, "value"},
        {PlaceholderRole, "placeholder"}, {RemovableRole, "removable"}, {ButtonRole, "button"},
    };
}
