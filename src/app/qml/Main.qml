// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The main window: a header across the top, the sidebar on the left, the folder on the right.
Window {
    id: root

    width: 1100
    height: 700
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: "Files"
    color: Theme.window

    HeaderBar {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
    }

    Sidebar {
        id: sidebar
        anchors { top: header.bottom; bottom: parent.bottom; left: parent.left }
    }

    ContentArea {
        anchors { top: header.bottom; bottom: parent.bottom; left: sidebar.right; right: parent.right }
    }
}
