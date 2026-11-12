# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-files: the program people start. QML lays the window out; the C++ here wires the
# core library to it and makes the decisions.

qt_add_executable(riley-files
    main.cpp
    uicheck.h uicheck.cpp
)

set(rf_qml_files
    qml/Main.qml
    qml/Theme.qml
    qml/HeaderBar.qml
    qml/Sidebar.qml
    qml/ContentArea.qml
)

# Theme is one shared object, so every part of the window reads the same colours.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The QML files live in qml/ for tidiness, but are served flat next to the module's qmldir,
# so they find each other (and the Theme singleton) without extra imports.
foreach(file IN LISTS rf_qml_files)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(riley-files
    URI RileyFiles
    VERSION 1.0
    QML_FILES ${rf_qml_files}
)

target_compile_definitions(riley-files PRIVATE
    RILEY_FILES_VERSION="${PROJECT_VERSION}")

target_link_libraries(riley-files PRIVATE
    riley-core
    riley_warnings
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

install(TARGETS riley-files
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
