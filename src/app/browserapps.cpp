// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// Opening files with applications (the default apps, Open With) and the icons a person chose
// for files (Change Icon, Reset Icon).

#include "browser.h"

#include "iconprovider.h"
#include "mimeapps.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QThread>

using Riley::DesktopEntry;
using Riley::MimeApps;
using Entry = Actions::Entry;

namespace {

QString mimeTypeOf(const QString &path)
{
    return QMimeDatabase().mimeTypeForFile(path).name();
}

QString nameOf(const QString &path)
{
    return QFileInfo(path).fileName();
}

} // namespace

void Browser::openFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QString type = mimeTypeOf(path);
        const QList<DesktopEntry> apps = MimeApps::applicationsFor(type);
        if (apps.isEmpty()) {
            m_ops->say(tr("No application is set to open “%1” (%2). Choose one with Open With.")
                           .arg(nameOf(path), type));
            continue;
        }
        launch(apps.first(), {path});
    }
}

void Browser::launch(const DesktopEntry &app, const QStringList &paths)
{
    QString error;
    if (!MimeApps::launch(app, paths, &error)) {
        m_ops->say(error);
        return;
    }
    // Every file opened here is a recent file, for every program that reads the bookmark file.
    for (const QString &path : paths) {
        QString why;
        if (QFileInfo(path).isFile() && !Riley::RecentFiles::record(path, mimeTypeOf(path), &why))
            m_ops->say(why);
    }
    if (m_place == QLatin1String("recent"))
        m_recent->reload();
}

QList<Entry> Browser::openWithEntries()
{
    const QStringList paths = targetPaths();
    const QString path = activeModel()->pathAt(activeCursor());
    if (path.isEmpty())
        return {Entry::oneOff(tr("No File Chosen"), {}, false)};
    const QString type = mimeTypeOf(path);
    const QList<DesktopEntry> apps = MimeApps::applicationsFor(type);

    QList<Entry> entries;
    QList<Entry> always;
    for (const DesktopEntry &app : apps) {
        entries.append(Entry::oneOff(app.name, [this, app, paths] { launch(app, paths); }));
        always.append(Entry::oneOff(app.name, [this, app, type] {
            if (!MimeApps::setDefault(type, app.id)) {
                m_ops->say(tr("The default application could not be saved in “%1”.")
                               .arg(MimeApps::userListFile()));
                return;
            }
            m_ops->say(tr("Files of this type (%1) now open with %2.").arg(type, app.name));
        }));
    }
    if (entries.isEmpty())
        entries.append(Entry::oneOff(tr("No Applications for This Type"), {}, false));
    entries.append(Entry::separator());
    if (always.isEmpty())
        always.append(Entry::oneOff(tr("No Applications for This Type"), {}, false));
    entries.append(Entry::submenu(QStringLiteral("always-use"), tr("Always Use for This Type"), always));
    entries.append(Entry::oneOff(tr("Other Application"), [this, paths] { chooseOtherApp(paths); }));
    return entries;
}

void Browser::chooseOtherApp(const QStringList &paths)
{
    QList<Chooser::Item> items;
    for (const DesktopEntry &app : MimeApps::allApplications()) {
        if (app.noDisplay)
            continue;
        items.append({app.name, app.id, app.id, QString()});
    }
    const QString title = paths.size() == 1 ? tr("Open “%1” With").arg(nameOf(paths.first()))
                                            : tr("Open %n Files With", nullptr, int(paths.size()));
    m_chooser->open(title, tr("Type to find an application"), items,
                    false, {}, [this, paths](const Chooser::Item &item) {
                        const DesktopEntry app = MimeApps::application(item.value);
                        if (app.isValid())
                            launch(app, paths);
                    });
}

QString Browser::customIcon(const QString &path) const
{
    return path.isEmpty() ? QString() : m_icons->icon(path);
}

QString Browser::customIconSource(const QString &path, int revision) const
{
    Q_UNUSED(revision);
    return IconProvider::sourceFor(customIcon(path));
}

void Browser::changeIcon(const QString &path)
{
    if (path.isEmpty())
        return;
    // A typed path to an image file is offered first.
    const auto extra = [](const QString &query) {
        QList<Chooser::Item> items;
        QString file = query;
        if (file == QLatin1String("~") || file.startsWith(QLatin1String("~/")))
            file = QDir::homePath() + file.mid(1);
        if (!QDir::isAbsolutePath(file))
            return items;
        file = QDir::cleanPath(file);
        if (QFileInfo(file).isFile() && QImageReader(file).canRead())
            items.append({tr("The image “%1”").arg(nameOf(file)), file, file,
                          IconProvider::sourceFor(file)});
        return items;
    };
    const auto chosen = [this, path](const Chooser::Item &item) {
        if (!m_icons->set(path, item.value))
            m_ops->say(tr("The icon could not be saved in “%1”.").arg(Riley::CustomIcons::storeFile()));
    };
    const int ticket = m_chooser->open(tr("Icon for “%1”").arg(nameOf(path)),
                                       tr("Type an icon's name, or the path of an image file"), {},
                                       true, extra, chosen);

    // The icon themes are walked off the UI thread; the names come back here.
    QPointer<Chooser> chooser = m_chooser.get();
    QThread *thread = QThread::create([chooser, ticket] {
        const QStringList names = IconIndex::get()->names();
        QList<Chooser::Item> items;
        items.reserve(names.size());
        for (const QString &name : names)
            items.append({name, name, QString(), IconProvider::sourceFor(name)});
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [chooser, ticket, items] {
                if (chooser)
                    chooser->setItems(ticket, items);
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Browser::resetIcon(const QString &path)
{
    if (path.isEmpty())
        return;
    if (!m_icons->reset(path))
        m_ops->say(tr("The icon could not be saved in “%1”.").arg(Riley::CustomIcons::storeFile()));
}
