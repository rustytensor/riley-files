// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The grid view: a tile per item, in rows as wide as the window allows. The browser moves the
// cursor; the grid tells it how many tiles fit in a row and how many rows fit on a page.
Item {
    id: grid

    required property Browser browser

    GridView {
        id: view
        objectName: "grid"

        anchors { fill: parent; leftMargin: Theme.spacing; topMargin: Theme.spacing / 2 }
        clip: true
        model: grid.browser.model
        cellWidth: Theme.tileWidth
        cellHeight: Theme.tileHeight
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        // The browser moves the cursor; the view only follows it.
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: FileTile {
            browser: grid.browser
            width: view.cellWidth
            height: view.cellHeight
        }

        function followCursor() {
            if (currentIndex !== grid.browser.cursor)
                currentIndex = grid.browser.cursor
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, GridView.Contain)
        }

        function tellSize() {
            grid.browser.gridColumns = Math.max(1, Math.floor(width / cellWidth))
            grid.browser.pageRows = Math.max(1, Math.floor(height / cellHeight))
        }

        onCurrentIndexChanged: if (currentIndex !== grid.browser.cursor) currentIndex = grid.browser.cursor
        onWidthChanged: tellSize()
        onHeightChanged: tellSize()
        Component.onCompleted: {
            tellSize()
            followCursor()
        }

        Connections {
            target: grid.browser
            function onCursorChanged() { view.followCursor() }
        }
    }

    // Shown when there is nothing to show.
    Text {
        anchors.centerIn: parent
        visible: view.count === 0 && !grid.browser.model.loading
        width: Math.min(parent.width - Theme.spacing * 4, 420)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: grid.browser.model.error.length > 0 ? grid.browser.model.error : "This folder is empty."
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
    }
}
