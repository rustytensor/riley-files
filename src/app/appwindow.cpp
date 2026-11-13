// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "appwindow.h"

#include "browser.h"
#include "thumbnailer.h"
#include "thumbnailprovider.h"

#include <QQuickWindow>
#include <QVariantMap>

AppWindow::AppWindow(const QString &settingsFile)
    : m_browser(std::make_unique<Browser>(settingsFile))
    , m_thumbnailer(std::make_shared<Riley::Thumbnailer>())
    , m_engine(std::make_unique<QQmlApplicationEngine>())
{
    // The engine owns the provider.
    m_engine->addImageProvider(ThumbnailProvider::id(), new ThumbnailProvider(m_thumbnailer));
}

AppWindow::~AppWindow()
{
    m_engine.reset();
}

bool AppWindow::load(const QString &location)
{
    // The folder is asked for first, so it is being read while the window builds.
    m_browser->openLocation(location);
    m_engine->setInitialProperties({{QStringLiteral("browser"), QVariant::fromValue(m_browser.get())}});
    m_engine->loadFromModule("RileyFiles", "Main");
    return window() != nullptr;
}

QQuickWindow *AppWindow::window() const
{
    const auto roots = m_engine->rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(roots.first());
}
