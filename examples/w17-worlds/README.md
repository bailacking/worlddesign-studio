# W17 两个世界

- **灯脊边境**（`frontier`）是三维种子方块项目。用工作台打开后按 F5，WASD 移动，空格跳跃。`(31,32)` 与 `(32,32)` 之间的道路横跨区块边界。
- **芦岸观测**（`wetlands`）是二维生态项目。F5 试玩，WASD 或方向键移动。画面的浅绿、土黄、淡紫标记分别为芦苇、野兔和蛾，位置与出现时刻可按种子和固定步进复跑。

下面的 PowerShell 命令假定已经构建 Windows CLI，当前目录为仓库根。修改前先复制样例。所有指令返回 JSON，提交修改须带 `--allow-project-write`。

```powershell
$cli = '.\build\native\wds_cli.exe'
Copy-Item -Recurse examples/w17-worlds/wetlands build/w17-wetlands

'{"version":1,"operation":"scene.landscape.sample","mode":"read","expected_revision":0,"input":{"x":31,"z":32}}' |
  & $cli project request examples/w17-worlds/frontier

'{"version":1,"operation":"project.play","mode":"read","expected_revision":0,"input":{"ticks":10,"keys":["D"]}}' |
  & $cli project request build/w17-wetlands

'{"version":1,"operation":"scene.character.set","mode":"apply","expected_revision":0,"input":{"id":"courier","speed":13,"sight":7}}' |
  & $cli project request build/w17-wetlands --allow-project-write

'{"version":1,"operation":"project.play","mode":"read","expected_revision":1,"input":{"ticks":10,"keys":["D"]}}' |
  & $cli project request build/w17-wetlands
```

同样向右移动 10 步，观察员到达 X=48，修改后的信使到达 X=138。比较 JSON 的 `ecology.character` 与 `ecology.populations` 可检查生效属性及可见种群。若要先查看修改，把 `mode` 改为 `preview`。

用 W17 播放器套件可在工作台导出 Windows x64 包。构建方式见 [Windows 导出](../../docs/engineering/development.md#导出-windows-x64-游戏)。
