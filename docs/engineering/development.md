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

Linux 由 [质量检查](quality.md#当前自动检查) 中的 CI 配置构建和测试，远端结果待验证。macOS 未验证。

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

### 试玩通用场景

在仓库根执行以下命令，按入口场景的修订 0 运行 30 步。输出中的 `marker` 位于 `(-230, -25)`。传入 0 步可查看初始状态。

```powershell
./build/dev/wds_cli.exe project play examples/scene-motion 0 30
```

Windows 工作台填入同一项目路径，点击 **Open** 和 **Play**。下方画布显示静止的原点和移动的标记。点击 **Stop** 结束试玩，再点击 **Play** 可从源位置开始。CLI 与工作台都使用同一个会话核心。[场景格式与限制](../architecture/runtime.md#场景与试玩会话-v1)

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
