# 相关项目调研

资料日期：2026-11-14。以下为上游 README 资料快照，能力属于项目自述；未构建、运行或完成性能、安全与许可尽调。不同项目分别属于引擎、插件框架或制作工作台，不进行简单成熟度排名。

[DeepSeek Harness 专题研究](deepseek-harness.md) 单独分析插件架构与工程机制。其他相关项目如下。

## ARP

**资料描述：** AI 原生引擎；README 描述共享命令入口与可读资源定义。

**研究价值：** 研究 GUI/CLI/AI 的公共操作与资源语义。

**许可线索：** README 声明 MIT；依赖另核对。

来源：[arp-engine/arp README](https://github.com/arp-engine/arp/blob/main/README.md)。文件内容指纹：`92bbb1f56e477a650bb13dc5aa15c18a3914930e`。

## nAIVE

**资料描述：** 以 Rust、YAML 场景、Lua 逻辑及 MCP 接入组织 AI 创作。

**研究价值：** 研究项目可读性、热更新与状态迁移。

**许可线索：** 引擎模块与 naive-server 的许可不同；后者声明 BSL 1.1，不能统称 MIT。

来源：[poro/nAIVE README](https://github.com/poro/nAIVE/blob/master/README.md)。文件内容指纹：`bae557fa01a29539533fb68706bb834a44101d27`。

## Renzora

**资料描述：** 基于 Bevy 的 2D/3D 引擎与编辑器，强调系统模块化。

**研究价值：** 研究插件替换、依赖及默认组合；不能推断任意热卸载。

**许可线索：** README 声明 MIT 或 Apache-2.0 双许可。

来源：[renzora/engine README](https://github.com/renzora/engine/blob/main/README.md)。文件内容指纹：`4cf216c03ece5a554f746a71edfe53fae492cb84`。

## AI Game Studio

**资料描述：** 2D 创作平台，描述时间轴、CLI、可读格式与可审查的 AI 修改。

**研究价值：** 研究交互与撤销；插件 SDK 和市场在所读材料中列为后续计划。

**许可线索：** README 声明 MIT。

来源：[Codigo-Free/ai-game-studio README](https://github.com/Codigo-Free/ai-game-studio/blob/main/README.md)。文件内容指纹：`9191518091b4175a3f23aa4610c21876d8899fa7`。

## Godogen

**资料描述：** 在多个现有引擎上组织 AI 游戏制作，强调运行或录像证据。

**研究价值：** 研究制作、检查与人工接管闭环；它不是自研引擎内核。

**许可线索：** 所读 README 未提供明确许可结论，复用前核对。

来源：[htdt/godogen README](https://github.com/htdt/godogen/blob/master/README.md)。文件内容指纹：`73ceb18ff743dd995f2d1619dd81e7564ea170e8`。

## Orkige

**资料描述：** C++ 引擎及面向智能体的 MCP 编辑入口。

**研究价值：** 研究编辑、运行、测试与平台证据的组织方式。

**许可线索：** README 声明 Apache-2.0，第三方内容另列。

来源：[orkitec/orkige README](https://github.com/orkitec/orkige/blob/main/README.md)。文件内容指纹：`4f35fcc2ae775d74c348f594046cdd4ef9ef9936`。

## Toybox

**资料描述：** 开发中的插件式引擎，描述动态契约、加载与重载。

**研究价值：** 研究卸载清理、依赖和故障边界。

**许可线索：** 所读 README 未提供明确许可结论，复用前核对。

来源：[Konfus-org/Toybox README](https://github.com/Konfus-org/Toybox/blob/main/README.md)。文件内容指纹：`cd1a1fbc6034762b4b0d67d0556eabc78810d8cd`。

## Summer

**资料描述：** CLI、MCP 与技能工具，连接游戏制作和发布流程。

**研究价值：** 研究操作发现；工具开源不能等同整个桌面引擎开源。

**许可线索：** README 区分 MIT 工具仓库、目前闭源的桌面编辑器与运行时、独立条件的托管服务。

来源：[SummerEngine/summer README](https://github.com/SummerEngine/summer/blob/main/README.md)。文件内容指纹：`82af972890e58e10175d946a5a1d549c3b8d7982`。

## OhMyGame

**资料描述：** 把策划、代码与多媒体资源组织为 AI 游戏制作工作室。

**研究价值：** 研究跨资源工作流、变更预览与恢复。

**许可线索：** README 声明 Apache-2.0，名称与标志不在覆盖范围。

来源：[WhiteTowerAI/ohmygame README](https://github.com/WhiteTowerAI/ohmygame/blob/main/README.md)。文件内容指纹：`cdf1fb6324362e52c3d36c0a12e17cd66ccb86f8`。

## OpenGame

**资料描述：** 网页游戏自动制作框架，积累模板和调试经验。

**研究价值：** 研究跨文件一致性、任务分解和实际评估。

**许可线索：** 所读 README 未提供明确许可结论，复用前核对。

来源：[0xinnovator/opengame README](https://github.com/0xinnovator/opengame/blob/main/README.md)。文件内容指纹：`fb7905959dcedbd76205f032f0755bccb1d5b81a`。

## Funplay

**资料描述：** 本地优先、跨引擎的 AI 工作台，结合项目文件、工具与运行检查。

**研究价值：** 研究项目上下文、检查点与不同引擎接入边界。

**许可线索：** README 声明 MIT。

来源：[FunplayAI/Funplay README](https://github.com/FunplayAI/Funplay/blob/main/README.md)。文件内容指纹：`ce424820013bef771e9c5a74ee188413de64f2df`。

## 选型方法

先选与当前原型相关的少量项目，固定提交与环境，用同一任务比较共享操作、资源改名、插件替换、失败恢复及平台限制。README 的内容指纹不是仓库提交号或发行版本；复用前应重新核对具体文件、依赖和再分发要求。

没有检索到完整匹配的项目，不证明不存在类似实现。WDS 优先研究复用、兼容与合作，不以“首个”“唯一”或未经测量的性能优势作为事实。
