// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The location typed in the path bar: starting and leaving the field, completing folder names
// with Tab and the suggestions under it, and opening what was typed (or saying why it cannot be).

#include "browser.h"

#include "folderlisting.h"

#include <QDir>
#include <QFileInfo>

namespace {

bool isUrl(const QString &text)
{
    return text.contains(QLatin1String("://"));
}

// The longest start the names share, ignoring case, spelled as in the first name.
QString sharedStart(const QStringList &names)
{
    if (names.isEmpty())
        return {};
    qsizetype length = names.first().size();
    for (const QString &name : names) {
        qsizetype same = 0;
        while (same < length && same < name.size()
               && names.first().at(same).toCaseFolded() == name.at(same).toCaseFolded())
            ++same;
        length = same;
    }
    return names.first().left(length);
}

} // namespace

void Browser::editLocation()
{
    if (m_locationEditing)
        return;
    putLocationText(folder());
    setLocationError({});
    setLocationSuggestions({}, {});
    m_locationEditing = true;
    emit locationEditingChanged();
}

void Browser::cancelLocation()
{
    if (!m_locationEditing)
        return;
    setLocationError({});
    setLocationSuggestions({}, {});
    m_locationEditing = false;
    emit locationEditingChanged();
}

QString Browser::expandTypedLocation(const QString &text) const
{
    QString path = text.trimmed();
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    if (!path.isEmpty() && !isUrl(path) && QDir::isRelativePath(path))
        return QDir(folder()).filePath(path);
    return path;
}

void Browser::openTypedLocation(const QString &text)
{
    const QString typed = text.trimmed();
    if (typed.isEmpty()) {
        cancelLocation();
        return;
    }
    const QString path = expandTypedLocation(typed);
    if (!isUrl(path) && !QFileInfo::exists(path)) {
        // The field stays open, with the reason under it, so the path can be put right.
        setLocationSuggestions({}, {});
        setLocationError(tr("There is no folder “%1”.").arg(typed));
        return;
    }
    cancelLocation();
    openLocation(path);
}

void Browser::setLocationText(const QString &text)
{
    if (text == m_locationText)
        return;
    m_locationText = text;
    emit locationTextChanged();
    setLocationError({});
    // Shown suggestions follow the typing; they go when nothing matches.
    if (!m_locationSuggestions.isEmpty()) {
        QString in;
        const QStringList names = locationMatches(expandTypedLocation(text), &in);
        setLocationSuggestions(in, names);
    }
}

void Browser::putLocationText(const QString &text)
{
    if (text == m_locationText)
        return;
    m_locationText = text;
    emit locationTextChanged();
}

void Browser::setLocationError(const QString &error)
{
    if (error == m_locationError)
        return;
    m_locationError = error;
    emit locationErrorChanged();
}

void Browser::setLocationSuggestions(const QString &in, const QStringList &names)
{
    if (names == m_locationSuggestions && in == m_suggestionsIn && m_locationSuggestion == -1)
        return;
    m_suggestionsIn = in;
    m_locationSuggestions = names;
    m_locationSuggestion = -1;
    emit locationSuggestionsChanged();
}

QStringList Browser::locationMatches(const QString &expanded, QString *in) const
{
    const qsizetype slash = expanded.lastIndexOf(QLatin1Char('/'));
    if (slash < 0 || isUrl(expanded))
        return {};
    const QString folderPart = expanded.left(slash + 1);
    const QString start = expanded.mid(slash + 1);
    *in = folderPart;

    Riley::ListingOptions options;
    // Hidden folders are offered when they are shown, or when the typing asks for one.
    options.showHidden = showHidden() || start.startsWith(QLatin1Char('.'));
    const Riley::ListingResult listing = Riley::listFolder(folderPart, options);
    QStringList names;
    for (const Riley::DirEntry &entry : listing.entries) {
        if (entry.isDir && entry.name.startsWith(start, Qt::CaseInsensitive))
            names.append(entry.name);
    }
    return names;
}

void Browser::completeLocation()
{
    if (isUrl(m_locationText))
        return;
    const QString expanded = expandTypedLocation(m_locationText);
    QString in;
    const QStringList names = locationMatches(expanded, &in);
    if (names.isEmpty()) {
        // Nothing to complete with; "~" still becomes the home folder's path.
        putLocationText(expanded);
        setLocationSuggestions({}, {});
        return;
    }
    if (names.size() == 1) {
        putLocationText(in + names.first() + QLatin1Char('/'));
        setLocationSuggestions({}, {});
        return;
    }
    // Several: as far as they agree, and the list of them under the field.
    const QString shared = sharedStart(names);
    const QString typedStart = expanded.mid(in.size());
    putLocationText(in + (shared.size() > typedStart.size() ? shared : typedStart));
    setLocationSuggestions(in, names);
}

void Browser::chooseLocationSuggestion(int row)
{
    if (row < 0 || row >= m_locationSuggestions.size())
        return;
    const QString path = m_suggestionsIn + m_locationSuggestions.at(row) + QLatin1Char('/');
    setLocationSuggestions({}, {});
    putLocationText(path);
    setLocationError({});
}

bool Browser::locationFieldKey(int key, int modifiers)
{
    Q_UNUSED(modifiers);
    if (!m_locationEditing)
        return false;
    const auto count = m_locationSuggestions.size();
    switch (key) {
    case Qt::Key_Tab:
        completeLocation();
        return true;
    case Qt::Key_Backtab:
        return true;
    case Qt::Key_Down:
        if (count == 0)
            return true;
        m_locationSuggestion = (m_locationSuggestion + 1) % int(count);
        emit locationSuggestionsChanged();
        return true;
    case Qt::Key_Up:
        if (count == 0)
            return true;
        m_locationSuggestion = m_locationSuggestion <= 0 ? int(count) - 1 : m_locationSuggestion - 1;
        emit locationSuggestionsChanged();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_locationSuggestion >= 0)
            chooseLocationSuggestion(m_locationSuggestion);
        else
            openTypedLocation(m_locationText);
        return true;
    case Qt::Key_Escape:
        if (count > 0)
            setLocationSuggestions({}, {});
        else
            cancelLocation();
        return true;
    default:
        return false;
    }
}
