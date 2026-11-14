# 开发指南

## 工作区与当前状态

从 [GitHub 仓库](https://github.com/bailacking/worlddesign-studio) 获取项目文档。引擎核心采用 C++。当前处于技术规划阶段，构建骨架已按 [试行基线](technical-evaluation.md#首轮试行基线) 交付，引擎源码与可运行原型待相关契约通过评审后实现。

## 目录职责

| 目录 | 责任 |
| --- | --- |
| `docs/product/` | 产品定位、范围和体验要求 |
| `docs/architecture/` | 按系统领域维护架构与接口要求 |
| `docs/engineering/` | 开发流程、质量标准与技术评估 |
| `docs/project/` | 实施阶段与交付门槛 |
| `docs/community/` | 社区治理与协作责任 |
| `docs/legal/` | 许可说明 |
| `docs/research/` | 外部技术研究、证据与适用边界 |

## 设计工作流

从真实用户任务定义问题，查阅所属章节，提出少量可比较方案，明确输入输出、失败处理、验证方式及影响范围。通过评审后更新主题文档和对应验收标准；未决定的实现保留在技术评估中。

方案说明应覆盖问题、约束、替代方案、推荐理由、实验计划、实际结果、迁移与退出路径。可以在议题或评审中讨论，不要求为每个设计点单独建立文档。

## 原型开发准备

第一轮原型验证插件生命周期、共享操作入口、最小 GUI/CLI、项目保存与恢复。按 [编码前的验收条件](technical-evaluation.md#编码前的验收条件) 完成规划和调研评审，再分配首批实现任务。

实验应小而可复现，记录依赖、平台、结果和限制。构建骨架已提供构建与测试命令，安装、运行及游戏入口命令随实现补充。完整游戏制作按 [实施路线](../project/roadmap.md) 推进。

## 构建与测试

构建骨架使用 CMake 3.25 起、Ninja 和 CTest，目标限定 C++20 并关闭编译器扩展。MSVC 启用 `/W4 /WX`，GCC 与 Clang 启用 `-Wall -Wextra -Wpedantic -Werror`。前提是本机已有 C++20 编译器、CMake 与 Ninja。

在仓库根目录依次执行，产物写入 `build/dev`：

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure --no-tests=error
```

`smoke` 用例运行 `wds_smoke`，输出实际 C++ 标准模式与 `std::span` 特性宏；标准低于 C++20 时测试失败。`build/` 与 `CMakeUserPresets.json` 已忽略，不进入仓库。

### Windows x64 开发者命令环境

Windows 需先进入带 MSVC 环境的终端，否则配置阶段找不到 `cl.exe`。任选其一：

- 开始菜单的 Developer PowerShell for VS 2022 或 x64 Native Tools Command Prompt
- 在普通命令行执行 `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"`

Visual Studio 2022 生成器可作备选，本轮统一使用 Ninja。实测版本为 MSVC 19.44.35228、CMake 3.31.6、Ninja 1.12.1，C++ 模式报 `202002`。

源码内容保持 ASCII：MSVC 在 CP936 代码页下遇到 UTF-8 中文注释会报 C4819，与 `/WX` 冲突。需要中文注释时须同时为 MSVC 加 `/utf-8`，该调整随首批引擎源码任务处理。

Linux 由 [质量检查](quality.md#当前自动检查) 中的 CI 跑同一组命令。当前分支未推送远端，Linux 结果待远端实际运行验证；macOS 未验证。

## 文档检查

检查本地链接与页面锚点、术语与实现状态、要求的主题归属、来源和许可说明。HTML 应保持单文件自包含，不能依赖远程字体、脚本、图片或运行时获取的文档内容。

跨主题改动应同步权威章节、概览摘要及验收标准。来源研究中的上游事实、WDS 设计方案和实际完成状态分别表达。临时调研缓存、备份和检查报告不作为产品规格正文。
