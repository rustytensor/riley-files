// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The column on the left: the places (home, the user folders, the trash), the pinned folders,
// shared with GTK file dialogs, the recent files, the drives the person mounted, each with an
// eject mark, and the servers connected (Connect to Server). The
// place shown is marked; with the keys in the sidebar (Tab), its cursor shows too. A section
// with nothing in it is not shown.
Rectangle {
    id: sidebar
    objectName: "sidebar"

    required property Browser browser

    implicitWidth: browser.panes.sidebar
    color: Theme.sidebar

    component Heading: Text {
        leftPadding: Theme.spacing * 1.5
        topPadding: Theme.spacing * 1.25
        bottomPadding: Theme.spacing / 2
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }

    // One row: an icon and a name. `first` is the row's place in the sidebar's cursor.
    component SidebarRow: Rectangle {
        id: row

        required property int index
        required property string name
        required property string path
        required property string icon
        property int first: 0
        property int markedRow: -1
        // Room kept on the right for a mark (the drives' eject).
        property real rightSpace: 0

        readonly property bool marked: index === markedRow
        readonly property bool current: sidebar.browser.sidebarFocus
                                        && sidebar.browser.sidebarCursor === first + index

        width: ListView.view ? ListView.view.width : 0
        height: Theme.rowHeight
        radius: Theme.radius - 2
        color: current ? Theme.cursorRow
                       : marked ? Theme.restingRow
                                : (mouse.containsMouse ? Theme.hoverRow : "transparent")
        border.width: current ? 1 : 0
        border.color: Theme.accent

        // The place shown: a stripe on the left, in the one colour that marks what is chosen.
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 6 }
            width: 2
            radius: 1
            visible: row.marked
            color: Theme.accent
        }

        SidebarIcon {
            id: glyph
            anchors { left: parent.left; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
            icon: row.icon
            tint: row.marked ? Theme.accent : Theme.textMuted
        }

        Text {
            anchors {
                left: glyph.right; leftMargin: Theme.spacing * 0.75
                right: parent.right; rightMargin: Theme.spacing + row.rightSpace
                verticalCenter: parent.verticalCenter
            }
            text: row.name
            elide: Text.ElideRight
            color: row.marked || row.current ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontNormal
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: sidebar.browser.openSidebarRow(row.first + row.index)
        }
    }

    Flickable {
        anchors { fill: parent; rightMargin: 1 }
        contentHeight: column.implicitHeight + Theme.spacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width

            Heading { text: "Places" }

            ListView {
                id: places
                objectName: "places"
                x: Theme.spacing / 2
                width: parent.width - Theme.spacing
                height: contentHeight
                interactive: false
                model: sidebar.browser.places
                currentIndex: sidebar.browser.markedPlace
                highlightFollowsCurrentItem: false
                delegate: SidebarRow {
                    first: 0
                    markedRow: sidebar.browser.markedPlace
                }
            }

            Heading { text: "Pinned" }

            ListView {
                id: pinned
                objectName: "pinned"
                x: Theme.spacing / 2
                width: parent.width - Theme.spacing
                height: contentHeight
                interactive: false
                model: sidebar.browser.pinned
                currentIndex: sidebar.browser.markedPin
                highlightFollowsCurrentItem: false
                delegate: SidebarRow {
                    first: sidebar.browser.places.count
                    markedRow: sidebar.browser.markedPin
                }
            }

            Text {
                visible: sidebar.browser.pinned.count === 0
                width: parent.width
                leftPadding: Theme.spacing * 1.5
                rightPadding: Theme.spacing * 1.5
                text: "Ctrl+D pins the folder you are in."
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }

            // The files used lately: one row, after the pinned folders in the cursor too.
            SidebarRow {
                id: recentRow
                objectName: "recentPlace"
                x: Theme.spacing / 2
                width: parent.width - Theme.spacing
                index: 0
                name: "Recent"
                path: ""
                icon: "recent"
                first: sidebar.browser.places.count + sidebar.browser.pinned.count
                markedRow: sidebar.browser.markedRecent ? 0 : -1
            }

            Heading {
                objectName: "drivesHeading"
                visible: sidebar.browser.drives.count > 0
                text: "Drives"
            }

            ListView {
                id: drives
                objectName: "drives"
                visible: count > 0
                x: Theme.spacing / 2
                width: parent.width - Theme.spacing
                height: visible ? contentHeight : 0
                interactive: false
                model: sidebar.browser.drives
                currentIndex: sidebar.browser.markedDrive
                highlightFollowsCurrentItem: false
                delegate: SidebarRow {
                    id: driveRow
                    first: sidebar.browser.places.count + sidebar.browser.pinned.count + 1
                    markedRow: sidebar.browser.markedDrive
                    rightSpace: eject.width + Theme.spacing / 2

                    Text {
                        id: eject
                        objectName: "eject"
                        anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
                        text: "⏏"
                        color: ejectMouse.containsMouse ? Theme.text : Theme.textFaint
                        font.pixelSize: Theme.fontNormal

                        MouseArea {
                            id: ejectMouse
                            anchors { fill: parent; margins: -4 }
                            hoverEnabled: true
                            onClicked: sidebar.browser.ejectDrive(driveRow.index)
                        }
                    }
                }
            }

            Heading {
                objectName: "networkHeading"
                visible: sidebar.browser.network.count > 0
                text: "Network"
            }

            ListView {
                id: network
                objectName: "network"
                visible: count > 0
                x: Theme.spacing / 2
                width: parent.width - Theme.spacing
                height: visible ? contentHeight : 0
                interactive: false
                model: sidebar.browser.network
                currentIndex: sidebar.browser.markedNetwork
                highlightFollowsCurrentItem: false
                delegate: SidebarRow {
                    first: sidebar.browser.places.count + sidebar.browser.pinned.count + 1
                           + sidebar.browser.drives.count
                    markedRow: sidebar.browser.markedNetwork
                }
            }
        }
    }

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: sidebar.browser.sidebarFocus ? Theme.accentSoft : Theme.divider
    }
}
