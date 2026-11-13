// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The pane on the right (Ctrl+I, or the button in the header): what the row under the cursor
// is. A picture shows itself at the top; everything else its icon.
Rectangle {
    id: pane
    objectName: "details"

    required property Browser browser
    readonly property FileDetails details: browser.details

    implicitWidth: Theme.detailsWidth
    color: Theme.sidebar
    clip: true

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
        width: 1
        color: Theme.divider
    }

    component Field: Column {
        id: field

        property string label
        property string value
        property alias valueName: valueText.objectName

        width: parent ? parent.width : 0
        spacing: 2
        visible: value.length > 0

        Text {
            text: field.label
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
        Text {
            id: valueText
            width: parent.width
            text: field.value
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
    }

    Flickable {
        anchors { fill: parent; leftMargin: 1 }
        contentHeight: content.implicitHeight + Theme.spacing * 3
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            x: Theme.spacing * 1.5
            y: Theme.spacing * 1.5
            width: parent.width - Theme.spacing * 3
            spacing: Theme.spacing

            // A picture, or the icon of what it is.
            Item {
                width: parent.width
                height: 150
                visible: pane.details.name.length > 0

                Image {
                    id: picture
                    anchors.centerIn: parent
                    width: Math.min(parent.width, implicitWidth)
                    height: Math.min(parent.height, implicitHeight)
                    source: pane.details.imageSource
                    sourceSize { width: 2 * parent.width; height: 2 * parent.height }
                    asynchronous: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    visible: status === Image.Ready
                }

                FileIcon {
                    anchors.centerIn: parent
                    visible: !picture.visible
                    size: 64
                    category: pane.details.category
                }
            }

            Text {
                objectName: "detailsName"
                width: parent.width
                text: pane.details.name
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.weight: Font.DemiBold
            }

            Rectangle { width: parent.width; height: 1; color: Theme.divider }

            Field { label: "Kind"; value: pane.details.kind; valueName: "detailsKind" }
            Field {
                label: pane.details.isDir ? "Holds" : "Size"
                value: pane.details.sizeText
                valueName: "detailsSize"
            }
            Field { label: "Dimensions"; value: pane.details.imageSize; valueName: "detailsImageSize" }
            Field { label: "Modified"; value: pane.details.modifiedText; valueName: "detailsModified" }
            Field { label: "Permissions"; value: pane.details.permissions; valueName: "detailsPermissions" }
        }
    }
}
