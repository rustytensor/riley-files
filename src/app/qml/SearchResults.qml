// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The results of a search, in the folder's place: one row per name found, or per matching line
// inside a file, each with the folder it is in. A line along the top says how the search goes.
Rectangle {
    id: results

    required property Browser browser

    color: Theme.content

    Item {
        id: status
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Theme.rowHeight

        Text {
            objectName: "searchStatus"
            anchors {
                left: parent.left; leftMargin: Theme.spacing * 1.5
                right: parent.right; rightMargin: Theme.spacing * 1.5
                verticalCenter: parent.verticalCenter
            }
            text: results.browser.searchStatus
            elide: Text.ElideRight
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.divider
        }
    }

    ListView {
        id: view
        objectName: "searchResults"

        anchors { top: status.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true
        model: results.browser.search
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: SearchRow {
            browser: results.browser
            width: view.width
        }

        function followCursor() {
            if (currentIndex !== results.browser.searchCursor)
                currentIndex = results.browser.searchCursor
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        onCurrentIndexChanged: if (currentIndex !== results.browser.searchCursor) currentIndex = results.browser.searchCursor
        onHeightChanged: if (visible) results.browser.pageRows = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
        Component.onCompleted: followCursor()

        Connections {
            target: results.browser
            function onSearchCursorChanged() { view.followCursor() }
        }
    }

    // The search's own message (rg missing) is said in the status line; this is for nothing found.
    Text {
        anchors.centerIn: view
        visible: view.count === 0 && results.browser.searchText.length > 0 && !results.browser.search.running
                 && results.browser.search.message.length === 0
        text: "Nothing matches “" + results.browser.searchText + "”."
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
    }
}
