cmake_minimum_required(VERSION 3.25)

project(
  WorldDesignStudio
  DESCRIPTION "世界设计工作室引擎核心"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(WDS_BUILD_MODERN_STUDIO "Build the Qt desktop workbench" OFF)
if(MSVC AND NOT WDS_BUILD_MODERN_STUDIO)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 编译告警统一挂在接口目标上，后续引擎目标直接链接即可继承同一组规则。
add_library(wds_compile_options INTERFACE)
if(MSVC)
  # /Zc:__cplusplus 让 __cplusplus 反映真实的 /std:c++20 模式，否则恒为 199711L。
  target_compile_options(wds_compile_options INTERFACE /W4 /WX /Zc:__cplusplus /utf-8)
else()
  target_compile_options(
    wds_compile_options
    INTERFACE -Wall -Wextra -Wpedantic -Werror
  )
endif()

# 构建骨架的冒烟程序：只验证编译、链接、C++ 标准模式与 CTest 链路。
add_executable(wds_smoke tests/smoke.cpp)
target_link_libraries(wds_smoke PRIVATE wds_compile_options)

enable_testing()
add_test(NAME smoke COMMAND wds_smoke)

add_library(wds_m0 STATIC src/plugin_host.cpp src/project_store.cpp src/workspace.cpp)
target_include_directories(wds_m0 PUBLIC src)
target_link_libraries(wds_m0 PRIVATE wds_compile_options)

add_executable(wds_cli src/cli.cpp)
target_link_libraries(wds_cli PRIVATE wds_m0 wds_compile_options)

add_library(wds_project STATIC src/platform_project.cpp)
target_include_directories(wds_project PUBLIC src)
target_include_directories(wds_project SYSTEM PRIVATE third_party/nlohmann)
target_link_libraries(wds_project PRIVATE wds_compile_options)
target_link_libraries(wds_cli PRIVATE wds_project)

if(WDS_BUILD_MODERN_STUDIO)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets LinguistTools)
  qt_add_executable(wds_studio_modern WIN32
    src/studio/main.cpp
    src/studio/bridge.cpp
    src/studio/i18n/language.cpp
    src/studio/output_panel.cpp
    src/studio/resource_panel.cpp
    src/studio/scene_tree.cpp
    src/studio/theme/theme.cpp
    src/studio/viewport.cpp
  )
  set_target_properties(wds_studio_modern PROPERTIES AUTOMOC ON)
  target_link_libraries(wds_studio_modern PRIVATE Qt6::Widgets wds_project wds_compile_options)
  add_dependencies(wds_studio_modern wds_cli)
  qt_add_resources(wds_studio_modern studio_theme PREFIX "/studio" BASE "src/studio"
    FILES src/studio/theme/workbench.qss)
  qt_add_translations(wds_studio_modern
    SOURCE_TARGETS wds_studio_modern
    TS_FILES src/studio/i18n/workbench_zh_CN.ts
    RESOURCE_PREFIX "/studio/i18n")
endif()

add_library(wds_voxel STATIC src/voxel_world.cpp src/sandbox_rules.cpp src/voxel_content.cpp)
target_include_directories(wds_voxel PUBLIC src)
target_link_libraries(wds_voxel PRIVATE wds_compile_options)
target_link_libraries(wds_cli PRIVATE wds_voxel)

if(WIN32)
  if(MINGW)
    target_link_options(wds_cli PRIVATE -municode)
  endif()
  add_executable(wds_editor WIN32 src/windows_editor.cpp)
  target_link_libraries(wds_editor PRIVATE wds_m0 wds_compile_options shell32 user32 gdi32)
  if(MINGW)
    target_link_options(wds_editor PRIVATE -municode)
  endif()
  set(wds_gui_option -DGUI=$<TARGET_FILE:wds_editor>)

  add_executable(wds_sandbox WIN32 src/windows_sandbox.cpp)
  target_link_libraries(wds_sandbox PRIVATE wds_voxel wds_compile_options user32 gdi32 shell32)
  if(MINGW)
    target_link_options(wds_sandbox PRIVATE -municode)
  endif()
  install(TARGETS wds_sandbox RUNTIME DESTINATION .)
endif()

install(TARGETS wds_cli RUNTIME DESTINATION .)
install(FILES LICENSE DESTINATION .)
install(FILES third_party/nlohmann/LICENSE.MIT DESTINATION licenses RENAME nlohmann-json.txt)

add_executable(wds_plugin_test tests/plugin_host.cpp)
target_link_libraries(wds_plugin_test PRIVATE wds_m0 wds_compile_options)
add_test(NAME plugin_host COMMAND wds_plugin_test)

add_executable(wds_voxel_test tests/voxel_world.cpp)
target_link_libraries(wds_voxel_test PRIVATE wds_voxel wds_compile_options)
add_test(NAME voxel_world COMMAND wds_voxel_test)
add_test(
  NAME platform_project
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wds_cli>
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/project-fixture
          -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/project-v1
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_project.cmake
)
add_test(
  NAME scene_play
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wds_cli>
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/scene-play
          -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/scene-motion
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/scene_play.cmake
)
add_test(
  NAME resource_import
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wds_cli>
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/import-fixture
          -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/scene-editing
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/resource_import.cmake
)
add_test(
  NAME sandbox_rules
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wds_cli>
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/rules-fixture
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/sandbox_rules.cmake
)
add_test(
  NAME rename_flow
  COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:wds_cli>
          ${wds_gui_option}
          -DROOT=${CMAKE_CURRENT_BINARY_DIR}/rename-fixture
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/rename_flow.cmake
)
