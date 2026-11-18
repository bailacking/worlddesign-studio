# 滑翔规则示例

用工作台打开本目录，点击试玩。滑翔标记从起点向右移动，逐步加速到巡航。代码保存在 `assets/glide.json` 的 `behavior.document`。从仓库根目录用 CLI 跑 5 步，初始修订为 0，预期 `speed=8`、标记 `x=-66`：

```sh
wds_cli project play examples/shared-behavior 0 5
```

将以下请求以 UTF-8 JSON 送到 `wds_cli project request examples/shared-behavior` 的标准输入，可以得到 `version:1` 节点图。转换只读，返回的新文档可作为另一份场景的 `behavior.document`，并将 `behavior.format` 改成 `graph`。

```json
{
  "operation": "behavior.convert",
  "version": 1,
  "mode": "read",
  "input": {
    "from": "code",
    "to": "graph",
    "document": "param step 2\nparam cruise 8\nstate speed 0\non start set speed step\non tick if speed < cruise add speed step\n"
  }
}
```

将图中 `cruise` 参数从 8 改成 6，再用 `from:graph` 和 `to:code` 转回代码，5 步后预期 `speed=6`、标记 `x=-72`。以项目操作保存时，使用带项目身份、修订和原场景字节的 `scene.restore`，从而在过期或文件被外部改动时保留现有内容。

`behavior_project` 自动核对两种表示的完整运行结果、参数编辑后的重开，以及不支持语句及其换行的保留。配置 Qt 工作台后，`workbench_behavior_smoke` 会打开示例并生成 `w11-workbench.png`。 [共同语义](../../docs/architecture/authoring.md#共同语义基础)
