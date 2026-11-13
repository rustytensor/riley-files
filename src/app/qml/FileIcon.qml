// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// A small drawn icon: a folder, or a page tinted by what the file holds. Drawn rather than
// taken from a font, so it looks the same on every machine. An icon the person chose for the
// item (Change Icon) takes its place: `customSource` is its image, from Browser.customIconSource.
Item {
    id: icon
    objectName: "fileIcon"

    property string category: "file"
    property int size: Theme.iconSize
    property string customSource: ""
    // The chosen icon is drawn (it is set and could be loaded).
    readonly property bool showsCustom: customSource.length > 0 && custom.status !== Image.Error
    readonly property bool drawn: !showsCustom

    width: size
    height: size

    Image {
        id: custom
        objectName: "customIcon"
        anchors.fill: parent
        visible: icon.showsCustom
        source: icon.customSource
        sourceSize { width: icon.size * 2; height: icon.size * 2 }
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    // Folder: a tab and a body.
    Rectangle {
        visible: icon.drawn && icon.category === "folder"
        x: 0; y: icon.size * 0.12
        width: icon.size * 0.45; height: icon.size * 0.25
        radius: 2
        color: Theme.folderTab
    }
    Rectangle {
        visible: icon.drawn && icon.category === "folder"
        x: 0; y: icon.size * 0.22
        width: icon.size; height: icon.size * 0.68
        radius: 3
        color: Theme.folder
    }

    // File: a page with a darker band along its top.
    Rectangle {
        id: page
        visible: icon.drawn && icon.category !== "folder"
        x: icon.size * 0.14
        width: icon.size * 0.72; height: icon.size
        radius: 3
        color: Theme.categoryColor(icon.category)

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: icon.size * 0.14 }
            height: Math.max(2, icon.size * 0.1)
            radius: 1
            color: Theme.window
            opacity: 0.35
        }
    }
}
