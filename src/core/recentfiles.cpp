// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "recentfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace Riley {

namespace {

const QString appName = QStringLiteral("Riley Files");
// As GTK writes it: the command in single quotes, the quotes written as &apos;.
const QString appExec = QStringLiteral("&apos;riley-files %u&apos;");

// A date of the bookmark file: "2026-11-13T03:04:05.123456Z" (fractions and the zone optional).
QDateTime parseStamp(const QString &text)
{
    static const QRegularExpression stamp(
        QStringLiteral(R"(^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d+))?(Z|[+-]\d\d:?\d\d)?$)"));
    const QRegularExpressionMatch m = stamp.match(text.trimmed());
    if (!m.hasMatch())
        return {};
    QDateTime when = QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-ddThh:mm:ss"));
    if (!when.isValid())
        return {};
    const QString zone = m.captured(3);
    if (zone.isEmpty() || zone == QLatin1String("Z")) {
        when.setTimeZone(QTimeZone::UTC);
    } else {
        const int sign = zone.startsWith(QLatin1Char('-')) ? -1 : 1;
        const QString digits = QString(zone.mid(1)).remove(QLatin1Char(':'));
        const int offset = sign * (digits.left(2).toInt() * 3600 + digits.mid(2, 2).toInt() * 60);
        when.setTimeZone(QTimeZone::fromSecondsAheadOfUtc(offset));
    }
    const QString fraction = m.captured(2);
    if (!fraction.isEmpty())
        when = when.addMSecs(QString(fraction + QStringLiteral("00")).left(3).toInt());
    return when;
}

QString stampOf(const QDateTime &when)
{
    // Microseconds, as GLib writes them.
    return when.toUTC().toString(QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz")) + QStringLiteral("000Z");
}

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

QString localPathOf(const QString &href)
{
    const QUrl url(href);
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString();
}

QString applicationElement(const QString &stamp, int count)
{
    return QStringLiteral("<bookmark:application name=\"%1\" exec=\"%2\" modified=\"%3\" count=\"%4\"/>")
        .arg(escaped(appName), appExec, stamp)
        .arg(count);
}

QString newBookmark(const QString &path, const QString &mimeType, const QString &stamp, const QString &added)
{
    const QString href = QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
    return QStringLiteral("  <bookmark href=\"%1\" added=\"%2\" modified=\"%3\" visited=\"%3\">\n"
                          "    <info>\n"
                          "      <metadata owner=\"http://freedesktop.org\">\n"
                          "        <mime:mime-type type=\"%4\"/>\n"
                          "        <bookmark:applications>\n"
                          "          %5\n"
                          "        </bookmark:applications>\n"
                          "      </metadata>\n"
                          "    </info>\n"
                          "  </bookmark>\n")
        .arg(escaped(href), added, stamp, escaped(mimeType), applicationElement(stamp, 1));
}

// Sets attribute name to value in a start tag, adding it before the tag's end when missing.
QString withAttribute(QString tag, const QString &name, const QString &value)
{
    const QRegularExpression attribute(QStringLiteral(R"((\s%1\s*=\s*)("[^"]*"|'[^']*'))")
                                           .arg(QRegularExpression::escape(name)));
    const QRegularExpressionMatch m = attribute.match(tag);
    if (m.hasMatch())
        return tag.replace(m.capturedStart(2), m.capturedLength(2), QLatin1Char('"') + value + QLatin1Char('"'));
    qsizetype end = tag.size() - 1; // the '>'
    if (tag.endsWith(QLatin1String("/>")))
        --end;
    tag.insert(end, QStringLiteral(" %1=\"%2\"").arg(name, value));
    return tag;
}

// The bookmark element given, updated: modified and visited now, and its Riley Files entry
// counted up (or added). Nothing when it cannot be read.
std::optional<QString> updatedBookmark(const QString &element, const QString &mimeType, const QString &stamp)
{
    const qsizetype tagEnd = element.indexOf(QLatin1Char('>'));
    if (tagEnd < 0)
        return std::nullopt;
    QString tag = element.left(tagEnd + 1);
    QString body = element.mid(tagEnd + 1);
    tag = withAttribute(withAttribute(tag, QStringLiteral("modified"), stamp), QStringLiteral("visited"), stamp);

    if (tag.endsWith(QLatin1String("/>"))) {
        // An empty bookmark: it gets the info a bookmark of ours has.
        tag.chop(2);
        tag = tag.trimmed() + QLatin1Char('>');
        body = QStringLiteral("\n    <info>\n      <metadata owner=\"http://freedesktop.org\">\n"
                              "        <mime:mime-type type=\"%1\"/>\n        <bookmark:applications>\n"
                              "          %2\n        </bookmark:applications>\n      </metadata>\n    </info>\n  "
                              "</bookmark>")
                   .arg(escaped(mimeType), applicationElement(stamp, 1));
        return tag + body;
    }

    static const QRegularExpression ours(
        QStringLiteral(R"(<bookmark:application\s[^>]*\bname\s*=\s*"Riley Files"[^>]*?/?>(?:\s*</bookmark:application>)?)"));
    const QRegularExpressionMatch m = ours.match(body);
    if (m.hasMatch()) {
        static const QRegularExpression countAttr(QStringLiteral(R"re(\bcount\s*=\s*"(\d+)")re"));
        const QRegularExpressionMatch c = countAttr.match(m.captured(0));
        const int count = c.hasMatch() ? c.captured(1).toInt() : 0;
        body.replace(m.capturedStart(0), m.capturedLength(0), applicationElement(stamp, count + 1));
        return tag + body;
    }
    const qsizetype apps = body.indexOf(QLatin1String("</bookmark:applications>"));
    if (apps >= 0) {
        body.insert(apps, QStringLiteral("  ") + applicationElement(stamp, 1) + QStringLiteral("\n        "));
        return tag + body;
    }
    const qsizetype metadata = body.indexOf(QLatin1String("</metadata>"));
    if (metadata >= 0) {
        body.insert(metadata, QStringLiteral("  <bookmark:applications>\n          ") + applicationElement(stamp, 1)
                                  + QStringLiteral("\n        </bookmark:applications>\n      "));
        return tag + body;
    }
    const qsizetype info = body.indexOf(QLatin1String("</info>"));
    const QString block = QStringLiteral("  <metadata owner=\"http://freedesktop.org\">\n"
                                         "        <mime:mime-type type=\"%1\"/>\n        <bookmark:applications>\n"
                                         "          %2\n        </bookmark:applications>\n      </metadata>\n    ")
                              .arg(escaped(mimeType), applicationElement(stamp, 1));
    if (info >= 0) {
        body.insert(info, block);
        return tag + body;
    }
    const qsizetype close = body.lastIndexOf(QLatin1String("</bookmark>"));
    if (close < 0)
        return std::nullopt;
    body.insert(close, QStringLiteral("  <info>\n    ") + block + QStringLiteral("</info>\n  "));
    return tag + body;
}

} // namespace

RecentFiles::RecentFiles(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RecentFiles::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant RecentFiles::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QFileInfo(item.path).fileName();
    case PathRole:
        return item.path;
    case FolderRole:
        return QFileInfo(item.path).absolutePath();
    case ModifiedRole:
        return item.modified.toLocalTime();
    case ModifiedTextRole:
        return item.modified.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case IsDirRole:
        return item.isDir;
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentFiles::roleNames() const
{
    return {{NameRole, "name"},         {PathRole, "path"},         {FolderRole, "folder"},
            {ModifiedRole, "modified"}, {ModifiedTextRole, "modifiedText"}, {IsDirRole, "isDir"}};
}

QString RecentFiles::pathAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).path : QString();
}

QString RecentFiles::nameAt(int row) const
{
    return row >= 0 && row < m_items.size() ? QFileInfo(m_items.at(row).path).fileName() : QString();
}

int RecentFiles::rowOf(const QString &path) const
{
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).path == path)
            return int(i);
    }
    return -1;
}

