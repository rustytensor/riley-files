// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One tile of the grid view: a large icon with the name below it. A file's thumbnail takes the
// icon's place once it is made (on worker threads; a tile scrolled away drops its request).
Item {
    id: tile

    required property Browser browser
    required property int index
    required property string name
    required property bool hidden
    required property string category
    required property string path
    required property bool isDir
    required property var modified

    readonly property bool current: GridView.isCurrentItem

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.selected: current
    Accessible.onPressAction: browser.clickRow(index)

    Rectangle {
        anchors { fill: parent; margins: 3 }
        radius: Theme.radius
        color: tile.current ? Theme.cursorRow : (mouse.containsMouse ? Theme.hoverRow : Theme.none)
        Behavior on color { ColorAnimation { duration: Theme.quick } }
        border.width: tile.current ? 1 : 0
        border.color: Theme.accent
    }

    FileIcon {
        id: icon
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.spacing }
        size: tile.browser.prefs.thumbnailSize
        category: tile.category
        customSource: tile.browser.customIconSource(tile.path, tile.browser.iconsRevision)
        opacity: tile.hidden ? 0.55 : 1
        visible: !thumbnail.shown
    }

    Image {
        id: thumbnail
        objectName: "thumbnail"

        readonly property bool shown: status === Image.Ready && implicitWidth > 0

        anchors.fill: icon
        // A chosen icon is shown instead of the thumbnail.
        source: icon.customSource.length > 0 ? "" : tile.browser.thumbnailSource(tile.path, tile.isDir, tile.modified)
        sourceSize { width: icon.size * 2; height: icon.size * 2 }
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: tile.hidden ? 0.55 : 1
        visible: shown
    }

    Text {
        anchors {
            top: icon.bottom; topMargin: Theme.spacing / 2
            left: parent.left; right: parent.right; margins: Theme.spacing / 2
        }
        text: tile.name
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        maximumLineCount: 2
        elide: Text.ElideRight
        color: tile.hidden ? Theme.textMuted : Theme.text
        font.pixelSize: Theme.fontSmall
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        // The middle button opens a folder in a new tab, in the background.
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        onClicked: event => {
            if (event.button === Qt.MiddleButton)
                tile.browser.openRowInTab(tile.index)
            else
                tile.browser.clickRow(tile.index)
        }
        onDoubleClicked: event => {
            if (event.button === Qt.LeftButton)
                tile.browser.doubleClickRow(tile.index)
        }
    }
}
