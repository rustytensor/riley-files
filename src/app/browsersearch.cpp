// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The search in the window: its field in the header, its results in the folder's place, and
// the keys while it is shown. The search itself (Riley::FileSearch) runs off the UI thread.

#include "browser.h"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

using Riley::FileSearch;

void Browser::openSearch(bool contents)
{
    if (m_searchShown) {
        setSearchContents(contents);
        return;
    }
    m_ops->cancelRename();
    m_ops->dismissMessage();
    m_searchContents = contents;
    m_searchText.clear();
    m_searchCursor = -1;
    m_searchCursorPath.clear();
    m_searchCursorLine = 0;
    m_search->clear();
    m_searchShown = true;
    emit searchModeChanged();
    emit searchTextChanged();
    emit searchCursorChanged();
    emit searchShownChanged();
    emit searchStatusChanged();
}

void Browser::closeSearch()
{
    if (!m_searchShown)
        return;
    m_searchShown = false;
    m_search->clear();
    m_searchText.clear();
    m_searchCursor = -1;
    m_searchCursorPath.clear();
    emit searchTextChanged();
    emit searchCursorChanged();
    emit searchShownChanged();
    emit searchStatusChanged();
}

void Browser::setSearchContents(bool contents)
{
    if (contents == m_searchContents)
        return;
    m_searchContents = contents;
    emit searchModeChanged();
    if (m_searchShown)
        runSearch();
}

void Browser::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    emit searchTextChanged();
    runSearch();
}

void Browser::runSearch()
{
    m_searchCursorPath.clear();
    m_searchCursorLine = 0;
    // Spaces around what is typed are not part of a name to find; inside files they are.
    const QString query = m_searchContents ? m_searchText : m_searchText.trimmed();
    m_search->start(folder(), query, m_searchContents ? FileSearch::Contents : FileSearch::Names, showHidden());
    followSearchRows();
}

void Browser::followSearchRows()
{
    int row = -1;
    const int count = m_search->count();
    if (count > 0) {
        row = 0;
        if (!m_searchCursorPath.isEmpty()) {
            // The cursor stays on its result while rows come in above it.
            for (int i = 0; i < count; ++i) {
                const Riley::SearchHit &hit = m_search->hit(i);
                if (hit.path == m_searchCursorPath && hit.line == m_searchCursorLine) {
                    row = i;
                    break;
                }
            }
        }
    }
    if (row != m_searchCursor) {
        m_searchCursor = row;
        emit searchCursorChanged();
    }
}

void Browser::setSearchCursor(int row)
{
    const int count = m_search->count();
    if (count == 0)
        row = -1;
    else
        row = std::clamp(row, 0, count - 1);
    if (row >= 0) {
        m_searchCursorPath = m_search->hit(row).path;
        m_searchCursorLine = m_search->hit(row).line;
    } else {
        m_searchCursorPath.clear();
        m_searchCursorLine = 0;
    }
    if (row == m_searchCursor)
        return;
    m_searchCursor = row;
    emit searchCursorChanged();
}

void Browser::clickSearchRow(int row)
{
    setSearchCursor(row);
    if (m_prefs->singleClick())
        openSearchRow(row);
}

void Browser::openSearchRow(int row)
{
    if (row < 0 || row >= m_search->count())
        return;
    const QString path = m_search->pathAt(row);
    const QString where = m_search->folderAt(row);
    closeSearch();
    open(where, path);
}

QString Browser::searchStatus() const
{
    if (!m_searchShown)
        return {};
    if (!m_search->message().isEmpty())
        return m_search->message();
    if (m_search->query().isEmpty())
        return {};
    const int count = m_search->count();
    if (count == 0)
        return m_search->isRunning() ? tr("Searching…") : tr("Nothing found.");
    if (m_search->isCapped())
        return m_search->note();
    if (count == 1)
        return m_search->isRunning() ? tr("1 found, still searching…") : tr("1 found.");
    const QString number = QLocale().toString(count);
    return m_search->isRunning() ? tr("%1 found, still searching…").arg(number) : tr("%1 found.").arg(number);
}

