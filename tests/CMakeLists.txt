add_executable(slrxfm_demo demo_main.cpp)
target_link_libraries(slrxfm_demo PRIVATE slrxfm)
add_test(NAME slrxfm_demo COMMAND slrxfm_demo)

if(SLRXFM_GUI_AVAILABLE)
    add_executable(slrxfm_gui_smoke_test gui_smoke_test.cpp)
    target_link_libraries(slrxfm_gui_smoke_test PRIVATE slrxfm-gui-core)

    add_test(NAME slrxfm_gui_smoke_test COMMAND slrxfm_gui_smoke_test)
    # Headless: no real display is available (or desired) for CTest/autopkgtest runs.
    set_tests_properties(slrxfm_gui_smoke_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # QtCore-only (no QApplication/widgets/display needed): proves the
    # TransferWorker worker-thread + conflict-marshaling path directly.
    add_executable(slrxfm_transfer_worker_test transfer_worker_test.cpp)
    target_link_libraries(slrxfm_transfer_worker_test PRIVATE slrxfm-gui-core)
    add_test(NAME slrxfm_transfer_worker_test COMMAND slrxfm_transfer_worker_test)
else()
    message(WARNING "Skipping slrxfm_gui_smoke_test -- Qt6 GUI was not built")
endif()
