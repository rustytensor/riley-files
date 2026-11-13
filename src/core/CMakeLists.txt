# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-core: everything a file manager decides, with no interface. Qt Core only, so it can be
# tested without a display and reused by other front ends or plugins.

add_library(riley-core SHARED
    direntry.h
    entryorder.h entryorder.cpp
    folderlisting.h folderlisting.cpp
    foldermodel.h foldermodel.cpp
    # File operations (milestone 2): jobs run off the UI thread by a queue. filejob.h is the
    # base (progress, stop, clash questions, failures, the steps an undo needs); filejobs.h the
    # copy, move, rename and new folder jobs; trash.h the freedesktop home trash and
    # trashjobs.h the trash, restore and delete-for-good jobs; undohistory.h undo and redo.
    filenames.h filenames.cpp
    filejob.h filejob.cpp
    filejobs.h filejobs.cpp
    jobqueue.h jobqueue.cpp
    trash.h trash.cpp
    trashjobs.h trashjobs.cpp
    undohistory.h undohistory.cpp
    # The sidebar (milestone 3): places.h the fixed places (home, XDG user folders, trash),
    # pinnedfolders.h the pinned folders kept in the GTK 3 bookmarks file.
    places.h places.cpp
    pinnedfolders.h pinnedfolders.cpp
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

# A description of the core's classes, so the interface can name them to QML without the core
# knowing anything of QML.
qt_extract_metatypes(riley-core)

# A private library of the app: the runtime file only, no development link or headers.
install(TARGETS riley-core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
