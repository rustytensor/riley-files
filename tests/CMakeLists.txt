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
