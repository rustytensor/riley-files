// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The list view, laid out as a repository page: one outlined table whose top band sums up the
// folder, then one row per item with its icon, name and how long ago it changed; under it, when
// the folder has a README, a second outlined box shows it. With the tree on, folders open in
// place below their row, inside the table.
Item {
    id: list

    required property Browser browser

    ListView {
        id: view
        objectName: "list"

        anchors {
            fill: parent
            leftMargin: Theme.spacing; rightMargin: Theme.spacing
            topMargin: Theme.spacing
        }
        clip: true
        model: list.browser.model
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        // The browser moves the cursor; the view only follows it.
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: -1

        // The table's top band: what the folder holds.
        header: Rectangle {
            id: band
            objectName: "tableHeader"

            width: view.width
            height: Theme.rowHeight + Theme.spaceMedium
            radius: Theme.radius
            color: Theme.tableHeader

            // Square below, so only the table's top corners are round.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: Theme.radius
                color: Theme.tableHeader
            }
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: 1
                color: Theme.tableBorder
            }

            // In a git repository: the branch, then the latest commit that touched the folder,
            // who made it and how long ago; the folder's summing-up moves to the right.
            readonly property bool inRepository: list.browser.model.inRepository

            Row {
                id: commitLine
                visible: band.inRepository
                anchors {
                    left: parent.left; leftMargin: Theme.spacing * 1.5
                    verticalCenter: parent.verticalCenter
                }
                spacing: Theme.spacing * 1.5
                width: Math.max(0, band.width - Theme.spacing * 4.5 - summaryText.implicitWidth)
                clip: true

                Text {
                    id: branchText
                    objectName: "tableBranch"
                    text: list.browser.model.branch
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                }
                Text {
                    id: commitAuthor
                    objectName: "tableCommitAuthor"
                    text: list.browser.model.latestCommitAuthor
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                }
                Text {
                    objectName: "tableCommit"
                    width: Math.min(implicitWidth, Math.max(0, commitLine.width - branchText.implicitWidth
                                                            - commitAge.implicitWidth - Theme.spacing * 6
                                                            - commitAuthor.implicitWidth))
                    elide: Text.ElideRight
                    text: list.browser.model.latestCommitSubject
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    id: commitAge
                    objectName: "tableCommitAge"
                    text: list.browser.model.latestCommitAgeText
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }

            Text {
                id: summaryText
                objectName: "tableSummary"
                anchors {
                    left: parent.left; leftMargin: Theme.spacing * 1.5
                    right: parent.right; rightMargin: Theme.spacing * 1.5
                    verticalCenter: parent.verticalCenter
                }
                horizontalAlignment: band.inRepository ? Text.AlignRight : Text.AlignLeft
                text: list.browser.model.summary
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                elide: Text.ElideRight
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
            }
        }

        delegate: FileRow {
            browser: list.browser
            width: view.width
        }

        // The README, under the table.
        footer: Item {
            width: view.width
            height: readme.visible ? readme.height + Theme.spaceHuge * 2 : Theme.spacing

            Rectangle {
                id: readme
                objectName: "readme"

                visible: list.browser.readmeName.length > 0
                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: Theme.spaceHuge }
                height: readmeTitle.height + readmeText.height + Theme.spaceHuge * 2
                radius: Theme.radius
                color: Theme.none
                border.color: Theme.tableBorder
                border.width: 1

                Rectangle {
                    id: readmeTitle
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
                    height: Theme.rowHeight + Theme.spaceMedium
                    radius: Theme.radius
                    color: Theme.tableHeader

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: Theme.radius
                        color: Theme.tableHeader
                    }
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 1
                        color: Theme.tableBorder
                    }
                    Text {
                        objectName: "readmeTitle"
                        anchors {
                            left: parent.left; leftMargin: Theme.spacing * 1.5
                            verticalCenter: parent.verticalCenter
                        }
                        text: list.browser.readmeName
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                    }
                }

                TextEdit {
                    id: readmeText
                    objectName: "readmeText"

                    anchors {
                        top: readmeTitle.bottom; topMargin: Theme.spaceLarge
                        left: parent.left; leftMargin: Theme.spaceHuge
                    }
                    width: Math.min(parent.width - Theme.spaceHuge * 2, Theme.readableWidth)
                    readOnly: true
                    // The keys stay with the files; a click on a link still follows it.
                    activeFocusOnPress: false
                    selectByMouse: true
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    text: list.browser.readmeHtml
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    font.pixelSize: Theme.fontNormal

                    Accessible.role: Accessible.Document
                    Accessible.name: list.browser.readmeName

                    onLinkActivated: link => list.browser.openReadmeLink(link)

                    // The pointing hand over a link.
                    HoverHandler {
                        cursorShape: readmeText.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                    }
                }
            }
        }

        function followCursor() {
            if (currentIndex !== list.browser.cursor)
                currentIndex = list.browser.cursor
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        // The view also moves its current row on its own when rows come and go; the browser's
        // cursor always wins.
        onCurrentIndexChanged: if (currentIndex !== list.browser.cursor) currentIndex = list.browser.cursor
        function tellRows() {
            list.browser.pageRows = Math.max(1, Math.floor(height / Theme.rowHeight) - 2)
        }
        onHeightChanged: tellRows()
        // The other views set the page size their own way; the list says its own when it shows.
        onVisibleChanged: if (visible) tellRows()
        Component.onCompleted: followCursor()

        Connections {
            target: list.browser
            function onCursorChanged() { view.followCursor() }
        }

        // The table's outline, over the band and the rows, scrolling with them.
        Rectangle {
            objectName: "repoTable"
            parent: view.contentItem
            z: 10
            x: 0
            y: view.headerItem ? view.headerItem.y : 0
            width: view.width
            height: (view.headerItem ? view.headerItem.height : 0) + view.count * Theme.rowHeight
            radius: Theme.radius
            color: Theme.none
            border.color: Theme.tableBorder
            border.width: 1
        }
    }

    // A right click opens the context menu: for the row under it, or for the folder when it
    // is on the empty space around the rows. Other buttons go on to the rows below.
    MouseArea {
        objectName: "listContextArea"
        anchors.fill: view
        acceptedButtons: Qt.RightButton
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Files in “%1”").arg(list.browser.folderName)
        onPressed: mouse => {
            const row = view.indexAt(mouse.x + view.contentX, mouse.y + view.contentY)
            const at = mapToItem(null, mouse.x, mouse.y)
            list.browser.contextMenuAt(row, at.x, at.y)
        }
    }

    // The name of the cursor's row, being edited in place.
    RenameField {
        readonly property FileRow row: view.currentItem as FileRow

        ops: list.browser.ops
        active: view.visible && row !== null
        x: view.x + (row ? row.textLeft + Theme.iconSize + Theme.spacing - 6 : 0)
        y: view.y + (row ? row.y - view.contentY : 0) + 2
        width: Math.max(Theme.tileWidth, view.width - x + view.x - Theme.dateColumn - Theme.spacing * 3)
        height: Theme.rowHeight - 4
    }

    // Shown when there is nothing to list.
    Text {
        anchors.centerIn: view
        visible: view.count === 0 && !list.browser.model.loading
        width: Math.min(view.width - Theme.spacing * 4, 420)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: list.browser.model.error.length > 0 ? list.browser.model.error : qsTr("This folder is empty.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
    }
}
