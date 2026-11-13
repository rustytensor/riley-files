// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>

#include <functional>
#include <memory>

namespace Riley {

// One commit, as a folder's table shows it.
struct GitCommit
{
    QString subject; // the first line of its message
    QString author;
    QDateTime when;  // the author date

    bool isValid() const { return when.isValid(); }
};

// What git says about a folder: the branch, the latest commit that touched anything in it, and
// for every file and folder below it the latest commit that touched it.
struct GitFolderInfo
{
    bool inRepository = false;
    QString root;   // the repository's top folder
    QString folder; // the folder asked about
    QString branch; // "main"; a short commit id when no branch is checked out
    QString head;   // the full id of the commit checked out
    GitCommit latest;
    QHash<QString, GitCommit> byPath; // absolute path -> the last commit that touched it
};

// Reads a folder's history from git: one `git log --name-only` pass over the folder, run as an
// argument list (never through a shell), its answer kept per repository and per commit checked
// out, so it is read again only when the repository moves on. It blocks while git runs: call it
// on a worker thread, never on the UI thread.
class RILEYCORE_EXPORT GitLog
{
public:
    using Info = std::shared_ptr<const GitFolderInfo>;

    // What git says about `folder`. A folder outside any repository (or git not installed)
    // gives inRepository false. `stop` is asked now and then; true ends the reading early, and
    // then the answer is empty. Safe to call on any thread at once.
    static Info read(const QString &folder, const std::function<bool()> &stop = {});

    // The top folder of the repository `folder` is in, found by looking for ".git" upwards,
    // without running git; empty when there is none.
    static QString repositoryOf(const QString &folder);

    // Forgets every kept answer.
    static void clearCache();

    // What was run, for tests: each program with its arguments, and every thread git ran on.
    struct Run
    {
        QString program;
        QStringList arguments;
    };
    static QList<Run> runs();
    static QSet<Qt::HANDLE> threads();
};

} // namespace Riley