void RecentFiles::reload()
{
    const QList<Item> items = read();
    const bool countChanges = items.size() != m_items.size();
    beginResetModel();
    m_items = items;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

QString RecentFiles::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/recently-used.xbel");
}

QList<RecentFiles::Bookmark> RecentFiles::parse(const QString &text, bool *ok)
{
    QList<Bookmark> bookmarks;
    QXmlStreamReader reader(text);
    int depth = 0;
    int bookmarkDepth = -1;
    Bookmark current;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            ++depth;
            if (bookmarkDepth < 0 && depth == 2 && reader.qualifiedName() == QLatin1String("bookmark")) {
                bookmarkDepth = depth;
                const QXmlStreamAttributes attributes = reader.attributes();
                current = Bookmark();
                current.href = attributes.value(QLatin1String("href")).toString();
                current.path = localPathOf(current.href);
                for (const char *name : {"modified", "visited", "added"}) {
                    current.modified = parseStamp(attributes.value(QLatin1String(name)).toString());
                    if (current.modified.isValid())
                        break;
                }
                const qsizetype after = qsizetype(reader.characterOffset());
                current.start = text.lastIndexOf(QLatin1String("<bookmark"), after - 1);
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (depth == bookmarkDepth) {
                current.end = qsizetype(reader.characterOffset());
                if (current.start >= 0 && current.end > current.start)
                    bookmarks.append(current);
                bookmarkDepth = -1;
            }
            --depth;
        }
    }
    const bool fine = !reader.hasError();
    if (ok)
        *ok = fine;
    return fine ? bookmarks : QList<Bookmark>();
}

