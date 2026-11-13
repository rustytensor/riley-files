// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The trash, in the folder's place: everything in the home trash and in each drive's own, with
// the folder it came from and when it was deleted. Restore puts the chosen thing back (Return
// does the same), Delete for Good deletes it after asking (Delete does the same), and Empty
// Trash deletes everything after asking.
Rectangle {
    id: view
    objectName: "trashView"

    required property Browser browser

    color: Theme.content

    component ToolButton: Rectangle {
        id: button

        property string label
        property bool active: true
        signal clicked()

        width: text.implicitWidth + Theme.spacing * 3
        height: Theme.buttonHeight - 6
        radius: Theme.radius
        opacity: active ? 1 : 0.45
        color: mouse.containsMouse && active ? Theme.buttonHover : Theme.button

        Text {
            id: text
            anchors.centerIn: parent
            text: button.label
            color: Theme.text
            font.pixelSize: Theme.fontSmall
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: if (button.active) button.clicked()
        }
    }

    Item {
        id: bar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: Theme.rowHeight + 12

        Text {
            anchors { left: parent.left; leftMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
            text: list.count === 0 ? "The trash is empty."
                                   : list.count === 1 ? "1 item in the trash" : list.count + " items in the trash"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        Row {
            anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
            spacing: 6

            ToolButton {
                objectName: "restoreButton"
                label: "Restore"
                active: view.browser.placeRow >= 0
                onClicked: view.browser.restoreRow(view.browser.placeRow)
            }
            ToolButton {
                objectName: "deleteForGoodButton"
                label: "Delete for Good"
                active: view.browser.placeRow >= 0
                onClicked: view.browser.deleteRowForGood(view.browser.placeRow)
            }
            ToolButton {
                objectName: "emptyTrashButton"
                label: "Empty Trash"
                active: list.count > 0
                onClicked: view.browser.emptyTrash()
            }
        }

        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.divider
        }
    }

    ListView {
        id: list
        objectName: "trash"

        anchors { top: bar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
        clip: true
        model: view.browser.trash
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        delegate: PlaceRow {


            required property int index
            required property string name
            required property string originalFolder
            required property string deletedText
            required property bool isDir

            browser: view.browser
            width: list.width
            rowIndex: index
            title: name
            where: originalFolder
            when: deletedText
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
