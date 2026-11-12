// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "foldermodel.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

// One column of the columns view: a folder's rows and the row the column's cursor is on.
// The first column shows the browser's own folder and cursor; every column after it reads a
// folder of its own and keeps its cursor by path.
class FolderColumn : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the columns.")

    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(Riley::FolderModel *model READ model CONSTANT)
    Q_PROPERTY(int cursor READ cursor NOTIFY cursorChanged)

public:
    // The first column: the browser's model (not owned), whose cursor the browser mirrors.
    explicit FolderColumn(Riley::FolderModel *shared, QObject *parent = nullptr);
    // A column of its own, reading `path`.
    FolderColumn(const QString &path, bool showHidden, QObject *parent = nullptr);
    ~FolderColumn() override;

    QString path() const { return m_model->path(); }
    Riley::FolderModel *model() const { return m_model; }

    // The row the cursor is on; -1 while the cursor has not come into this column.
    int cursor() const { return m_cursor; }
    // Puts the cursor on `row` (clamped to the rows there are).
    void setCursor(int row);
    // For the first column: shows the browser's cursor.
    void mirrorCursor(int row);

    // True once the folder's own rows are in.
    bool isReady() const { return m_ready; }
    void setReady(bool ready) { m_ready = ready; }

signals:
    void pathChanged();
    void cursorChanged();

private:
    void followRows();

    std::unique_ptr<Riley::FolderModel> m_own;
    Riley::FolderModel *m_model = nullptr;
    int m_cursor = -1;
    QString m_cursorPath;
    bool m_ready = false;
};
