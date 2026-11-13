// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// A folder's README, shown under the list of its files. Markdown is read with Qt's own Markdown
// support with HTML switched off, so nothing in the file is taken as HTML; the document is then
// made safe before it is shown: an image is kept only when it is a local file named by a
// relative path (nothing is ever fetched from the network), and a link only when it goes to an
// http or https address or to a relative path. A plain README is shown as preformatted text.
namespace Readme {

// The names a README may have, in the order they are looked for.
QStringList names();

// The bytes read at most from a README.
constexpr qint64 maxBytes = 512 * 1024;

struct Found
{
    QString path;    // empty when the folder has none
    QString name;    // its file name, "README.md"
    bool markdown = false;
    QByteArray bytes;
};

// Looks for a README in `folder` and reads it (at most maxBytes). Safe to call on any thread.
Found find(const QString &folder);

// The README as rich text (HTML) for the panel. `folder` is where relative images are found.
QString toHtml(const QString &text, bool markdown, const QString &folder);

// Whether a link in a README may be followed: http, https and relative paths only.
bool linkAllowed(const QString &href);
// Whether an image in a README may be shown: a relative path to a local file only.
bool imageAllowed(const QString &source);

} // namespace Readme
