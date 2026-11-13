// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One row of a place that is not a folder (the trash, the recent files): an icon, the name, the
// folder it is from and a date. A click puts the keys on it, a double-click opens it (a recent
// file) or puts it back (a thing in the trash); a right click asks for the place's own menu,
// where it has one (the previous versions).
Rectangle {
    id: row

    required property Browser browser
    property int rowIndex: -1
    property string title
    property string where
    // What comes before `where`: "in " for a folder; nothing for a version's description.
    property string wherePrefix: "in "
    property string when
    property bool isFolder: false

    signal menuAsked(real x, real y)

    readonly property bool current: browser.placeRow === rowIndex

    height: Theme.rowHeight
    color: current ? Theme.cursorRow : (mouse.containsMouse ? Theme.hoverRow : "transparent")

    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 2
        visible: row.current
        color: Theme.accent
    }

    FileIcon {
        id: icon
        x: Theme.spacing * 1.5
        anchors.verticalCenter: parent.verticalCenter
        category: row.isFolder ? "folder" : "file"
    }

    Text {
        id: nameText
        anchors { left: icon.right; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        width: Math.min(implicitWidth, row.width * 0.4)
        text: row.title
        textFormat: Text.PlainText
        elide: Text.ElideMiddle
        color: Theme.text
        font.pixelSize: Theme.fontNormal
    }

    Text {
        anchors {
            left: nameText.right; leftMargin: Theme.spacing
            right: date.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        text: row.where.length > 0 ? row.wherePrefix + row.where : ""
        textFormat: Text.PlainText
        elide: Text.ElideMiddle
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
    }

    Text {
        id: date
        anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        text: row.when
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: event => {
            if (event.button === Qt.RightButton) {
                const at = row.mapToItem(null, event.x, event.y)
                row.menuAsked(at.x, at.y)
            } else {
                row.browser.clickPlaceRow(row.rowIndex)
            }
        }
        onDoubleClicked: event => {
            if (event.button === Qt.LeftButton)
                row.browser.openPlaceRow(row.rowIndex)
        }
    }
}
