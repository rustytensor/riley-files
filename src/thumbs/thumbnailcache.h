// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include "rileythumbs_export.h"

#include <QImage>
#include <QString>

namespace Riley {

// The shared thumbnail cache of the freedesktop thumbnail spec, which every file manager and
// image viewer on the desktop reads and fills:
//
//     $XDG_CACHE_HOME/thumbnails/normal/<md5 of the file's URI>.png    up to 128 x 128
//     $XDG_CACHE_HOME/thumbnails/large/<md5 of the file's URI>.png     up to 256 x 256
//     $XDG_CACHE_HOME/thumbnails/fail/riley-files/<md5>.png            "this file cannot be done"
//
// A thumbnail holds the text keys Thumb::URI (the file's URI) and Thumb::MTime (the file's
// modified time in whole seconds). It is good while both still match the file.
namespace ThumbnailCache {

// The cache folder, read from the environment on every call (so tests can move it).
RILEYTHUMBS_EXPORT QString root();

// The file's URI as the spec writes it: file:// and the absolute path, escaped.
RILEYTHUMBS_EXPORT QString uriOf(const QString &path);

// The size class for a wanted size: 128 for anything up to 128 pixels, 256 above.
RILEYTHUMBS_EXPORT int bucketFor(int wanted);
// "normal" or "large".
RILEYTHUMBS_EXPORT QString bucketName(int bucket);

// Where the thumbnail of `path` in the size class of `wanted` lives (it may not exist).
RILEYTHUMBS_EXPORT QString thumbnailPath(const QString &path, int wanted);
// Where this program's fail entry for `path` lives (it may not exist).
RILEYTHUMBS_EXPORT QString failPath(const QString &path);

// True when the PNG at `thumbnail` exists and was made from `path` as it is now: its
// Thumb::URI and Thumb::MTime match. Only the PNG's text chunks are read, not its pixels.
RILEYTHUMBS_EXPORT bool isCurrent(const QString &thumbnail, const QString &path);

// Writes `image` to `target` as a PNG with the spec's keys for `path`. Written to a temporary
// file beside it and renamed into place, so no reader ever sees half a thumbnail; the folders
// are made private (0700) and the file 0600, as the spec asks.
RILEYTHUMBS_EXPORT bool store(const QImage &image, const QString &target, const QString &path);

// The source's modified time in whole seconds, as Thumb::MTime holds it; -1 when it cannot be read.
RILEYTHUMBS_EXPORT qint64 modifiedSeconds(const QString &path);

} // namespace ThumbnailCache
} // namespace Riley
