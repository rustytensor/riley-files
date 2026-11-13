// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One result of a search: the name and the folder it is in; for a search inside files, also the
// line that matched, with the match marked.
Rectangle {
    id: row

    required property Browser browser
    required property int index
    required property string name
    required property string path
    required property string place
    required property bool isDir
    required property int line
    required property string text
    required property var matches

    readonly property bool current: ListView.isCurrentItem
    readonly property bool inside: line > 0
    // The folder it is in, below the folder searched; the folder searched itself by its name.
    readonly property string where: place.length > 0 ? place : browser.folderName

    height: inside ? Theme.rowHeight * 2 : Theme.rowHeight
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
        y: (Theme.rowHeight - height) / 2
        category: row.isDir ? "folder" : "file"
    }

    Text {
        id: nameText
        objectName: "resultName"
        anchors { left: icon.right; leftMargin: Theme.spacing }
        y: 0
        height: Theme.rowHeight
        width: Math.min(implicitWidth, row.width / 2)
        verticalAlignment: Text.AlignVCenter
        text: row.name
        textFormat: Text.PlainText
        elide: Text.ElideMiddle
        color: Theme.text
        font.pixelSize: Theme.fontNormal
    }

    Text {
        objectName: "resultFolder"
        anchors {
            left: nameText.right; leftMargin: Theme.spacing
            right: parent.right; rightMargin: Theme.spacing * 1.5
        }
        height: Theme.rowHeight
        verticalAlignment: Text.AlignVCenter
        text: "in " + row.where
        textFormat: Text.PlainText
        elide: Text.ElideMiddle
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
    }

    // The matching line, below the name.
    Text {
        id: lineNumber
        visible: row.inside
        anchors { left: icon.right; leftMargin: Theme.spacing }
        y: Theme.rowHeight - 4
        height: Theme.rowHeight
        verticalAlignment: Text.AlignVCenter
        text: row.line + ":"
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
    }

    Text {
        objectName: "resultLine"
        visible: row.inside
        anchors {
            left: lineNumber.right; leftMargin: Theme.spacing / 2
            right: parent.right; rightMargin: Theme.spacing * 1.5
        }
        y: Theme.rowHeight - 4
        height: Theme.rowHeight
        verticalAlignment: Text.AlignVCenter
        // Escaped and marked by the browser: the file's text is never read as markup.
        text: row.inside ? row.browser.markMatches(row.text, row.matches, Theme.accent.toString()) : ""
        textFormat: Text.StyledText
        elide: Text.ElideRight
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: row.browser.clickSearchRow(row.index)
        onDoubleClicked: row.browser.openSearchRow(row.index)
    }
}
