// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One row of the list view's table: the icon, the name, and on the right how long ago it changed.
// In a git repository, the subject of the last commit that touched it sits between the two.
Rectangle {
    id: row

    required property Browser browser
    required property int index
    required property string name
    required property bool isDir
    required property bool hidden
    required property string sizeText
    required property string modifiedText
    required property string ageText
    required property string commitSubject
    required property string category
    required property int depth
    required property bool open
    required property string lines
    required property string path
    required property var modified

    readonly property bool current: ListView.isCurrentItem
    // How far the row is pushed in by the tree; 0 with the tree off.
    readonly property int indent: browser.tree ? depth * Theme.indent : 0
    readonly property int textLeft: Theme.spacing * 1.5 + indent + (browser.tree ? Theme.indent : 0)
    // The commit column shows in a repository, at the same place on every row.
    readonly property bool inRepository: browser.model.inRepository

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: (isDir ? [commitSubject, ageText] : [sizeText, commitSubject, ageText])
                                .filter(s => s.length > 0).join(", ")
    Accessible.selected: current
    Accessible.onPressAction: browser.clickRow(index)

    height: Theme.rowHeight
    color: current ? Theme.cursorRow : (mouse.containsMouse ? Theme.hoverRow : Theme.none)
    Behavior on color { ColorAnimation { duration: Theme.quick } }

    // The cursor's left edge, in the one colour that marks what is chosen.
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 2
        visible: row.current
        color: Theme.accent
    }

    TreeGuide {
        visible: row.browser.tree && row.depth > 0 && row.browser.prefs.treeLines
        x: Theme.spacing * 1.5
        height: parent.height
        lines: row.lines
    }

    // The tree's open and close mark for folders.
    Text {
        id: disclosure
        visible: row.browser.tree && row.isDir
        x: Theme.spacing * 1.5 + row.indent
        width: Theme.indent
        height: parent.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: row.open ? "▾" : "▸"
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall

        Accessible.role: Accessible.Button
        Accessible.name: row.open ? qsTr("Close “%1”").arg(row.name) : qsTr("Open “%1” in place").arg(row.name)
        Accessible.onPressAction: {
            row.browser.cursor = row.index
            row.browser.model.toggleOpen(row.index)
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                row.browser.cursor = row.index
                row.browser.model.toggleOpen(row.index)
            }
        }
    }

    FileIcon {
        id: icon
        x: row.textLeft
        anchors.verticalCenter: parent.verticalCenter
        category: row.category
        customSource: row.browser.customIconSource(row.path, row.browser.iconsRevision)
        opacity: row.hidden ? 0.55 : 1
        visible: !thumbnail.shown
    }

    // A file's small thumbnail takes the icon's place once it is made.
    Image {
        id: thumbnail
        objectName: "thumbnail"

        readonly property bool shown: status === Image.Ready && implicitWidth > 0

        anchors.fill: icon
        // A chosen icon is shown instead of the thumbnail.
        source: icon.customSource.length > 0 ? "" : row.browser.thumbnailSource(row.path, row.isDir, row.modified)
        sourceSize { width: Theme.iconSize * 2; height: Theme.iconSize * 2 }
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: row.hidden ? 0.55 : 1
        visible: shown
    }

    Text {
        anchors {
            left: icon.right; leftMargin: Theme.spacing
            right: row.inRepository ? commit.left : date.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        text: row.name
        // While the name is edited, the field over it shows it instead.
        visible: !(row.current && row.browser.ops.editing)
        elide: Text.ElideMiddle
        color: row.hidden ? Theme.textMuted : Theme.text
        font.pixelSize: Theme.fontNormal
    }

    Text {
        id: commit
        objectName: "commit"
        visible: row.inRepository
        x: Math.round(Math.max(row.width * 0.38, Theme.spacing * 1.5 + Theme.tileWidth + Theme.iconSize))
        width: Math.max(0, date.x - Theme.spacing - x)
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.commitSubject
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    Text {
        id: date
        objectName: "age"
        anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        width: Theme.dateColumn
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        text: row.ageText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    MouseArea {
        id: mouse
        anchors { fill: parent; leftMargin: disclosure.visible ? disclosure.x + disclosure.width : 0 }
        hoverEnabled: true
        onClicked: row.browser.clickRow(row.index)
        onDoubleClicked: row.browser.doubleClickRow(row.index)
    }
}
