// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// One column of the columns view: a folder's rows, with the column's cursor.
Item {
    id: pane

    required property Browser browser
    required property FolderColumn modelData
    required property int index

    readonly property bool active: browser.activeColumn === index

    width: browser.panes.column

    ListView {
        id: view
        objectName: "column" + (pane.index + 1)

        anchors { fill: parent; rightMargin: 1 }
        clip: true
        model: pane.modelData.model
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: ColumnRow {
            browser: pane.browser
            active: pane.active
            width: view.width
        }

        function followCursor() {
            if (currentIndex !== pane.modelData.cursor)
                currentIndex = pane.modelData.cursor
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        onCurrentIndexChanged: if (currentIndex !== pane.modelData.cursor) currentIndex = pane.modelData.cursor
        onHeightChanged: if (pane.active) pane.browser.pageRows = Math.max(1, Math.floor(height / Theme.rowHeight) - 1)
        Component.onCompleted: followCursor()

        Connections {
            target: pane.modelData
            function onCursorChanged() { view.followCursor() }
        }
    }

    Text {
        anchors.centerIn: view
        visible: view.count === 0 && !pane.modelData.model.loading
        width: view.width - Theme.spacing * 2
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: pane.modelData.model.error.length > 0 ? pane.modelData.model.error : qsTr("Empty")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    Rectangle {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 1
        color: Theme.divider
    }

    PaneDivider {
        objectName: "columnDivider" + (pane.index + 1)
        panes: pane.browser.panes
        pane: "column"
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
    }
}
