# WDS 远征示例

**铜脊接力**从草甸采铜、制作工具并穿过熔渣地，在云环击败守卫后带着徽记返回。**芦风博物队**从海岸进入洞穴和遗迹，造船出航、修船并登陆两座岛。

生成工程可由 WDS 工作台或 CLI 打开。远征运行在独立播放器中，规则源文件在 `assets/expedition.json`，可编辑和重新打包。

## Windows 试玩

在仓库根目录、已构建 Windows 播放器时执行。生成目录需要为空或尚未创建，请使用下列新目录。

```powershell
python examples/w24-expeditions/generate.py build/w24-projects
$cli = '.\build\runtime\wds_cli.exe'
& $cli project export build/w24-projects/copper-relay 0 windows-x64 build/copper-relay
& $cli project export build/w24-projects/drift-naturalists 0 windows-x64 build/drift-naturalists
```

运行导出目录中的 **Play.exe**。用 WASD 或方向键移动，Tab 选择相邻站点，F 使用，G 采集，空格攻击。Q 选择配方，C 制作，E 吃食物，句点键等待，R 尝试复活。成功行动自动保存。复制整个包可单独试玩。

- 铜脊接力从右侧铜矿采集，多采两份原料留给后续交易。沿东西走廊依次交易、照顾动物、迎击袭击者，再过熔炉门户。
- 芦风博物队先采芦材、储粮并制作灯、矛和船。先调查洞穴与遗迹，然后回到起点地区，从船坞下水。

## 从项目复跑

可为铜脊接力生成四种模式的独立工程。每次指定一个新的生成目录。例如 `python examples/w24-expeditions/generate.py build/copper-survey --mode survey`，生成的铜脊接力工程位于 `build/copper-survey/copper-relay`。构建模式用 **Y** 选补给、**I** 领取。勘察模式用 **N** 访问下一地区。 [模式和规则包](../../docs/project/w25-w32-systems.md#w28-分支章节)

生成命令加 `--crew` 可在芦风博物队启用角色队伍。选中近处站点可看到检视台词，完成 `rising` 章节后会启用岸线向导及其耐热和抗毒特性。

```powershell
$cli = '.\build\native\wds_cli.exe'
'{"version":1,"operation":"expedition.replay","mode":"read","expected_revision":0,"input":{"actions":[{"command":"gather","target":"copper-vein"},{"command":"craft","target":"spear"}]}}' |
  & $cli project request build/w24-projects/copper-relay
'{"version":1,"operation":"resource.field.set","mode":"apply","expected_revision":0,"input":{"resource_id":"expedition","field":"hunger_interval","value":3}}' |
  & $cli project request build/w24-projects/copper-relay --allow-project-write
```

修改后以修订 1 重放或导出到新目录。Windows 的复跑和分阶段计时命令如下。

```powershell
python examples/w24-expeditions/verify.py --cli build/native/wds_cli.exe --probe build/native/wds_expedition_save.exe --runtime build/runtime/wds_cli.exe --work build/w24-check --report build/w24-report.json
```

Linux 与 macOS 从原生 CMake 构建运行 `ctest --preset dev -R '^expedition_replay$' --verbose`。 [验收文档](../../docs/project/w18-w24-expeditions.md)

## W24 分段计时与 Java 参考数据复核

给验证命令追加 `--seed` 和 `--scale` 可指定试演组合。四轮使用 `73021/0`、`73021/4`、`16431/4`、`4294967295/4`。每轮重新生成工程，用共享项目操作修改两个源工程的饥饿间隔和远征地形种子，并从修订 2 完整重放。Windows 记录单包的打包、完整性检查和图形冒烟时间。Linux 与 macOS 复跑生成、回放和存档，不生成 Windows 运行包。

规模设为 4 时，每个区域额外生成 16 条补给链。先用 G 采集，再用 Q 选配方、C 合成，最后用 F 在交易站兑换营地凭券。采集点的专属 PNG 和类型默认图标一起打包，验证入口检查每份成品中的图标是否与源项目逐字节相同。

W2 原版文件保留在仓库外。用已取得的参考目录运行以下命令，输出只有版本指纹、注册与文件计数和抽查结果。

```powershell
python examples/w24-expeditions/inspect_java_reference.py <W2-reference-folder>
```

参见 [W17–W24 参考复核](../../docs/project/w17-w24-reference-audit.md)。
