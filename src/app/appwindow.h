// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QQmlApplicationEngine>
#include <QString>

#include <memory>

class Browser;
class QQuickWindow;

// One main window: the QML engine, the Main window from the RileyFiles module and the browser
// behind it. The program makes one; the interface tests make one per test, the same way.
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
    Browser &browser() { return *m_browser; }

    // Opens `location` (see Browser::openLocation) and loads the window. False when the window
    // did not load.
    bool load(const QString &location);

    // The loaded window, or null.
    QQuickWindow *window() const;

private:
    // The browser outlives the engine, so no binding ever reads a deleted browser.
    std::unique_ptr<Browser> m_browser;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};
