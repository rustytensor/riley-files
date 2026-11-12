# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

add_executable(core_tests core_tests.cpp)
target_link_libraries(core_tests PRIVATE riley-core riley_warnings Qt6::Test)

add_test(NAME core_tests COMMAND core_tests)
# The sort order is checked in a fixed language, so the result does not depend on the machine.
set_tests_properties(core_tests PROPERTIES ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen")
