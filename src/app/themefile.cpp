// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "themefile.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

// How long after a change on disk the theme is read again: long enough for a switch (a link
// made, then renamed) to finish, short enough to look immediate.
constexpr int settleMs = 120;

QString configHome()
{
    const QString set = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!set.isEmpty() && QDir::isAbsolutePath(set))
        return set;
    return QDir::homePath() + QStringLiteral("/.config");
}

// The value after '=': a quoted string (with its quotes taken off and anything after it, such
// as a comment, left out), or a bare word up to a comment.
QString valueOf(QStringView rest)
{
    rest = rest.trimmed();
    if (rest.isEmpty())
        return {};
    const QChar quote = rest.front();
    if (quote == u'"' || quote == u'\'') {
        const qsizetype end = rest.indexOf(quote, 1);
        if (end < 0)
            return {};
        return rest.mid(1, end - 1).toString();
    }
    const qsizetype hash = rest.indexOf(u'#');
    // A bare colour such as #aabbcc is not a comment; a '#' after a space is.
    if (hash > 0 && rest.at(hash - 1).isSpace())
        rest = rest.left(hash);
    return rest.trimmed().toString();
}

} // namespace

ThemeFile::ThemeFile(QObject *parent)
    : QObject(parent)
    , m_folder(configHome() + QStringLiteral("/riley"))
    , m_link(m_folder + QStringLiteral("/current-theme"))
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
    connect(&m_settle, &QTimer::timeout, this, &ThemeFile::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    reload();
}

QString ThemeFile::path() const
{
    return m_link + QStringLiteral("/colors.toml");
}

QVariantMap ThemeFile::readKeys(const QString &file)
{
    QVariantMap keys;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return keys;
    // A theme file is a few hundred bytes; anything far larger is not one.
    const QString text = QString::fromUtf8(in.read(256 * 1024));
    const auto lines = QStringView(text).split(u'\n');
    for (QStringView line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u'['))
            continue;
        const qsizetype equals = line.indexOf(u'=');
        if (equals <= 0)
            continue;
        QString key = line.left(equals).trimmed().toString().toLower();
        if (key.size() >= 2 && (key.front() == u'"' || key.front() == u'\'') && key.back() == key.front())
            key = key.mid(1, key.size() - 2);
        const QString value = valueOf(line.mid(equals + 1));
        if (!key.isEmpty() && !value.isEmpty())
            keys.insert(key, value);
    }
    return keys;
}

void ThemeFile::reload()
{
    const QString file = path();
    const bool found = QFileInfo(file).isFile();
    QVariantMap colors;
    QString mode = QStringLiteral("dark");
    if (found) {
        const QVariantMap keys = readKeys(file);
        for (auto it = keys.cbegin(); it != keys.cend(); ++it) {
            const QString value = it.value().toString().trimmed();
            if (it.key() == u"mode") {
                if (value.compare(u"light", Qt::CaseInsensitive) == 0)
                    mode = QStringLiteral("light");
                continue;
            }
            // Only real colours reach the window, written one way, so a typo in the file
            // leaves that one colour to the built-in palette.
            const QColor colour = QColor::fromString(value);
            if (colour.isValid())
                colors.insert(it.key(), colour.name(colour.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb));
        }
    }
    watchAgain();
    if (found == m_found && mode == m_mode && colors == m_colors)
        return;
    m_found = found;
    m_mode = mode;
    m_colors = colors;
    emit changed();
}

void ThemeFile::watchAgain()
{
    // A watch stays on what a path named when it was added, so after a switch the paths are
    // added again to follow the new link. The config folder is watched too, for a riley
    // folder made later.
    const QStringList old = m_watcher.files() + m_watcher.directories();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    QStringList paths;
    for (const QString &candidate : {configHome(), m_folder, m_link, path()}) {
        if (QFileInfo::exists(candidate))
            paths.append(candidate);
    }
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}
