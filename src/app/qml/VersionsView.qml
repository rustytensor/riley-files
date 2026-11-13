// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The earlier versions of a file or folder, in the folder's place, newest first: each with the
// date of its snapshot and the snapshot's description. Return restores the one chosen after
// asking (a folder's is opened, to look through it read-only), its context menu offers Restore
// as Copy, and Escape closes the list.
Rectangle {
    id: view
    objectName: "versionsView"

    required property Browser browser
    readonly property VersionModel versions: browser.versions
    readonly property string ofName: {
        const path = browser.versionsOf
        return path.substring(path.lastIndexOf("/") + 1)
    }

    color: Theme.content

    Item {
        id: bar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Theme.rowHeight + 12

        Text {
            objectName: "versionsTitle"
            anchors {
                left: parent.left; leftMargin: Theme.spacing * 1.5
                right: hint.left; rightMargin: Theme.spacing
                verticalCenter: parent.verticalCenter
            }
            text: view.versions.loading && list.count === 0 ? "Looking for earlier versions of “" + view.ofName + "”…"
                  : list.count === 0 ? "There are no earlier versions of “" + view.ofName + "”."
                  : "Earlier versions of “" + view.ofName + "”, newest first"
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        Text {
            id: hint
            anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
            text: "Return restores  ·  Esc closes"
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
        objectName: "versions"

        anchors { top: bar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true
        model: view.versions
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: PlaceRow {
            id: version

            required property int index
            required property string dateText
            required property string description
            required property bool isDir
            // What the row says first, as the other views name their rows.
            readonly property string name: dateText

            browser: view.browser
            width: list.width
            rowIndex: index
            title: dateText
            where: description
            wherePrefix: ""
            when: isDir ? "folder" : ""
            isFolder: isDir
            onMenuAsked: (x, y) => version.browser.versionMenuAt(version.index, x, y)
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
