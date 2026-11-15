# 开发指南

## 工作区与当前状态

从 [GitHub 仓库](https://github.com/bailacking/worlddesign-studio) 获取项目。M0 源码在 `src/`，测试在 `tests/`。构建沿用 [试行基线](technical-evaluation.md#首轮试行基线)。

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

### 模块入口

按入口查调用方，再用 `rg` 搜类型名、操作 ID 或错误码。目录是实现入口，行为契约维护在对应规格中。

| 要改什么 | 先查现有实现 | 主要规格 |
| --- | --- | --- |
| 项目、路径、资源、修订、写入与恢复 | `src/platform_project.hpp/.cpp`。GUI 和 CLI 共用项目服务，旧工具的存储另见 `src/project_store.*` | [项目格式](../architecture/project-format.md) |
| 操作发现、CLI 和工作台调用 | `src/cli.cpp`、`project_operations_json()`、`src/studio/bridge.*`。公共入口负责参数与结果，调用方在项目服务中复用业务规则 | [操作与状态](../architecture/commands.md) |
| 工作台状态、视觉与交互 | `src/studio/main.cpp` 装配窗口；`src/studio/theme/` 管全局主题和通用控件；`scene_tree`、`resource_panel`、`output_panel`、`viewport` 管各自交互；`i18n/` 管语言 | [桌面工作台](../architecture/workbench.md#组件与交互规范) |
| 运行时与方块样例 | `src/platform_project.*` 的场景会话；`src/voxel_world.*` 与 `src/sandbox_rules.*`、`src/voxel_content.*` 管早期方块样例 | [游戏运行时](../architecture/runtime.md) |
| 插件生命周期与能力边界 | `src/plugin_host.*` 是现有宿主入口，新能力包跟随对应契约 | [插件与组合](../architecture/plugins.md) |
| 构建、依赖与覆盖范围 | `CMakeLists.txt`、`CMakePresets.json`、`.github/workflows/quality.yml`、`tests/` | [质量与验证](quality.md) |

### 设计与实验

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

源码使用 UTF-8。MSVC 构建选项包含 `/utf-8`，避免依赖系统代码页。

Linux CI 已通过已合并基线的默认构建与测试。现代工作台的 Linux 和 macOS 原生运行仍需验证。 [质量检查](quality.md#当前自动检查)

## 运行 M0 样例

构建后，在 PowerShell 中复制 [样例项目](../../examples/m0-project) 到构建目录下的两个新位置，分别用命令行和窗口改名：

```powershell
Copy-Item -Recurse examples/m0-project build/cli-demo
Copy-Item -Recurse examples/m0-project build/gui-demo
./build/dev/wds_cli.exe build/cli-demo open
./build/dev/wds_cli.exe build/cli-demo asset.rename B stone.txt 0
./build/dev/wds_editor.exe build/gui-demo
```

窗口中点击 **Open / Recover**，确认修订号为 0，再点击 **Rename**。两个项目的修订号都应变为 1，`assets/stone.txt` 保留源文件内容。用 `wds_cli PROJECT open` 可重开并检查修订。改名请求需指定当前修订号。

## 创建通用平台项目

从仓库根运行以下命令。目标目录须为新目录，父目录须已存在：

```powershell
./build/dev/wds_cli.exe project create build/first-project first-project
./build/dev/wds_cli.exe project open build/first-project
./build/dev/wds_cli.exe project validate build/first-project
./build/dev/wds_cli.exe project resources build/first-project
./build/dev/wds_cli.exe --project-operations
```

命令输出 JSON，包含状态、项目 ID 和修订号。打开与验证还返回入口场景和资源数量，`resources` 列出 ID、类型、路径及引用。失败附错误码和位置。新项目包含一个可启动的空场景。

Windows 可运行 `./build/dev/wds_studio.exe`，填入项目路径，选择 **Open** 或指定项目 ID 后选择 **Create**。窗口显示当前修订、入口场景、资源列表或诊断。窗口和 CLI 使用同一项目服务。[格式与诊断](../architecture/project-format.md)

### 预览与保存资源字段

从仓库根复制一份包含数据资源的测试样例到新目录，然后写入用于演示的规则数据：

```powershell
Copy-Item -Recurse tests/fixtures/project-v1/valid-with-refs build/field-demo
'{"jump_speed":6}' | Set-Content -Encoding ascii build/field-demo/assets/rules.json
./build/dev/wds_cli.exe project preview-field build/field-demo rules jump_speed 0 8
./build/dev/wds_cli.exe project set-field build/field-demo rules jump_speed 0 8
```

工作台打开项目后，输入资源 ID `rules`、字段 `jump_speed` 和新整数，先点击 **Preview**，核对差异，再点击 **Apply**。修改输入或目录会取消旧预览。若项目修订已变化，重新打开再预览。中断后若项目提示 `recovery_required`，使用 **Recover** 或 `wds_cli project recover DIRECTORY`。无法确认的磁盘状态会保留供检查。该样例演示项目数据编辑，运行时接入另行实现。

### 导入外部资源

外部文件可导入为 `data` 或 `scene` 资源，源文件按原字节保存。先预览，再提交。

```powershell
Copy-Item -Recurse examples/scene-editing build/import-demo
'{"title":"import demo","limit":30}' | Set-Content -Encoding ascii -NoNewline build/import-notes.json
./build/dev/wds_cli.exe project preview-import build/import-demo build/import-notes.json notes data assets/notes.json 0
./build/dev/wds_cli.exe project import build/import-demo build/import-notes.json notes data assets/notes.json 0
./build/dev/wds_cli.exe project imports build/import-demo
```

预览不落盘，项目文件字节保持不变。提交后修订加一，`project resources` 与重开都能读回新资源。再执行一次同样的导入返回 `changed=false`，修订保持不变。换内容占同一 ID 返回 `duplicate_id`，目标路径已属于其他资源返回 `duplicate_path`，两者都保留已有资源。源文件上限 1 MiB。中断后若提示 `recovery_required`，用 `project recover DIRECTORY` 回到原修订或保留已提交结果。[资源导入](../architecture/project-format.md#资源导入)

### 试玩通用场景

在仓库根执行以下命令，按入口场景的修订 0 运行 30 步。输出中的 `marker` 位于 `(-230, -25)`。传入 0 步可查看初始状态。

```powershell
./build/dev/wds_cli.exe project play examples/scene-motion 0 30
```

Windows 工作台填入同一项目路径，点击 **Open** 和 **Play**。下方画布显示静止的原点和移动的标记。点击 **Stop** 结束试玩，再点击 **Play** 可从源位置开始。CLI 与工作台都使用同一个会话核心。[场景格式与限制](../architecture/runtime.md#场景与试玩会话-v1)

### 运行现代工作台

在 [MSVC 开发环境](#windows-x64-开发者命令环境)中配置独立构建目录。已用 Qt 6.8.3 MSVC 2022 x64 构建，需安装 Qt Widgets 和 Linguist Tools。将示例里的路径替换成本机 Qt SDK 路径。

```powershell
$qt = 'C:/Qt/6.8.3/msvc2022_64'
cmake --preset dev -B build/studio -DCMAKE_BUILD_TYPE=Release -DWDS_BUILD_MODERN_STUDIO=ON "-DCMAKE_PREFIX_PATH=$qt"
cmake --build build/studio --target wds_studio_modern
& "$qt/bin/windeployqt.exe" --translations zh_CN --no-compiler-runtime ./build/studio/wds_studio_modern.exe
Copy-Item -Recurse examples/scene-motion build/studio-demo
./build/studio/wds_studio_modern.exe build/studio-demo
```

选中 `marker`，修改 **X 速度** 并点击 **保存属性**。界面显示新修订后，点击 **试玩** 和 **停止**。一轮可保存一个字段，未保存改动可在 **项目** 菜单放弃。停靠面板与窗口布局在关闭后保留。运行目录中需同时有 `wds_cli.exe` 和 Qt 运行库。上述命令用于本地开发，编辑器发行包由 SHIP-05 完成。 [工作台规格](../architecture/workbench.md)

工作台默认使用简体中文。在 **设置 → 界面语言**（英文界面为 **Settings → Interface Language**）选择语言，点击 **应用** 即时切换。语言设置随下次启动恢复。工作台翻译随程序编译，`translations/qt_zh_CN.qm` 用于 Qt 文件选择等标准控件。

用 **场景 → 新建实体** 添加零位置、零速度的实体。选中实体后可复制、删除或调整它在场景树中的顺序。结构修改在操作完成时保存，属性草稿需先保存或放弃。工作台与 CLI 共用场景编辑操作。[CLI 用法](../architecture/commands.md)

用 **编辑 → 撤销场景改动** 或 **重做场景改动** 恢复本次打开项目后的编辑，操作会保存为新的修订。重新打开项目会清空界面历史。[CLI 恢复接口](../architecture/commands.md#事务撤销与外部副作用)

[四方汇流示例](../../examples/scene-editing/README.md) 用四个实体演示运动、属性编辑和资源引用。在二维视口按住中键拖动可平移，滚轮以鼠标所在点为中心缩放，双击中键复位。资源区显示选中项引用谁、被谁引用。输出区可按级别筛选并复制消息。

可把同一项目的资源、实体与修订生成 Markdown，交给另一个创作者或 Agent。

```powershell
python scripts/project_handoff.py --cli ./build/studio/wds_cli.exe --project ./examples/scene-editing
```

## 运行方块沙盒

Windows 构建完成后执行 `./build/dev/wds_sandbox.exe`。点击窗口锁定鼠标，移动鼠标转向，WASD 移动，空格跳跃，方向键也可转向。对准方块时准星变黄，左键破坏、右键放置石块，成功时准星闪绿。F5 保存，Esc 先释放鼠标，再按一次退出。重开恢复地形，玩家回到出生点。

存档位于 `%LOCALAPPDATA%\WDS\sandbox-v1.dat`。首次打开生成默认地形，损坏的旧存档会保留并提示当前会话无法覆盖。

### 从模板创建可编辑项目

在一个已有目录下为项目选一个新的目录名。命令行创建的项目可交给人类继续编辑。

```powershell
./build/dev/wds_cli.exe --sandbox-project build/stone-island create
./build/dev/wds_cli.exe --sandbox-project build/stone-island show
./build/dev/wds_cli.exe --sandbox-project build/stone-island preview 6 8
./build/dev/wds_cli.exe --sandbox-project build/stone-island apply 6 8
./build/dev/wds_sandbox.exe --project build/stone-island
```

窗口中按 F6 编辑跳跃规则，用加减键预览数值，Enter 应用，Esc 取消。变更保存到项目的 `sandbox-rules-v1.txt`，命令行可再次读取。F5 将地形保存到同一项目的 `sandbox-v1.dat`。项目内的规则和地形可随目录复制和交接。

创建项目时可选择 `meadow`（默认）或 `mesa`。台地版使用暖色天空、不可破坏的石块、默认放置泥土和跳跃速度 8。两个预设使用同一个可执行文件。

```powershell
./build/dev/wds_cli.exe --sandbox-project build/mesa-demo create mesa
./build/dev/wds_cli.exe --sandbox-project build/mesa-demo show
./build/dev/wds_sandbox.exe --project build/mesa-demo
```

窗口中按 F7 编辑右键放置的方块。按 1 选草地、2 选泥土、3 选石块，Enter 应用，Esc 取消。天空和方块的颜色及破坏规则可在 [项目内容文件](../architecture/runtime.md#方块项目内容) 中修改，重开项目生效。命令行 `show` 会报告内容格式错误。

### 预览、应用与恢复规则

外部 Agent 可先查询当前跳跃速度并展示差异。用户确认后，用当前值作为前置条件应用修改。以下示例假定原值为 6：

```powershell
$rulesPath = Join-Path $env:LOCALAPPDATA 'WDS/sandbox-rules-v1.txt'
./build/dev/wds_cli.exe --sandbox-rules $rulesPath show
./build/dev/wds_cli.exe --sandbox-rules $rulesPath preview 6 8
./build/dev/wds_cli.exe --sandbox-rules $rulesPath apply 6 8
# 重开沙盒，观察 Jump speed: 8 并尝试跳跃。
./build/dev/wds_cli.exe --sandbox-rules $rulesPath apply 8 6
```

窗口在启动时读取规则。有效整数为 3 到 9。预期值过时会返回冲突，文件保持原值。试验按单写入者运行，规则文件与地形存档独立。

### 生成独立运行目录

在 Windows x64 的 MSVC 开发者终端执行：

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure --no-tests=error
cmake --install build/release --prefix build/m1-windows-x64
```

产物含 `wds_sandbox.exe`、`wds_cli.exe`、`LICENSE` 与 JSON 依赖许可。可从产物目录直接启动沙盒，Release 版采用静态 MSVC 运行库。

## 文档检查

检查本地链接与页面锚点、术语与实现状态、要求的主题归属、来源和许可说明。HTML 应保持单文件自包含，不能依赖远程字体、脚本、图片或运行时获取的文档内容。

跨主题改动应同步权威章节、概览摘要及验收标准。来源研究中的上游事实、WDS 设计方案和实际完成状态分别表达。临时调研缓存、备份和检查报告不作为产品规格正文。
