add_executable(slrxfm_demo demo_main.cpp)
target_link_libraries(slrxfm_demo PRIVATE slrxfm)

if(SLRXFM_GUI_AVAILABLE)
    add_executable(slrxfm_gui_smoke_test gui_smoke_test.cpp)
    target_link_libraries(slrxfm_gui_smoke_test PRIVATE slrxfm-gui-core)
else()
    message(WARNING "Skipping slrxfm_gui_smoke_test -- Qt6 GUI was not built")
endif()
