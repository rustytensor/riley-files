# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-graph: what the graph view shows, with no interface (milestone 9). graphtree.h walks a
# folder and everything below it into nodes joined to their parent folder, folders only when the
# tree is large; forcelayout.h places the nodes in 3D, the same way every time; graphbuilder.h does
# both on a worker thread. Qt Core only, like riley-core. The look of servorra.com's graph (v0.8)
# as numbers the view draws: graphlook.h (projection, depth bands, colours, node sizes, labels),
# sitelayout.h (the site's layout), graphmotion.h (turning, zoom, pulses, flashes), graphsky.h (the
# stars), graphgrowth.h (when each node appeared, and "Watch it grow") and graphframe.h (one frame
# of it all as triangles, ready for the view to copy to the screen).

add_library(riley-graph SHARED
    graphtree.h graphtree.cpp
    forcelayout.h forcelayout.cpp
    graphbuilder.h graphbuilder.cpp
    graphlook.h graphlook.cpp
    sitelayout.h sitelayout.cpp
    graphmotion.h graphmotion.cpp
    graphsky.h graphsky.cpp
    graphgrowth.h graphgrowth.cpp
    graphframe.h graphframe.cpp
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
