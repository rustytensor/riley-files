// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "preferences.h"

#include <QSettings>

#include <algorithm>

Preferences::Preferences(QSettings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
{
}

Preferences::~Preferences() = default;

void Preferences::add(const Option &option)
{
    const int row = int(m_options.size());
    beginInsertRows({}, row, row);
    m_options.append(option);
    endInsertRows();
}

int Preferences::rowOf(const QString &id) const
{
    for (qsizetype i = 0; i < m_options.size(); ++i) {
        if (m_options.at(i).id == id)
            return int(i);
    }
    return -1;
}

QString Preferences::valueAt(int row) const
{
    if (row < 0 || row >= m_options.size())
        return {};
    const Option &o = m_options.at(row);
    if (o.get)
        return o.get();
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
    return valueAt(rowOf(id));
}

void Preferences::setValue(const QString &id, const QString &value)
{
    store(rowOf(id), value);
}

void Preferences::store(int row, const QString &value)
{
    if (row < 0 || row >= m_options.size() || valueAt(row) == value)
        return;
    const Option &o = m_options.at(row);
    if (o.set) {
        o.set(value);
    } else {
        m_settings->setValue(o.key, value);
        m_settings->sync();
    }
    emit dataChanged(index(row), index(row));
    emit valuesChanged();
    emit changed(o.id, valueAt(row));
}

int Preferences::thumbnailSize() const
{
    const int size = value(QStringLiteral("thumbnailSize")).toInt();
    return size > 0 ? size : 44;
}

QStringList Preferences::sections() const
{
    QStringList list;
    for (const Option &o : m_options) {
        if (!list.contains(o.section))
            list.append(o.section);
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
    if (m_options.isEmpty())
        return;
    row = std::clamp(row, 0, int(m_options.size()) - 1);
    if (row == m_cursor)
        return;
    m_cursor = row;
    emit cursorChanged();
}

void Preferences::toggle(int row)
{
    if (row < 0 || row >= m_options.size() || m_options.at(row).kind != Toggle)
        return;
    setCursor(row);
    store(row, isOn(m_options.at(row).id) ? QStringLiteral("false") : QStringLiteral("true"));
}

void Preferences::choose(int row, int choice)
{
    if (row < 0 || row >= m_options.size() || m_options.at(row).kind != Choice)
        return;
    setCursor(row);
    const QStringList &values = m_options.at(row).choiceValues;
    if (choice >= 0 && choice < values.size())
        store(row, values.at(choice));
}

void Preferences::step(int row, int by)
{
    const Option &o = m_options.at(row);
    const int at = int(o.choiceValues.indexOf(valueAt(row)));
    const int count = int(o.choiceValues.size());
    choose(row, std::clamp(at + by, 0, count - 1));
}

void Preferences::editText(int row)
{
    if (row < 0 || row >= m_options.size() || m_options.at(row).kind != Text)
        return;
    setCursor(row);
    if (m_editing == row)
        return;
    m_editing = row;
    emit editingChanged();
}

void Preferences::setText(int row, const QString &text)
{
    if (row >= 0 && row < m_options.size() && m_options.at(row).kind == Text)
        store(row, text.trimmed());
    cancelEditing();
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
    if (mods != Qt::NoModifier || m_options.isEmpty())
        return true;
    const Option &o = m_options.at(m_cursor);
    switch (key) {
    case Qt::Key_Up:
        setCursor(m_cursor - 1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Tab:
        setCursor(m_cursor + 1);
        break;
    case Qt::Key_Home:
        setCursor(0);
        break;
    case Qt::Key_End:
        setCursor(int(m_options.size()) - 1);
        break;
    case Qt::Key_Left:
        if (o.kind == Choice)
            step(m_cursor, -1);
        break;
    case Qt::Key_Right:
        if (o.kind == Choice)
            step(m_cursor, 1);
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (o.kind == Toggle)
            toggle(m_cursor);
        else if (o.kind == Text)
            editText(m_cursor);
        else
            choose(m_cursor, (int(o.choiceValues.indexOf(valueAt(m_cursor))) + 1) % int(o.choiceValues.size()));
        break;
    default:
        break;
    }
    return true;
}

void Preferences::refresh()
{
    if (m_options.isEmpty())
        return;
    emit dataChanged(index(0), index(int(m_options.size()) - 1));
    emit valuesChanged();
}

int Preferences::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_options.size());
}

QVariant Preferences::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_options.size())
        return {};
    const Option &o = m_options.at(index.row());
    switch (role) {
    case SectionRole:
        return o.section;
    case IdRole:
        return o.id;
    case Qt::DisplayRole:
    case LabelRole:
        return o.label;
    case HintRole:
        return o.hint;
    case KindRole:
        return int(o.kind);
    case ChoicesRole:
        return o.choiceLabels;
    case ChoiceRole:
        return int(o.choiceValues.indexOf(valueAt(index.row())));
    case OnRole:
        return valueAt(index.row()) == QLatin1String("true");
    case TextRole:
        return valueAt(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> Preferences::roleNames() const
{
    return {
        {SectionRole, "section"}, {IdRole, "optionId"}, {LabelRole, "label"},  {HintRole, "hint"},
        {KindRole, "kind"},       {ChoicesRole, "choices"}, {ChoiceRole, "choice"}, {OnRole, "on"},
        {TextRole, "value"},
    };
}
