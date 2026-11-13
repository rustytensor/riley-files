# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-changes: the change helper (v0.7) and its protocol. Plain C++ and Linux, no Qt: the helper
# runs as root, so it is kept small. changeprotocol.h the messages on the socket; permissions.h
# what a user may see; eventsource.h where events come from (fanotifysource.h on the machine, a
# test's queue in the tests); changeserver.h the socket and its clients; changeclient.h the side
# the indexer speaks. The helper itself is riley-files-changes, a system service.

add_library(riley-changes STATIC
    changeprotocol.h changeprotocol.cpp
    permissions.h permissions.cpp
    eventsource.h eventsource.cpp
    fanotifysource.h fanotifysource.cpp
    changeserver.h changeserver.cpp
    changeclient.h changeclient.cpp
)
# Linked into the riley-index shared library too, and kept out of its exported symbols.
set_target_properties(riley-changes PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(riley-changes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(riley-changes PRIVATE riley_warnings)

add_executable(riley-files-changes main.cpp)
target_link_libraries(riley-files-changes PRIVATE riley-changes riley_warnings)
install(TARGETS riley-files-changes RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

configure_file(riley-files-changes.service.in riley-files-changes.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/riley-files-changes.service
    DESTINATION ${RILEY_SYSTEMD_SYSTEM_UNIT_DIR})
