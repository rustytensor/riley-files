// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The graph view (Ctrl+4): the folder shown and everything below it, as a galaxy of nodes in 3D.
// The browser builds the graph and takes the keys; this shows it. Where 3D graphics cannot start
// (the window is drawn in software), it says so, and the other views go on working.
Rectangle {
    id: view
    objectName: "graph"

    required property Browser browser
    readonly property bool canDraw: view.browser.graphCanDraw(view.Window.window)

    color: Theme.graphSpace

    Loader {
        id: scene
        anchors.fill: parent
        active: view.canDraw
        sourceComponent: Component {
            GraphScene { browser: view.browser }
        }
    }

    // What the graph leaves out, when the tree is too large for its files.
    Text {
        objectName: "graphNote"
        anchors { left: parent.left; top: parent.top; margins: Theme.spaceLarge }
        visible: view.canDraw && text.length > 0
        text: view.browser.graph.note
        color: Theme.graphTextMuted
        font.pixelSize: Theme.fontSmall
    }

    Text {
        objectName: "graphNo3D"
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spaceHuge
        visible: !view.canDraw
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("The graph needs 3D graphics.")
        color: Theme.graphText
        font.pixelSize: Theme.fontLarge
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
