# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-thumbs: thumbnails as the freedesktop thumbnail spec says. thumbnailcache.h the cache
# folders, names and checks; installedthumbnailers.h the .thumbnailer files other programs
# install; thumbnailer.h makes missing thumbnails on worker threads, at most four at a time.
# Qt Gui is allowed here (images), never in riley-core.

add_library(riley-thumbs SHARED
    thumbnailcache.h thumbnailcache.cpp
    installedthumbnailers.h installedthumbnailers.cpp
    thumbnailer.h thumbnailer.cpp
)

include(GenerateExportHeader)
generate_export_header(riley-thumbs
    BASE_NAME rileythumbs
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/rileythumbs_export.h)

set_target_properties(riley-thumbs PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

target_include_directories(riley-thumbs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(riley-thumbs
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE riley_warnings)

install(TARGETS riley-thumbs
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
