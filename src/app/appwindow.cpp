// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "appwindow.h"

#include "browser.h"
#include "tabs.h"
#include "thumbnailer.h"
#include "fileglyphs.h"
#include "iconprovider.h"
#include "thumbnailprovider.h"
#include "translations.h"

#include <QIcon>

#include <QQuickWindow>
#include <QVariantMap>

AppWindow::AppWindow(const QString &settingsFile)
    : m_language(Translations::install())
    , m_tabs(std::make_unique<Tabs>(settingsFile))
    , m_thumbnailer(std::make_shared<Riley::Thumbnailer>())
    , m_engine(std::make_unique<QQmlApplicationEngine>())
{
    // The engine owns the provider.
    m_engine->addImageProvider(ThumbnailProvider::id(), new ThumbnailProvider(m_thumbnailer));
    m_engine->addImageProvider(IconProvider::id(), new IconProvider());
    m_engine->addImageProvider(GlyphProvider::id(), new GlyphProvider());
    IconIndex::setThemeName(QIcon::themeName());
}

AppWindow::~AppWindow()
{
    m_engine.reset();
}

Browser &AppWindow::browser() const
{
    return *m_tabs->browser();
}

bool AppWindow::load(const QString &location)
{
    return load(QStringList{location});
}

bool AppWindow::load(const QStringList &locations)
{
    // The folders are asked for first, so they are being read while the window builds.
    const QStringList each = locations.isEmpty() ? QStringList{QString()} : locations;
    for (const QString &location : each)
        m_tabs->open(location, true, m_tabs->count());
    m_tabs->activate(0);
    m_engine->setInitialProperties({{QStringLiteral("tabs"), QVariant::fromValue(m_tabs.get())}});
    m_engine->loadFromModule("RileyFiles", "Main");
    return window() != nullptr;
}

QQuickWindow *AppWindow::window() const
{
    const auto roots = m_engine->rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
}
