# DeepSeek Harness 技术研究

## 研究摘要

DeepSeek Harness（DSH）对 WDS 最有价值的参照是：围绕可替换能力组织系统，将依赖与配置显式化，使注册和资源生命周期可以追踪，并把失败行为写入文档与测试。借鉴重点在这些工程边界，而非直接将智能体框架作为实时游戏引擎使用。

研究日期：2026-11-14。固定快照：`39163dd30f703a79079e2d2446545cff3c453738`。范围为仓库文档、关键源文件和相关测试的静态阅读；未安装、构建、运行 DSH，也未执行其测试。下文区分仓库事实与 WDS 的设计分析。

## 仓库定位与组织

README 将 DSH 定位为基于 Cordis 的开源智能体框架，并标注开发者预览及兼容性可能发生破坏性变化。工作区按应用入口、领域包、原生组件、外部基础库和文档站点组织；这为区分产品装配与能力实现提供了明确位置。

来源：[README](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/README.zh.md)、[工作区声明](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/pnpm-workspace.yaml)。

## 能力定义、实现与调用方

DSH 架构文档用服务定义、服务实现与调用方描述可替换能力。调用方通过契约获得能力，模型适配、工具、文件与进程等实现可在各自边界替换。

对 WDS 的启示：渲染、资源导入、版本工具、AI 提供商等都应有明确契约和责任归属。管理接口可借鉴这种划分，但实时渲染与物理需要独立评估数据路径、线程和调用成本。

来源：[架构文档](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/docs/architecture.md)。

## 生命周期与可清理注册

Cordis 入门文档描述按服务依赖激活插件，以及将注册、监听等作为可释放作用域管理。注册表源码的删除路径会处置插件对应的运行实例。它说明生命周期可以成为统一机制，而不依赖每个插件自行约定清理方式。

对 WDS 的启示：服务、监听、后台任务和图形资源都应登记所属者与释放路径，部分初始化失败也需清理。资源释放语义仍取决于插件正确实现；这不是操作系统级隔离或任意代码安全的证明。

来源：[Cordis 入门](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/docs/cordis-primer.md)、[注册表源码](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/vendor/cordis/src/registry.ts)。

## 配置装配与运行状态

DSH 使用工作配置与组合包组织启动树，并按层应用配置。启动器负责配置装配和运行依赖解析；是否开启热重载由组合决定。配置检查入口能够展示最终配置与层来源。

WDS 可研究默认发行组合、用途组合和项目配置之间的分工，并明确覆盖顺序。应把期望配置、已安装版本和当前生效状态分开；不能把保存配置等同于即时生效，也不能直接照搬上游的所有覆盖规则。

来源：[启动与装配](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/packages/boot/app-boot/README.md)。

## 兼容性准入

启动文档描述在所属装配边界检查插件声明的 DSH 版本要求，并区分普通插件、组合包和精确版本豁免。文档也指出检查只覆盖它管理的边界，不构成对恶意代码的隔离。

WDS 应区分版本准入、功能契约验证、组合测试与权限执行。即使版本声明匹配，也仍可能出现运行期冲突；若允许豁免，应限制到明确版本组合并保留诊断。

来源：[版本准入边界](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/packages/boot/app-boot/README.md)、[插件管理](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/packages/boot/plugin-manager/README.md)。

## 启动失败、更新与恢复

DSH 的启动文档和测试明确区分可选与必需插件。测试代码覆盖导入、依赖、配置及初始化等失败，检查未失败的部分和恢复行为。插件故障对整体启动的影响取决于其必要性、依赖关系和失败阶段。阅读测试只能证明存在这些断言，不能证明该快照的所有测试实际通过。

插件安装源码包含保存与恢复清单、锁文件和尝试重建安装内容的路径，并区分重建成功与失败的诊断。插件管理文档还区分安装失败、激活失败和需重启状态，不能把所有失败都解释为完整事务回滚。

WDS 应建立分阶段故障矩阵，区分配置恢复、安装内容恢复、运行实例恢复与项目数据恢复，报告实际完成的恢复范围。

来源：[失败矩阵测试](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/apps/cli/tests/profiles/web/tests/web-failure-matrix.expected.e2e.ts)、[安装操作源码](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/packages/boot/plugin-manager/src/operations.ts)、[插件管理说明](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/packages/boot/plugin-manager/README.md)。

## 状态记录与用户界面

DSH 将持久会话事实、运行中的智能体事件与能力事件区分，并从持久资料派生不同视图。WDS 可以研究将操作变更、临时进度和运行观察分开，以便 GUI、CLI 与 AI 得到一致结果。

不应将会话日志模式直接扩大为全部游戏状态的存储方案：逐帧变换、物理和渲染的吞吐、存储与确定性需求不同，需要专项实验。

来源：[状态与事件设计](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/docs/architecture.md)。

## 文档与工程检查

DSH 的站点配置按使用指南、开发与参考资料组织入口；包文档进一步说明用途、实现与限制。文档来源映射和路径校验帮助降低结构变化后的引用失效。

WDS 采用按读者任务和系统主题组织文档的方式：产品说明目标，架构章节维护系统要求，开发指南提供可执行流程，研究文档保留外部证据。尚未实现的项目不应照搬上游已经存在的安装与构建命令。

来源：[文档站点映射](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/website/docs.ts)、[开发指南](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/docs/development.md)、[引用检查源码](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/scripts/verify-doc-refs.ts)。

## 社区与安全边界

DSH 的贡献说明强调仓库内包不天然高于社区包；同时该快照暂不接受外部合并请求。WDS 可认同贡献者平等的原则，但贡献流程依据自身阶段设计，不复制这一临时准入政策。

安全说明将 DSH 标为未经安全审计的实验软件，指出插件、进程与权限控制的局限。WDS 不应把采用某个框架作为插件安全、故障隔离或生产可用性的证明。

来源：[贡献说明](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/CONTRIBUTING.md)、[安全说明](https://github.com/deepseek-ai/deepseek-harness/blob/39163dd30f703a79079e2d2446545cff3c453738/SAFETY.md)。

## 对 WDS 的适用结论

| 研究方向 | WDS 的处理 |
| --- | --- |
| 能力定义、实现与调用分离 | 纳入架构方案，明确各系统责任归属 |
| 生命周期与可清理注册 | 纳入插件要求，通过失败注入验证 |
| 分层组合与配置来源 | 纳入设计研究，覆盖顺序和格式待评审 |
| 版本准入与失败分类 | 纳入兼容性及恢复要求，不替代真实隔离 |
| 持久事实与临时观察分开 | 用于操作与诊断设计，不预设全游戏事件溯源 |
| 文档按主题维护 | 用作文档组织方式，避免把协作过程写成产品规格 |
| Cordis、TypeScript、Node.js 与具体发布体系 | 未选定为 WDS 技术栈；实时能力与平台需另行验证 |

未引入上游源代码或依赖。后续复用前需固定版本并核对具体文件及第三方许可。
