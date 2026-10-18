cmake_minimum_required(VERSION 3.16)
project(slrxfm
    VERSION 0.1.0
    DESCRIPTION "Smech Labs Remastered Xandros File Manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src)

option(SLRXFM_BUILD_TESTS "Build the demo/test harness" ON)
if(SLRXFM_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(SLRXFM_GUI_AVAILABLE)
    include(GNUInstallDirs)
    install(FILES data/slrxfm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/icons/slrxfm.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES data/dev.smech.slrxfm.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()
