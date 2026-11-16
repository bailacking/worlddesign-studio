cmake_minimum_required(VERSION 3.25)
project(WdsW4Lab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
FetchContent_Declare(raylib
  URL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5
  URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(w4_qt qt_lab.cpp)
target_link_libraries(w4_qt PRIVATE Qt6::Widgets)

add_executable(w4_raylib raylib_lab.cpp)
target_link_libraries(w4_raylib PRIVATE raylib)

if(MSVC)
  target_compile_options(w4_qt PRIVATE /utf-8)
  target_compile_options(w4_raylib PRIVATE /utf-8)
endif()
