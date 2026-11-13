# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME core_tests COMMAND core_tests)
# The sort order is checked in a fixed language, so the result does not depend on the machine.
set_tests_properties(core_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

add_executable(model_tests model_tests.cpp)
target_link_libraries(model_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME model_tests COMMAND model_tests)
set_tests_properties(model_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The main window driven with keys. uiharness.{h,cpp} starts the real window; add new interface
# tests as slots in ui_tests.cpp.
add_executable(ui_tests ui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(ui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME ui_tests COMMAND ui_tests)
set_tests_properties(ui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# File operations on real files in temporary folders: copy, move, rename, new folder, clashes,
# stop, progress, failures.
add_executable(fileops_tests fileops_tests.cpp)
target_link_libraries(fileops_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME fileops_tests COMMAND fileops_tests)
set_tests_properties(fileops_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# File operations in the main window, driven with keys through the same harness: copy, cut,
# paste, rename, new folder, trash, undo, the clash question, Stop and the shared clipboard.
add_executable(fileui_tests fileui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(fileui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME fileui_tests COMMAND fileui_tests)
set_tests_properties(fileui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The sidebar's models on real files: the places (home, XDG user folders, trash) and the pinned
# folders kept in the GTK 3 bookmarks file.
add_executable(sidebar_tests sidebar_tests.cpp)
target_link_libraries(sidebar_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME sidebar_tests COMMAND sidebar_tests)
set_tests_properties(sidebar_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Thumbnails on real files: the freedesktop cache, fail entries and installed thumbnailers.
add_executable(thumbs_tests thumbs_tests.cpp)
target_link_libraries(thumbs_tests PRIVATE riley-thumbs riley_warnings Qt6::Test)

add_test(NAME thumbs_tests COMMAND thumbs_tests)
set_tests_properties(thumbs_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Thumbnails in the window: the grid stays smooth while they are made.
add_executable(thumbui_tests thumbui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(thumbui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME thumbui_tests COMMAND thumbui_tests)
set_tests_properties(thumbui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The sidebar, the details pane and the preview in the window, driven with keys.
add_executable(paneui_tests paneui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(paneui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME paneui_tests COMMAND paneui_tests)
set_tests_properties(paneui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The menu bar, the context menus and the list of keyboard shortcuts in the window, driven with
# the mouse and the keys.
add_executable(menuui_tests menuui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(menuui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME menuui_tests COMMAND menuui_tests)
set_tests_properties(menuui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The Preferences and the pane widths in the window, driven with the mouse and the keys.
add_executable(prefsui_tests prefsui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(prefsui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME prefsui_tests COMMAND prefsui_tests)
set_tests_properties(prefsui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Default applications and custom icons in the window: Open With, mimeapps.list, Return opening
# a file with its application, Change Icon and Reset Icon.
add_executable(appsui_tests appsui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(appsui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME appsui_tests COMMAND appsui_tests)
set_tests_properties(appsui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Search on real files: by name below a folder and inside files through ripgrep, a new search
# dropping the one before, the row limit and the message when rg is missing.
add_executable(search_tests search_tests.cpp)
target_link_libraries(search_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME search_tests COMMAND search_tests)
set_tests_properties(search_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Search in the window: the field from "/", Ctrl+F and Ctrl+Shift+F, the results in the folder's
# place, Return going to a result's folder, and the window drawing on while a search runs.
add_executable(searchui_tests searchui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(searchui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME searchui_tests COMMAND searchui_tests)
set_tests_properties(searchui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Drives on real files: the drives read from a mountinfo file and watched, and a file on another
# file system than the home folder trashed to that drive's own trash and put back.
add_executable(drives_tests drives_tests.cpp)
target_link_libraries(drives_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME drives_tests COMMAND drives_tests)
set_tests_properties(drives_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Drives in the window: the eject mark in the sidebar running udisksctl with an argument list.
add_executable(drivesui_tests drivesui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(drivesui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME drivesui_tests COMMAND drivesui_tests)
set_tests_properties(drivesui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The places that are not folders, in the window: the trash view with Restore, Delete for Good
# and Empty Trash, the recent files of recently-used.xbel, Connect to Server and the Network
# section, and the sidebar's sections that show only when they have something.
add_executable(placesui_tests placesui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(placesui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME placesui_tests COMMAND placesui_tests)
set_tests_properties(placesui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Previous versions on real files in the snapper layout: finding snapshots, the versions of a file
# and a folder, restore (the live one to the trash first, one undo), restore as copy, and every
# change inside a snapshot refused.
add_executable(versions_tests versions_tests.cpp)
target_link_libraries(versions_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME versions_tests COMMAND versions_tests)
set_tests_properties(versions_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# Previous versions in the window: Alt+V, File > Previous Versions and the details pane's section,
# Return restoring after asking, Restore as Copy from a version's context menu, a folder's
# version looked through read-only, and every change inside a snapshot refused.
add_executable(versionsui_tests versionsui_tests.cpp uiharness.h uiharness.cpp)
find_package(Qt6 REQUIRED COMPONENTS QuickTest)
target_link_libraries(versionsui_tests PRIVATE riley-ui riley_warnings Qt6::Test Qt6::QuickTest)

add_test(NAME versionsui_tests COMMAND versionsui_tests)
set_tests_properties(versionsui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The look of the window: the colours following the Riley desktop's theme as it switches, no
# colour outside Theme.qml, the window at twice the scale, how fast the program starts, and
# pictures of the main views in both variants (to RILEY_SHOTS when it is set).
add_executable(lookui_tests lookui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(lookui_tests PRIVATE riley-ui riley_warnings Qt6::Test)
target_compile_definitions(lookui_tests PRIVATE RILEY_FILES_BINARY="$<TARGET_FILE:riley-files>")
add_dependencies(lookui_tests riley-files)

add_test(NAME lookui_tests COMMAND lookui_tests)
set_tests_properties(lookui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The window in German and French from the system's language, catalogs that translate every
# string the sources mark (checked with Qt's own lupdate against the sources), and every control
# in the window and its dialogs named for assistive technology and reached with Tab.
add_executable(i18nui_tests i18nui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(i18nui_tests PRIVATE riley-ui riley_warnings Qt6::Test)
target_compile_definitions(i18nui_tests PRIVATE
    RILEY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    RILEY_LUPDATE="$<TARGET_FILE:Qt6::lupdate>")

add_test(NAME i18nui_tests COMMAND i18nui_tests)
set_tests_properties(i18nui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The graph view's library on real files: the tree walked into nodes and edges, folders only for a
# large tree, and the 3D layout the same every time, its nodes kept apart, made off the UI thread.
add_executable(graph_tests graph_tests.cpp)
target_link_libraries(graph_tests PRIVATE riley-graph riley_warnings Qt6::Test)

add_test(NAME graph_tests COMMAND graph_tests)
set_tests_properties(graph_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The graph view in the window, drawn with real OpenGL and no display: Mesa's software renderer
# through EGL's surfaceless platform, by a small window system of the test's own
# (glplatform.cpp, built into the test). Keys through the graph, a drawn frame, the label under
# the mouse, and what the view says where 3D cannot start.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
add_executable(graphui_tests graphui_tests.cpp glplatform.cpp uiharness.h uiharness.cpp)
set_source_files_properties(glplatform.cpp PROPERTIES COMPILE_DEFINITIONS QT_STATICPLUGIN)
target_link_libraries(graphui_tests PRIVATE riley-ui riley_warnings Qt6::Test Qt6::GuiPrivate EGL)

add_test(NAME graphui_tests COMMAND graphui_tests)
set_tests_properties(graphui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8")

# The file icons: nine drawings from the program's own SVG files with shapes of their own, used
# for each kind of file in every view, and drawn at the screen's pixels at twice the scale.
add_executable(iconui_tests iconui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(iconui_tests PRIVATE riley-ui riley_warnings Qt6::Test)

add_test(NAME iconui_tests COMMAND iconui_tests)
set_tests_properties(iconui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")

# The list view as a repository page, in the window: the outlined table with its summing-up band
# and how long ago each item changed, the README rendered under it with nothing in it run or
# fetched (a local listener stands in for the network), and the path in the header as parts to
# click.
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(repoui_tests repoui_tests.cpp uiharness.h uiharness.cpp)
target_link_libraries(repoui_tests PRIVATE riley-ui riley_warnings Qt6::Test Qt6::Network)

add_test(NAME repoui_tests COMMAND repoui_tests)
set_tests_properties(repoui_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")
