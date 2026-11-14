# 项目目录与清单

## 首轮实现范围

这是 PRJ-01 的项目契约草案。格式对真实项目生效前，由 Codex 审定迁移、文件提交和跨平台路径细节。首轮样例与测试只使用本页固定子集，借此并行准备测试资料。现有方块项目仍使用当前的运行格式。

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
- `entry_scene` 引用清单中类型为 `scene` 的资源。每个 `refs` 元素引用同一项目内存在的资源。样例的场景与数据文件使用 `{}` 作为占位，当前仅验证清单与文件关联，场景内部结构由 RUN-01 定义。
- 读取验证失败时报告清单字段或资源位置，项目不写盘。失败分类使用 `unsupported_version`、`duplicate_id`、`duplicate_path`、`invalid_path`、`missing_reference`、`missing_file`、`invalid_entry_scene`。测试样例一次引入一类错误。格式仍可在正式实现前调整，调整需同步样例和 [平台总纲](../project/platform-plan.md)。
