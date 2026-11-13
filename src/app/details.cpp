// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "details.h"

#include "foldermodel.h"
#include "translations.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>
#include <QPointer>
#include <QThreadPool>

namespace {

QString withSeparators(qint64 n)
{
    // Grouped the same way everywhere ("4,200"), so the pane reads alike on every machine.
    QString digits = QString::number(n);
    for (qsizetype at = digits.size() - 3; at > 0; at -= 3)
        digits.insert(at, QLatin1Char(','));
    return digits;
}

} // namespace

FileDetails::FileDetails(QObject *parent)
    : QObject(parent)
{
}

FileDetails::~FileDetails() = default;

QString FileDetails::sizeTextFor(qint64 bytes)
{
    if (bytes == 1)
        return tr("1 byte");
    if (bytes < 1000)
        return tr("%1 bytes").arg(bytes);
    static const char *const units[] = {"KB", "MB", "GB", "TB", "PB"};
    double value = double(bytes);
    int unit = -1;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    const QString amount = value < 10.0 ? QString::number(value, 'f', 1) : QString::number(qRound(value));
    return tr("%1 %2 (%3 bytes)").arg(amount, QLatin1String(units[unit]), withSeparators(bytes));
}

QString FileDetails::itemsTextFor(qint64 items)
{
    if (items == 0)
        return tr("No items");
    if (items == 1)
        return tr("1 item");
    return tr("%1 items").arg(withSeparators(items));
}

QString FileDetails::permissionsTextFor(const QString &path)
{
    const QFileDevice::Permissions p = QFileInfo(path).permissions();
    const auto bit = [p](QFileDevice::Permission flag, char letter) {
        return QLatin1Char(p.testFlag(flag) ? letter : '-');
    };
    QString text;
    text += bit(QFileDevice::ReadOwner, 'r');
    text += bit(QFileDevice::WriteOwner, 'w');
    text += bit(QFileDevice::ExeOwner, 'x');
    text += bit(QFileDevice::ReadGroup, 'r');
    text += bit(QFileDevice::WriteGroup, 'w');
    text += bit(QFileDevice::ExeGroup, 'x');
    text += bit(QFileDevice::ReadOther, 'r');
    text += bit(QFileDevice::WriteOther, 'w');
    text += bit(QFileDevice::ExeOther, 'x');
    return text;
}

QString FileDetails::dateTextFor(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    // The order of day, month and year follows the language of the words; the month names come with it.
    return Translations::locale().toString(when.toLocalTime(), tr("d MMMM yyyy, HH:mm"));
}

QString FileDetails::categoryFor(const QString &mimeName, bool isDir)
{
    if (isDir)
        return QStringLiteral("folder");
    // The same kinds as the folder's rows, so a file has one icon wherever it is shown.
    return Riley::FolderModel::categoryOf(QMimeDatabase().mimeTypeForName(mimeName));
}

void FileDetails::setPath(const QString &path, bool countHidden)
{
    m_path = path;
    m_countHidden = countHidden;
    refresh();
}

void FileDetails::refresh()
{
    ++m_generation;
    m_name.clear();
    m_kind.clear();
    m_category.clear();
    m_isDir = false;
    m_sizeText.clear();
    m_modifiedText.clear();
    m_permissions.clear();
    m_imageSize.clear();
    m_imageSource.clear();

    const QFileInfo info(m_path);
    if (!m_path.isEmpty() && (info.exists() || info.isSymLink())) {
        m_name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
        m_isDir = info.isDir();
        m_modifiedText = dateTextFor(info.lastModified());
        m_permissions = permissionsTextFor(m_path);
        if (m_isDir) {
            m_kind = tr("Folder");
            m_category = QStringLiteral("folder");
            m_sizeText = tr("Counting…");
            countItems();
        } else {
            const QMimeType mime = QMimeDatabase().mimeTypeForFile(info);
            m_kind = mime.comment().isEmpty() ? mime.name() : mime.comment();
            if (!m_kind.isEmpty())
                m_kind[0] = m_kind.at(0).toUpper();
            m_category = categoryFor(mime.name(), false);
            m_sizeText = sizeTextFor(info.size());
            if (m_category == QLatin1String("image")) {
                // The reader looks at the header only; nothing is decoded.
                QImageReader reader(m_path);
                const QSize size = reader.size();
                if (size.isValid())
                    m_imageSize = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
                if (reader.canRead())
                    m_imageSource = QUrl::fromLocalFile(m_path);
            }
        }
    }
    emit changed();
}

void FileDetails::countItems()
{
    const quint64 generation = m_generation;
    const QString path = m_path;
    const bool hidden = m_countHidden;
    QPointer<FileDetails> self(this);
    QThreadPool::globalInstance()->start([self, generation, path, hidden] {
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (hidden)
            filters |= QDir::Hidden;
        qint64 items = 0;
        QDirIterator it(path, filters);
        while (it.hasNext()) {
            it.next();
            ++items;
        }
        const QString text = QFileInfo(path).isReadable() ? itemsTextFor(items)
                                                          : tr("Cannot be read");
        QMetaObject::invokeMethod(
            qApp, [self, generation, text] {
                if (!self || self->m_generation != generation)
                    return;
                self->m_sizeText = text;
                emit self->changed();
            },
            Qt::QueuedConnection);
    });
}
