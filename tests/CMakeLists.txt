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
