add_library(slrxfm STATIC
    directory_listing.cpp
    file_operations.cpp
    trash_manager.cpp
    symlink_dialog.cpp
    network_browsing.cpp
)

target_include_directories(slrxfm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
