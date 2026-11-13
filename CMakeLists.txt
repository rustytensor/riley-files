# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

cmake_minimum_required(VERSION 3.22)

project(riley-files
    VERSION 0.7.0
    DESCRIPTION "The file manager of the Riley desktop"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CTest)   # defines BUILD_TESTING (on by default) and enables ctest

set(rf_qt_components Core Gui Qml Quick Quick3D LinguistTools)
if(BUILD_TESTING)
    list(APPEND rf_qt_components Test)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS ${rf_qt_components})

# Turns on automoc and the Qt 6.8 policies (QML modules under /qt/qml, and so on).
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES de fr)

# Our own targets link this to get the warning flags. Kept off Qt's generated helper
# targets so the "no warnings" rule is about code we write.
add_library(riley_warnings INTERFACE)
target_compile_options(riley_warnings INTERFACE -Wall -Wextra)

# Where the systemd units go: the change helper's system service and the indexer's user service.
set(RILEY_SYSTEMD_SYSTEM_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "systemd system unit folder")
set(RILEY_SYSTEMD_USER_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/user" CACHE PATH "systemd user unit folder")

add_subdirectory(src/core)
add_subdirectory(src/changes)
add_subdirectory(src/index)
add_subdirectory(src/thumbs)
add_subdirectory(src/graph)
add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES share/riley-files.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES share/riley-files.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/riley-files)
