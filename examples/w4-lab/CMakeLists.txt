cmake_minimum_required(VERSION 3.25)
project(WdsW4Lab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

include(../../cmake/raylib.cmake)

add_executable(w4_qt qt_lab.cpp)
target_link_libraries(w4_qt PRIVATE Qt6::Widgets)

add_executable(w4_raylib raylib_lab.cpp)
target_link_libraries(w4_raylib PRIVATE raylib)

if(MSVC)
  target_compile_options(w4_qt PRIVATE /utf-8)
  target_compile_options(w4_raylib PRIVATE /utf-8)
endif()
