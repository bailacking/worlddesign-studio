# 项目目录与清单

## 首轮实现范围

本页定义 PRJ-02 实现所用的首版项目清单和诊断契约。现有 M0 与方块项目仍可通过原入口运行，迁移见本页。PRJ-01A 样例继续使用下文固定子集。

项目根含 `wds.project.json` 和 `assets/`。清单是源文件，运行存档和可重建产物分别放在 `state/` 与 `build/`，发布按依赖闭包选择输入。场景、规则和内容是项目资源，通用项目格式不依赖具体游戏。资源 ID 代表身份，改名只改路径。

## 固定样例子集

```json
{
  "format_version": 1,
  "project_id": "demo",
  "revision": 0,
  "entry_scene": "intro",
  "resources": [
    {"id": "intro", "kind": "scene", "path": "assets/intro.json", "refs": ["rules"]},
    {"id": "rules", "kind": "data", "path": "assets/rules.json", "refs": []}
  ]
}
```

- 清单及样例资源使用 UTF-8 JSON。字段为必填，首轮样例只使用上述字段。格式版本必须等于 1，修订号为非负整数。资源 ID 为 ASCII 小写字母、数字与短横线，首位为小写字母。
- 资源路径必须以 `assets/` 开头，使用正斜线。首轮样例的路径段只用小写字母、数字、短横线和点，禁止空段、绝对路径、`.`、`..`、尾点及设备名。两个资源不能占用同一路径。实现须检查文件真实位置与普通文件类型，再允许读写。
- `entry_scene` 引用清单中类型为 `scene` 的资源。每个 `refs` 元素引用同一项目内存在的资源。项目验证检查清单和资源文件关联，试玩时额外验证 [入口场景](runtime.md#场景与试玩会话-v1) 的内部结构。
- 读取验证失败时报告清单字段或资源位置，项目不写盘。失败分类使用 `unsupported_version`、`duplicate_id`、`duplicate_path`、`invalid_path`、`missing_reference`、`missing_file`、`invalid_entry_scene`。测试样例一次引入一类错误。

## 读取与验证契约

- 清单严格解析 UTF-8 JSON，拒绝重复对象键和未知字段。格式版本字段必须为整数；识别到未知版本后返回 `unsupported_version`，无需按 v1 结构检查后续字段。修订号范围为 0 至 9007199254740991，便于不同工具精确交换。超过范围或出现小数、指数形式时返回 `invalid_manifest`。[JSON 数字与互操作性](https://www.rfc-editor.org/rfc/rfc8259#section-6)
- 项目 ID 与资源 ID 使用 `[a-z][a-z0-9-]{0,63}`。资源类型支持 `scene`、`data`、`image` 与 `text`。已有只有场景和数据的 v1 项目可直接打开和导入。使用新增类型的工程需由支持这些类型的工作台或 CLI 打开。资源顺序不影响身份或引用。首轮检查引用是否存在，领域依赖闭环在后续运行或发布契约中检查。
- 路径为 `assets/` 下的正斜线相对路径，每段匹配 `[a-z0-9][a-z0-9.-]{0,63}`，不能以点结尾，设备保留名连扩展名也拒绝。校验时使用实际目录项的拼写，拒绝大小写不符和同目录仅大小写不同的歧义项。资源必须是普通文件。项目根、清单及项目内每一段均拒绝符号链接与 Windows 重解析点。当前只支持可信本地工作区和单写入者，暂不承诺并发外部替换路径时的安全性。[Windows 文件命名](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)
- 检查顺序为：路径与清单可读性 → JSON 与版本 → 必填字段和类型 → 资源 ID、路径与重复项 → 入口与引用 → 实际资源文件。返回首个错误及可定位的字段或相对路径。无清单返回 `missing_manifest`，格式错误返回 `invalid_manifest`，不可读返回 `io_error`，链接、非普通文件或歧义路径返回 `unsafe_path`。查询和验证不创建文件。

## 创建与兼容边界

PRJ-02 只在已存在的父目录下创建一个全新的项目目录。显式传入项目 ID，默认入口为 `intro`，初始修订为 0。先写 `assets/intro.json`，再发布有效清单。已存在目标目录时返回 `already_exists`，其中内容保持不变。创建失败仅清理该次创建且可确认归属的文件，报告清理状态。后续属性写入及多文件事务由 PRJ-04 定义。

PRJ-03 先识别旧 `.wds-project` 及方块规则文件。旧项目继续使用原入口，迁移由用户明确发起并写到新目录。报告迁移前后的资源 ID 映射、无法转换的内容和保留文件位置。旧样例的 `A`、`B` 等大写 ID 需要一致映射，所有引用同步变更，冲突时中止。方块存档与规则按原格式复制保留，领域映射完成后再声明新版项目可直接运行。原目录全程只读。

## PRJ-02 实现交接

- 输入是项目目录与创建时的项目 ID。现有 `wds_cli project create DIRECTORY ID`、`wds_cli project open DIRECTORY` 与 `wds_cli project validate DIRECTORY` 共用项目服务，后续 GUI 也通过该服务打开项目。
- 成功输出机器可读的项目 ID、修订号、入口场景与资源计数。失败输出稳定错误码和位置。用 PRJ-01A 的固定用例、创建后重开，以及验证前后文件不变来验收。
- 项目服务使用锁定版本的 [nlohmann/json 3.12.0](../../third_party/nlohmann/README.md)。依赖内置单头文件和 MIT 许可，可离线构建。项目服务在解析时检查重复键，并限制清单为 16 MiB、嵌套深度为 16。对外 C++ 类型不暴露解析库。

## 资源导入

PRD-05 让外部文件成为项目资源。工作台与 CLI 共用校验和写入，源文件按原字节保存。

| 项 | 约定 |
| --- | --- |
| 输入 | 项目目录、源文件、资源 ID、类型、`assets/` 下的目标路径、预期修订 |
| 预览 | `project preview-import DIRECTORY SOURCE ID KIND PATH REVISION`，返回资源身份、目标路径、字节数、摘要与 `changed`，不写盘 |
| 提交 | `project import DIRECTORY SOURCE ID KIND PATH REVISION [--expect-digest DIGEST]`，新资源提交后递增修订并追加清单条目 |
| 查询 | `project imports DIRECTORY` 列出导入来源记录 |

- 工作台提交携带预览的 `digest`。指定摘要与当前读入的字节不匹配时返回 `source_changed`，在写入前结束。
- 同一个资源 ID、同一目标路径且类型和字节完全一致时返回 `ok` 与 `changed=false`，修订保持不变，重复导入可判定为无变化。
- 已有 ID 换成不同字节返回 `duplicate_id`；目标路径已被其他资源占用返回 `duplicate_path`；目标位置存在不属于本项目的文件返回 `target_exists`。三者都不改动已有内容。
- 源文件上限 1 MiB，超过返回 `source_too_large`。目标路径沿用 v1 规则：`assets/` 开头、路径段合法、拒绝大小写不符与歧义项、拒绝符号链接与重解析点。
- `scene` 须能被试玩读取（场景 v1），`data` 须是 JSON 对象且数值可精确交换，失败返回 `invalid_content`。
- `image` 支持 PNG，每边最多 2048 像素。预览返回 `width` 与 `height`，失败分别报告 `invalid_png` 或 `image_too_large`。使用随仓库固定的 [LodePNG 解码器](../../third_party/lodepng/README.md)。
- `text` 支持 UTF-8 纯文本及可选 BOM，拒绝无效编码和除制表、换行外的 ASCII 控制字符。预览输出 `line_count` 及最多 4096 字节的 `text_preview`。仅预览去 BOM、将 CRLF 或 CR 统一为 LF，并保持字符边界。失败返回 `invalid_utf8`。

来源记录写在项目根的 `.wds-imports.json`，随同一次提交写入：

```json
{"version": 1, "imports": [{"resource": "notes", "kind": "data", "path": "assets/notes.json",
  "source_name": "notes.json", "bytes": 47, "digest": "fnv1a64:2aaaefebc8b114dc", "revision": 1}]}
```

- `source_name` 只保留源文件名。机器绝对路径只用于当次读取与诊断，不写进项目，项目内记录保持可移植。
- `digest` 是 FNV-1a 64 位摘要，用于对照同一批字节，不作安全校验。身份判断仍逐字节比较。

导入先写事务记录，再写目标资源与来源记录，最后替换清单。场景、数据和文本使用版本 5，PNG 使用版本 6，在 `after_data` 写原字节的十六进制。清单是提交点，恢复按现场决定。

| 现场 | 结果 |
| --- | --- |
| 清单仍是旧版本 | 删除本次写入的目标文件与来源记录，回到原修订；目标字节不符时保留现场并报告 `recovery_required` |
| 清单已是新版本 | 保留新资源，补齐来源记录，清理事务记录 |
| 现场与记录都不符 | 报告 `recovery_required`，停止写入并保留文件供检查 |

`version` 1 至 4 的历史记录继续按原规则恢复。当前边界是可信本机单写入者与进程中断，断电与多进程同时写入未验收。

项目服务在 `src/platform_project.cpp`，图文适配器在 `src/asset_import.*`。回归在 `tests/resource_import.cmake`，用旧版 `examples/scene-editing` 副本覆盖导入、重开、冲突和版本 5／6 恢复。工作台从原图生成缩略图，以 SHA-256 和适配器版本定位缓存。缓存放在 Qt 的应用缓存目录 `thumbnails-png-v1`，与项目文件及导入记录分离。清理后从源重新生成。

## 首批字段写入

PRJ-04 和 OPS-02 首先支持已有 `data` JSON 资源中的顶层整数。字段名以小写字母开头，可包含小写字母、数字、短横线和下划线，长度最多 64。首版编辑器要求资源内所有数值都是可精确交换的整数，避免重新序列化时改变其他字段的精度。预览与提交共用校验，单次提交修改资源和项目修订。CLI 和 Windows 工作台调用同一项目服务。

| 场景 | 验收 |
| --- | --- |
| 预览 | 输入资源 ID、字段、预期修订和值，输出旧值与新值。比较整个项目的文件清单和内容，均保持原样 |
| 提交 | 读取实际修订并再次校验，成功时只修改目标数据和项目修订。重开后两者一致。相同值返回无变化 |
| 过期请求 | 返回当前修订。目标数据、清单及事务记录保持不变 |
| 中断 | 模拟数据替换前、替换后但清单提交前、清单提交后未清理三个中断点。重开只能得到原修订及旧值，或新修订及新值 |
| 不确定状态 | 原文件、清单或事务记录与恢复依据不符时，报告需恢复并停止写入。保留现有文件供检查 |

事务记录 `.wds-field-edit.json` 先写入，再替换资源，最后替换清单。清单是提交点。记录包含旧、新文件的完整内容，恢复前核对清单、资源路径与实际文件。提交前恢复旧资源，提交后保留新资源。查询遇到待处理记录时报告 `recovery_required`，由显式恢复入口处理。单次编辑涉及的清单和资源文件各限 1 MiB。仅支持可信本机单写入者与进程中断，断电和多进程写入另做验收。
