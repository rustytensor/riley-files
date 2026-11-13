// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "direntry.h"
#include "entryorder.h"
#include "rileycore_export.h"
#include "snapshots.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QMimeType>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <vector>

namespace Riley {

struct ListingResult;

// The contents of one folder as rows for a view, with folders that can be opened in place as a
// tree. Folders are read on a worker thread, so setting the path returns at once; the rows
// arrive a moment later (loaded() says when). The folders shown are watched, and changes on
// disk come in as row inserts and removals, so a view keeps its place.
//
// The tree: every row has a depth. Opening a folder row (expand) puts its contents right below
// it, one level deeper. Folders opened inside a folder that is then closed stay remembered and
// open again with it. Going to another folder forgets them all.
//
// Selection is kept by path, so it survives reloads and new sort orders.
class RILEYCORE_EXPORT FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool readOnly READ isReadOnly NOTIFY pathChanged)
    Q_PROPERTY(Sort sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasOpenFolders READ hasOpenFolders NOTIFY countChanged)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        HiddenRole,
        SizeRole,         // bytes, qint64
        SizeTextRole,     // "4.2 KB"; empty for folders
        ModifiedRole,     // QDateTime
        ModifiedTextRole, // "Today 14:03", or the date and time
        ExtensionRole,    // "txt"; empty for folders and names without one
        KindTextRole,     // "Folder", "plain text document", ...
        CategoryRole,     // folder, image, audio, video, text, code, document, archive, file
        IconNameRole,     // a freedesktop icon name for the type
        DepthRole,        // 0 for the folder's own contents, 1 inside an opened folder, ...
        OpenRole,         // a folder row whose contents are shown below it
        LinesRole,        // tree guide: one character per level, e.g. "│└"; empty at depth 0
        SelectedRole,
    };
    Q_ENUM(Role)

    // Mirrors Riley::SortKey, for QML.
    enum Sort {
        SortByName = int(SortKey::Name),
        SortBySize = int(SortKey::Size),
        SortByModified = int(SortKey::Modified),
        SortByKind = int(SortKey::Kind),
    };
    Q_ENUM(Sort)

    explicit FolderModel(QObject *parent = nullptr);
    ~FolderModel() override;

    // What a file holds, by its MIME type, as the "category" role gives it: image, audio,
    // video, document, archive, code, text, or file for anything else.
    static QString categoryOf(const QMimeType &mime);
    // The same for a file name (by its extension), or "folder".
    static QString categoryOfName(const QString &name, bool isDir);

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_root.entry.path; }
    // Shows another folder: the rows go at once and the new ones arrive when read. Open
    // folders and the selection are forgotten. The same path again does nothing (see reload).
    void setPath(const QString &path);
    // True when the folder is in a snapshot: it is shown, but nothing in it may be changed.
    bool isReadOnly() const { return Snapshots::isInSnapshot(path()); }

    Sort sortKey() const { return Sort(int(m_sortKey)); }
    void setSortKey(Sort key);

    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);

    // Folders before files in every sort (the default); off, they are sorted with the files.
    bool foldersFirst() const { return m_foldersFirst; }
    void setFoldersFirst(bool on);

    // Why the folder could not be read; empty when it was.
    QString error() const { return m_error; }
    // True while any folder shown is being read.
    bool isLoading() const { return !m_pending.isEmpty(); }
    int count() const { return int(m_rows.size()); }
    bool hasOpenFolders() const;

    // Reads the folder and every open folder again.
    Q_INVOKABLE void reload();

    // Row access, -1 or empty when out of range.
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE bool isOpenAt(int row) const;
    Q_INVOKABLE int depthAt(int row) const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int rowOfPath(const QString &path) const;
    // The first row from `from` on (wrapping round) whose name starts with `prefix`, ignoring
    // case; -1 when none does. For type-to-find.
    Q_INVOKABLE int findByPrefix(const QString &prefix, int from = 0) const;

    // The tree.
    // Opens a folder row in place; false when the row is not a closed folder.
    Q_INVOKABLE bool expand(int row);
    // Closes an open folder row; false when it is not open.
    Q_INVOKABLE bool collapse(int row);
    Q_INVOKABLE bool toggleOpen(int row);
    // Closes every folder and forgets which were open.
    Q_INVOKABLE void collapseAll();
    // The row of the folder this row is in, or -1 for the folder's own contents.
    Q_INVOKABLE int parentRow(int row) const;
    // True once an open folder's contents have been read (it may still have none).
    Q_INVOKABLE bool isLoadedAt(int row) const;

    // Selection, kept by path.
    QStringList selectedPaths() const;
    int selectedCount() const { return int(m_selected.size()); }
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE void selectOnly(int row);
    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void toggleSelected(int row);
    // Adds the rows from..to (either order) to the selection.
    Q_INVOKABLE void selectRange(int from, int to);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    // Every thread a folder was read on. A test can check that the main thread is never among them.
    QSet<Qt::HANDLE> listingThreads() const;

