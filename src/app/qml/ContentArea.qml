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
        visible: area.browser.view === Browser.List && !area.browser.searchShown && area.browser.place.length === 0
    }

    Loader {
        anchors.fill: parent
        active: area.browser.view === Browser.Grid
        visible: !area.browser.searchShown && area.browser.place.length === 0
        sourceComponent: Component {
            FileGrid { browser: area.browser }
        }
    }

    Loader {
        anchors.fill: parent
        active: area.browser.view === Browser.Columns
        visible: !area.browser.searchShown && area.browser.place.length === 0
        sourceComponent: Component {
            ColumnsView { browser: area.browser }
        }
    }

    // The trash and the recent files, in the folder's place.
    Loader {
        anchors.fill: parent
        active: area.browser.place === "trash" && !area.browser.searchShown
        sourceComponent: Component {
            TrashView { browser: area.browser }
        }
    }

    Loader {
        anchors.fill: parent
        active: area.browser.place === "recent" && !area.browser.searchShown
        sourceComponent: Component {
            RecentView { browser: area.browser }
        }
    }

    // While searching, the results take the folder's place.
    SearchResults {
        anchors.fill: parent
        browser: area.browser
        visible: area.browser.searchShown
    }
}
