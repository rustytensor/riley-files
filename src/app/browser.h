// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "foldercolumn.h"
#include "foldermodel.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QSettings;

// The window's state and every decision about it: which folder is shown, where the cursor is,
// the way back and forward, the view and the tree. QML shows what this says and passes the keys
// it gets to handleKey(); it decides nothing itself.
class Browser : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The program makes the browser; QML only shows it.")

    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY folderChanged)
    Q_PROPERTY(Riley::FolderModel *model READ model CONSTANT)
    Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY cursorChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY folderChanged)
    Q_PROPERTY(View view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString viewName READ viewName NOTIFY viewChanged)
    Q_PROPERTY(bool tree READ tree WRITE setTree NOTIFY treeChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int pageRows READ pageRows WRITE setPageRows NOTIFY pageRowsChanged)
    Q_PROPERTY(int gridColumns READ gridColumns WRITE setGridColumns NOTIFY gridColumnsChanged)
    Q_PROPERTY(QList<QObject *> columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(int activeColumn READ activeColumn NOTIFY activeColumnChanged)

public:
    enum View { List, Grid, Columns };
    Q_ENUM(View)

    // Reads and keeps its choices in `settingsFile` (an INI file), or in the user's usual
    // settings for the program when it is empty.
    explicit Browser(const QString &settingsFile = QString(), QObject *parent = nullptr);
    ~Browser() override;

    QString folder() const { return m_model->path(); }
    QString folderName() const;
    Riley::FolderModel *model() const { return m_model.get(); }

    // The row the keys act on; -1 when the folder has no rows (yet).
    int cursor() const { return m_cursor; }
    void setCursor(int row);
    Q_INVOKABLE QString cursorPath() const { return m_cursorPath; }

    bool canGoBack() const { return m_historyAt > 0; }
    bool canGoForward() const { return m_historyAt + 1 < m_history.size(); }
    bool canGoUp() const;

    View view() const { return m_view; }
    void setView(View view);
    // "list", "grid" or "columns": also the objectName of the item that shows the view.
    QString viewName() const;

    // Folders open in place in the list view. Off by default; turning it off closes them.
    bool tree() const { return m_tree; }
    void setTree(bool on);

    bool showHidden() const;
    void setShowHidden(bool show);

    // How many rows Page Up and Page Down move; the view sets it from its height.
    int pageRows() const { return m_pageRows; }
    void setPageRows(int rows);

    // How many tiles fit in a row of the grid view; the grid sets it from its width.
    int gridColumns() const { return m_gridColumns; }
    void setGridColumns(int tiles);

    // The columns view: the folder first, then each folder opened from it, left to right
    // (FolderColumn objects). The keys act on the active column.
    QList<QObject *> columns() const;
    int activeColumn() const { return m_activeColumn; }

    // Opens what the program was given to start with: a folder, a file (its folder, with the
    // cursor on it) or a file:// URL. Empty means the home folder.
    Q_INVOKABLE void openLocation(const QString &location);

    // Shows `path` as a new step in the history. When `cursorOn` is a row's path, the cursor
    // goes there once the rows are in; otherwise to the first row.
    Q_INVOKABLE void open(const QString &path, const QString &cursorOn = QString());
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    // The folder that holds this one, with the cursor on the folder just left.
    Q_INVOKABLE void goUp();
    // Opens the folder under the cursor; for a file, does nothing yet.
    Q_INVOKABLE void activate(int row);

    // One key press from the window. Returns true when it was used.
    Q_INVOKABLE bool handleKey(int key, int modifiers, const QString &text);

signals:
    void folderChanged();
    void cursorChanged();
    void historyChanged();
    void viewChanged();
    void treeChanged();
    void showHiddenChanged();
    void pageRowsChanged();
    void gridColumnsChanged();
    void columnsChanged();
    void activeColumnChanged();

private:
    struct Step
    {
        QString path;
        QString cursorPath; // where the cursor was when this folder was left
    };

    void show(const QString &path, const QString &cursorOn);
    void rememberCursor();
    void placeCursor(int row);
    void followRows();

    bool listKey(int key, Qt::KeyboardModifiers mods);
    bool gridKey(int key);
    bool columnsKey(int key);
    bool columnsRight();
    bool columnsLeft();
    void previewUnderCursor();
    void appendColumn(const QString &path);
    void closeColumnsAfter(int column);
    void setActiveColumn(int column);
    void flattenTree();

    // The model and cursor the keys act on: the folder's own, or the active column's.
    Riley::FolderModel *activeModel() const;
    int activeCursor() const;
    void setActiveCursor(int row);
    bool treeRight();
    bool treeLeft();
    bool typeToFind(const QString &text);

    std::unique_ptr<QSettings> m_settings;
    std::unique_ptr<Riley::FolderModel> m_model;

    int m_cursor = -1;
    QString m_cursorPath;     // the path the cursor is on, or is waiting for
    bool m_cursorWaiting = false;
    bool m_rootLoaded = false;

    QList<Step> m_history;
    qsizetype m_historyAt = -1;

    View m_view = List;
    bool m_tree = false;
    int m_pageRows = 10;
    int m_gridColumns = 1;

    QList<FolderColumn *> m_columns; // the first one shows m_model
    int m_activeColumn = 0;

    QString m_findText;
    QElapsedTimer m_findTimer;
};
