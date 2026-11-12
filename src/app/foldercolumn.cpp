// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "foldercolumn.h"

#include <algorithm>

using Riley::FolderModel;

FolderColumn::FolderColumn(FolderModel *shared, QObject *parent)
    : QObject(parent)
    , m_model(shared)
{
    connect(m_model, &FolderModel::pathChanged, this, &FolderColumn::pathChanged);
}

FolderColumn::FolderColumn(const QString &path, bool showHidden, QObject *parent)
    : QObject(parent)
    , m_own(std::make_unique<FolderModel>())
    , m_model(m_own.get())
{
    connect(m_model, &FolderModel::pathChanged, this, &FolderColumn::pathChanged);
    connect(m_model, &QAbstractItemModel::modelReset, this, &FolderColumn::followRows);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &FolderColumn::followRows);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &FolderColumn::followRows);
    connect(m_model, &QAbstractItemModel::rowsMoved, this, &FolderColumn::followRows);
    connect(m_model, &QAbstractItemModel::layoutChanged, this, &FolderColumn::followRows);
    connect(m_model, &FolderModel::loaded, this, [this] {
        m_ready = true;
        followRows();
    });
    m_model->setShowHidden(showHidden);
    m_model->setPath(path);
}

FolderColumn::~FolderColumn() = default;

void FolderColumn::setCursor(int row)
{
    const int n = m_model->count();
    row = n == 0 ? -1 : std::clamp(row, 0, n - 1);
    const QString path = m_model->pathAt(row);
    if (row == m_cursor && path == m_cursorPath)
        return;
    m_cursor = row;
    m_cursorPath = path;
    emit cursorChanged();
}

void FolderColumn::mirrorCursor(int row)
{
    if (row == m_cursor)
        return;
    m_cursor = row;
    emit cursorChanged();
}

// The cursor stays on its path through changes on disk; when that row goes, it stays about
// where it was. A column the cursor has not been in keeps no cursor.
void FolderColumn::followRows()
{
    if (m_cursor < 0 && m_cursorPath.isEmpty())
        return;
    const int row = m_model->rowOfPath(m_cursorPath);
    setCursor(row >= 0 ? row : std::max(m_cursor, 0));
}
