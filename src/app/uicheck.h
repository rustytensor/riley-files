// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QObject>

class QQmlApplicationEngine;
class QQuickWindow;

// Backs "riley-files --check-ui": proves the main window loads and draws, and that QML is
// quiet while it does. A build check can run it without a display (offscreen platform).
class UiCheck : public QObject
{
    Q_OBJECT

public:
    // Starts counting QML warnings. Create it before the engine and keep it alive until run()
    // returns; only one may exist at a time.
    UiCheck();
    ~UiCheck() override;

    // Also count the warnings the engine reports itself. Call before loading.
    void watch(QQmlApplicationEngine &engine);

    // Call after loading. Waits for the first frame and returns the exit code:
    // 0 when the window was shown with no QML warning.
    int run(QQmlApplicationEngine &engine);

private:
    void finish(QQuickWindow *window, bool shown);

    bool m_done = false;
};
