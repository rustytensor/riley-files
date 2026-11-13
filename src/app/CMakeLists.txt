# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Marc Mironescu

# riley-ui: the RileyFiles QML module (the window and its parts) with the C++ that makes its
# decisions. A library of its own, so the program and the interface tests load the very same
# window with the very same C++ behind it.
#
# riley-files: the program people start. It reads the command line and shows one window.

qt_add_library(riley-ui SHARED
    actions.h actions.cpp
    browser.h browser.cpp browseractions.cpp browserprefs.cpp browserapps.cpp browsersearch.cpp
    browserplaces.cpp browserversions.cpp browsergraph.cpp
    graphscene.h graphscene.cpp
    chooser.h chooser.cpp
    iconprovider.h iconprovider.cpp
    fileglyphs.h fileglyphs.cpp
    panewidths.h panewidths.cpp
    preferences.h preferences.cpp
    fileops.h fileops.cpp
    foldercolumn.h foldercolumn.cpp
    appwindow.h appwindow.cpp
    commandline.h commandline.cpp
    coretypes.h
    thumbnailprovider.h thumbnailprovider.cpp
    details.h details.cpp
    preview.h preview.cpp
    themefile.h themefile.cpp
    translations.h translations.cpp
)

set(rf_qml_files
    qml/Main.qml
    qml/Theme.qml
    qml/HeaderBar.qml
    qml/Sidebar.qml
    qml/SidebarIcon.qml
    qml/ContentArea.qml
    qml/FileList.qml
    qml/FileRow.qml
    qml/FileIcon.qml
    qml/TreeGuide.qml
    qml/FileGrid.qml
    qml/FileTile.qml
    qml/ColumnsView.qml
    qml/ColumnPane.qml
    qml/ColumnRow.qml
    qml/RenameField.qml
    qml/ProgressStrip.qml
    qml/MessageBar.qml
    qml/ClashDialog.qml
    qml/DetailsPane.qml
    qml/PreviewOverlay.qml
    qml/AppMenuBar.qml
    qml/MenuPopups.qml
    qml/ShortcutsOverlay.qml
    qml/PaneDivider.qml
    qml/PreferencesPanel.qml
    qml/QuestionDialog.qml
    qml/ChooserDialog.qml
    qml/SearchResults.qml
    qml/SearchRow.qml
    qml/TrashView.qml
    qml/RecentView.qml
    qml/PlaceRow.qml
    qml/VersionsView.qml
    qml/ConnectDialog.qml
    qml/GraphView.qml
    qml/GraphScene.qml
)

# Theme is one shared object, so every part of the window reads the same colours.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The QML files live in qml/ for tidiness, but are served flat next to the module's qmldir,
# so they find each other (and the Theme singleton) without extra imports.
foreach(file IN LISTS rf_qml_files)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(riley-ui
    URI RileyFiles
    VERSION 1.0
    # The library is linked straight into the program and the tests, so no plugin is needed.
    NO_PLUGIN
    QML_FILES ${rf_qml_files}
)

target_include_directories(riley-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(riley-ui
    PUBLIC riley-core riley-thumbs riley-graph Qt6::Gui Qt6::Qml Qt6::Quick
    PRIVATE Qt6::Quick3D riley_warnings)

set_target_properties(riley-ui PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# The German and French catalogs (i18n/riley-files_<language>.ts), made into .qm files, built into
# the interface library and installed beside it, so a catalog put in the installed place is used
# first. `cmake --build build --target update_translations` brings the catalogs up to date with
# the sources.
qt_add_translations(
    TARGETS riley-ui riley-core riley-thumbs riley-graph
    TS_FILE_DIR ${PROJECT_SOURCE_DIR}/i18n
    TS_FILE_BASE riley-files
    LUPDATE_OPTIONS -locations none
    QM_FILES_OUTPUT_VARIABLE rf_qm_files)
# The file icons: nine SVG drawings, coloured and sized by FileGlyphs.
qt_add_resources(riley-ui riley_glyphs
    PREFIX /glyphs
    BASE glyphs
    FILES glyphs/folder.svg glyphs/image.svg glyphs/audio.svg glyphs/video.svg glyphs/text.svg
          glyphs/code.svg glyphs/document.svg glyphs/archive.svg glyphs/other.svg)
qt_add_resources(riley-ui riley_translations
    PREFIX /i18n
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${rf_qm_files})
install(FILES ${rf_qm_files}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/riley-files/translations)

qt_add_executable(riley-files
    main.cpp
    uicheck.h uicheck.cpp
)

target_compile_definitions(riley-ui PRIVATE
    RILEY_TRANSLATIONS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/riley-files/translations")

target_compile_definitions(riley-files PRIVATE
    RILEY_FILES_VERSION="${PROJECT_VERSION}")

target_link_libraries(riley-files PRIVATE
    riley-ui
    riley_warnings
)

install(TARGETS riley-files
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# A private library of the app: the runtime file only, no development link or headers.
install(TARGETS riley-ui
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
