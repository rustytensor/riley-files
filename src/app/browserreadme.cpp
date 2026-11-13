// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The folder's README under the list, and the folder's path as parts to click.

#include "browser.h"

#include "readme.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QUrl>
#include <QVariantMap>

void Browser::refreshReadme()
{
    const QString folderShown = folder();
    const quint64 ask = ++m_readmeAsk;
    // Another folder: its README is not the one shown.
    if (folderShown != m_readmeFolder)
        setReadme(folderShown, QString(), QString());
    if (folderShown.isEmpty())
        return;
    if (!m_readmePool) {
        m_readmePool = std::make_unique<QThreadPool>();
        m_readmePool->setMaxThreadCount(1);
    }
    m_readmePool->start([this, ask, folderShown] {
        Readme::Found found = Readme::find(folderShown);
        QMetaObject::invokeMethod(
            this,
            [this, ask, folderShown, found = std::move(found)] {
                if (ask != m_readmeAsk || folderShown != folder())
                    return; // a newer reading is on its way, or another folder is shown
                if (found.path.isEmpty()) {
                    setReadme(folderShown, QString(), QString());
                    return;
                }
                const QString text = QString::fromUtf8(found.bytes);
                setReadme(folderShown, found.name, Readme::toHtml(text, found.markdown, folderShown));
            },
            Qt::QueuedConnection);
    });
}

void Browser::setReadme(const QString &folder, const QString &name, const QString &html)
{
    m_readmeFolder = folder;
    if (name == m_readmeName && html == m_readmeHtml)
        return;
    m_readmeName = name;
    m_readmeHtml = html;
    emit readmeChanged();
}

void Browser::openReadmeLink(const QString &href)
{
    if (!Readme::linkAllowed(href))
        return;
    const QUrl url(href.trimmed());
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        QDesktopServices::openUrl(url);
        return;
    }
    // A relative path, from the folder the README is in; a part after '#' or '?' is not a file.
    const QString relative = url.path();
    if (relative.isEmpty())
        return;
    const QString target = QDir::cleanPath(QDir(m_readmeFolder.isEmpty() ? folder() : m_readmeFolder)
                                               .absoluteFilePath(relative));
    const QFileInfo info(target);
    if (info.isDir())
        open(target);
    else if (info.exists())
        openFiles({target});
    else
        m_ops->say(tr("“%1” is not there.").arg(relative));
}

QVariantList Browser::crumbs() const
{
    const QString path = folder();
    QVariantList parts;
    if (path.isEmpty())
        return parts;
    auto add = [&parts](const QString &name, const QString &at) {
        parts.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("path"), at}});
    };
    const QString home = QDir::cleanPath(QDir::homePath());
    QString at;
    QString rest;
    if (home != QLatin1String("/") && (path == home || path.startsWith(home + QLatin1Char('/')))) {
        add(tr("Home"), home);
        at = home;
        rest = path.mid(home.size());
    } else {
        add(QStringLiteral("/"), QStringLiteral("/"));
        at = QString();
        rest = path;
    }
    const QStringList names = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        at += QLatin1Char('/') + name;
        add(name, at);
    }
    return parts;
}
