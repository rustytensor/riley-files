// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// A small drawn icon: one of nine drawings by what the item is (a folder, an image, sound, a
// film, text, code, a document, an archive, anything else), in the Theme's colour for it. Drawn
// from the program's own SVG files, so it looks the same on every machine. An icon the person
// chose for the item (Change Icon) takes its place: `customSource` is its image, from
// Browser.customIconSource.
Item {
    id: icon
    objectName: "fileIcon"

    property string category: "file"
    property int size: Theme.iconSize
    property string customSource: ""
    // The chosen icon is drawn (it is set and could be loaded).
    readonly property bool showsCustom: customSource.length > 0 && custom.status !== Image.Error
    readonly property bool drawn: !showsCustom
    // The colour of the drawing: the folders' own, or the one for what the file holds.
    readonly property color colour: category === "folder" ? Theme.folder : Theme.categoryColor(category)

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

    // The drawing for the kind (FileGlyphs: one SVG for each of the nine kinds), in the kind's
    // colour, drawn at the size it is shown in the screen's pixels.
    Image {
        id: glyph
        objectName: "glyph"
        anchors.fill: parent
        visible: icon.drawn
        source: icon.drawn ? FileGlyphs.source(icon.category, icon.colour) : ""
        sourceSize { width: icon.size; height: icon.size }
        smooth: true
    }
}
