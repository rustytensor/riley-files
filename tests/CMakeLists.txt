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
