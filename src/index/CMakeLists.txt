# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-index: the search index (v0.7), kept in SQLite. indexsettings.h what the person chose to
# index (kept in the program's settings file); indexdatabase.h the database file, its tables and
# the name and contents queries; indexer.h the first scan, the changes, the catch-up after a
# restart, Pause, Resume and Rebuild; changefollower.h the changes the helper (src/changes)
# reports. Qt Core and SQLite only: the indexer service and the window both use it.

find_package(SQLite3 REQUIRED)

add_library(riley-index SHARED
    indexsettings.h indexsettings.cpp
    indexdatabase.h indexdatabase.cpp
    indexer.h indexer.cpp
    changefollower.h changefollower.cpp
    indexerclient.h indexerclient.cpp
)

include(GenerateExportHeader)
generate_export_header(riley-index
    BASE_NAME rileyindex
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/rileyindex_export.h)

set_target_properties(riley-index PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(riley-index PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riley-index
    PUBLIC Qt6::Core
    PRIVATE SQLite::SQLite3 riley-changes riley_warnings)

install(TARGETS riley-index
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)

# riley-files-indexer: the user service that keeps the index (systemd's user manager runs it as
# riley-files-indexer.service; the window starts it on the first run).
find_package(Qt6 REQUIRED COMPONENTS Network)
add_executable(riley-files-indexer
    service/main.cpp
    service/indexservice.h service/indexservice.cpp)
target_link_libraries(riley-files-indexer PRIVATE riley-index Qt6::Network riley_warnings)
install(TARGETS riley-files-indexer RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})

configure_file(service/riley-files-indexer.service.in riley-files-indexer.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/riley-files-indexer.service
    DESTINATION ${RILEY_SYSTEMD_USER_UNIT_DIR})
