// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// Where a folder's files are shown, in the view the browser says. The grid and the columns
// are made only while they are shown.
Rectangle {
    id: area

    required property Browser browser

    color: Theme.content

    FileList {
        anchors.fill: parent
        browser: area.browser
        visible: area.browser.view === Browser.List && !area.browser.searchShown
    }

    Loader {
        anchors.fill: parent
        active: area.browser.view === Browser.Grid
        visible: !area.browser.searchShown
        sourceComponent: Component {
            FileGrid { browser: area.browser }
        }
    }

    Loader {
        anchors.fill: parent
        active: area.browser.view === Browser.Columns
        visible: !area.browser.searchShown
        sourceComponent: Component {
            ColumnsView { browser: area.browser }
        }
    }

    // While searching, the results take the folder's place.
    SearchResults {
        anchors.fill: parent
        browser: area.browser
        visible: area.browser.searchShown
    }
}
