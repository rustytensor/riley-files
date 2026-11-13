// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The files used lately, in the folder's place, newest first: the ones every program records
// in the shared bookmark file, as long as they still exist. Return opens one.
Rectangle {
    id: view
    objectName: "recentView"

    required property Browser browser

    color: Theme.content

    Item {
        id: bar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Theme.rowHeight

        Text {
            anchors { left: parent.left; leftMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
            text: list.count === 0 ? "No files were used lately." : "Used lately, newest first"
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
        id: list
        objectName: "recentFiles"

        anchors { top: bar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true
        model: view.browser.recent
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: PlaceRow {
            required property int index
            required property string name
            required property string folder
            required property string modifiedText
            required property bool isDir

            browser: view.browser
            width: list.width
            rowIndex: index
            title: name
            where: folder
            when: modifiedText
            isFolder: isDir
        }

        function followCursor() {
            if (currentIndex !== view.browser.placeRow)
                currentIndex = view.browser.placeRow
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        onCountChanged: followCursor()
        onCurrentIndexChanged: if (currentIndex !== view.browser.placeRow) currentIndex = view.browser.placeRow
        onHeightChanged: if (visible) view.browser.pageRows = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
        Component.onCompleted: followCursor()

        Connections {
            target: view.browser
            function onPlaceRowChanged() { list.followCursor() }
        }
    }
}
