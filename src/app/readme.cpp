// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "readme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>

namespace Readme {

namespace {

// A relative reference: no scheme, no host, not rooted.
bool isRelative(const QString &reference)
{
    const QString trimmed = reference.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QLatin1Char('\\')))
        return false;
    const QUrl url(trimmed);
    return url.isValid() && url.scheme().isEmpty() && url.host().isEmpty() && url.isRelative();
}

// One change to make to the document, kept while walking it and made afterwards.
struct Change
{
    int start = 0;
    int length = 0;
    bool drop = false;           // the fragment goes (an image that may not be shown)
    QTextCharFormat format;      // else its new format
};

} // namespace

QStringList names()
{
    return {QStringLiteral("README.md"), QStringLiteral("readme.md"), QStringLiteral("README"),
            QStringLiteral("README.txt")};
}

Found find(const QString &folder)
{
    Found found;
    if (folder.isEmpty())
        return found;
    const QDir dir(folder);
    // The names exactly as written: the folder's own entries are compared, so a file system that
    // ignores case does not make "readme.md" stand for "README.md".
    const QStringList entries = dir.entryList(names(), QDir::Files | QDir::Readable | QDir::Hidden);
    for (const QString &name : names()) {
        if (!entries.contains(name))
            continue;
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        found.path = file.fileName();
        found.name = name;
        found.markdown = name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
        found.bytes = file.read(maxBytes);
        break;
    }
    return found;
}

bool linkAllowed(const QString &href)
{
    const QString trimmed = href.trimmed();
    if (trimmed.isEmpty())
        return false;
    const QUrl url(trimmed);
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        return !url.host().isEmpty();
    return isRelative(trimmed);
}

bool imageAllowed(const QString &source)
{
    return isRelative(source);
}

QString toHtml(const QString &text, bool markdown, const QString &folder)
{
    if (!markdown)
        return QStringLiteral("<pre>") + text.toHtmlEscaped() + QStringLiteral("</pre>");

    QTextDocument document;
    document.setMarkdown(text, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub
                                                               | QTextDocument::MarkdownNoHTML));

    QList<Change> changes;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCharFormat format = fragment.charFormat();
            bool changed = false;
            if (format.isImageFormat()) {
                QTextImageFormat image = format.toImageFormat();
                if (!imageAllowed(image.name())) {
                    changes.append({fragment.position(), fragment.length(), true, {}});
                    continue;
                }
                image.setName(QUrl::fromLocalFile(QDir(folder).absoluteFilePath(image.name())).toString());
                format = image;
                changed = true;
            }
            if (format.isAnchor() || !format.anchorHref().isEmpty()) {
                if (!linkAllowed(format.anchorHref())) {
                    format.setAnchor(false);
                    format.clearProperty(QTextFormat::AnchorHref);
                    format.clearProperty(QTextFormat::AnchorName);
                    // It reads as text, not as a link.
                    format.clearProperty(QTextFormat::TextUnderlineStyle);
                    format.clearProperty(QTextFormat::FontUnderline);
                    format.clearForeground();
                    changed = true;
                }
            }
            if (changed)
                changes.append({fragment.position(), fragment.length(), false, format});
        }
    }
    // From the end, so the positions still to come stay where they were.
    QTextCursor cursor(&document);
    for (auto it = changes.crbegin(); it != changes.crend(); ++it) {
        cursor.setPosition(it->start);
        cursor.setPosition(it->start + it->length, QTextCursor::KeepAnchor);
        if (it->drop)
            cursor.removeSelectedText();
        else
            cursor.setCharFormat(it->format);
    }
    return document.toHtml();
}

} // namespace Readme
