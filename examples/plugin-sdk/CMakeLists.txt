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

add_executable(wds_plugin_sdk_demo demo.cpp "${WDS_PLUGIN_HOST_SOURCE}/plugin_host.cpp")
target_include_directories(wds_plugin_sdk_demo PRIVATE "${WDS_PLUGIN_HOST_SOURCE}")
target_compile_features(wds_plugin_sdk_demo PRIVATE cxx_std_20)

enable_testing()
add_test(NAME sdk_install_replace_restore COMMAND wds_plugin_sdk_demo)
