// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "jobqueue.h"
#include "undohistory.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

class QMimeData;

// The window's file operations and every decision about them: what the keys copy, cut, paste,
// rename, make, trash, undo and redo; the system clipboard, shared with other programs; the
// question asked when a name is taken; the progress strip; the messages. The work itself runs
// in the core's job queue, off the UI thread. QML only shows what this says.
class FileOps : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the file operations; QML only shows them.")

    Q_PROPERTY(bool showProgress READ showProgress NOTIFY progressChanged)
    Q_PROPERTY(QString progressTitle READ progressTitle NOTIFY progressChanged)
    Q_PROPERTY(QString progressDetail READ progressDetail NOTIFY progressChanged)
    Q_PROPERTY(double progressFraction READ progressFraction NOTIFY progressChanged)
    Q_PROPERTY(bool stopping READ stopping NOTIFY progressChanged)

    Q_PROPERTY(bool asking READ asking NOTIFY clashChanged)
    Q_PROPERTY(QString clashTitle READ clashTitle NOTIFY clashChanged)
    Q_PROPERTY(QString clashOld READ clashOld NOTIFY clashChanged)
    Q_PROPERTY(QString clashNew READ clashNew NOTIFY clashChanged)
    Q_PROPERTY(int clashButton READ clashButton WRITE setClashButton NOTIFY clashChanged)
    Q_PROPERTY(bool clashForAll READ clashForAll WRITE setClashForAll NOTIFY clashChanged)
    Q_PROPERTY(bool clashOffersForAll READ clashOffersForAll NOTIFY clashChanged)
    Q_PROPERTY(bool clashCanMerge READ clashCanMerge NOTIFY clashChanged)

    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QString editingPath READ editingPath NOTIFY editingChanged)
    Q_PROPERTY(QString editingName READ editingName NOTIFY editingChanged)

    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)

public:
    // The clash dialog's buttons, left to right. Merge is there only when a folder meets a folder.
    enum ClashButton { SkipButton, KeepBothButton, ReplaceButton, MergeButton };
    Q_ENUM(ClashButton)

    // The two clipboard formats other file managers read and write.
    static constexpr const char *uriListType = "text/uri-list";
    static constexpr const char *gnomeFilesType = "x-special/gnome-copied-files";

    explicit FileOps(QObject *parent = nullptr);
    ~FileOps() override;

    Riley::JobQueue &queue() { return m_queue; }
    Riley::UndoHistory &history() { return m_history; }

    // Puts paths on the system clipboard, to be copied or (cut) moved where they are pasted.
    void copy(const QStringList &paths, bool cut);
    // Copies or moves what the clipboard holds (from this program or another) into folder.
    // False when it holds no local files.
    bool paste(const QString &folder);
    void trash(const QStringList &paths);
    // Puts things from a trash (paths in a trash's files/) back where they came from; a taken
    // name is asked about, as a paste's is.
    void restore(const QStringList &trashedPaths);
    // Deletes for good, after the person was asked; things in a trash lose their info file too.
    void deleteForGood(const QStringList &paths);
    void newFolder(const QString &folder);
    // Puts an earlier version (a copy in a snapshot) back in the live file's place, the live one
    // going to the trash first; or, asCopy, copies it beside the live file under a dated name.
    void restoreVersion(const QString &copyPath, const QString &livePath, const QDateTime &date, bool asCopy);
    void undo();
    void redo();

    // The inline name editor. startRename puts it on path; commitRename gives the new name
    // (a taken or impossible name keeps the editor open, with a message saying why).
    void startRename(const QString &path);
    Q_INVOKABLE void commitRename(const QString &name);
    Q_INVOKABLE void cancelRename();
    bool editing() const { return !m_editingPath.isEmpty(); }
    QString editingPath() const { return m_editingPath; }
    QString editingName() const;

    // Progress of the job running now.
    bool showProgress() const { return m_showProgress; }
    QString progressTitle() const;
    QString progressDetail() const;
    double progressFraction() const;
    bool stopping() const;
    // Stops the job running now and those waiting.
    Q_INVOKABLE void stop();
    bool busy() const { return m_queue.activeCount() > 0; }

    // A name clash the running job waits on.
    bool asking() const { return !m_asking.isNull(); }
    QString clashTitle() const;
    QString clashOld() const;
    QString clashNew() const;
    int clashButton() const { return m_clashButton; }
    void setClashButton(int button);
    bool clashForAll() const { return m_clashForAll; }
    void setClashForAll(bool on);
    bool clashOffersForAll() const;
    // A folder meets a folder: Merge is offered (and chosen at first).
    bool clashCanMerge() const;
    // Answers with this button (a ClashButton), or with the one chosen when -1.
    Q_INVOKABLE void answerClash(int button = -1);
    // A key while the clash is asked. Returns true when used (every key is, while asking).
    bool clashKey(int key, Qt::KeyboardModifiers mods);

    QString message() const { return m_message; }
    void say(const QString &message);
    Q_INVOKABLE void dismissMessage();

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    QString undoText() const { return m_history.undoText(); }
    QString redoText() const { return m_history.redoText(); }

    // The clipboard contents other programs understand, and what they mean.
    static QMimeData *mimeFor(const QStringList &paths, bool cut);
    static QStringList pathsIn(const QMimeData *data, bool *cut);

signals:
    // The cursor should go to this path once it is shown (something made, pasted or put back).
    // With edit set, its name is edited once the cursor is on it (a new folder).
    void land(const QString &path, bool edit);
    // A finished job (a move, a rename, an undo of one) put `from` at `to`.
    void moved(const QString &from, const QString &to);
    void progressChanged();
    void clashChanged();
    void editingChanged();
    void messageChanged();
    void historyChanged();

private:
    void run(Riley::FileJob *job);
    void watch(Riley::FileJob *job);
    void landAfter(Riley::FileJob *job);
    void jobEnded(Riley::FileJob *job);
    void clashAsked(Riley::FileJob *job);
    void updateProgress();
    void reportFailures(Riley::FileJob *job);
    Riley::FileJob *runningJob() const;

    Riley::JobQueue m_queue;
    Riley::UndoHistory m_history;

    QPointer<Riley::FileJob> m_asking;
    int m_clashButton = KeepBothButton;
    bool m_clashForAll = false;

    QString m_editingPath;
    QString m_message;
    QTimer m_messageTimer;

    bool m_showProgress = false;
    QTimer m_progressTimer; // shows the strip for a job that runs a while
    QTimer m_cleanup;
    QPointer<Riley::FileJob> m_running;
    QElapsedTimer m_runningFor;
    // A cut whose move is under way: once it is done, the clipboard is cleared if it still
    // holds that cut (the files are no longer where it says).
    QStringList m_cutPaths;
};
