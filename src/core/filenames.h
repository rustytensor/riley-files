// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileycore_export.h"

#include <QString>

namespace Riley {

// The name a second copy gets so that nothing is overwritten: "a.txt" -> "a (2).txt", then
// "a (3).txt"; "New Folder" -> "New Folder (2)". A name that already ends in " (n)" counts on
// from n. Folders keep dots in their names ("v1.2" -> "v1.2 (2)"); hidden names keep their
// leading dot (".bashrc" -> ".bashrc (2)").
RILEYCORE_EXPORT QString numberedName(const QString &name, int number, bool isFolder);

// The first of name, numberedName(name, 2), (3), ... that names nothing in folder (a dangling
// symlink counts as something). Returns name itself when it is free.
RILEYCORE_EXPORT QString freeName(const QString &folder, const QString &name, bool isFolder);

// True when path exists as anything, without following a final symlink.
RILEYCORE_EXPORT bool pathExists(const QString &path);

// True when inner is outer itself or lies somewhere below it. Both are made absolute and
// resolved (symlinks and "..") as far as they exist, so "D/sub/../sub" counts as inside D.
RILEYCORE_EXPORT bool isSameOrInside(const QString &inner, const QString &outer);

// A name a person may give a file: not empty, not "." or "..", no "/" and no NUL.
RILEYCORE_EXPORT bool isValidFileName(const QString &name);

// Renames from to to, never replacing anything at to (RENAME_NOREPLACE; where the drive lacks
// it, a look first). Returns 0, or the errno: EEXIST when to is taken, EXDEV across drives.
RILEYCORE_EXPORT int renameNoReplace(const QString &from, const QString &to);

} // namespace Riley
