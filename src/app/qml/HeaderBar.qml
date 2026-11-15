// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The strip along the top: back, forward and up, then the folder's name and where it is.
Rectangle {
    id: bar

    required property Browser browser

    implicitHeight: Theme.headerHeight
    color: Theme.header

    component NavButton: Rectangle {
        id: button

        property string glyph
        property string name
        property bool active: true
        signal clicked()

        Accessible.role: Accessible.Button
        Accessible.name: button.name
        Accessible.onPressAction: if (button.active) button.clicked()

        width: 30
        height: 30
        radius: Theme.radius
        color: mouse.containsMouse && button.active ? Theme.hoverRow : Theme.none
        Behavior on color { ColorAnimation { duration: Theme.quick } }

        Text {
            anchors.centerIn: parent
            text: button.glyph
            color: button.active ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontLarge
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: button.active
            onClicked: button.clicked()
        }
    }

    Row {
        id: nav
        anchors { left: parent.left; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        spacing: 2

        NavButton {
            glyph: "‹"; name: qsTr("Back"); active: bar.browser.canGoBack; onClicked: bar.browser.goBack()
        }
        NavButton {
            glyph: "›"; name: qsTr("Forward"); active: bar.browser.canGoForward; onClicked: bar.browser.goForward()
        }
        NavButton {
            glyph: "↑"; name: qsTr("Up to the folder that holds this one"); active: bar.browser.canGoUp
            onClicked: bar.browser.goUp()
        }
    }

    // Gives the typing to the search field, when it is shown.
    function focusSearch() { search.forceActiveFocus() }

    // A place that is not a folder (the trash, the recent files) is named.
    Text {
        id: title
        visible: !bar.browser.locationEditing && !bar.browser.searchShown && bar.browser.place.length > 0
        anchors { left: nav.right; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        text: bar.browser.folderName.length > 0 ? bar.browser.folderName : qsTr("Files")
        Accessible.role: Accessible.Heading
        Accessible.name: text
        color: Theme.text
        font.pixelSize: Theme.fontLarge
        font.weight: Font.DemiBold
    }

    // A folder's path, as parts to click: "Home / Projects / site". The last part is the folder
    // shown. When the parts do not fit, the first ones go out of sight on the left.
    Item {
        id: crumbBox
        objectName: "breadcrumb"

        visible: !bar.browser.locationEditing && !bar.browser.searchShown && bar.browser.place.length === 0
        anchors {
            left: nav.right; leftMargin: Theme.spacing
            right: tools.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        height: Theme.rowHeight
        clip: true

        // The empty part of the path bar, right of the last part: a click there types the
        // location instead (as Ctrl+L). The parts lie over it and keep their own clicks.
        MouseArea {
            objectName: "pathBarEmpty"
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Type a location")
            Accessible.onPressAction: bar.browser.editLocation()
            onClicked: bar.browser.editLocation()
        }

        Row {
            id: crumbs
            x: Math.min(0, crumbBox.width - width)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            Repeater {
                model: bar.browser.crumbs

                Row {
                    id: crumb

                    required property var modelData
                    required property int index
                    readonly property bool last: index === bar.browser.crumbs.length - 1


                    // Between two parts; none after the root, which is a "/" itself.
                    Text {
                        visible: crumb.index > 0 && bar.browser.crumbs[crumb.index - 1].path !== "/"
                        anchors.verticalCenter: parent.verticalCenter
                        text: " / "
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontLarge
                    }

                    Text {
                        id: part
                        objectName: "crumb" + crumb.index
                        anchors.verticalCenter: parent.verticalCenter
                        text: crumb.modelData.name
                        color: crumb.last ? Theme.text : (partMouse.containsMouse ? Theme.link : Theme.textMuted)
                        font.pixelSize: Theme.fontLarge
                        font.weight: crumb.last ? Font.DemiBold : Font.Normal
                        font.underline: partMouse.containsMouse && !crumb.last

                        Accessible.role: crumb.last ? Accessible.Heading : Accessible.Link
                        Accessible.name: crumb.last ? text : qsTr("Go to “%1”").arg(text)
                        Accessible.onPressAction: bar.browser.open(crumb.modelData.path)

                        MouseArea {
                            id: partMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: crumb.last ? Qt.ArrowCursor : Qt.PointingHandCursor
                            onClicked: bar.browser.open(crumb.modelData.path)
                        }
                    }
                }
            }
        }
    }

    // The folder's path, typed (Ctrl+L). Return opens it, Escape leaves the folder as it is.
    Rectangle {
        id: locationBox
        visible: bar.browser.locationEditing
        anchors {
            left: nav.right; leftMargin: Theme.spacing
            right: tools.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        height: Theme.rowHeight
        radius: 4
        color: Theme.field
        border.color: Theme.accent
        border.width: 1

        onVisibleChanged: {
            if (visible) {
                location.text = bar.browser.locationText
                location.selectAll()
                location.forceActiveFocus()
            }
        }

        Connections {
            target: bar.browser
            // Completion and a chosen suggestion put the text; the cursor goes to its end.
            function onLocationTextChanged() {
                if (location.text !== bar.browser.locationText) {
                    location.text = bar.browser.locationText
                    location.cursorPosition = location.text.length
                }
            }
        }

        TextInput {
            id: location
            objectName: "location"

            Accessible.role: Accessible.EditableText
            Accessible.name: qsTr("Location")
            Accessible.description: bar.browser.locationError

            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.text
            font.pixelSize: Theme.fontNormal
            selectByMouse: true

            // Tab completes, Up and Down choose a suggestion, Return opens, Escape leaves.
            Keys.onPressed: event => {
                event.accepted = bar.browser.locationFieldKey(event.key, event.modifiers)
            }
            onTextEdited: bar.browser.setLocationText(text)
            onActiveFocusChanged: if (!activeFocus && locationBox.visible) bar.browser.cancelLocation()
        }
    }

    // Under the field: why the typed location cannot be opened.
    Rectangle {
        id: errorBox
        visible: bar.browser.locationEditing && bar.browser.locationError.length > 0
        anchors { left: locationBox.left; right: locationBox.right; top: locationBox.bottom; topMargin: 2 }
        height: errorText.implicitHeight + 8
        radius: 4
        color: Theme.card
        border.color: Theme.danger
        border.width: 1

        Text {
            id: errorText
            objectName: "locationError"
            anchors { left: parent.left; right: parent.right; leftMargin: 6; rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: bar.browser.locationError
            color: Theme.danger
            elide: Text.ElideRight
            font.pixelSize: Theme.fontNormal
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }

    // Under the field: the folders the location can be completed with (Tab).
    Rectangle {
        id: suggestionsBox
        visible: bar.browser.locationEditing && bar.browser.locationSuggestions.length > 0
        anchors { left: locationBox.left; right: locationBox.right; top: locationBox.bottom; topMargin: 2 }
        height: Math.min(suggestions.count, 8) * Theme.rowHeight + 4
        radius: 4
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        ListView {
            id: suggestions
            objectName: "locationSuggestions"
            anchors { fill: parent; margins: 2 }
            clip: true
            model: bar.browser.locationSuggestions
            currentIndex: bar.browser.locationSuggestion
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Folders")

            delegate: Rectangle {
                id: suggestion

                required property string modelData
                required property int index
                readonly property string name: modelData

                width: ListView.view.width
                height: Theme.rowHeight
                radius: 3
                color: suggestion.index === bar.browser.locationSuggestion
                       ? Theme.cursorRow : (suggestionMouse.containsMouse ? Theme.hoverRow : Theme.none)
                Accessible.role: Accessible.ListItem
                Accessible.name: suggestion.name
                Accessible.onPressAction: bar.browser.chooseLocationSuggestion(suggestion.index)

                Text {
                    anchors { left: parent.left; leftMargin: 6; right: parent.right; verticalCenter: parent.verticalCenter }
                    text: suggestion.name + "/"
                    color: Theme.text
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontNormal
                }

                MouseArea {
                    id: suggestionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: bar.browser.chooseLocationSuggestion(suggestion.index)
                }
            }
        }
    }

    // The search ('/', Ctrl+F; Ctrl+Shift+F inside files). What is typed goes to the browser,
    // which searches again; the keys it uses (Return, Escape, Up, Down, ...) go to it first.
    Rectangle {
        id: searchBox
        objectName: "searchBox"
        visible: bar.browser.searchShown
        anchors {
            left: nav.right; leftMargin: Theme.spacing
            right: tools.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        height: Theme.rowHeight
        radius: 4
        color: Theme.field
        border.color: Theme.accent
        border.width: 1

        onVisibleChanged: {
            if (visible) {
                search.text = bar.browser.searchText
                search.forceActiveFocus()
            }
        }

        TextInput {
            id: search
            objectName: "searchField"

            Accessible.role: Accessible.EditableText
            Accessible.name: bar.browser.searchContents ? qsTr("Find inside files") : qsTr("Find names")
            Accessible.description: bar.browser.searchHint

            anchors {
                left: parent.left; leftMargin: 6
                right: modes.left; rightMargin: 6
                top: parent.top; bottom: parent.bottom
            }
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.text
            font.pixelSize: Theme.fontNormal
            selectByMouse: true

            Keys.onPressed: event => {
                event.accepted = bar.browser.searchFieldKey(event.key, event.modifiers, event.text)
            }
            onTextEdited: bar.browser.setSearchText(text)

            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                visible: search.text.length === 0
                text: bar.browser.searchHint
                color: Theme.textFaint
                font.pixelSize: Theme.fontNormal
            }
        }

        // Names or inside files: a small switch at the field's end.
        Row {
            id: modes
            objectName: "searchModes"
            anchors { right: parent.right; rightMargin: 3; verticalCenter: parent.verticalCenter }
            spacing: 2

            component ModeButton: Rectangle {
                id: modeButton

                property string label
                property string key
                property bool on

                objectName: "searchMode" + key
                Accessible.role: Accessible.RadioButton
                Accessible.name: label
                Accessible.checkable: true
                Accessible.checked: on
                Accessible.onPressAction: choose()

                function choose() {
                    bar.browser.searchContents = modeButton.key === "Contents"
                    search.forceActiveFocus()
                }
                width: modeText.implicitWidth + 12
                height: Theme.rowHeight - 6
                radius: 3
                color: on ? Theme.accentSoft : (modeMouse.containsMouse ? Theme.hoverRow : Theme.none)
                Behavior on color { ColorAnimation { duration: Theme.quick } }

                Text {
                    id: modeText
                    anchors.centerIn: parent
                    text: modeButton.label
                    color: modeButton.on ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }

                MouseArea {
                    id: modeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: modeButton.choose()
                }
            }

            ModeButton { key: "Names"; label: qsTr("Names"); on: !bar.browser.searchContents }
            ModeButton { key: "Contents"; label: qsTr("Contents"); on: bar.browser.searchContents }
        }
    }

    // On the right: the details pane's switch (Ctrl+I), lit while the pane is shown.
    Row {
        id: tools
        anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        spacing: 2

        Rectangle {
            id: detailsButton
            objectName: "detailsButton"

            readonly property bool on: bar.browser.detailsShown

            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Details")
            Accessible.checkable: true
            Accessible.checked: on
            Accessible.onPressAction: bar.browser.detailsShown = !bar.browser.detailsShown

            width: 30
            height: 30
            radius: Theme.radius
            color: on ? Theme.accentSoft : (detailsMouse.containsMouse ? Theme.hoverRow : Theme.none)
            Behavior on color { ColorAnimation { duration: Theme.quick } }

            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 16
                radius: 8
                color: Theme.none
                border.width: 1.5
                border.color: detailsButton.on ? Theme.accent : Theme.textMuted

                Text {
                    anchors.centerIn: parent
                    text: "i"
                    color: detailsButton.on ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Bold
                }
            }

            MouseArea {
                id: detailsMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: bar.browser.detailsShown = !bar.browser.detailsShown
            }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
