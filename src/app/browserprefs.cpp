// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The browser's options: what the Preferences panel offers, where each is kept and what it
// changes at once. Also the clicks on rows, the question before moving to the trash, and the
// names the automatic pane widths measure.

#include "browser.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>

using Riley::FolderModel;
using Option = Preferences::Option;
using namespace Qt::StringLiterals;

namespace {

const QStringList onOff{u"false"_s, u"true"_s};

QStringList namesOf(const QAbstractItemModel &model, int role)
{
    QStringList names;
    const int rows = model.rowCount();
    names.reserve(rows);
    for (int row = 0; row < rows; ++row)
        names.append(model.data(model.index(row, 0), role).toString());
    return names;
}

} // namespace

void Browser::addPreferences()
{
    Preferences &p = *m_prefs;
    const QString general = u"General"_s;
    const QString appearance = u"Appearance"_s;
    const QString behaviour = u"Behaviour"_s;

    p.add({general, u"startFolder"_s, u"Start folder"_s, u"Where a new window opens; empty for the home folder"_s,
           Preferences::Text, {}, {}, u"general/startFolder"_s, {}, {}, {}});
    p.add({general, u"defaultView"_s, u"Default view"_s, u"The view a new window starts in"_s, Preferences::Choice,
           {u"Last used"_s, u"List"_s, u"Grid"_s, u"Columns"_s}, {QString(), u"list"_s, u"grid"_s, u"columns"_s},
           u"View/default"_s, QString(), {}, {}});
    p.add({general, u"hidden"_s, u"Show hidden files"_s, u"Ctrl+H"_s, Preferences::Toggle, {}, onOff, {}, {},
           [this] { return showHidden() ? u"true"_s : u"false"_s; },
           [this](const QString &v) { setShowHidden(v == QLatin1String("true")); }});
    p.add({general, u"singleClick"_s, u"Open with a single click"_s, u"Otherwise a double-click opens"_s,
           Preferences::Toggle, {}, onOff, u"general/singleClick"_s, u"false"_s, {}, {}});

    p.add({appearance, u"paneWidths"_s, u"Pane widths"_s,
           u"Fixed, fitted to the names, or scaled with the window; a dragged divider keeps its width"_s,
           Preferences::Choice, {u"Fixed"_s, u"Automatic"_s, u"Proportional"_s},
           {u"fixed"_s, u"automatic"_s, u"proportional"_s}, {}, {}, [this] { return m_paneWidths; },
           [this](const QString &v) { setPaneWidths(v); }});
    p.add({appearance, u"thumbnailSize"_s, u"Thumbnail size"_s, u"Icons and pictures in the grid"_s,
           Preferences::Choice, {u"Small"_s, u"Medium"_s, u"Large"_s}, {u"44"_s, u"64"_s, u"96"_s},
           u"appearance/thumbnailSize"_s, u"44"_s, {}, {}});
    p.add({appearance, u"treeLines"_s, u"Tree lines"_s, u"Lines that join folders opened in place"_s,
           Preferences::Toggle, {}, onOff, u"appearance/treeLines"_s, u"true"_s, {}, {}});

    p.add({behaviour, u"askTrash"_s, u"Ask before moving to the trash"_s, u"Moving to the trash can be undone"_s,
           Preferences::Toggle, {}, onOff, u"behaviour/askBeforeTrash"_s, u"false"_s, {}, {}});
    p.add({behaviour, u"foldersFirst"_s, u"Folders first in every sort"_s, {}, Preferences::Toggle, {}, onOff,
           u"behaviour/foldersFirst"_s, u"true"_s, {}, {}});
}

void Browser::applyPreference(const QString &id, const QString &value)
{
    if (id == QLatin1String("defaultView")) {
        // Shown at once too, so the choice is seen working.
        if (value == QLatin1String("list"))
            setView(List);
        else if (value == QLatin1String("grid"))
            setView(Grid);
        else if (value == QLatin1String("columns"))
            setView(Columns);
    } else if (id == QLatin1String("foldersFirst")) {
        const bool on = value == QLatin1String("true");
        m_model->setFoldersFirst(on);
        for (qsizetype i = 1; i < m_columns.size(); ++i)
            m_columns.at(i)->model()->setFoldersFirst(on);
    }
    // The other options are read where they are used: the clicks, the grid, the tree, the trash.
}

void Browser::clickRow(int row)
{
    if (row < 0 || row >= m_model->count())
        return;
    setCursor(row);
    if (m_prefs->singleClick())
        activate(row);
}

void Browser::doubleClickRow(int row)
{
    // With one click opening, the first click of a double-click already did.
    if (m_prefs->singleClick() || row < 0 || row >= m_model->count())
        return;
    activate(row);
}

void Browser::trashTargets()
{
    const QStringList paths = targetPaths();
    if (paths.isEmpty())
        return;
    if (!m_prefs->isOn(QStringLiteral("askTrash"))) {
        m_ops->trash(paths);
        return;
    }
    const QString text = paths.size() == 1
                             ? QStringLiteral("Move “%1” to the trash?").arg(QFileInfo(paths.first()).fileName())
                             : QStringLiteral("Move %1 items to the trash?").arg(paths.size());
    ask(text, [this, paths] { m_ops->trash(paths); });
}

void Browser::ask(const QString &text, std::function<void()> yes)
{
    m_onYes = std::move(yes);
    m_question = text;
    emit questionChanged();
}

void Browser::answer(bool yes)
{
    if (m_question.isEmpty())
        return;
    const std::function<void()> run = std::exchange(m_onYes, {});
    m_question.clear();
    emit questionChanged();
    if (yes && run)
        run();
}

bool Browser::questionKey(int key, Qt::KeyboardModifiers mods)
{
    if (mods == Qt::NoModifier && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Y))
        answer(true);
    else if (mods == Qt::NoModifier && (key == Qt::Key_Escape || key == Qt::Key_N))
        answer(false);
    // The question is in front; the other keys wait for its answer.
    return true;
}

void Browser::measureSidebar()
{
    QStringList names = namesOf(*m_places, Riley::PlacesModel::NameRole);
    names += namesOf(*m_pinned, Riley::PinnedModel::NameRole);
    names += namesOf(*m_drives, Riley::DrivesModel::NameRole);
    names += namesOf(*m_network, Riley::NetworkModel::NameRole);
    names.append(QStringLiteral("Recent"));
    m_panes->measure(PaneWidths::Sidebar, names);
}

void Browser::measureColumns()
{
    // Only the automatic way needs them, and only the columns view shows them.
    if (m_paneWidths != QLatin1String("automatic") || m_view != Columns)
        return;
    QStringList names;
    for (FolderColumn *column : std::as_const(m_columns)) {
        const FolderModel *model = column->model();
        const int rows = std::min(model->count(), 5000);
        for (int row = 0; row < rows; ++row)
            names.append(model->nameAt(row));
    }
    m_panes->measure(PaneWidths::Column, names);
}
