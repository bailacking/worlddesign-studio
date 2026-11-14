# 文档中心

本目录是 WDS 产品与工程设计的主题入口。**产品要求描述目标行为，架构方案描述拟采用的职责划分，技术评估记录仍需实验解决的问题；三者均不表示功能已经实现。** 项目实现状态以根目录 README 与实施路线为准。

## 产品

| 章节 | 内容 |
| --- | --- |
| [愿景与设计原则](product/vision.md) | 使命、目标读者、五项设计原则与开放协作 |
| [产品范围](product/scope.md) | 创作体验、能力范围、首个验证游戏与阶段边界 |

## 系统架构

| 章节 | 内容 |
| --- | --- |
| [架构总览](architecture/overview.md) | 分层、职责、依赖方向与数据流 |
| [插件与组合](architecture/plugins.md) | 能力契约、生命周期、分层配置、兼容性和恢复 |
| [操作与状态](architecture/commands.md) | GUI/CLI/AI 共享操作、错误、取消、并发与变更记录 |
| [项目与资源](architecture/assets.md) | 文件夹、资源身份、多种编辑形式与冲突 |
| [项目目录与清单](architecture/project-format.md) | 首版项目格式、创建与读取契约、旧格式迁移边界 |
| [逻辑与编辑器](architecture/authoring.md) | 代码、可视化逻辑、共同语义及调试 |
| [AI 协作](architecture/ai.md) | 项目理解、自主程度、权限、执行与验证 |
| [游戏运行时](architecture/runtime.md) | 2D/3D、场景、渲染、物理与导出 |
| [版本管理](architecture/version-control.md) | Git 候选方案、资源差异、大文件与恢复 |
| [平台与交付](architecture/platforms.md) | 三桌面平台、Web 范围与支持矩阵 |
| [离线架构图解](architecture.html) | 单文件架构浏览与操作流程示意 |

## 工程与实施

- [开发指南](engineering/development.md)：工作目录、当前可执行检查、设计与原型工作流。
- [质量与验证](engineering/quality.md)：契约测试、故障矩阵、平台证据和可玩性验收。
- [技术评估](engineering/technical-evaluation.md)：C++ 核心的选定理由、工具链与依赖调研、进入编码的条件。
- [平台建设总纲](project/platform-plan.md)：平台边界、优先级和首个可用版本。
- [实施路线](project/roadmap.md)：工程底座、创作闭环、可复用生产和全量验证。
- [开发任务](project/development-tasks.md)：可认领的交付物、依赖关系和验收条件。
- [方块项目验收基准](project/voxel-benchmark.md)：1.6 全量内容与行为对照、耗时、Token 和人工接手。
- [前期原型与验证](project/prototype-baseline.md)：已完成的 M0、M1 技术样例与限制。

## 社区、许可与研究

- [贡献指南](../CONTRIBUTING.md)、[社区规范](../CODE_OF_CONDUCT.md)、[社区治理](community/governance.md)。
- [MIT 许可说明](legal/license.md)。
- [DeepSeek Harness 专题研究](research/deepseek-harness.md)、[相关项目调研](research/landscape.md)。

## 文档维护规则

每项要求在所属主题中维护；概览只摘要并链接，不重复保存完整规格。行为、接口或范围变化应同步修改相关章节和验证标准。外部技术结论附可核对来源与研究范围；未完成的实验注明未验证。

新增内容先判断属于产品要求、架构方案、开发操作还是参考研究，再放入相应目录。使用清晰中文标题和描述性文件名，避免为单个小结论增设文件。
