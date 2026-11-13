// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "filejobs.h"
#include "rileycore_export.h"

#include <QDateTime>
#include <QString>

namespace Riley {

// Brings back an earlier version of a file or folder from a snapshot (see Snapshots).
//
// Restore: the live one goes to the trash first, so it is never lost; then the old copy is
// copied into its place. If the live one cannot go to the trash, nothing is restored. The
// steps are a Trashed and a Created step, so one undo puts the live one back.
//
// AsCopy: the old copy is copied beside the live one as "name (YYYY-MM-DD hh.mm).ext", named
// by the version's date (or "... (2)" when that is taken); the live one is not touched.
//
// Nothing in a snapshot is ever written: a live path inside one is refused.
class RILEYCORE_EXPORT VersionRestoreJob : public TransferJob
{
    Q_OBJECT

public:
    enum class Mode { Restore, AsCopy };
    Q_ENUM(Mode)

    // copyPath: the version's copy in a snapshot (FileVersion::path); livePath: where the file
    // lives now; date: the version's date (FileVersion::snapshot.date).
    VersionRestoreJob(const QString &copyPath, const QString &livePath, const QDateTime &date, Mode mode,
                      QObject *parent = nullptr);
    Kind kind() const override { return Kind::RestoreVersion; }

    Mode mode() const { return m_mode; }
    QString livePath() const { return m_livePath; }
    // Where the version now is: livePath for Restore, the dated copy for AsCopy. Empty until
    // the job is done, or when it failed.
    QString restoredPath() const;

protected:
    void run() override;

private:
    const QString m_copyPath;
    const QString m_livePath;
    const QDateTime m_date;
    const Mode m_mode;
};

} // namespace Riley
