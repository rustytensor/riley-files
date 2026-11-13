// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The main window: a header across the top, the sidebar on the left, the folder on the right,
// and along the bottom the messages and the progress of file operations. Every key goes to the
// browser, which decides what it does; only a name being edited takes the typing.
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
            anchors { top: header.bottom; bottom: bottomBars.top; left: sidebar.right; right: parent.right }
        }

        // Along the bottom, from the edge up: progress, messages, and the name editor of the
        // views that do not edit names in place. Each takes no room while hidden.
        ProgressStrip {
            id: progress
            anchors { bottom: parent.bottom; left: sidebar.right; right: parent.right }
            height: implicitHeight
            ops: root.browser.ops
        }

        MessageBar {
            id: messages
            anchors { bottom: progress.top; left: sidebar.right; right: parent.right }
            height: implicitHeight
            ops: root.browser.ops
        }

        // The list edits a name in its row; the other views edit it here.
        Rectangle {
            id: bottomBars
            anchors { bottom: messages.top; left: sidebar.right; right: parent.right }
            visible: renameHere.visible
            height: visible ? Theme.rowHeight + 16 : 0
            color: Theme.strip

            Text {
                id: renameLabel
                anchors { left: parent.left; leftMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
                text: "Rename to"
                color: Theme.textMuted
                font.pixelSize: Theme.fontNormal
            }
            RenameField {
                id: renameHere
                ops: root.browser.ops
                active: root.browser.view !== Browser.List
                anchors {
                    left: renameLabel.right; leftMargin: Theme.spacing
                    right: parent.right; rightMargin: Theme.spacing * 1.5
                    verticalCenter: parent.verticalCenter
                }
                height: Theme.rowHeight
            }
        }

        ClashDialog {
            anchors.fill: parent
            ops: root.browser.ops
        }
    }

    // Once a name is no longer edited, the keys go back to the browser.
    Connections {
        target: root.browser.ops
        function onEditingChanged() {
            if (!root.browser.ops.editing)
                keys.forceActiveFocus()
        }
    }
}
