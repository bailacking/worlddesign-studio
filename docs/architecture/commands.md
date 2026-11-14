# 操作与状态

## 统一操作模型

GUI、CLI 和 AI 适配入口共享查询与修改语义。界面负责收集输入和呈现反馈，业务校验与状态修改由共同操作层承担。在相同项目修订、输入和权限下，不同入口应产生等价结果。

操作接口、传输协议和序列化格式仍在设计。进程内调用、命令行、MCP 或其他协议可以作为适配方式，不应各自形成第二套业务规则。

## 契约内容

每个操作应说明标识及版本、输入结构、作用项目与资源、所需权限、前置修订、输出、错误、任务进度、取消行为及恢复方式。对可能重试的修改研究去重标识；对长任务区分提交成功、执行中与最终完成。

查询、项目修改、外部程序执行和对外发布应有可区分的权限语义。自动化入口不能绕过普通入口的授权与冲突检查。

### M0 资源改名契约

操作 `asset.rename` 第一版作用于一个已打开项目。输入是稳定资源 ID、同目录下的新文件名和预期项目修订号。调用上下文由宿主提供项目句柄和写权限，GUI 与 CLI 都只负责把输入转换成同一请求。M0 写入按项目串行，修订校验和提交在同一写入范围完成。路径、名称和保存规则见 [M0 项目保存契约](assets.md#m0-项目保存契约)。

| 顺序 | 条件 | 结果 |
| --- | --- | --- |
| 1 | 缺项目写权限 | `permission_denied` |
| 2 | 缺少必填字段、类型错误或文件名无效 | `invalid_argument` |
| 3 | 项目存在未完成事务或须恢复状态 | `recovery_required` |
| 4 | 预期修订号与当前值不同 | `revision_conflict`，附当前修订号 |
| 5 | 资源 ID 不存在 | `asset_not_found` |
| 6 | 新文件名与原文件名完全相同 | 成功，`changed=false`，修订号保持 |
| 7 | 目标被占用或仅大小写不同 | `path_conflict` |
| 8 | 保存失败 | 已恢复原修订时返回 `save_failed`。磁盘状态未能判定或恢复时返回 `recovery_required`，暂停写入 |

保存成功后返回资源 ID、旧路径、新路径、提交后的修订号和 `changed=true`。只增加一次修订号，资源内容与引用 ID 保持不变。拒绝或无操作时保持原数据与修订。提交已经生效但清理未完成时，若能核验新修订完整，返回成功和清理诊断。调用方重试前先读取当前项目状态；重复提交旧修订号会得到冲突。M0 的操作同步完成，异步取消、通用撤销和跨目录移动留待后续。

## 执行流程

### 平台操作接口 v1

OPS-01 确定平台操作的公共语义。GUI、CLI 与 Agent 将参数转成同一请求，项目服务负责验证与执行。首轮覆盖 `project.create`、`project.open`、`project.validate`、`project.resources`，下一步由 OPS-02 接入资源字段修改。操作发现应返回 ID、版本、输入定义、错误码及是否写入项目。

当前可用 `wds_cli --project-operations` 查询十七项能力的 JSON 说明，包括项目恢复、资源导入预览与提交、导入来源查询、数据编辑、场景属性读取和修改、场景试玩。发现命令无需项目目录。

请求包含 `operation`、`version` 和 `input`。打开已有项目的调用另带由宿主解析的项目上下文。对已有项目的写入还须带整数 `expected_revision` 和 `mode`（`preview` 或 `apply`）。权限来自宿主上下文，用户输入里的同名字段不能授予权限。创建新项目使用目录不存在作为前置条件。

| 行为 | 约定 |
| --- | --- |
| 查询 | 返回实际读到的修订、结果或诊断，不改项目 |
| 预览 | 使用与提交相同的校验，返回将改变的资源、字段和值，不写文件也不更新修订 |
| 提交 | 在项目写入范围内再次校验修订与权限，成功一次递增修订。无变化保留原修订 |
| 失败 | 返回稳定 `code` 和可定位的 `location`。过期修订附实际值，失败状态和恢复依据可核对 |

```json
{
  "operation": "resource.field.set",
  "version": 1,
  "expected_revision": 3,
  "mode": "preview",
  "input": {"resource_id": "rules", "field": "/jump_speed", "value": 8}
}
```

`resource.field.set` 已支持替换已有数据资源的顶层整数。CLI 与工作台使用共享 C++ 服务。统一 JSON 请求的传输入口与领域元信息仍待实现。[首批字段写入](project-format.md#首批字段写入)

`project.play` 接收目录、预期修订号和运行步数（0 到 600），读取入口场景并返回修订、场景 ID、步数和实体状态。修订冲突返回当前修订号，场景诊断带资源相对路径及字段位置。该操作只读取源项目。[场景与试玩会话](runtime.md#场景与试玩会话-v1)

`scene.inspect` 对应 `project scene DIRECTORY REVISION`，返回入口实体的初始值及字段类型、默认值与范围。用 `project preview-scene-field DIRECTORY ENTITY FIELD REVISION VALUE` 预览，用 `project set-scene-field DIRECTORY ENTITY FIELD REVISION VALUE` 保存。按稳定实体 ID 选中实体，支持 `x`、`y`、`vx`、`vy`；修改曾省略的字段时从默认值计算差异。保存复用字段编辑事务，场景事务使用版本 2，恢复也兼容现有版本 1。

`scene.entity.preview` 与 `scene.entity.apply` 支持以下入口场景操作。将命令中的 `edit-entity` 换成 `preview-entity` 即可检查参数、修订与写入限制，预览会返回 `changed` 和定位路径。

| 用途 | 命令 |
| --- | --- |
| 新建实体，位置与速度默认均为 0 | `project edit-entity DIRECTORY add ID REVISION` |
| 复制实体属性，新 ID 需唯一 | `project edit-entity DIRECTORY copy ID REVISION NEW_ID` |
| 删除实体 | `project edit-entity DIRECTORY remove ID REVISION` |
| 移动到指定次序，索引从 0 开始 | `project edit-entity DIRECTORY move ID REVISION INDEX` |

新建与复制追加到场景末尾。实际编辑提交后修订加一，相同位置的移动不产生新修订。结构编辑使用事务记录版本 3，恢复时重放该操作并核对新旧场景内容，兼容旧事务。当前以本机单写入者为使用边界。

`resource.import.preview` 与 `resource.import.apply` 把外部文件导入为 `data` 或 `scene` 资源，源文件按原字节保存。用 `project preview-import DIRECTORY SOURCE ID KIND PATH REVISION` 检查身份、目标位置与字节数，用 `project import` 提交，用 `project imports DIRECTORY` 读取来源记录。同 ID、同路径且字节一致返回 `changed=false`；字节或路径冲突分别返回 `duplicate_id`、`duplicate_path`，已有内容保持不变。导入使用事务记录版本 5。[资源导入](project-format.md#资源导入)

提出请求 → 校验参数和权限 → 检查项目修订 → 解析能力实现 → 准备变更 → 应用与持久化 → 更新或失效相关索引 → 返回变更及诊断 → 运行验证。

“修改方块颜色”应定位稳定资源编号及当前修订，而不是仅凭一个易变文件名。发现并发修改时，进入资源类型对应的冲突流程，保留双方内容。

## 事务、撤销与外部副作用

现代工作台可撤销、重做本次打开项目以来的最近 32 次场景结构修改和属性保存。每次撤销或重做也是一次保存，修订递增。保存新改动后清空重做记录，重新打开项目后开始新的操作历史。

CLI 的 `set-scene-field` 与 `edit-entity` 可在参数末尾追加 `--capture-change`。发生修改时额外返回 `scene_change`，包含 `project_id`、`scene_id`、`resource_path` 和场景文件的 `before`、`after` 字节文本。将这一对象作为 UTF-8 JSON 从标准输入传给 `project restore-scene DIRECTORY REVISION`。撤销时交换 `before`、`after`，重做时使用原对象。恢复会核对项目和入口资源身份、修订及当前场景字节。

恢复只写入口场景与清单修订，验证源与目标场景格式。若文件被外部修改，即使修订号相同也返回 `scene_content_conflict`。恢复使用事务记录版本 4，已有版本的恢复规则继续有效。

能保证原子性的范围需明确。对不能原子完成的多步骤任务，记录已完成步骤及补偿方式。撤销恢复的是所声明范围内的项目状态；网络请求、已启动的外部进程或对外发布可能无法简单逆转，必须分别表达结果。

取消应定义何时可受理、何时已越过提交点，以及已提交部分如何处理。任务失败、取消、拒绝、冲突和需重启是不同结果，不压缩成一个模糊的“失败”。

## 记录与观察

建议将持久变更事实、临时进度和运行观察分开。操作记录包含输入摘要、资源与修订、调用来源、结果、诊断和恢复关联。AI 和界面读取同一结果，避免屏幕状态与真实项目状态分叉。

持久记录不应包含凭据；资源正文是否保存由隐私、存储与恢复需求决定。记录模型仍待评估，不要求把每帧渲染或每次鼠标移动都写入持久日志。

## 验证标准

同一任务从 GUI 与 CLI 得到等价项目数据；拒绝无权限修改；并发变更不静默覆盖；重复请求不意外重复执行；长任务取消后最终状态可解释；撤销与重开项目的结果可复核。

关联：[项目与资源](assets.md)、[AI 协作](ai.md)、[质量与验证](../engineering/quality.md)。