QString Browser::searchHint() const
{
    if (folderName().isEmpty())
        return m_searchContents ? tr("Find inside the files in this folder") : tr("Find names in this folder");
    return m_searchContents ? tr("Find inside the files in “%1”").arg(folderName())
                            : tr("Find names in “%1”").arg(folderName());
}

bool Browser::somethingInFront() const
{
    return m_ops->asking() || m_actions->menuOpen() || !m_question.isEmpty() || m_chooser->shown()
           || m_shortcutsShown || m_prefs->shown() || m_preview->isOpen() || m_connectShown;
}

bool Browser::searchFieldKey(int key, int modifiers, const QString &text)
{
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;
    // A menu, a question or a panel in front takes the keys, as anywhere else.
    if (somethingInFront())
        return handleKey(key, int(mods), text);
    if (searchKey(key, mods))
        return true;
    // The window's commands; the editing keys (Ctrl+A, Ctrl+C, ...) stay the field's.
    const bool command = (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
                         || (key >= Qt::Key_F1 && key <= Qt::Key_F35);
    if (!command)
        return false;
    if (key == Qt::Key_F10 && mods == Qt::NoModifier) {
        m_actions->openBarMenu(0);
        return true;
    }
    const QString id = m_actions->idForKey(key, mods);
    return !id.isEmpty() && searchAllows(id) && m_actions->trigger(id);
}

bool Browser::searchKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods != Qt::NoModifier)
        return false;
    switch (key) {
    case Qt::Key_Escape:
        closeSearch();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        openSearchRow(m_searchCursor);
        return true;
    case Qt::Key_Down:
        setSearchCursor(m_searchCursor + 1);
        return true;
    case Qt::Key_Up:
        setSearchCursor(std::max(0, m_searchCursor - 1));
        return true;
    case Qt::Key_PageDown:
        setSearchCursor(m_searchCursor + m_pageRows);
        return true;
    case Qt::Key_PageUp:
        setSearchCursor(std::max(0, m_searchCursor - m_pageRows));
        return true;
    default:
        return false;
    }
}

bool Browser::searchAllows(const QString &actionId) const
{
    // What makes sense with the folder hidden behind the results.
    static const QStringList allowed = {
        QStringLiteral("find"),       QStringLiteral("find-contents"), QStringLiteral("hidden"),
        QStringLiteral("sidebar"),    QStringLiteral("details"),       QStringLiteral("preferences"),
        QStringLiteral("shortcuts"),  QStringLiteral("about"),         QStringLiteral("new-window"),
        QStringLiteral("close-window"), QStringLiteral("quit"),
    };
    return allowed.contains(actionId);
}

QString Browser::markMatches(const QString &text, const QVariantList &matches, const QString &color)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$"));
    const bool coloured = hex.match(color).hasMatch();

    QList<std::pair<int, int>> spans;
    for (qsizetype i = 0; i + 1 < matches.size(); i += 2) {
        const int start = matches.at(i).toInt();
        const int length = matches.at(i + 1).toInt();
        if (start >= 0 && length > 0 && start < text.size())
            spans.append({start, std::min<int>(length, int(text.size()) - start)});
    }
    std::sort(spans.begin(), spans.end());

    QString out;
    int at = 0;
    for (const auto &[start, length] : std::as_const(spans)) {
        if (start < at)
            continue; // overlaps the one before
        out += text.mid(at, start - at).toHtmlEscaped();
        out += coloured ? QStringLiteral("<b><font color=\"%1\">").arg(color) : QStringLiteral("<b>");
        out += text.mid(start, length).toHtmlEscaped();
        out += coloured ? QStringLiteral("</font></b>") : QStringLiteral("</b>");
        at = start + length;
    }
    out += text.mid(at).toHtmlEscaped();
    return out;
}
