find_package(Qt6 COMPONENTS Widgets QUIET)

if(NOT Qt6Widgets_FOUND)
    message(WARNING "Qt6Widgets not found -- skipping the slrxfm-gui target")
    set(SLRXFM_GUI_AVAILABLE FALSE PARENT_SCOPE)
    return()
endif()

set(SLRXFM_GUI_AVAILABLE TRUE PARENT_SCOPE)

set(CMAKE_AUTOMOC ON)

add_library(slrxfm-gui-core STATIC
    directory_tree_model.hpp
    directory_list_model.hpp
    main_window.hpp
    main_window.cpp
    transfer_worker.hpp
    transfer_worker.cpp
)
target_include_directories(slrxfm-gui-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(slrxfm-gui-core PUBLIC slrxfm Qt6::Widgets)

add_executable(slrxfm-gui app_main.cpp)
target_link_libraries(slrxfm-gui PRIVATE slrxfm-gui-core)

include(GNUInstallDirs)
install(TARGETS slrxfm-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
