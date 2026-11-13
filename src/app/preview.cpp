// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "preview.h"

#include "details.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QStringDecoder>

Preview::Preview(QObject *parent)
    : QObject(parent)
{
}

void Preview::setOpen(bool open)
{
    if (open == m_open)
        return;
    m_open = open;
    if (!open)
        show(QString());
    emit openChanged();
}

QString Preview::firstLines(const QString &path, bool *cut)
{
    if (cut)
        *cut = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.read(maxBytes);
    qsizetype end = 0;
    int lines = 0;
    while (end < bytes.size() && lines < maxLines) {
        const qsizetype newline = bytes.indexOf('\n', end);
        if (newline < 0) {
            end = bytes.size();
            ++lines;
            break;
        }
        end = newline + 1;
        ++lines;
    }
    if (cut)
        *cut = end < bytes.size() || !file.atEnd();
    QStringDecoder decode(QStringDecoder::Utf8);
    QString text = decode(bytes.first(end));
    if (text.endsWith(QLatin1Char('\n')))
        text.chop(1);
    // Tabs as four spaces, so code lines up the same in every font.
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    return text;
}

void Preview::show(const QString &path)
{
    m_path = path;
    m_name.clear();
    m_category.clear();
    m_kind = Nothing;
    m_imageSource.clear();
    m_text.clear();
    m_cut = false;

    const QFileInfo info(path);
    if (!path.isEmpty() && info.exists()) {
        m_name = info.fileName();
        m_kind = Other;
        m_category = info.isDir() ? QStringLiteral("folder") : QStringLiteral("file");
        if (info.isFile()) {
            const QMimeType mime = QMimeDatabase().mimeTypeForFile(info);
            m_category = FileDetails::categoryFor(mime.name(), false);
            if (mime.name().startsWith(QLatin1String("image/")) && QImageReader(path).canRead()) {
                m_kind = Image;
                m_imageSource = QUrl::fromLocalFile(path);
            } else if (mime.inherits(QStringLiteral("text/plain"))) {
                m_kind = Text;
                m_text = firstLines(path, &m_cut);
            }
        }
    }
    emit changed();
}
