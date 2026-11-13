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
    # Default apps and custom icons (milestone 4): desktopentry.h reads an application's
    # .desktop file and fills in its command line; mimeapps.h the freedesktop MIME apps lists;
    # customicons.h the icons chosen for files, kept by Riley Files itself.
    desktopentry.h desktopentry.cpp
    mimeapps.h mimeapps.cpp
    customicons.h customicons.cpp
    # Search (milestone 5): filesearch.h finds files below a folder by name, or inside them
    # through ripgrep, off the UI thread.
    filesearch.h filesearch.cpp
    # Drives (milestone 6): drives.h the drives the person mounted, read from mountinfo, and
    # eject through udisksctl. Things on a drive go to its own trash (trash.h).
    drives.h drives.cpp
    # The trash view, recent files and network places (milestone 6): trashmodel.h everything in
    # every trash; recentfiles.h the freedesktop recently-used.xbel, read and written;
    # networkplaces.h the servers connected through GVfs, and Connect to Server through gio.
    trashmodel.h trashmodel.cpp
    recentfiles.h recentfiles.cpp
    networkplaces.h networkplaces.cpp
    # Previous versions (milestone 7): snapshots.h finds Btrfs snapshots in the snapper layout
    # and the versions of a file in them; versionmodel.h lists them off the UI thread;
    # versionjobs.h restores one (the live one to the trash first) or restores it as a copy.
    snapshots.h snapshots.cpp
    versionmodel.h versionmodel.cpp
    versionjobs.h versionjobs.cpp
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
