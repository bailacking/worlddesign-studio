# WDS · World Design Studio

**让每一个人，都能创造自己的世界。**

WDS 是面向人类与 AI 共同创作的开放式游戏引擎与开发平台。项目以可组合的插件体系为基础，将图形界面、命令行、代码与可视化编辑连接到统一的项目模型和操作能力。

> **当前进度：Windows 现代工作台已跑通场景编辑、保存和试玩。** 工作台默认使用简体中文，可从 **设置 → 界面语言** 即时切换英文并记住选择。

## 体验现代工作台

在 Windows x64 使用 MSVC 和 Qt 6.8 以上版本从源码构建。打开随仓库提供的二维示例，在场景树选中 `marker`，修改 **X 速度**，点击 **保存属性** 后即可用新修订试玩。[构建与运行步骤](docs/engineering/development.md#运行现代工作台)

- **项目与资源**：新建或打开项目，按 ID、路径和类型查找资源。
- **场景编辑**：新建、复制、删除与排序实体。从场景树或二维视口选中实体，逐项修改并保存位置或速度。
- **AI 与命令行**：CLI 提供机器可读的操作清单，可读取、预览和按修订提交场景属性。工作台使用同一套项目操作。

当前是开发版本。资源导入、独立导出和完整 3D 编辑仍在规划内，暂无正式发行包。

## 产品方向

- **一切皆插件**：精简运行基础负责组合与生命周期，具体创作能力通过公开契约提供。默认发行组合覆盖完整基础工作流，用户可安装、卸载、替换和分享组合。
- **人与 AI 平等创造**：不同入口共享项目状态与操作能力，AI 自主程度由创作者控制，人类可随时理解、修改和接管工作。
- **统一的创作语义**：代码与可视化逻辑共享底层语义，同一资源可提供多种编辑形式；转换边界和冲突处理规则清晰可见。
- **开放的项目资料**：普通文件夹保存源内容，资源编号与路径共同维持引用，自动项目说明帮助人类与 AI 理解项目。
- **2D 与 3D 并行发展**：共享通用逻辑，渲染、物理与空间处理分别优化。桌面优先，将 Windows、Linux、macOS 与 Web 的约束纳入架构研究。

首个可用平台用两个不同类型的项目验证创建、编辑、试玩和导出。长期以《我的世界》Java 版 1.6 全量内容和行为的一比一复刻检验平台，目标是在 10 分钟内用少量 Token 交付可玩、可编辑项目。[验收基准](docs/project/voxel-benchmark.md)

## 文档入口

| 读者任务 | 入口 |
| --- | --- |
| 理解产品目标与范围 | [愿景与设计原则](docs/product/vision.md) · [产品范围](docs/product/scope.md) |
| 理解各系统如何协作 | [系统架构](docs/architecture/overview.md) · [离线架构图解](docs/architecture.html) |
| 开展工程设计与原型开发 | [开发指南](docs/engineering/development.md) · [技术评估](docs/engineering/technical-evaluation.md) |
| 认领任务与查看验收门槛 | [并行开发计划](docs/project/parallel-delivery.md) · [开发任务](docs/project/development-tasks.md) · [实施路线](docs/project/roadmap.md) |
| 研究相关技术 | [DeepSeek Harness 专题](docs/research/deepseek-harness.md) · [相关项目](docs/research/landscape.md) |
| 参与项目 | [贡献指南](CONTRIBUTING.md) · [社区治理](docs/community/governance.md) · [社区规范](CODE_OF_CONDUCT.md) |

完整章节见 [文档中心](docs/README.md)。直接使用浏览器打开 `docs/architecture.html` 可离线阅读架构图与主题说明，无需服务器或外部资源。

## 开发状态

项目在 [GitHub 仓库](https://github.com/bailacking/worlddesign-studio) 维护。构建、测试及运行样例见 [开发指南](docs/engineering/development.md#运行-m0-样例)。

现代工作台已在 Windows x64 实测。默认构建的 Linux CI 覆盖核心和 CLI。现有样例及其验证范围见 [前期原型与验证](docs/project/prototype-baseline.md)。

## 开源与协作

WDS 由王大帅发起，以 **MIT** 许可证开放自有且有权授权的代码与文档。欢迎参与、复用、商业应用和独立衍生。贡献者地位平等，维护权限对应具体责任；插件评价依据用户体验与公开证据。

[标准许可文本](LICENSE) · [中文许可说明](docs/legal/license.md)

**自强不息，止于至善。功成不必在我，成功一定有我。**
