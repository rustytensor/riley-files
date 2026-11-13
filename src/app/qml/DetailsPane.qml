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

    implicitWidth: browser.panes.details
    color: Theme.sidebar
    clip: true

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Details")

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

            Field { label: qsTr("Kind"); value: pane.details.kind; valueName: "detailsKind" }
            Field {
                label: pane.details.isDir ? qsTr("Holds") : qsTr("Size")
                value: pane.details.sizeText
                valueName: "detailsSize"
            }
            Field { label: qsTr("Dimensions"); value: pane.details.imageSize; valueName: "detailsImageSize" }
            Field { label: qsTr("Modified"); value: pane.details.modifiedText; valueName: "detailsModified" }
            Field { label: qsTr("Permissions"); value: pane.details.permissions; valueName: "detailsPermissions" }

            // The earlier versions, when there are snapshots above it: the newest few, and the
            // whole list (Alt+V) one click away.
            Column {
                id: versionsSection
                objectName: "detailsVersions"

                readonly property VersionModel versions: pane.browser.versions

                width: parent.width
                spacing: 4
                visible: versions.available && versions.path === pane.details.path
                         && pane.details.name.length > 0

                Rectangle { width: parent.width; height: 1; color: Theme.divider }

                Text {
                    text: qsTr("Previous Versions")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }

                Text {
                    objectName: "detailsVersionsCount"
                    width: parent.width
                    text: versionsSection.versions.loading && versionsSection.versions.count === 0 ? qsTr("Looking…")
                          : versionsSection.versions.count === 0 ? qsTr("No earlier versions")
                          : versionsSection.versions.count === 1 ? qsTr("1 earlier version")
                          : qsTr("%1 earlier versions").arg(versionsSection.versions.count)
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                }

                Repeater {
                    objectName: "detailsVersionRows"
                    model: versionsSection.versions

                    delegate: Text {
                        required property int index
                        required property string dateText
                        required property string description

                        width: versionsSection.width
                        visible: index < 5
                        text: description.length > 0 ? dateText + "  ·  " + description : dateText
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Rectangle {
                    objectName: "detailsVersionsButton"
                    width: showText.implicitWidth + Theme.spacing * 3
                    height: Theme.buttonHeight - 6
                    radius: Theme.radius
                    visible: versionsSection.versions.count > 0
                    color: showMouse.containsMouse ? Theme.buttonHover : Theme.button
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Show Versions")
                    Accessible.description: qsTr("Alt+V")
                    Accessible.onPressAction: pane.browser.showVersions()
                    Behavior on color { ColorAnimation { duration: Theme.quick } }

                    Text {
                        id: showText
                        anchors.centerIn: parent
                        text: qsTr("Show Versions  (Alt+V)")
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                    }

                    MouseArea {
                        id: showMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: pane.browser.showVersions()
                    }
                }
            }
        }
    }
}