signals:
    void pathChanged();
    void sortKeyChanged();
    void showHiddenChanged();
    void errorChanged();
    void loadingChanged();
    void countChanged();
    void selectionChanged();
    // The folder's own contents have been read (after setPath, reload or a change on disk).
    void loaded();
    // A folder shown (the folder itself or an open one) has new rows from disk.
    void folderLoaded(const QString &path);

private:
    struct Node
    {
        DirEntry entry;
        Node *parent = nullptr;
        int depth = -1;       // -1 for the folder itself
        bool open = false;    // contents shown below it
        bool loaded = false;  // contents read at least once since opened
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeAt(int row) const;
    int rowOf(const Node *node) const;
    int shownBelow(const Node *node) const;
    Node *folderNode(const QString &path);

    void requestListing(Node *folder);
    void applyListing(quint64 generation, quint64 request, const QString &folderPath,
                      SortKey key, ListingResult result);
    void mergeChildren(Node *folder, const QList<DirEntry> &entries);
    void openRememberedChildren(Node *folder);
    void openNode(Node *node, int row);
    void closeNode(Node *node, int row);
    void insertChildren(Node *folder, std::size_t at, int row, const QList<DirEntry> &entries);
    void removeChild(Node *folder, std::size_t at, int row);
    void forgetSubtree(Node *node, bool gone);
    void sortChildren(Node *folder, const EntryOrder &order);
    void appendRows(const Node *folder, QList<Node *> &rows) const;
    void relayout();
    void refreshLines();
    void setError(const QString &error);
    void watch(const QString &path);
    void unwatch(const QString &path);
    void folderChangedOnDisk(const QString &path);
    void refreshDirtyFolders();
    // Sends the change signals for count, loading and selection that are due.
    void settle();

    QString linesFor(const Node *node) const;

    Node m_root;
    QList<Node *> m_rows;              // the rows shown, in order
    QHash<QString, Node *> m_byPath;   // every node below the root
    QSet<QString> m_openPaths;         // folders to show open, also those inside closed ones
    QSet<QString> m_selected;
    QHash<QString, quint64> m_pending; // folder path -> latest request
    QSet<QString> m_dirty;             // folders changed on disk, waiting for the debounce

    SortKey m_sortKey = SortKey::Name;
    bool m_foldersFirst = true;
    EntryOrder order() const;
    bool m_showHidden = false;
    QString m_error;

    quint64 m_generation = 0;
    quint64 m_nextRequest = 0;
    // The generation still wanted, read by the workers to give up early.
    std::shared_ptr<std::atomic<quint64>> m_wanted;

    mutable QMutex m_threadsLock;
    QSet<Qt::HANDLE> m_listingThreads;

    QThreadPool m_pool;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;

    int m_shownCount = 0;
    bool m_shownLoading = false;
    bool m_selectionDirty = false;

    struct TypeInfo
    {
        QString kindText;
        QString category;
        QString iconName;
    };
    const TypeInfo &typeInfo(const DirEntry &entry) const;
    mutable QHash<QString, TypeInfo> m_typeCache; // by lower-case extension
};

} // namespace Riley
