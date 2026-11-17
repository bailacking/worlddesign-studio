# 文档中心

本目录是 WDS 产品与工程设计的主题入口。产品文档说明目标，架构文档说明契约，开发指南给可复跑步骤，研究报告记录当时的证据。当前实现查根目录 README、实施路线、代码与运行结果。

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
| [桌面工作台](architecture/workbench.md) | 场景编辑、资源筛选、试玩、语言设置与组件接口 |
| [AI 协作](architecture/ai.md) | 项目理解、自主程度、权限、执行与验证 |
| [游戏运行时](architecture/runtime.md) | 2D/3D、场景、渲染、物理与导出 |
| [版本管理](architecture/version-control.md) | Git 候选方案、资源差异、大文件与恢复 |
| [平台与交付](architecture/platforms.md) | 三桌面平台、Web 范围与支持矩阵 |
| [离线架构图解](architecture.html) | 单文件架构浏览与操作流程示意 |

## 工程与实施

- [开发指南](engineering/development.md)：工作目录、当前可执行检查、设计与原型工作流。体验中文版见 [运行现代工作台](engineering/development.md#运行现代工作台)。
- [质量与验证](engineering/quality.md)：契约测试、故障矩阵、平台证据和可玩性验收。
- [技术评估](engineering/technical-evaluation.md)：C++ 核心的选定理由、工具链与依赖调研、进入编码的条件。
- [平台建设总纲](project/platform-plan.md)：平台边界、优先级和首个可用版本。
- [实施路线](project/roadmap.md)：W1–W6 验收记录、下一周入口及各阶段放行条件。
- [最终版 50 周开发计划](project/50-week-plan.md)：逐周目标和验收、零资产计时、共同调研与产能复核。
- [W1 基线与需求台账](project/w1-baseline.md)：Windows 实测、核心需求与任务映射、验收机器与本周投入。
- [W2 参考证据与评分](project/w2-references.md)：版本、兼容边界、对照步骤、历史缺证和同量级评分。
- [W4 样机实测与工期](project/w4-benchmarks.md)：两条内容生成路线、原创资产、绘制对照和产能情景。
- [开发任务](project/development-tasks.md)：可认领的交付物、依赖关系和验收条件。
- [并行开发计划](project/parallel-delivery.md)：工作树、首批派发、各模块的交付边界和合并顺序。
- [方块项目验收基准](project/voxel-benchmark.md)：Java 1.16.5 版本、行为矩阵及 30 分钟交付。
- [饥荒项目验收基准](project/dontstarve-benchmark.md)：PC 单机原版、巨人国及海难 2017 年前的内容及 30 分钟交付。
- [前期原型与验证](project/prototype-baseline.md)：已完成的 M0、M1 技术样例与限制。

## 社区、许可与研究

- [贡献指南](../CONTRIBUTING.md)、[社区规范](../CODE_OF_CONDUCT.md)、[社区治理](community/governance.md)。
- [MIT 许可说明](legal/license.md)。
- [DeepSeek Harness 专题研究](research/deepseek-harness.md)、[相关项目调研](research/landscape.md)。

## 文档维护规则

一项规则由所属主题维护，其他页面链接到该处。行为或接口变更时，在同一个改动里更新对应章节。开发过程和当前交接写在 PR 或团队共享库。研究和验收记录注明快照、日期和适用范围，当前状态由可复跑的行为支持。

新页面须从本页或相关主题可达。若已有章节能承载内容，就更新原文。替代旧说明时更新引用，失去用途的说明随改动清理。需要长期留存的报告链接到当前入口。 [长期维护](engineering/quality.md#长期维护)
