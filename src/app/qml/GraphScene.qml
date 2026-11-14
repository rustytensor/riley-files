// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The graph as servorra.com draws its own: stars, the tree's edges, the nodes coloured by
// top-level folder, pulses along the edges. Dragging turns it (and it keeps turning a little
// after), the wheel zooms, names show when zoomed in, and when nothing has moved for a while it
// turns slowly by itself. The node under the mouse, or else the one the keys are on, has its
// label beside it. "Watch it grow" (or G) replays how the tree came to be. What is drawn is
// decided in C++ (GraphCanvas); this places it and gives it the time.
Item {
    id: scene
    objectName: "graphScene"

    required property Browser browser
    readonly property var colours: scene.browser.graphPalette

    // Where a node is drawn in this item, for the label and for tests.
    function screenPointOf(node) {
        return canvas.screenPointOf(node)
    }

    GraphCanvas {
        id: canvas
        objectName: "graphCanvas"
        anchors.fill: parent
        builder: scene.browser.graph
        selected: scene.browser.graphNode
        tooltipNode: label.text.length > 0 ? scene.browser.graphLabelNode : -1
        tooltipSize: Qt.size(label.width, label.height)
        onHovered: node => scene.browser.hoverGraphNode(node)
        onClicked: node => scene.browser.clickGraphNode(node)
        onActivated: node => scene.browser.openGraphNode(node)
    }

    FrameAnimation {
        running: scene.visible
        onTriggered: canvas.advance(frameTime * 1000)
    }

    Connections {
        target: scene.browser
        function onGraphGrowAsked() { canvas.watchItGrow() }
    }

    // The label of the node under the mouse, or else of the node the keys are on.
    Rectangle {
        id: label
        objectName: "graphLabel"
        readonly property string text: scene.browser.graphLabel
        // Placed by the canvas, which keeps the labels clear of it.
        readonly property rect box: canvas.tooltipRect
        visible: text.length > 0
        x: box.x
        y: box.y
        width: labelText.implicitWidth + 2 * Theme.spaceMedium
        height: labelText.implicitHeight + Theme.spaceMedium
        radius: Theme.spaceSmall
        color: Qt.alpha(scene.colours.panel, 0.88)
        border.color: Qt.alpha(scene.colours.accent, 0.35)

        Text {
            id: labelText
            anchors.centerIn: parent
            text: label.text
            color: scene.colours.text
            font.family: "monospace"
            font.pixelSize: 11
        }
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // "Watch it grow", and the month it is at while it runs.
    Row {
        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.spaceLarge }
        spacing: Theme.spaceLarge

        Rectangle {
            id: grow
            objectName: "graphGrow"
            width: growText.implicitWidth + 2 * Theme.spaceLarge
            height: growText.implicitHeight + 2 * Theme.spaceMedium
            radius: height / 2
            color: Qt.alpha(scene.colours.panel, growMouse.containsMouse ? 0.95 : 0.75)
            border.color: Qt.alpha(scene.colours.accent, growMouse.containsMouse ? 0.8 : 0.45)

            Text {
                id: growText
                anchors.centerIn: parent
                text: qsTr("Watch it grow")
                color: scene.colours.text
                font.family: "monospace"
                font.pixelSize: 12
            }
            MouseArea {
                id: growMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: canvas.watchItGrow()
            }
            Accessible.role: Accessible.Button
            Accessible.name: growText.text
            Accessible.onPressAction: canvas.watchItGrow()
        }

        Text {
            objectName: "graphGrowthCaption"
            anchors.verticalCenter: grow.verticalCenter
            visible: canvas.growing
            text: canvas.growthCaption
            color: scene.colours.accent
            font.family: "monospace"
            font.pixelSize: 14
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
