cmake_minimum_required(VERSION 3.20)
project(WDSPluginSDKExample LANGUAGES CXX)

set(WDS_PLUGIN_HOST_SOURCE "" CACHE PATH "Folder containing the WDS plug-in host source")
if(NOT WDS_PLUGIN_HOST_SOURCE)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin_host.hpp")
    set(WDS_PLUGIN_HOST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  else()
    set(WDS_PLUGIN_HOST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
  endif()
endif()

add_library(wds_sdk_host STATIC "${WDS_PLUGIN_HOST_SOURCE}/plugin_host.cpp")
target_include_directories(wds_sdk_host PUBLIC "${WDS_PLUGIN_HOST_SOURCE}")
target_compile_features(wds_sdk_host PUBLIC cxx_std_20)
add_executable(wds_plugin_sdk_demo demo.cpp)
target_link_libraries(wds_plugin_sdk_demo PRIVATE wds_sdk_host)

enable_testing()
add_test(NAME sdk_install_replace_restore COMMAND wds_plugin_sdk_demo)

function(palette_module target abi blue)
  add_library(${target} MODULE native_palette.cpp)
  target_compile_definitions(${target} PRIVATE WDS_BUILD_PALETTE WDS_SAMPLE_ABI=${abi}
    WDS_SAMPLE_BLUE=${blue})
  set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden)
endfunction()

palette_module(sdk_daylight 1 0xc0)
palette_module(sdk_overcast 1 0x60)
palette_module(sdk_obsolete 0 0xff)
add_executable(wds_native_sdk_demo native_demo.cpp)
target_link_libraries(wds_native_sdk_demo PRIVATE wds_sdk_host ${CMAKE_DL_LIBS})
if(MINGW)
  target_link_options(wds_native_sdk_demo PRIVATE -municode)
endif()
add_test(NAME sdk_native_replace_restore
  COMMAND wds_native_sdk_demo "$<TARGET_FILE:sdk_daylight>" "$<TARGET_FILE:sdk_overcast>"
    "$<TARGET_FILE:sdk_obsolete>")
