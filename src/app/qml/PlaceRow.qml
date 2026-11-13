// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One row of a place that is not a folder (the trash, the recent files): an icon, the name, the
// folder it is from and a date. A click puts the keys on it, a double-click opens it (a recent
// file) or puts it back (a thing in the trash).
Rectangle {
    id: row

    required property Browser browser
    property int rowIndex: -1
    property string title
    property string where
    property string when
    property bool isFolder: false

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
        text: row.where.length > 0 ? "in " + row.where : ""
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
        onClicked: row.browser.clickPlaceRow(row.rowIndex)
        onDoubleClicked: row.browser.openPlaceRow(row.rowIndex)
    }
}
