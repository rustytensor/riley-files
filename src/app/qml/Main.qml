// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The main window: a header across the top, the sidebar on the left, the folder on the right.
// Every key goes to the browser, which decides what it does.
Window {
    id: root

    required property Browser browser

    width: 1100
    height: 700
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    title: root.browser.folderName.length > 0 ? root.browser.folderName + " – Files" : "Files"
    color: Theme.window

    Item {
        id: keys
        objectName: "keys"
        anchors.fill: parent
        focus: true

        Keys.onPressed: event => {
            event.accepted = root.browser.handleKey(event.key, event.modifiers, event.text)
        }

        HeaderBar {
            id: header
            browser: root.browser
            anchors { top: parent.top; left: parent.left; right: parent.right }
        }

        Sidebar {
            id: sidebar
            anchors { top: header.bottom; bottom: parent.bottom; left: parent.left }
        }

        ContentArea {
            browser: root.browser
            anchors { top: header.bottom; bottom: parent.bottom; left: sidebar.right; right: parent.right }
        }
    }
}
