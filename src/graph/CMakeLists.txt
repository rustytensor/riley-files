# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-graph: what the graph view shows, with no interface (milestone 9). graphtree.h walks a
# folder and everything below it into nodes joined to their parent folder, folders only when the
# tree is large; forcelayout.h places the nodes in 3D, the same way every time; graphbuilder.h does
# both on a worker thread. Qt Core only, like riley-core.

add_library(riley-graph SHARED
    graphtree.h graphtree.cpp
    forcelayout.h forcelayout.cpp
    graphbuilder.h graphbuilder.cpp
)

include(GenerateExportHeader)
generate_export_header(riley-graph
    BASE_NAME rileygraph
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/rileygraph_export.h)

set_target_properties(riley-graph PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(riley-graph PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riley-graph
    PUBLIC Qt6::Core
    PRIVATE riley-core riley_warnings)

# The graph's classes described for QML, as riley-core's are.
qt_extract_metatypes(riley-graph)

install(TARGETS riley-graph
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
