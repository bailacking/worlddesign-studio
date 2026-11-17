# WDS · World Design Studio

**让每一个人，都能创造自己的世界。**

WDS 是面向人类与 AI 共同创作的开放式游戏引擎与开发平台。项目以可组合的插件体系为基础，将图形界面、命令行、代码与可视化编辑连接到统一的项目模型和操作能力。

> **Windows 工作台支持场景编辑、试玩、资源导入与图文预览。** 工作台默认使用简体中文，可从 **设置 → 界面语言** 即时切换英文。

## 体验现代工作台

在 Windows x64 使用 MSVC 和 Qt 6.8 以上版本从源码构建。打开「四方汇流」示例的副本，在场景树选中 `drifter`，修改 **X 速度**，保存后用新修订试玩。[示例](examples/scene-editing/README.md) · [构建与运行](docs/engineering/development.md#运行现代工作台)

- **项目与资源**：新建或打开项目，按 ID、路径及类型筛选资源，查看引用，预览导入场景、数据、PNG 和 UTF-8 文本，重开后可查来源和图文预览。
- **场景编辑**：在可平移缩放的二维视口选中实体，修改位置或速度；支持实体新建、复制、删除、排序和当前会话内撤销重做。
- **AI 与命令行**支持按修订读取、预览和提交项目改动、恢复中断写入和生成 Markdown 交接摘要。内容工具可展开规则、校验引用和按 ID 修补生成结果。[资源导入](docs/engineering/development.md#导入外部资源) · [内容生成](docs/engineering/development.md#生成与修补内容)

当前提供源码构建。工作台支持图文导入、拖放、缩略图、交互式二维试玩和 Windows x64 独立导出。打开「礁灯投递」体验精灵、碰撞和胜负，导出后双击 `Play.exe` 运行。 [试玩步骤](examples/reef-delivery/README.md) · [导出步骤](docs/engineering/development.md#导出-windows-x64-游戏)

## 产品方向

- **一切皆插件**：精简运行基础负责组合与生命周期，具体创作能力通过公开契约提供。默认发行组合覆盖完整基础工作流，用户可安装、卸载、替换和分享组合。
- **人与 AI 平等创造**：不同入口共享项目状态与操作能力，AI 自主程度由创作者控制，人类可随时理解、修改和接管工作。
- **统一的创作语义**：代码与可视化逻辑共享底层语义，同一资源可提供多种编辑形式；转换边界和冲突处理规则清晰可见。
- **开放的项目资料**：普通文件夹保存源内容，资源编号与路径共同维持引用，自动项目说明帮助人类与 AI 理解项目。
- **2D 与 3D 并行发展**：共享通用逻辑，渲染、物理与空间处理分别优化。桌面优先，将 Windows、Linux、macOS 与 Web 的约束纳入架构研究。

按 [50 周开发计划](docs/project/50-week-plan.md)推进平台建设。目标是各用 **30 分钟**从零资产复刻《我的世界》Java 1.16.5 与《饥荒》单机原版、巨人国和海难在 2017 年前的内容，并各用 **60 分钟**从新需求制作同量级方块与非方块游戏。四项均需交付可玩成品和可续编工程。 [方块验收](docs/project/voxel-benchmark.md) · [饥荒验收](docs/project/dontstarve-benchmark.md)

## 文档入口

| 读者任务 | 入口 |
| --- | --- |
| 理解产品目标与范围 | [愿景与设计原则](docs/product/vision.md) · [产品范围](docs/product/scope.md) |
| 理解各系统如何协作 | [系统架构](docs/architecture/overview.md) · [离线架构图解](docs/architecture.html) |
| 开展工程设计与原型开发 | [开发指南](docs/engineering/development.md) · [长期维护](docs/engineering/quality.md#长期维护) · [技术评估](docs/engineering/technical-evaluation.md) |
| 认领任务与查看里程碑 | [最终版 50 周计划](docs/project/50-week-plan.md) · [并行开发计划](docs/project/parallel-delivery.md) · [实施路线](docs/project/roadmap.md) |
| 研究相关技术 | [DeepSeek Harness 专题](docs/research/deepseek-harness.md) · [相关项目](docs/research/landscape.md) |
| 参与项目 | [贡献指南](CONTRIBUTING.md) · [社区治理](docs/community/governance.md) · [社区规范](CODE_OF_CONDUCT.md) |

完整章节见 [文档中心](docs/README.md)。直接使用浏览器打开 `docs/architecture.html` 可离线阅读架构图与主题说明，无需服务器或外部资源。

## 开发状态

项目在 [GitHub 仓库](https://github.com/bailacking/worlddesign-studio) 维护。构建、测试及运行样例见 [开发指南](docs/engineering/development.md#运行-m0-样例)。

W1–W7 已交付 Windows 基线、参考版本与验收口径、工作台导入、内容生成、图文预览、二维交互试玩和 Windows x64 导出。默认 Linux CI 覆盖核心和 CLI。Windows 发行链在本机验收，第二台机器列入 W8。 [验收记录](docs/project/roadmap.md#周交付记录) · [W4 后端对照](examples/w4-lab/README.md)

## 开源与协作

WDS 由王大帅发起，以 **MIT** 许可证开放自有且有权授权的代码与文档。欢迎参与、复用、商业应用和独立衍生。贡献者地位平等，维护权限对应具体责任；插件评价依据用户体验与公开证据。

[标准许可文本](LICENSE) · [中文许可说明](docs/legal/license.md)

**自强不息，止于至善。功成不必在我，成功一定有我。**
