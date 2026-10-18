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
