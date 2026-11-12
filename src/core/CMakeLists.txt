# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-core: everything a file manager decides, with no interface. Qt Core only, so it can be
# tested without a display and reused by other front ends or plugins.

add_library(riley-core SHARED
    direntry.h
    entryorder.h entryorder.cpp
    folderlisting.h folderlisting.cpp
)

include(GenerateExportHeader)
generate_export_header(riley-core
    BASE_NAME rileycore
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/rileycore_export.h)

# Only symbols marked RILEYCORE_EXPORT leave the library.
set_target_properties(riley-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(riley-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riley-core
    PUBLIC Qt6::Core
    PRIVATE riley_warnings)

# A private library of the app: the runtime file only, no development link or headers.
install(TARGETS riley-core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
