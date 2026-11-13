// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "desktopentry.h"

#include <QFile>
#include <QUrl>

namespace Riley {

namespace {

// Undoes the string escapes of a desktop file value: \s \n \t \r \\ and, in a list, \;.
QString unescape(QStringView value, bool list)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != u'\\' || i + 1 >= value.size()) {
            out.append(c);
            continue;
        }
        const QChar next = value.at(++i);
        switch (next.unicode()) {
        case 's': out.append(u' '); break;
        case 'n': out.append(u'\n'); break;
        case 't': out.append(u'\t'); break;
        case 'r': out.append(u'\r'); break;
        case '\\': out.append(u'\\'); break;
        case ';':
            if (list)
                out.append(QChar(0xE000)); // a ';' that does not split, put back below
            else
                out.append(u"\\;");
            break;
        default:
            out.append(u'\\');
            out.append(next);
        }
    }
    return out;
}

QStringList splitList(QStringView value)
{
    QStringList items;
    const QString text = unescape(value, true);
    for (QString item : text.split(u';', Qt::SkipEmptyParts)) {
        item.replace(QChar(0xE000), u';');
        item = item.trimmed();
        if (!item.isEmpty())
            items.append(item);
    }
    return items;
}

bool isTrue(QStringView value)
{
    return value.trimmed() == u"true";
}

struct Token
{
    QString text;
    bool quoted = false;
};

bool tokenize(const QString &exec, QList<Token> *tokens)
{
    tokens->clear();
    Token current;
    bool inToken = false;
    bool inQuotes = false;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (inQuotes) {
            if (c == u'"') {
                inQuotes = false;
            } else if (c == u'\\' && i + 1 < exec.size()
                       && QStringView(u"\"`$\\").contains(exec.at(i + 1))) {
                current.text.append(exec.at(++i));
            } else {
                current.text.append(c);
            }
            continue;
        }
        if (c == u' ' || c == u'\t' || c == u'\n') {
            if (inToken) {
                tokens->append(current);
                current = Token();
                inToken = false;
            }
            continue;
        }
        inToken = true;
        if (c == u'"') {
            inQuotes = true;
            current.quoted = true;
        } else {
            current.text.append(c);
        }
    }
    if (inQuotes)
        return false;
    if (inToken)
        tokens->append(current);
    return !tokens->isEmpty();
}

QString urlOf(const QString &path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

} // namespace

DesktopEntry DesktopEntry::read(const QString &file, const QString &id)
{
    DesktopEntry entry;
    entry.id = id;
    entry.file = file;
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;
    bool inMain = false;
    while (!in.atEnd()) {
        const QString line = QString::fromUtf8(in.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        if (line.startsWith(u'[')) {
            inMain = line == u"[Desktop Entry]";
            continue;
        }
        if (!inMain)
            continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq <= 0)
            continue;
        const QStringView key = QStringView(line).left(eq).trimmed();
        const QStringView value = QStringView(line).mid(eq + 1).trimmed();
        if (key == u"Type")
            entry.application = value == u"Application";
        else if (key == u"Name")
            entry.name = unescape(value, false);
        else if (key == u"Exec")
            entry.exec = unescape(value, false);
        else if (key == u"Icon")
            entry.icon = unescape(value, false);
        else if (key == u"TryExec")
            entry.tryExec = unescape(value, false);
        else if (key == u"MimeType")
            entry.mimeTypes = splitList(value);
        else if (key == u"NoDisplay")
            entry.noDisplay = isTrue(value);
        else if (key == u"Hidden")
            entry.hidden = isTrue(value);
    }
    if (entry.name.isEmpty())
        entry.name = id.chopped(id.endsWith(u".desktop") ? 8 : 0);
    return entry;
}

bool DesktopEntry::splitExec(const QString &exec, QStringList *args)
{
    QList<Token> tokens;
    if (!tokenize(exec, &tokens))
        return false;
    args->clear();
    for (const Token &token : tokens)
        args->append(token.text);
    return true;
}

QList<QStringList> DesktopEntry::commandLines(const QStringList &paths) const
{
    QList<Token> tokens;
    if (!tokenize(exec, &tokens))
        return {};

    bool single = false;
    for (const Token &token : tokens) {
        if (!token.quoted && (token.text.contains(u"%f") || token.text.contains(u"%u")))
            single = true;
    }

    // One command line for the given files: `one` for %f and %u, `all` for %F and %U.
    const auto build = [&](const QString &one, const QStringList &all) {
        QStringList args;
        for (const Token &token : tokens) {
            if (token.quoted) {
                args.append(token.text);
                continue;
            }
            if (token.text == u"%F") {
                args.append(all);
                continue;
            }
            if (token.text == u"%U") {
                for (const QString &path : all)
                    args.append(urlOf(path));
                continue;
            }
            if (token.text == u"%i") {
                if (!icon.isEmpty())
                    args << QStringLiteral("--icon") << icon;
                continue;
            }
            QString out;
            bool onlyCodes = true;
            const QString &text = token.text;
            for (qsizetype i = 0; i < text.size(); ++i) {
                if (text.at(i) != u'%' || i + 1 >= text.size()) {
                    out.append(text.at(i));
                    onlyCodes = false;
                    continue;
                }
                const QChar code = text.at(++i);
                switch (code.unicode()) {
                case 'f': out.append(one); break;
                case 'u': out.append(one.isEmpty() ? QString() : urlOf(one)); break;
                case 'c': out.append(name); break;
                case 'k': out.append(file); break;
                case '%':
                    out.append(u'%');
                    onlyCodes = false;
                    break;
                default: break; // %F %U inside a word, the deprecated codes, unknown ones
                }
            }
            if (out.isEmpty() && onlyCodes)
                continue;
            args.append(out);
        }
        return args;
    };

    QList<QStringList> lines;
    if (single && !paths.isEmpty()) {
        for (const QString &path : paths)
            lines.append(build(path, {path}));
    } else {
        lines.append(build(QString(), paths));
    }
    // A command line without its program is no command line.
    QList<QStringList> kept;
    for (const QStringList &line : lines) {
        if (!line.isEmpty() && !line.first().isEmpty())
            kept.append(line);
    }
    return kept;
}

} // namespace Riley
