// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The main window: the menu bar and a header across the top, the sidebar on the left, the folder in the middle,
// the details pane on the right when it is shown, and along the bottom the messages and the
// progress of file operations. The preview, when open, lies over all of it. Every key goes to the
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

        AppMenuBar {
            id: menuBar
            actions: root.browser.actions
            z: 2
            height: implicitHeight
            anchors { top: parent.top; left: parent.left; right: parent.right }
        }

        HeaderBar {
            id: header
            browser: root.browser
            anchors { top: menuBar.bottom; left: parent.left; right: parent.right }
        }

        Sidebar {
            id: sidebar
            browser: root.browser
            visible: root.browser.sidebarShown
            width: visible ? implicitWidth : 0
            anchors { top: header.bottom; bottom: parent.bottom; left: parent.left }
        }

        DetailsPane {
            id: details
            browser: root.browser
            visible: root.browser.detailsShown
            width: visible ? implicitWidth : 0
            anchors { top: header.bottom; bottom: parent.bottom; right: parent.right }
        }

        ContentArea {
            id: content
            browser: root.browser
            anchors { top: header.bottom; bottom: bottomBars.top; left: sidebar.right; right: details.left }
        }

        // Each pane's edge drags its width; a double-click gives it back the chosen way's.
        PaneDivider {
            objectName: "sidebarDivider"
            visible: sidebar.visible
            panes: root.browser.panes
            pane: "sidebar"
            anchors { top: header.bottom; bottom: parent.bottom; horizontalCenter: sidebar.right }
        }

        PaneDivider {
            objectName: "detailsDivider"
            visible: details.visible
            panes: root.browser.panes
            pane: "details"
            anchors { top: header.bottom; bottom: parent.bottom; horizontalCenter: details.left }
        }

        // Along the bottom, from the edge up: progress, messages, and the name editor of the
        // views that do not edit names in place. Each takes no room while hidden.
        ProgressStrip {
            id: progress
            anchors { bottom: parent.bottom; left: sidebar.right; right: details.left }
            height: implicitHeight
            ops: root.browser.ops
        }

        MessageBar {
            id: messages
            anchors { bottom: progress.top; left: sidebar.right; right: details.left }
            height: implicitHeight
            ops: root.browser.ops
        }

        // The list edits a name in its row; the other views edit it here.
        Rectangle {
            id: bottomBars
            anchors { bottom: messages.top; left: sidebar.right; right: details.left }
            // From what the field needs, not from the field's own visibility: that is false while
            // this bar is hidden, so the bar would never show again.
            visible: renameHere.active && root.browser.ops.editing
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

        PreviewOverlay {
            anchors.fill: parent
            browser: root.browser
        }

        ClashDialog {
            anchors.fill: parent
            ops: root.browser.ops
        }

        ShortcutsOverlay {
            anchors.fill: parent
            browser: root.browser
        }

        PreferencesPanel {
            anchors.fill: parent
            browser: root.browser
        }

        QuestionDialog {
            anchors.fill: parent
            browser: root.browser
        }

        ChooserDialog {
            anchors.fill: parent
            browser: root.browser
        }

        ConnectDialog {
            anchors.fill: parent
            browser: root.browser
        }

        // A click anywhere outside an open menu closes it. The menu bar stays above it, so its
        // titles still switch between menus.
        MouseArea {
            anchors.fill: parent
            z: 1
            visible: root.browser.actions.menuOpen
            acceptedButtons: Qt.AllButtons
            onPressed: root.browser.actions.closeMenu()
        }

        MenuPopups {
            anchors.fill: parent
            z: 3
            actions: root.browser.actions
            menuBar: menuBar
            keyboardPoint: Qt.point(content.x + content.width / 3, content.y + Theme.rowHeight * 2)
        }
    }

    // The proportional pane widths follow the window's width.
    Binding {
        target: root.browser.panes
        property: "windowWidth"
        value: root.width
    }

    // The automatic pane widths measure names in the rows' font, with what else a row holds:
    // a sidebar row its margins and icon, a column row its margins, icon and folder mark.
    Component.onCompleted: root.browser.panes.setRowFont(
        Qt.font({ pixelSize: Theme.fontNormal }),
        Math.ceil(Theme.spacing * 3.75 + 16 + 1),
        Math.ceil(Theme.spacing * 3.25 + Theme.iconSize + 10))

    // The keys go back to the browser, or to the search field while it is shown.
    function giveKeysBack() {
        if (root.browser.searchShown)
            header.focusSearch()
        else
            keys.forceActiveFocus()
    }

    // Once a name is no longer edited, the keys go back to the browser.
    Connections {
        target: root.browser.ops
        function onEditingChanged() {
            if (!root.browser.ops.editing)
                root.giveKeysBack()
        }
    }
    Connections {
        target: root.browser
        function onLocationEditingChanged() {
            if (!root.browser.locationEditing)
                root.giveKeysBack()
        }
        function onSearchShownChanged() {
            if (!root.browser.searchShown)
                keys.forceActiveFocus()
        }
        function onConnectShownChanged() {
            if (!root.browser.connectShown)
                root.giveKeysBack()
        }
        function onCloseWindowAsked() { root.close() }
    }
    Connections {
        target: root.browser.prefs
        function onEditingChanged() {
            if (root.browser.prefs.editing < 0)
                root.giveKeysBack()
        }
        function onShownChanged() { root.giveKeysBack() }
    }
}
