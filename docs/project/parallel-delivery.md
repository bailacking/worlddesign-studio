# 并行开发计划

本页是 [开发任务](development-tasks.md) 的实施分解。父任务继续表示系统目标，本页任务用于认领、提交与合并。任务状态随可用产物推进。目标为现代工作台、可复用创作能力及可交付项目，以非方块项目和 1.6.4 参考项目持续检验。

## 开工与交接

每位执行者从指定基线使用独立分支和工作树。依赖项的实际产物进入工作树后才开始接入。目录是预计归属，尚未存在的目录由相应任务创建。新增公共接口、文件格式、全局构建配置或第三方依赖由对应的「核心」任务统一提交。实现者优先把模块交付在自己的目录，公共入口由所属工作流负责人集中接线。同一文件的改动按合并顺序串行。

- **核心**由 Codex 负责设计、关键实现、兼容性与集成。
- **实现**在接口、范围与验收明确后，由 Codex 判断是否值得手动派发。规则明确、改动局部的重复实现优先派发。
- **批量**由用户选择 Agent 手动派发。任务卡给出输入、文件范围与完成标志。一个任务只分配一次，交接用一行 `任务｜提交或产物｜实际检查｜阻塞`。
- 有实现就交付实现。功能验收使用本任务的一条真实路径，复用已有构建和检查。时长与 Token 对照放到正式交付基准阶段。

## 第一批可派发

