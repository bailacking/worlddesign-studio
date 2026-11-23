# 风园试验场

这是 WDS 的原创种植和烹饪示例。采集种子、香草和树皮，制作炉具，在地块播种。前往另一片区域并返回，成熟后收获。选择厨房，将根茎和香草加入锅中，最高优先级的匹配配方会生成炖食。相同名称、不同到期时间的食材分批保存。

Windows 可导出并打开独立包。在仓库根目录运行以下命令。

```powershell
& .\build\runtime\wds_cli.exe project export examples/w25-lab 0 windows-x64 build/weather-garden
& .\build\weather-garden\Play.exe
```

用 WASD 移动，Tab 选附近地点，G 采集，Q 选配方，C 制作，F 播种或收获，句点等待。在厨房用 Z 选择食材、X 加入锅中、退格移除最后一份、F 烹饪。成功操作后自动保存。

直接启动使用第 1 个存档槽。传入 `--slot=2` 或 `--slot=3` 可分开游玩。 `--check-save=2` 校验第 2 槽并退出，返回值 0 表示有效，1 表示无法读取或验证，2 表示槽位不存在或参数超出范围。

```powershell
python examples/w25-lab/verify.py --cli build/native/wds_cli.exe --probe build/native/wds_expedition_save.exe --runtime build/runtime/wds_cli.exe --work build/w25-check
```

CMake 回归名为 `living_systems`。可在支持的桌面构建中运行无界面用例。Windows 可用 `--runtime` 加跑发行包自检和图形启动。
