// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The searchable list in front of the window (Change Icon, Other Application): what is typed,
// and the rows it matches. The keys go to the browser, which narrows and chooses; a click on a
// row chooses it, a click outside closes.
Rectangle {
    id: dialog
    objectName: "chooser"

    required property Browser browser
    readonly property Chooser chooser: browser.chooser

    visible: chooser.shown
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: chooser.title

    // A click outside the card closes it, as Escape does; it is not a control.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        Accessible.ignored: true
        onClicked: dialog.chooser.close()
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(Theme.dialogWidth + 80, dialog.width - Theme.spacing * 4)
        height: Math.min(dialog.height - Theme.spacing * 4, 460)
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        // Clicks on the card stay on it.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
        }

        Text {
            id: title
            objectName: "chooserTitle"
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.spacing * 1.5 }
            text: dialog.chooser.title
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: search
            anchors {
                left: parent.left; right: parent.right; top: title.bottom
                leftMargin: Theme.spacing * 1.5; rightMargin: Theme.spacing * 1.5; topMargin: Theme.spacing
            }
            height: Theme.rowHeight
            radius: Theme.radius
            color: Theme.window
            border.color: Theme.accent
            border.width: 1

            Text {
                objectName: "chooserQuery"
                anchors { fill: parent; leftMargin: Theme.spacing; rightMargin: Theme.spacing }
                verticalAlignment: Text.AlignVCenter
                text: dialog.chooser.query.length > 0 ? dialog.chooser.query : dialog.chooser.hint
                color: dialog.chooser.query.length > 0 ? Theme.text : Theme.textMuted
                elide: Text.ElideLeft
                font.pixelSize: Theme.fontNormal
            }
        }

        ListView {
            id: list
            objectName: "chooserList"
            anchors {
                left: parent.left; right: parent.right; top: search.bottom; bottom: parent.bottom
                margins: Theme.spacing * 1.5; topMargin: Theme.spacing
            }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.chooser.rows
            currentIndex: dialog.chooser.cursor
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: Rectangle {
                id: row
                objectName: "chooserRow"

                required property var modelData
                required property int index
                readonly property string label: modelData.label
                readonly property bool current: index === dialog.chooser.cursor

                Accessible.role: Accessible.ListItem
                Accessible.name: label
                Accessible.description: modelData.detail ?? ""
                Accessible.selected: current
                Accessible.onPressAction: dialog.chooser.choose(index)

                width: list.width
                height: Theme.rowHeight
                radius: Theme.radius
                color: current ? Theme.cursorRow : (mouse.containsMouse ? Theme.hoverRow : Theme.none)
                Behavior on color { ColorAnimation { duration: Theme.quick } }

                Image {
                    id: picture
                    x: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSize
                    height: Theme.iconSize
                    source: row.modelData.icon
                    sourceSize { width: Theme.iconSize * 2; height: Theme.iconSize * 2 }
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    visible: source.toString().length > 0
                }

                Text {
                    anchors {
                        left: picture.right; leftMargin: Theme.spacing
                        right: detail.left; rightMargin: Theme.spacing
                        verticalCenter: parent.verticalCenter
                    }
                    text: row.label
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                }

                Text {
                    id: detail
                    anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
                    width: Math.min(implicitWidth, parent.width / 2)
                    text: row.modelData.detail
                    elide: Text.ElideMiddle
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: dialog.chooser.choose(row.index)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: dialog.chooser.loading ? qsTr("Looking…") : qsTr("Nothing matches")
                color: Theme.textMuted
                font.pixelSize: Theme.fontNormal
            }
        }
    }
}
