# W16 双类型样品

「风标接力」用 WASD 或方向键移动。绕开中央障碍，走到右侧终点。
「坡地营地」用 WASD 移动，方向键转动视角，空格跳跃，鼠标左键拆除、右键放置。

## 生成工程和独立包

在 Windows x64 的 MSVC 开发环境中，从仓库根目录执行以下命令。也可以使用已经带 W16 播放器套件的 CLI。

```powershell
cmake --preset dev -B build/windows-player -DCMAKE_BUILD_TYPE=Release -DWDS_BUILD_WINDOWS_PLAYER=ON
cmake --build build/windows-player --target wds_player wds_cli
python examples/w16-delivery/accept.py --cli build/windows-player/wds_cli.exe --output build/w16-replay
```

请选择一个尚未存在的输出目录。打开后缀为「玩家版」的目录中的 `Play.exe` 试玩，用 Esc 退出。续编时用工作台打开后缀为「工程」的目录。初版和续编版均记录在 `report.json`。离线建议、确认、撤权和接手过程见 `agent-receipts.json`。执行详情见 [W16 验收记录](../../docs/project/w16-shared-delivery.md)。

## 单独试用配色和 AI

用工作台打开「坡地营地 工程」的副本。选择 **场景 → 方块配色 → 琥珀暮色**。天空变色后按 **Ctrl+Z**，天空恢复日光。也可从 CLI 用 `scene.palette.set`、`scene.restore` 查看相同的项目事务。

要单独复跑 AI，把「风标接力 工程」复制为同在输出目录下的「风标接力 试验副本」，复用生成的 `adapters.json`。该配置含本机 Python 和样品适配器的绝对路径。

```powershell
python scripts/project_agent.py --cli build/windows-player/wds_cli.exe --project 'build/w16-replay/风标接力 试验副本' --adapters build/w16-replay/adapters.json
```

每行输入一个 JSON 对象。下列顺序先预览，再明确确认一次写入，最后把工作交回人工。请在试验用工程中执行。

```jsonl
{"type":"grant","operations":["scene.field.set"]}
{"type":"propose","task":"调整 runner 的起点"}
{"type":"commit"}
{"type":"switch","adapter":"west"}
{"type":"pause"}
{"type":"revoke"}
{"type":"resume"}
{"type":"handoff"}
```
