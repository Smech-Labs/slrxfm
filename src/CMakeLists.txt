add_library(slrxfm STATIC
    directory_listing.cpp
    file_operations.cpp
    trash_manager.cpp
    symlink_dialog.cpp
    network_browsing.cpp
)

target_include_directories(slrxfm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SMBCLIENT QUIET smbclient)
    pkg_check_modules(LIBCURL QUIET libcurl)
endif()

if(SMBCLIENT_FOUND)
    target_compile_definitions(slrxfm PUBLIC SLRXFM_HAVE_LIBSMBCLIENT=1)
    target_include_directories(slrxfm PRIVATE ${SMBCLIENT_INCLUDE_DIRS})
    target_link_libraries(slrxfm PUBLIC ${SMBCLIENT_LIBRARIES})
else()
    message(WARNING "libsmbclient not found via pkg-config -- SmbShare will stay stubbed")
endif()

if(LIBCURL_FOUND)
    target_compile_definitions(slrxfm PUBLIC SLRXFM_HAVE_LIBCURL=1)
    target_include_directories(slrxfm PRIVATE ${LIBCURL_INCLUDE_DIRS})
    target_link_libraries(slrxfm PUBLIC ${LIBCURL_LIBRARIES})
else()
    message(WARNING "libcurl not found via pkg-config -- FtpSite/WebDavSite will stay stubbed")
endif()

option(SLRXFM_BUILD_GUI "Build the Qt6 GUI application" ON)
set(SLRXFM_GUI_AVAILABLE FALSE)
if(SLRXFM_BUILD_GUI)
    add_subdirectory(gui)
endif()
set(SLRXFM_GUI_AVAILABLE ${SLRXFM_GUI_AVAILABLE} PARENT_SCOPE)
