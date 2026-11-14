cmake_minimum_required(VERSION 3.25)

project(
  WorldDesignStudio
  DESCRIPTION "世界设计工作室引擎核心"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译告警统一挂在接口目标上，后续引擎目标直接链接即可继承同一组规则。
add_library(wds_compile_options INTERFACE)
if(MSVC)
  # /Zc:__cplusplus 让 __cplusplus 反映真实的 /std:c++20 模式，否则恒为 199711L。
  target_compile_options(wds_compile_options INTERFACE /W4 /WX /Zc:__cplusplus)
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