从含本计划的提交开工。首批三个模块各自占用独立工作树，接口见 [桌面工作台](../architecture/workbench.md#首批可并行交付的组件)。WKB-01 集中接入 CMake 和程序入口。共享 Qt SDK 未就绪时，可先完成独立文件并在交接中注明编译待集成。主题、场景树和资源面板可并行提交。

| 任务 | 任务分支 | 只改 | 可交付的结果 |
| --- | --- | --- | --- |
| WKB-02 | `task/wkb-02` | `src/studio/theme/workbench.qss` | 一套可加载的工作台深色样式，四种交互状态清楚可辨 |
| WKB-04 | `task/wkb-04` | `src/studio/scene_tree.hpp/.cpp` | 读真实快照的实体 ID，列表可搜索、键盘选择、回调稳定 ID |
| WKB-05 | `task/wkb-05` | `src/studio/resource_panel.hpp/.cpp` | 读真实资源清单，按 ID、路径及类型筛选并回调资源 ID |

Codex 并行承担 WKB-01、WKB-03、EDT-03A 和 EDT-03B。先合入窗口骨架和只读数据接入，再把场景树、资源、视口和属性接入同一项目会话。图形运行后端、能力包契约和 1.6 资料结构可同时推进。

现代工作台已接入主题、场景树与资源面板。资源面板可以按 ID 或路径搜索、按类型筛选。入口场景支持逐项保存位置或速度字段，并从保存后的修订试玩。当前界面与已有诊断支持简体中文和英文即时切换。 [运行步骤](../engineering/development.md#运行现代工作台)

## 第二批可派发

以下五项可从同一批次的任务卡提交独立开工。可用接口来自已经合入的现代工作台和项目查询命令。每项只改对应目录，交接写一行 `任务｜提交｜实际验证｜阻塞`。Codex 在主工作树负责场景写入、CLI 和工作台接线。

| 任务 | 分支 | 改动范围 | 交付与验收 |
| --- | --- | --- | --- |
| WKB-07A 输出面板 | `task/wkb-07a` | `src/studio/output_panel.hpp/.cpp` | 新建 `wds::studio::OutputPanel : public QWidget`，构造函数接受 `QWidget* parent = nullptr`。提供 `setEntries(const QJsonArray&)`，每项含已本地化的 `text` 和稳定级别 `info/warning/error`。按入参顺序显示，支持按级别筛选和复制选中行，双语文案使用独立的 `OutputPanel` 翻译上下文。用三条不同级别的记录演示筛选和复制。 |
| WKB-08A 二维视口导航 | `task/wkb-08a` | `src/studio/viewport.hpp/.cpp` | 保持现有构造、选中和快照接口，编辑态增加中键平移、滚轮以鼠标为锚缩放、双击中键复位。仅改变视图，点击选实体时使用同一坐标变换。以 `scene-motion` 验证缩放和平移后仍能选中 `marker`，试玩期间这些快捷操作让给运行态。 |
| PRD-04A 资源引用查看 | `task/prd-04a` | `src/studio/resource_panel.hpp/.cpp` | 复用 `project resources` 返回的 `refs`，在资源面板显示选中项的直接引用和被引用项。按稳定 ID 显示，用 `valid-with-refs` 样例验收 `intro → rules` 双向查看，筛选隐藏某行时仍按全量资源计算。只读，保留现有选择回调和语言切换。 |
| SIM-14A 非方块编辑示例 | `task/sim-14a` | `examples/scene-editing/` | 用当前场景 v1 制作一个原创二维运动示例，包含至少三个可辨识实体和一个被入口场景引用的数据资源。附简短中文操作卡，列出三项可修改参数和预期效果。用 CLI 验证、试玩 30 步并记录一个实体的预期位置。 |
| AUT-03A 项目交接摘要 | `task/aut-03a` | `scripts/project_handoff.py` | 用 Python 标准库实现 `--cli 路径 --project 目录`，调用 `project resources` 后按返回修订查询 `project scene`。以 Markdown 输出项目 ID、修订、入口、资源和实体列表，支持空场景。子进程失败或修订冲突时报错并返回非零。用 `scene-motion` 及缺清单目录各跑一次。 |

各组件可单独编译验证，CMake、工作台入口和共享翻译文件由集成时统一接线。WKB-07A、WKB-08A、PRD-04A 复用项目现有 Qt 版本。SIM-14A 和 AUT-03A 使用现有 CLI。

## 合并顺序与交付点

| 批次 | 核心交付 | 完成标志 |
| --- | --- | --- |
| 1 现代工作台 | 工作区、场景树、资源、属性、二维视口 | 打开真实项目，选中实体，改属性并保存，停止后可重跑。界面布局可调整 |
| 2 创作与发布 | 项目操作、实体编辑、资产导入、实时输入、独立导出 | 用工作台完成一个可交给另一台机器运行的非方块项目。人可从源码项目继续编辑 |
| 3 能力包与 AI | 能力发现、模板、行为、共享操作、Agent 交付 | AI 组合能力包并交付项目。人能修改行为并重新导出。两个不同项目使用同一流程 |
| 4 方块能力 | 区块、方块数据、背包配方、环境、生物和多人 | 各机制进入可替换的能力包，参考项目能使用工具重组内容和规则 |
| 5 全量交付 | 1.6 内容与行为矩阵、许可、计时与人工续编 | 冻结版本后逐项核对范围，在声明环境下测完整的 10 分钟及 Token 指标 |

### 下一批放行条件

- WKB-03 接通真实项目读取后，WKB-07 可开工。EDT-03A 与 EDT-03B 落盘后，WKB-06 可开工。用 WKB-13 收口，不要求等到 3D 渲染、行为图或资源导入才交付工作台。
- PRD-05 的输入、输出和错误固定后，将图像与文本适配器分别派发。SHIP-01 固定构建清单后，发行资源收集可与独立播放器并行。
- MAT-01 冻结条目结构后可先派发世界与探索盘点。其余条目按数据格式分别放行，每批按自己的目录写入。内容编写与引擎功能的负责人分别提交，集成时以数据能加载为准。
- Codex 暂不可用时，按此表继续认领已有接口和实际依赖的任务。工作树基线用依赖的已合并提交，或集成负责人给出的明确分支。接口未定时可换到已经放行的任务。

以下表中的代码路径描述交付位置，未出现的任务 ID 引用 [父任务表](development-tasks.md)。前置指所需的接口或已有产物，例如 WKB-03 使用 OPS-03 已有的查询入口。

## 现代工作台

| 任务 | 分工 | 前置 | 交付位置 | 完成标志 |
| --- | --- | --- | --- | --- |
| WKB-01 | 核心 | PRJ-02 | src/studio/、CMakeLists.txt | 可选的 Qt 工作区入口、菜单、停靠面板和布局持久化。无项目时动作状态正确，实际启动显示工作区 |
| WKB-02 | 批量 | 工作台规格 | src/studio/theme/ | 深色样式覆盖主要控件，清晰显示键盘焦点、选中、禁用及错误 |
| WKB-03 | 核心 | WKB-01、OPS-03 | src/studio/bridge/ | 调用同构建 CLI，解析状态、诊断和快照。项目切换后旧结果失效，带修订的写入按序执行 |
| WKB-04 | 批量 | 工作台规格 | src/studio/scene_tree.hpp/.cpp | 用真实实体 ID 填充、搜索、选择并返回稳定 ID |
| WKB-05 | 批量 | 工作台规格 | src/studio/resource_panel.hpp/.cpp | 展示真实资源数据，支持文本和类型过滤、空态及完整路径提示 |
| WKB-06 | 实现 | WKB-03、EDT-03A、EDT-03B | src/studio/inspector/ | 字段按类型显示，提交前看见改动，冲突保留草稿并说明当前修订 |
| WKB-07 | 实现 | WKB-03 | src/studio/output/ | 分级展示真实诊断，按资源定位，过期会话的输出有明确归属 |
| WKB-08 | 核心 | WKB-03、SIM-01 | src/studio/viewport/ | 视口与场景选择联动，运行会话进出可控，结束后返回编辑选中态 |
| WKB-09 | 实现 | WKB-06、WKB-08、CMD-04 | src/studio/gizmos/ | 二维移动工具带吸附和取消，拖动提交合并成一次可撤销操作 |
| WKB-10 | 实现 | WKB-05、PRD-05 | src/studio/import/ | 拖入一个图像或文本后显示来源和导入结果，选中项可预览 |
| WKB-11 | 实现 | WKB-03、SHIP-04 | src/studio/export/ | 选择导出目标与目录，可取消并定位失败，成功后打开成品目录 |
| WKB-12 | 实现 | WKB-01、WKB-06、WKB-07 | src/studio/i18n/ | 首批菜单、操作和诊断可切换简体中文与英文，资源 ID 保持稳定 |
| WKB-13 | 核心 | WKB-02、WKB-04、WKB-05、WKB-06、WKB-07、WKB-08 | src/studio/ | 集成第一条创作路径，真实项目能编辑保存和试玩，缩放与键盘可操作 |
| WKB-14 | 核心 | WKB-13、SHIP-05 | CMakeLists.txt、docs/engineering/development.md | 现代工作台通过相同操作后成为文档默认入口，原版项目继续可读 |

## 项目数据与共享操作

| 任务 | 分工 | 前置 | 交付位置 | 完成标志 |
| --- | --- | --- | --- | --- |
| PRD-01 | 核心 | EDT-03A、EDT-03B | src/platform_project.* | 统一场景字段元信息和写入的边界，保存后工作台与 CLI 查询一致 |
| PRD-02 | 核心 | PRD-01、OPS-02 | src/platform_project.* | 实体创建、复制、删除和顺序变更，重开后 ID 及引用稳定 |
| PRD-03 | 核心 | PRD-02 | src/platform_project.* | 场景层级与父子变换，防环、旧场景保持可读，变更可撤回 |
| PRD-04 | 核心 | PRJ-05、CMD-02 | src/resources/index/ | 资源移动和删除前显示引用方，改名不改 ID，仍有引用时可取消 |
| PRD-05 | 核心 | PRJ-06、CMD-02 | src/resources/import/ | 导入接口保存原始资源和来源，重复导入可复用，失败保留旧资产 |
| PRD-06 | 实现 | PRD-05 | src/resources/importers/image/ | 支持一类图像导入、尺寸和缩略图，错误可定位原文件 |
| PRD-07 | 实现 | PRD-05 | src/resources/importers/text/ | 识别 UTF-8 文本，输出内容摘要和规范化诊断 |
| PRD-08 | 核心 | PRD-05、PRD-06、PRD-07 | src/resources/cache/ | 源和派生物分离，按输入指纹增量重建，缓存可清除重建 |
| PRD-09 | 核心 | PRJ-03、PRD-01 | src/migrations/ | 旧通用项目、旧方块项目显式迁移或报告不支持项，原目录可重开 |
| PRD-10 | 核心 | PRD-04、CMD-04 | src/resources/diff/ | 用稳定 ID 比较结构化改动，区分内容冲突和路径冲突 |
| CMD-01 | 核心 | OPS-01、OPS-03 | src/operations/registry/ | 统一 JSON 请求、结果及字段说明，旧 CLI 参数可用 |
| CMD-02 | 核心 | CMD-01、PRJ-04 | src/operations/transaction/ | 带预期修订的多文件提交，失败可诊断和恢复 |
| CMD-03 | 实现 | CMD-01 | src/operations/cli/ | 接入发现、查询和预览，保留退出码和 JSON 兼容性 |
| CMD-04 | 核心 | CMD-02、OPS-04 | src/operations/history/ | 撤销重做走相同写入路径，重开历史可解释，外部冲突不静默覆盖 |
| CMD-05 | 核心 | CMD-02、CMD-04 | src/operations/batch/ | 批次按声明的原子范围提交，取消能说明哪些已落盘 |
| CMD-06 | 实现 | CMD-01 | src/operations/diagnostics/ | 结构化错误带操作、资源、字段和修订，工作台与 CLI 可定位 |

## 运行时与脚本

| 任务 | 分工 | 前置 | 交付位置 | 完成标志 |
| --- | --- | --- | --- | --- |
| SIM-01 | 核心 | RUN-01 | src/runtime/session/ | 从现有场景运行核心提供开始、步进、快照和停止，交互式预览无需逐帧创建进程，输入适配可后接 |
| SIM-02 | 核心 | SIM-01、RUN-03 | src/runtime/render/ | 图形后端第一条可替换接口，能显示真实场景、视口与尺寸变化 |
| SIM-03 | 核心 | SIM-01、RUN-03 | src/runtime/input/ | 动作映射和输入焦点可重绑定，退出试玩归还键鼠，失焦清除按键 |
| SIM-04 | 核心 | SIM-01、SIM-03 | src/runtime/physics/ | 固定步长、碰撞查询和角色控制，移动、跳跃和地面边界可复现 |
| SIM-05 | 实现 | SIM-02、PRD-06 | src/runtime/sprites/ | 2D 精灵读取图像、变换和层级，有缺失资产提示 |
| SIM-06 | 核心 | SIM-02、PRD-03 | src/runtime/scene3d/ | 3D 相机和基础几何共用场景身份，加载与切换生命周期一致 |
| SIM-07 | 核心 | SIM-01、PRD-05 | src/runtime/audio/ | 独立音频后端可加载和播放一类声音，停机释放资源 |
| SIM-08 | 实现 | SIM-05、SIM-06 | src/runtime/animation/ | 数据驱动的关键帧和播放状态，可用同一时间语义推进 |
| SIM-09 | 核心 | SIM-01、CMD-02 | src/runtime/save/ | 运行存档分离于项目源，坏档保留并报告，支持显式版本迁移 |
| SIM-10 | 核心 | SIM-01、SIM-09 | src/runtime/net/ | 权威实例、消息版本和重连，基础状态用两个本地客户端复现 |
| SIM-11 | 核心 | LOG-01、SIM-01 | src/logic/model/ | 定义类型、事件与状态边界，行为可由运行时加载并定位错误 |
| SIM-12 | 核心 | SIM-11、LOG-02 | src/logic/script/ | 首个脚本适配器提供生命周期、权限及错误定位，可替换后端 |
| SIM-13 | 核心 | SIM-11、WKB-13 | src/studio/logic/ | 同一行为模型的节点编辑器，改图保存后运行结果可复验 |
| SIM-14 | 实现 | SIM-12、SIM-13 | examples/shared-behavior/ | 同一行为用于两个项目，展示可表达子集的编辑及重开 |
| SIM-15 | 实现 | SIM-06、SHIP-04 | docs/engineering/ | 按已有工作流记录 Linux、macOS、Web 支持状态及真实阻塞 |

## 能力包、AI 与发行

| 任务 | 分工 | 前置 | 交付位置 | 完成标志 |
| --- | --- | --- | --- | --- |
| KIT-01 | 核心 | CAP-01、CMD-01 | src/capabilities/manifest/ | 清单声明能力版本、依赖、配置字段和编辑器或运行归属 |
| KIT-02 | 核心 | KIT-01 | src/capabilities/resolve/ | 确定依赖顺序，锁定版本，说明缺失、冲突和循环 |
| KIT-03 | 核心 | KIT-02、CMD-02 | src/capabilities/templates/ | 把锁定模板实例化为新项目，保留来源与可改字段，模板源不变 |
| KIT-04 | 实现 | KIT-03、SIM-05、SIM-11 | examples/starter-2d/ | 原创非方块模板包含一条可修改玩法，运行时和工作台能打开 |
| KIT-05 | 核心 | KIT-02、KIT-03 | src/capabilities/install/ | 能力安装、替换、禁用与回退，依赖变化有诊断 |
| KIT-06 | 实现 | KIT-01、KIT-02、KIT-05 | docs/engineering/、examples/capability/ | 交付可复制的包开发样例，演示声明、装配及卸载 |
| AUT-01 | 核心 | CMD-01、KIT-01 | src/automation/discovery/ | 按任务查询相关操作、字段和能力摘要，保持稳定标识 |
| AUT-02 | 核心 | AUT-01、CMD-04、CMD-05 | src/automation/session/ | 外部 Agent 可查询、预览、应用及回退，权限和修订由宿主核验 |
| AUT-03 | 实现 | AUT-02、KIT-03 | src/automation/handoff/ | 生成最小项目摘要、改动及可复跑入口，人可从同一项目接手 |
| AUT-04 | 核心 | AUT-02、SIM-01、SHIP-04 | src/automation/validation/ | Agent 可从现有操作完成创建、试玩和导出，失败定位到具体步骤 |
| AUT-05 | 核心 | AUT-04、WKB-13 | src/studio/agent/ | 工作台可查看、批准和接管操作，实际改动可回溯 |
| SHIP-01 | 核心 | KIT-02、SIM-01、PRD-08 | src/export/plan/ | 按修订、入口、能力闭包和目标平台生成可核对的构建计划 |
| SHIP-02 | 实现 | SHIP-01 | src/export/resources/ | 收集资源、保存路径与许可证，编辑器文件不进入游戏成品 |
| SHIP-03 | 核心 | SIM-02、SIM-09、SHIP-01 | src/player/ | 独立运行程序能在项目目录之外启动；已有存档能力的游戏可保存重开 |
| SHIP-04 | 核心 | SHIP-02、SHIP-03 | src/export/windows/ | 工作台和 CLI 共享 Windows 导出，外部目录可启动与重开 |
| SHIP-05 | 核心 | WKB-01、WKB-13 | src/studio/、构建配置 | 交付独立工作台包及依赖清单，能打开用户已有项目 |
| SHIP-06 | 实现 | SHIP-04、KIT-05 | src/export/platforms/ | 为已选平台接入发布适配，给出同一项目的启动证据 |
| SHIP-07 | 核心 | AUT-03、AUT-04、WKB-11、KIT-04、BLK-01 | examples/、交付说明 | 用非方块与已具备最小玩法的方块项目验证同一创建、编辑、试玩与导出流程 |

## 方块能力与全量内容

| 任务 | 分工 | 前置 | 交付位置 | 完成标志 |
| --- | --- | --- | --- | --- |
| BLK-01 | 核心 | KIT-03、SIM-04、SIM-06、SIM-09 | capabilities/voxel/world/ | 分块世界的生成、可见区加载、存取和重开，连接通用编辑与试玩 |
| BLK-02 | 核心 | BLK-01、MAT-01 | capabilities/voxel/blocks/ | 注册方块和材料状态，放置、破坏、掉落及遮挡由数据驱动 |
| BLK-03 | 核心 | BLK-02、PRD-01 | capabilities/voxel/items/ | 物品、背包、堆叠、容器和传送，规则能从工作台调整 |
| BLK-04 | 核心 | BLK-03 | capabilities/voxel/recipes/ | 合成、熔炼及工具耐久，经同一背包接口扣料和产出，配方可编辑 |
| BLK-05 | 实现 | BLK-03、SIM-04 | capabilities/voxel/player/ | 生命、饥饿、伤害、状态和重生，行为保存后可恢复 |
| BLK-06 | 核心 | BLK-01、BLK-02、MAT-01 | capabilities/voxel/terrain/ | 种子、群系、地形、矿物与结构，边界和生成顺序可复现 |
| BLK-07 | 实现 | BLK-06、SIM-09 | capabilities/voxel/dimensions/ | 维度、传送门和各维度存档独立可重开 |
| BLK-08 | 核心 | BLK-02、BLK-06 | capabilities/voxel/environment/ | 光照、时间、天气、流体、火和重力的更新规则可观察 |
| BLK-09 | 实现 | BLK-02、BLK-08 | capabilities/voxel/farming/ | 作物成长、收获和环境条件可配置，重开后行为一致 |
| BLK-10 | 核心 | BLK-05、SIM-11 | capabilities/voxel/creatures/ | 生物生成与行为数据，覆盖移动、战斗、掉落、繁殖与驯养 |
| BLK-11 | 实现 | BLK-03、BLK-04、BLK-10 | capabilities/voxel/progression/ | 交易、酿造、附魔、成长和相关界面状态可重开 |
| BLK-12 | 核心 | BLK-02、BLK-08、SIM-11 | capabilities/voxel/redstone/ | 信号与时序、活塞和典型电路，边界行为由参考条目定位 |
| BLK-13 | 实现 | BLK-10、BLK-12 | capabilities/voxel/vehicles/ | 矿车、轨道、船和骑乘，放置、控制与保存可复现 |
| BLK-14 | 实现 | BLK-07、BLK-10 | capabilities/voxel/endgame/ | 首领、跨维度进度、掉落与回归存档贯通 |
| BLK-15 | 实现 | BLK-05、BLK-11、BLK-14、SIM-07 | capabilities/voxel/presentation/ | HUD、创造模式、音效、菜单、统计、成就与玩家命令 |
| BLK-16 | 核心 | SIM-10、BLK-04、BLK-07、BLK-14 | capabilities/voxel/multiplayer/ | 服务端与多客户端共享状态，权限、容器、重连与存档一致 |
| BLK-17 | 实现 | BLK-04、SHIP-07 | examples/voxel-reference/ | 把参考玩法作为组合项目交付，可换配方与世界参数，不改引擎源码 |
| MAT-01 | 核心 | REF-01、KIT-01 | docs/project/voxel-benchmark.md | 冻结条目 ID、分类、版本、来源及覆盖字段，再按领域开内容批次 |
| MAT-02 | 批量 | MAT-01 | reference/voxel/world/ | 整理创建、探索、结构与维度条目和来源，待核数据逐条标记 |
| MAT-03 | 批量 | MAT-01、BLK-02、BLK-04 | reference/voxel/content/ | 补方块、物品和配方的数据目录，符合已定格式，逐批导入可读 |
| MAT-04 | 批量 | MAT-01、BLK-10、BLK-13 | reference/voxel/entities/ | 整理生物、载具和边界交互的数据及行为出处 |
| MAT-05 | 批量 | MAT-01、BLK-15、BLK-16 | reference/voxel/player/ | 整理表现、统计、命令和多人条目，关联实际系统和证据 |
| MAT-06 | 批量 | MAT-03、BLK-15 | examples/voxel-reference/assets/ | 补可再分发的原创素材或占位替代，登记来源和许可 |
| MAT-07 | 核心 | MAT-02、MAT-03、MAT-04、MAT-05、MAT-06、BLK-16、BLK-17 | docs/project/voxel-benchmark.md、参考项目 | 合并全量矩阵，列出遗漏及差异，复跑可编辑项目与成品 |
| MAT-08 | 核心 | MAT-07、AUT-04、SHIP-07 | 参考交付记录 | 用声明的能力包和环境执行完整需求，统计 10 分钟和 Token 结果并完成续编交接 |
