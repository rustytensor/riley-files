// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick3D

// The graph in 3D: a sphere for each node, a line from each to its parent folder, stars behind.
// Dragging turns the camera round the graph, scrolling brings it nearer or farther, and when
// nothing has moved for a while it turns slowly by itself. The node under the mouse, or else the
// one the keys are on, has its label beside it.
Item {
    id: scene
    objectName: "graphScene"

    required property Browser browser

    // The camera, turned round the folder at the centre.
    property real yaw: 25
    property real pitch: -18
    property real zoom: 1
    readonly property real distance: Math.max(140, nodes.radius * 2.3 + 80) * zoom
    // Seconds since the last drag, scroll or mouse move; the slow turn starts after a few.
    property real idle: 0

    // Where a node is drawn in this item, for the label and for tests.
    function screenPointOf(node) {
        if (!view3d.camera)
            return Qt.point(-1, -1)
        const at = view3d.mapFrom3DScene(nodes.positionOf(node))
        return Qt.point(at.x, at.y)
    }

    View3D {
        id: view3d
        objectName: "graphView3D"
        anchors.fill: parent
        camera: camera

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: Theme.graphSpace
            antialiasingMode: SceneEnvironment.NoAA
        }

        Node {
            eulerRotation: Qt.vector3d(scene.pitch, scene.yaw, 0)

            PerspectiveCamera {
                id: camera
                z: scene.distance
                clipNear: 1
                clipFar: scene.distance + stars.radius * 1.6
                fieldOfView: 50
            }
            // The light comes from behind the camera, so the side seen is lit.
            DirectionalLight {
                eulerRotation.x: -20
                eulerRotation.y: 15
                color: Theme.graphLight
                ambientColor: Theme.mix(Theme.graphSpace, Theme.graphLight, 0.35)
                brightness: 1.1
            }
        }

        Model {
            geometry: GraphStars {
                id: stars
                radius: Math.max(3000, nodes.radius * 6)
                count: 1100
                colour: Theme.graphStar
            }
            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                vertexColorsEnabled: true
                diffuseColor: Theme.graphStar
            }
        }

        Model {
            geometry: GraphEdges { nodes: nodes }
            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                diffuseColor: Theme.graphEdge
            }
        }

        Model {
            source: "#Sphere"
            instancing: GraphInstancing {
                id: nodes
                builder: scene.browser.graph
                folderColor: Theme.folder
                kindColors: Theme.categoryColors
            }
            materials: PrincipledMaterial {
                baseColor: Theme.graphLight
                roughness: 0.55
                metalness: 0
            }
        }

        // A halo round the node the keys are on.
        Model {
            objectName: "graphMark"
            source: "#Sphere"
            visible: scene.browser.graphNode >= 0
            position: { nodes.revision; return nodes.positionOf(scene.browser.graphNode) }
            scale: {
                const s = (nodes.sizeOf(scene.browser.graphNode) * 2.2 + 6) / 100
                return Qt.vector3d(s, s, s)
            }
            materials: DefaultMaterial {
                lighting: DefaultMaterial.NoLighting
                diffuseColor: Theme.graphMark
                opacity: 0.3
            }
        }
    }

    // The label of the node under the mouse, or else of the node the keys are on.
    Rectangle {
        id: label
        objectName: "graphLabel"
        readonly property string text: scene.browser.graphLabel
        readonly property point at: {
            scene.yaw; scene.pitch; scene.distance; nodes.revision; scene.width; scene.height
            return scene.screenPointOf(scene.browser.graphLabelNode)
        }
        visible: text.length > 0
        x: Math.max(0, Math.min(scene.width - width, at.x + Theme.spaceLarge))
        y: Math.max(0, Math.min(scene.height - height, at.y - height / 2))
        width: labelText.implicitWidth + 2 * Theme.spaceMedium
        height: labelText.implicitHeight + Theme.spaceMedium
        radius: Theme.spaceSmall
        color: Theme.withAlpha(Theme.card, 0.88)
        border.color: Theme.cardBorder

        Text {
            id: labelText
            anchors.centerIn: parent
            text: label.text
            color: Theme.text
            font.pixelSize: Theme.fontSmall
        }
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property point last

        onPressed: mouse => { last = Qt.point(mouse.x, mouse.y); scene.idle = 0 }
        onPositionChanged: mouse => {
            scene.idle = 0
            if (pressed) {
                scene.yaw -= (mouse.x - last.x) * 0.4
                scene.pitch = Math.max(-85, Math.min(85, scene.pitch - (mouse.y - last.y) * 0.4))
                last = Qt.point(mouse.x, mouse.y)
            } else {
                scene.browser.hoverGraphNode(nodes.nodeAt(view3d, mouse.x, mouse.y))
            }
        }
        onExited: scene.browser.hoverGraphNode(-1)
        onClicked: mouse => scene.browser.clickGraphNode(nodes.nodeAt(view3d, mouse.x, mouse.y))
        onDoubleClicked: mouse => scene.browser.openGraphNode(nodes.nodeAt(view3d, mouse.x, mouse.y))
    }

    WheelHandler {
        onWheel: event => {
            scene.idle = 0
            scene.zoom = Math.max(0.15, Math.min(4, scene.zoom * Math.pow(0.9985, event.angleDelta.y)))
        }
    }

    // The slow turn, a few degrees a second, once nothing has moved for four seconds.
    FrameAnimation {
        running: scene.visible
        onTriggered: {
            scene.idle += frameTime
            if (scene.idle > 4)
                scene.yaw += frameTime * 4
        }
    }
}
