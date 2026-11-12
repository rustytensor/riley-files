// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The list view: one row per item, with its name, size and date. With the tree on, folders open
// in place below their row.
Item {
    id: list

    required property Browser browser

    // Column titles.
    Item {
        id: titles
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Theme.rowHeight

        component Title: Text {
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
            height: parent.height
        }

        Title {
            anchors { left: parent.left; leftMargin: Theme.spacing * 1.5 + Theme.iconSize + Theme.spacing }
            text: "Name"
        }
        Title {
            anchors { right: parent.right; rightMargin: Theme.spacing * 1.5 + Theme.dateColumn + Theme.spacing }
            text: "Size"
        }
        Title {
            anchors { right: parent.right; rightMargin: Theme.spacing * 1.5 }
            width: Theme.dateColumn
            text: "Modified"
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.divider
        }
    }

    ListView {
        id: view
        objectName: "list"

        anchors { top: titles.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true
        model: list.browser.model
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        // The browser moves the cursor; the view only follows it.
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: FileRow {
            browser: list.browser
            width: view.width
        }

        function followCursor() {
            if (currentIndex !== list.browser.cursor)
                currentIndex = list.browser.cursor
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        // The view also moves its current row on its own when rows come and go; the browser's
        // cursor always wins.
        onCurrentIndexChanged: if (currentIndex !== list.browser.cursor) currentIndex = list.browser.cursor
        function tellRows() {
            list.browser.pageRows = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
        }
        onHeightChanged: tellRows()
        // The other views set the page size their own way; the list says its own when it shows.
        onVisibleChanged: if (visible) tellRows()
        Component.onCompleted: followCursor()

        Connections {
            target: list.browser
            function onCursorChanged() { view.followCursor() }
        }
    }

    // Shown when there is nothing to list.
    Text {
        anchors.centerIn: view
        visible: view.count === 0 && !list.browser.model.loading
        width: Math.min(view.width - Theme.spacing * 4, 420)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: list.browser.model.error.length > 0 ? list.browser.model.error : "This folder is empty."
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
    }
}