QList<RecentFiles::Item> RecentFiles::read()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QList<Bookmark> bookmarks = parse(QString::fromUtf8(file.readAll()));
    QList<Item> items;
    QStringList seen;
    for (const Bookmark &bookmark : bookmarks) {
        if (bookmark.path.isEmpty() || seen.contains(bookmark.path))
            continue;
        const QFileInfo info(bookmark.path);
        if (!info.exists())
            continue;
        seen.append(bookmark.path);
        items.append({bookmark.path, bookmark.modified, info.isDir()});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) { return a.modified > b.modified; });
    return items;
}

std::optional<QString> RecentFiles::recordIn(const QString &text, const QString &path, const QString &mimeType,
                                             const QDateTime &now)
{
    const QString clean = QDir::cleanPath(path);
    const QString stamp = stampOf(now);
    QString source = text;
    if (source.trimmed().isEmpty()) {
        source = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<xbel version=\"1.0\"\n"
                                "      xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\"\n"
                                "      xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\"\n"
                                ">\n"
                                "</xbel>\n");
    }
    bool ok = false;
    const QList<Bookmark> bookmarks = parse(source, &ok);
    if (!ok)
        return std::nullopt;
    for (const Bookmark &bookmark : bookmarks) {
        if (bookmark.path != clean)
            continue;
        const std::optional<QString> element =
            updatedBookmark(source.mid(bookmark.start, bookmark.end - bookmark.start), mimeType, stamp);
        if (!element)
            return std::nullopt;
        return source.left(bookmark.start) + *element + source.mid(bookmark.end);
    }
    const qsizetype close = source.lastIndexOf(QLatin1String("</xbel>"));
    if (close < 0)
        return std::nullopt;
    // At the start of the closing tag's line, so the bookmarks stay one under the other.
    qsizetype at = close;
    while (at > 0 && (source.at(at - 1) == QLatin1Char(' ') || source.at(at - 1) == QLatin1Char('\t')))
        --at;
    if (at > 0 && source.at(at - 1) != QLatin1Char('\n'))
        at = close;
    QString element = newBookmark(clean, mimeType, stamp, stamp);
    if (at == close && at > 0 && source.at(at - 1) != QLatin1Char('\n'))
        element.prepend(QLatin1Char('\n'));
    return source.left(at) + element + source.mid(at);
}

bool RecentFiles::record(const QString &path, const QString &mimeType, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    const QString target = filePath();
    QString text;
    QFile file(target);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("“%1” could not be read: %2").arg(target, file.errorString()));
        text = QString::fromUtf8(file.readAll());
        file.close();
    }
    const std::optional<QString> updated = recordIn(text, path, mimeType, QDateTime::currentDateTimeUtc());
    if (!updated)
        return fail(QStringLiteral("“%1” is not a bookmark file that can be read; it was left as it is.").arg(target));
    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("“%1” could not be written: %2").arg(target, out.errorString()));
    const QByteArray bytes = updated->toUtf8();
    if (out.write(bytes) != bytes.size() || !out.commit())
        return fail(QStringLiteral("“%1” could not be written: %2").arg(target, out.errorString()));
    return true;
}

} // namespace Riley
