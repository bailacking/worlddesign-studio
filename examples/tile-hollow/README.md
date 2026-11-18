# 谷地昼夜

用工作台打开本目录，点「试玩」，在视口中用 WASD 或方向键移动。出生点右侧的岩石会挡路，绕行向东可跨多个区域，最后抵达路标。树冠盖住经过下面的实体并遮挡查询视线，角色仍能从树冠下走过。昼夜循环在 240 步内完成，从正午开始。

复制示例后可用共享操作修改地块。以下请求写入 UTF-8 JSON 后送到 `wds_cli project request 工程路径 --allow-project-write`，把坐标 `(0, 0)` 的岩石改成泥地。先用 `mode: "preview"` 可校验改动，源项目保持原修订。

```json
{
  "operation": "scene.tile.set",
  "version": 1,
  "mode": "apply",
  "expected_revision": 0,
  "input": {"x": 0, "y": 0, "kind": "soil"}
}
```

保存成功后重开，再沿原方向前进，角色会穿过原先的岩石位置。 [格式、寻路查询与验收](../../docs/project/w13-worlds.md)
