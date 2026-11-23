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
| 运行时与方块样例 | `src/platform_project.*` 装载指定修订，`src/scene_play.*` 推进二维会话；`src/voxel_world.*` 与 `src/sandbox_rules.*`、`src/voxel_content.*` 管早期方块样例 | [游戏运行时](../architecture/runtime.md) |
| 红石逻辑步与局域网前期验证 | `src/signal_circuit.*` 和 `examples/w15-lab/`，复跑信号顺序、服务端提交与分层换装 | [W15 试验](../project/w15-runtime-lab.md) |
| 物品、容器、配方与掉落 | `src/workshop.*` 执行共用规则，`src/workshop_document.*` 校验目录和进度，二维会话与方块项目提供来源 | [W14 物品与配方](../project/w14-workshop.md) |
| 种子地形、区块缓存、地块导航 | `src/voxel_terrain.*`、`src/tile_world.*`，由玩家移动和二维会话更新已加载区域 | [W13 双世界](../project/w13-worlds.md) |
| 代码、节点图和规则执行 | `src/behavior.cpp` 解析与编译，`src/scene_play.*` 在二维步进内派发事件 | [行为语义](../architecture/authoring.md#共同语义基础) |
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

也可在[现代工作台](#运行现代工作台)用 **项目 → 新建项目** 或 **打开项目**。窗口显示当前修订、场景、资源或诊断。窗口和 CLI 使用同一项目服务。 [格式与诊断](../architecture/project-format.md)

### 预览与保存资源字段

从仓库根复制一份包含数据资源的测试样例到新目录，然后写入用于演示的规则数据：

```powershell
Copy-Item -Recurse tests/fixtures/project-v1/valid-with-refs build/field-demo
'{"jump_speed":6}' | Set-Content -Encoding ascii build/field-demo/assets/rules.json
./build/dev/wds_cli.exe project preview-field build/field-demo rules jump_speed 0 8
./build/dev/wds_cli.exe project set-field build/field-demo rules jump_speed 0 8
```

工作台打开一份未执行上述提交的项目副本，选择 **编辑 → 编辑整数数据字段**。填写资源 ID `rules`、字段 `jump_speed` 和新值，点 **预览** 后确认差异。若项目修订已变化，重新打开再预览。中断后若项目提示 `recovery_required`，使用 **项目 → 恢复项目** 或 `wds_cli project recover DIRECTORY`。无法确认的磁盘状态会保留供检查。

### 导入外部资源

外部文件可导入为 `data`、`scene`、PNG `image` 或 UTF-8 `text` 资源，源文件按原字节保存。

在工作台打开项目副本，选择 **项目 → 导入资源**。选源文件，检查资源 ID、类型和 `assets/` 下的目标路径，点击 **预览**。核对确认框的文件名、目标与大小后点击 **导入**。在资源面板选中新资源可见 **导入来源**。重新打开项目后仍可查看，同一资源重复导入时修订和数量保持原值。已有入口场景由项目清单决定。

可以把单个 PNG、TXT、MD 或 JSON 文件拖到工作台，类型和目标扩展名会预填。选中图片查看缩略图，选中文本查看行数与纯文本试读。**项目 → 清理缩略图缓存** 清理本机工作台的缩略图，当前选中的图片立即重建。旧场景工程无需迁移即可执行这些操作。

命令行复用相同项目服务。

```powershell
Copy-Item -Recurse examples/scene-editing build/import-demo
'{"title":"import demo","limit":30}' | Set-Content -Encoding ascii -NoNewline build/import-notes.json
./build/dev/wds_cli.exe project preview-import build/import-demo build/import-notes.json notes data assets/notes.json 0
./build/dev/wds_cli.exe project import build/import-demo build/import-notes.json notes data assets/notes.json 0
./build/dev/wds_cli.exe project imports build/import-demo
```

预览不落盘，项目文件字节保持不变。提交后修订加一，`project resources` 与重开都能读回新资源。再执行一次同样的导入返回 `changed=false`，修订保持不变。换内容占同一 ID 返回 `duplicate_id`，目标路径已属于其他资源返回 `duplicate_path`，两者都保留已有资源。源文件上限 1 MiB。中断后若提示 `recovery_required`，用 `project recover DIRECTORY` 回到原修订或保留已提交结果。[资源导入](../architecture/project-format.md#资源导入)

### 生成与修补内容

[样例源文件](../../examples/content-generation/entries.json)来自 [W2 参考用例](../project/w2-references.md)，包含重生锚状态范围、小麦与面包、花衬衫的已知用料、巨人国默认季节。样例保留来源 ID。《饥荒》配方的 `layout: "partial"` 只表达已知材料要求，历史原版行为仍待核对。

```powershell
python scripts/content_bundle.py expand examples/content-generation/entries.json build/content-demo/bundle.json
python scripts/content_bundle.py validate build/content-demo/bundle.json
Copy-Item examples/content-generation/entries.json build/content-demo/edited.json
```

用文本编辑器在 `edited.json` 修改 `mc-bread` 的名称，按 ID 生成一份修补后的数据包。

```powershell
python scripts/content_bundle.py repair build/content-demo/edited.json build/content-demo/bundle.json build/content-demo/repaired.json mc-bread
```

源文件的 `entries` 按稳定 ID 列出 `kind`、参考用例 `reference` 和 `rule`。方块规则把整数范围展开为状态，物品提供名称，配方引用物品 ID，状态规则把启用项展开为记录。校验核对字段、整数区间、重复状态、引用对象和展开结果。把重生锚范围改成 `[4, 0]`，或把配方用料改成缺失的 ID，可看到带字段位置的 `invalid_rule` 和 `missing_reference`。

修补以现有包为输入，仅从源文件替换指定条目，再校验整个结果。所有命令返回 JSON 诊断，校验失败保留已有文件。有效结果可作为数据资源导入项目。运行中消费这些规则由后续运行时与玩法任务接入。

### 试玩通用场景

在仓库根执行以下命令，按入口场景的修订 0 运行 30 步。输出中的 `marker` 位于 `(-230, -25)`。传入 0 步可查看初始状态。

```powershell
./build/dev/wds_cli.exe project play examples/scene-motion 0 30
```

在现代工作台打开同一项目，点击 **试玩** 和 **停止**。视口显示静止的原点和移动的标记。重新试玩从源位置开始。CLI 与工作台都使用同一个会话核心。 [场景格式与限制](../architecture/runtime.md#场景与试玩会话-v1)

### 试玩礁灯投递

用 [现代工作台](#运行现代工作台)打开 `examples/reef-delivery`，按 **F5** 开始，使用 **WASD** 或 **方向键**移动，将信送到绿色信站。**Shift+F5** 停止试玩。可以先复制样例再编辑。试玩只读取保存后的修订，停止后会丢弃运行位置和胜负状态。

在仓库根可选构建 raylib 5.5 试验播放器。配置时下载锁定的 5.5 源码，也可用 `FETCHCONTENT_SOURCE_DIR_RAYLIB` 指向已有的同版本源码。

```powershell
cmake --preset dev -B build/scene-preview -DCMAKE_BUILD_TYPE=Release -DWDS_BUILD_RAYLIB_PREVIEW=ON
cmake --build build/scene-preview --target wds_player_preview
./build/scene-preview/wds_player_preview.exe examples/reef-delivery
```

试验播放器直接读取项目文件夹，**Esc** 退出。可用正式导出入口把当前修订封装成独立游戏。 [示例操作](../../examples/reef-delivery/README.md)

### 导出 Windows x64 游戏

在 MSVC x64 开发环境中配置单独的播放器构建目录。Qt 工作台和独立播放器分别构建。播放器套件会在链接后生成，包含实际二进制、系统依赖、插件版本和许可。

```powershell
cmake --preset dev -B build/windows-player -DCMAKE_BUILD_TYPE=Release -DWDS_BUILD_WINDOWS_PLAYER=ON
cmake --build build/windows-player --target wds_player wds_cli
./build/windows-player/wds_cli.exe project export examples/reef-delivery 0 windows-x64 build/reef-release
```

在 [工作台构建目录](#运行现代工作台)装上同一套件，再通过 **项目 → 导出 Windows x64** 选择项目外的父目录和新文件夹名。导出菜单在保存属性或放弃草稿、停止试玩后可用。目标文件夹已存在时会提示改名。

```powershell
cmake --install build/windows-player --prefix build/studio --component WindowsRuntime
```

二维与三维方块项目走同一导出入口。方块工程可以用 `wds_cli project create DIRECTORY ID --kind voxel` 创建，再用声明 `voxel_format: 1` 的运行套件发布。 [双类型样品](../../examples/w16-delivery/README.md)

复制完整导出文件夹即可转移。双击 **Play.exe** 开始，**Esc** 退出。脚本可运行 `Play.exe --check` 检查发布锁和场景装载，成功返回 0。游戏所需环境和锁内字段见 [Windows x64 发布包](../architecture/platforms.md#windows-x64-发布包)。

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

本机可复跑与 Windows CI 相同的工作台冒烟。将 Qt 的 `bin` 加入 `PATH` 后，运行 `ctest --test-dir build/studio -R '^workbench_(voxel_|workyard_)?smoke$' --output-on-failure`。测试离屏启动工作台，并绘制「礁灯投递」「方块工坊」和「块石工场」的视口。

### 启动工作台与试玩

在仓库根目录运行下列入口。默认读取 `build/native`，按上文构建在 `build/studio` 时，给命令加 `--build build/studio`。

| 入口 | 命令 |
| --- | --- |
| 工作台 | `python scripts/launch.py studio` |
| 方块游戏「块石工场」 | `python scripts/launch.py voxel` |
| 非方块游戏「风园」 | `python scripts/launch.py planar`，打开浏览器试玩。 |

两个工作台入口支持 `--project <目录>`。浏览器入口在回环地址运行，终端会显示本地 URL，按 **Ctrl+C** 关闭。三个入口都支持 `--check` 验证启动路径。

### 编辑方块工坊

复制 `examples/voxel-workshop` 到可写目录，在同一个工作台打开。用 WASD 移动、方向键转头，准星指向方块后左键破坏、右键放置。按 Ctrl+S 再重开工程可检查保存结果。工作台使用当前工程的材质资源。 [试用操作](../../examples/voxel-workshop/README.md)

用 **场景 → 新建实体** 添加零位置、零速度的实体。选中实体后可复制、删除或调整它在场景树中的顺序。结构修改在操作完成时保存，属性草稿需先保存或放弃。工作台与 CLI 共用场景编辑操作。[CLI 用法](../architecture/commands.md)

用 **编辑 → 撤销场景改动** 或 **重做场景改动** 恢复本次打开项目后的编辑，操作会保存为新的修订。重新打开项目会清空界面历史。[CLI 恢复接口](../architecture/commands.md#事务撤销与外部副作用)

[四方汇流示例](../../examples/scene-editing/README.md) 用四个实体演示运动、属性编辑和资源引用。在二维视口按住中键拖动可平移，滚轮以鼠标所在点为中心缩放，双击中键复位。资源区显示选中项引用谁、被谁引用。输出区可按级别筛选并复制消息。

可把同一项目的资源、实体与修订生成 Markdown，交给另一个创作者或 Agent。

```powershell
python scripts/project_handoff.py --cli ./build/studio/wds_cli.exe --project ./examples/scene-editing
```

### 复跑共享操作

在仓库根使用带 W10 的 `wds_cli` 和未改动的「运动场景」副本。先读取公共操作清单，再运行示例计划。

```powershell
$trial = Join-Path (Get-Location) ('build/w10-replay-' + [guid]::NewGuid().ToString('N'))
Copy-Item examples/scene-motion $trial -Recurse
./build/studio/wds_cli.exe --project-operations
python scripts/project_workflow.py --cli ./build/studio/wds_cli.exe --project $trial --plan "$trial/w10-replay.json" --allow-project-write
```

计划先预览并取消，再提交属性修改，验证重试旧修订及无效字段的结果，最后通过保存回执回退。完成时项目到修订 2，场景内容与初始相同。工作台的 **项目 → 可用操作** 显示公共清单与本窗口请求用量。 [JSON 请求与诊断](../architecture/commands.md#平台操作接口-v1)

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
