// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "installedthumbnailers.h"

#include "thumbnailcache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace Riley {

namespace {

// The keys of the [Thumbnailer Entry] group of a key file. Other groups are skipped.
QHash<QString, QString> readEntryGroup(const QString &path)
{
    QHash<QString, QString> keys;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return keys;
    bool inGroup = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u'[')) {
            inGroup = line == QStringLiteral("[Thumbnailer Entry]");
            continue;
        }
        if (!inGroup)
            continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (!keys.contains(key))
            keys.insert(key, line.mid(eq + 1).trimmed());
    }
    return keys;
}

bool programExists(const QString &program)
{
    if (program.contains(u'/'))
        return QFileInfo(program).isExecutable();
    return !QStandardPaths::findExecutable(program).isEmpty();
}

// The data folders in the order the XDG base directory spec gives them.
QStringList dataFolders()
{
    QStringList folders;
    QString home = qEnvironmentVariable("XDG_DATA_HOME");
    if (home.isEmpty())
        home = QDir::homePath() + QStringLiteral("/.local/share");
    folders.append(home);
    QString dirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dirs.isEmpty())
        dirs = QStringLiteral("/usr/local/share:/usr/share");
    for (const QString &dir : dirs.split(u':', Qt::SkipEmptyParts))
        folders.append(dir);
    return folders;
}

} // namespace

InstalledThumbnailers::InstalledThumbnailers()
{
    QSet<QString> seenNames;
    for (const QString &folder : dataFolders()) {
        const QDir dir(folder + QStringLiteral("/thumbnailers"));
        const QStringList names =
            dir.entryList({QStringLiteral("*.thumbnailer")}, QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &name : names) {
            if (seenNames.contains(name))
                continue;
            seenNames.insert(name);
            const QString path = dir.filePath(name);
            const QHash<QString, QString> keys = readEntryGroup(path);
            Entry entry{path, keys.value(QStringLiteral("Exec")), {}};
            const QString tryExec = keys.value(QStringLiteral("TryExec"));
            if (entry.exec.isEmpty() || (!tryExec.isEmpty() && !programExists(tryExec)))
                continue;
            entry.mimeTypes = keys.value(QStringLiteral("MimeType")).split(u';', Qt::SkipEmptyParts);
            m_entries.append(entry);
            for (const QString &mime : std::as_const(entry.mimeTypes)) {
                if (!m_byMime.contains(mime.trimmed()))
                    m_byMime.insert(mime.trimmed(), m_entries.size() - 1);
            }
        }
    }
}

const InstalledThumbnailers::Entry *InstalledThumbnailers::forMimeType(const QString &mimeType) const
{
    const auto it = m_byMime.constFind(mimeType);
    return it == m_byMime.constEnd() ? nullptr : &m_entries.at(*it);
}

QStringList InstalledThumbnailers::command(const QString &exec, const QString &input,
                                           const QString &output, int size)
{
    // Split first, then expand, so a path with spaces stays one argument.
    QStringList words;
    QString word;
    bool inWord = false;
    bool quoted = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (quoted) {
            if (c == u'\\' && i + 1 < exec.size()) {
                word.append(exec.at(++i));
            } else if (c == u'"') {
                quoted = false;
            } else {
                word.append(c);
            }
        } else if (c == u'"') {
            quoted = true;
            inWord = true;
        } else if (c == u' ' || c == u'\t') {
            if (inWord)
                words.append(word);
            word.clear();
            inWord = false;
        } else {
            word.append(c);
            inWord = true;
        }
    }
    if (quoted)
        return {};
    if (inWord)
        words.append(word);

    QStringList args;
    for (const QString &w : std::as_const(words)) {
        QString out;
        for (qsizetype i = 0; i < w.size(); ++i) {
            if (w.at(i) != u'%' || i + 1 >= w.size()) {
                out.append(w.at(i));
                continue;
            }
            const QChar code = w.at(++i);
            if (code == u'i')
                out.append(input);
            else if (code == u'u')
                out.append(ThumbnailCache::uriOf(input));
            else if (code == u'o')
                out.append(output);
            else if (code == u's')
                out.append(QString::number(size));
            else if (code == u'%')
                out.append(u'%');
            // Any other field code is dropped, as the desktop entry spec says.
        }
        args.append(out);
    }
    return args;
}

} // namespace Riley
