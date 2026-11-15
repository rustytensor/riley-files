// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QQmlApplicationEngine>
#include <QString>
#include <QStringList>

#include <memory>

class Browser;
class Tabs;
namespace Riley {
class Thumbnailer;
}
class QQuickWindow;

// One main window: the QML engine, the Main window from the RileyFiles module and its tabs, a
// browser behind each. The program makes one; the interface tests make one per test, the same way.
class AppWindow
{
public:
    // `settingsFile` as for Browser: empty means the user's usual settings.
    explicit AppWindow(const QString &settingsFile = QString());
    ~AppWindow();

    AppWindow(const AppWindow &) = delete;
    AppWindow &operator=(const AppWindow &) = delete;

    // The engine, for watching its warnings before load().
    QQmlApplicationEngine &engine() { return *m_engine; }
    // The active tab's browser.
    Browser &browser() const;
    Tabs &tabs() const { return *m_tabs; }
    Riley::Thumbnailer &thumbnailer() { return *m_thumbnailer; }

    // Opens `location` (see Browser::openLocation) and loads the window. False when the window
    // did not load.
    bool load(const QString &location);
    // The same with a tab for each location, the first active; none means the home folder.
    bool load(const QStringList &locations);

    // The loaded window, or null.
    QQuickWindow *window() const;

private:
    // The language is chosen before anything that shows words is made.
    QString m_language;
    // The tabs (and their browsers) outlive the engine, so no binding ever reads a deleted browser.
    std::unique_ptr<Tabs> m_tabs;
    // Makes the grid's thumbnails; shared with the engine's image provider.
    std::shared_ptr<Riley::Thumbnailer> m_thumbnailer;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};
