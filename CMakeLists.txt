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
option(WDS_BUILD_RAYLIB_PREVIEW "Build the experimental raylib scene player" OFF)
option(WDS_BUILD_WINDOWS_PLAYER "Build the statically linked Windows x64 export runtime" OFF)
if(MSVC AND NOT WDS_BUILD_MODERN_STUDIO)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(WDS_BUILD_WINDOWS_PLAYER)
  if(NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64" OR WDS_BUILD_MODERN_STUDIO)
    message(FATAL_ERROR "Build the Windows player in a separate MSVC x64 build with Qt disabled.")
  endif()
  set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
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

add_library(wds_png STATIC third_party/lodepng/lodepng.cpp)
target_include_directories(wds_png SYSTEM PUBLIC third_party/lodepng)
target_compile_definitions(wds_png PUBLIC
  LODEPNG_NO_COMPILE_ENCODER
  LODEPNG_NO_COMPILE_DISK
  LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
)

add_library(wds_project STATIC
  src/platform_project.cpp src/project_io.cpp src/project_export.cpp src/project_operations.cpp
  src/asset_import.cpp
  src/behavior.cpp src/scene_play.cpp)
target_include_directories(wds_project PUBLIC src)
target_include_directories(wds_project SYSTEM PRIVATE third_party/nlohmann)
target_link_libraries(wds_project PRIVATE wds_compile_options wds_png)
target_link_libraries(wds_cli PRIVATE wds_project)
target_include_directories(wds_cli SYSTEM PRIVATE third_party/nlohmann)
if(WIN32)
  set(kit_spec "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows-x64-runtime.json")
  file(READ "${kit_spec}" WDS_WINDOWS_RUNTIME_SPEC)
  configure_file(cmake/windows-runtime-spec.hpp.in generated/windows_runtime_spec.hpp @ONLY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${kit_spec}")
  target_include_directories(wds_project PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(wds_project PRIVATE bcrypt)
endif()

if(WDS_BUILD_RAYLIB_PREVIEW OR WDS_BUILD_WINDOWS_PLAYER)
  enable_language(C)
  if(WDS_BUILD_WINDOWS_PLAYER)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link the packaged raylib statically" FORCE)
    set(PLATFORM Desktop CACHE STRING "Build the Windows desktop runtime" FORCE)
    set(USE_EXTERNAL_GLFW OFF CACHE STRING "Use the GLFW bundled with raylib" FORCE)
    set(OPENGL_VERSION 3.3 CACHE STRING "Graphics API for the Windows runtime" FORCE)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "Build only the raylib features used by the player" FORCE)
    foreach(feature USE_AUDIO SUPPORT_MODULE_RAUDIO SUPPORT_MODULE_RMODELS SUPPORT_CLIPBOARD_IMAGE
          SUPPORT_FILEFORMAT_PNG SUPPORT_FILEFORMAT_GIF SUPPORT_FILEFORMAT_QOI SUPPORT_FILEFORMAT_TTF
          SUPPORT_FILEFORMAT_FNT SUPPORT_SCREEN_CAPTURE SUPPORT_GIF_RECORDING SUPPORT_IMAGE_EXPORT
          SUPPORT_COMPRESSION_API SUPPORT_RPRAND_GENERATOR)
      set(${feature} OFF CACHE BOOL "Exclude unused Windows runtime features" FORCE)
    endforeach()
  endif()
  include(cmake/raylib.cmake)
endif()
if(WDS_BUILD_RAYLIB_PREVIEW)
  add_executable(wds_player_preview src/player_preview.cpp)
  target_link_libraries(wds_player_preview PRIVATE raylib wds_project wds_compile_options)
  if(MINGW)
    target_link_options(wds_player_preview PRIVATE -municode)
  endif()
endif()
if(WDS_BUILD_WINDOWS_PLAYER)
  add_executable(wds_player WIN32 src/player_preview.cpp src/player_windows.cpp)
  target_compile_definitions(wds_player PRIVATE WDS_PACKAGED_PLAYER)
  target_link_libraries(wds_player PRIVATE raylib wds_project wds_compile_options user32)
  set(kit_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows-player-kit.cmake")
  set(runtime_kit "$<TARGET_FILE_DIR:wds_player>/wds-runtime/windows-x64")
  set_property(TARGET wds_player APPEND PROPERTY LINK_DEPENDS
    "${kit_script}" "${kit_spec}" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lodepng/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/LICENSE.MIT"
    "${raylib_SOURCE_DIR}/LICENSE" "${raylib_SOURCE_DIR}/src/external/glfw/LICENSE.md")
  add_custom_command(TARGET wds_player POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DPLAYER=$<TARGET_FILE:wds_player>" "-DOUTPUT=${runtime_kit}" "-DLINKER=${CMAKE_LINKER}"
      "-DREPO=${CMAKE_CURRENT_SOURCE_DIR}" "-DRAYLIB=${raylib_SOURCE_DIR}" "-DSPEC=${kit_spec}"
      "-DTOOLCHAIN=${CMAKE_CXX_COMPILER_VERSION}"
      -P "${kit_script}"
    VERBATIM)
  install(DIRECTORY "${runtime_kit}" DESTINATION wds-runtime COMPONENT WindowsRuntime)
endif()

if(WDS_BUILD_MODERN_STUDIO)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets LinguistTools)
  qt_add_executable(wds_studio_modern WIN32
    src/studio/main.cpp
    src/studio/asset_preview.cpp
    src/studio/bridge.cpp
    src/studio/i18n/language.cpp
    src/studio/output_panel.cpp
    src/studio/resource_panel.cpp
    src/studio/scene_tree.cpp
    src/studio/theme/theme.cpp
    src/studio/viewport.cpp
    src/studio/voxel_viewport.cpp
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
  add_test(NAME workbench_behavior_smoke COMMAND wds_studio_modern --smoke
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/shared-behavior"
    "${CMAKE_CURRENT_BINARY_DIR}/w11-workbench.png")
  set_tests_properties(workbench_behavior_smoke PROPERTIES TIMEOUT 25)
  if(WIN32)
    add_test(NAME workbench_smoke COMMAND wds_studio_modern --smoke
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/reef-delivery")
    add_test(NAME workbench_voxel_smoke COMMAND wds_studio_modern --smoke
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/voxel-workshop")
    set_tests_properties(workbench_smoke workbench_voxel_smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 25)
  endif()
endif()

add_library(wds_voxel STATIC src/voxel_world.cpp src/sandbox_rules.cpp src/voxel_content.cpp)
target_include_directories(wds_voxel PUBLIC src)
target_link_libraries(wds_voxel PRIVATE wds_compile_options)
target_link_libraries(wds_project PRIVATE wds_voxel)

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
install(FILES third_party/lodepng/LICENSE DESTINATION licenses RENAME lodepng.txt)

add_executable(wds_plugin_test tests/plugin_host.cpp)
target_link_libraries(wds_plugin_test PRIVATE wds_m0 wds_compile_options)
add_test(NAME plugin_host COMMAND wds_plugin_test)

add_executable(wds_voxel_test tests/voxel_world.cpp)
target_link_libraries(wds_voxel_test PRIVATE wds_voxel wds_compile_options)
add_test(NAME voxel_world COMMAND wds_voxel_test)
add_executable(wds_w12_probe EXCLUDE_FROM_ALL examples/w12-lab/probe.cpp)
target_link_libraries(wds_w12_probe PRIVATE wds_project wds_compile_options)

add_executable(wds_voxel_project_test tests/voxel_project.cpp)
target_link_libraries(wds_voxel_project_test PRIVATE wds_project wds_compile_options)
add_test(NAME voxel_project COMMAND "${CMAKE_COMMAND}"
  -DTEST=$<TARGET_FILE:wds_voxel_project_test>
  -DROOT=${CMAKE_CURRENT_BINARY_DIR}/voxel-projects
  -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/voxel-workshop
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/voxel_project.cmake")
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
add_test(NAME behavior_project COMMAND "${CMAKE_COMMAND}"
  -DCLI=$<TARGET_FILE:wds_cli>
  -DROOT=${CMAKE_CURRENT_BINARY_DIR}/behavior-projects
  -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/shared-behavior
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/behavior_project.cmake")
add_test(NAME project_request COMMAND "${CMAKE_COMMAND}"
  -DCLI=$<TARGET_FILE:wds_cli>
  -DROOT=${CMAKE_CURRENT_BINARY_DIR}/project-requests
  -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/scene-motion
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_request.cmake")
add_executable(wds_scene_controls_test tests/scene_controls.cpp)
target_link_libraries(wds_scene_controls_test PRIVATE wds_project wds_compile_options)
add_test(NAME scene_controls COMMAND wds_scene_controls_test
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/examples/reef-delivery")
if(WDS_BUILD_WINDOWS_PLAYER)
  add_test(NAME project_export COMMAND "${CMAKE_COMMAND}"
    -DCLI=$<TARGET_FILE:wds_cli>
    -DKIT=${runtime_kit}
    -DROOT=${CMAKE_CURRENT_BINARY_DIR}/exports
    -DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/reef-delivery
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_export.cmake")
endif()
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
