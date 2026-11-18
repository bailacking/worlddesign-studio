# W12 双样本计时

先按[开发指南](../../docs/engineering/development.md#导出-windows-x64-游戏)构建 Windows 工作台、CLI 和 Windows 运行包，并把运行包安装到工作台目录。使用同一个工作台构建目录编译试验程序。仓库根目录示例如下，输出路径必须尚不存在。

```powershell
cmake --build build/studio --target wds_w12_probe
python examples/w12-lab/run.py --cli build/studio/wds_cli.exe --studio build/studio/wds_studio_modern.exe --probe build/studio/wds_w12_probe.exe --output build/w12-replay
```

如果使用独立 MSVC 运行包构建目录，按开发指南将对应运行包安装到 `build/studio`。复跑从空输出目录开始，按[任务书](brief.md)生成两套目录、资产、场景和规则，并执行复跑、工作台截图以及二维导出。方块项目保存为修订 1，二维从修订 0 导出。 `report.json` 记录工具指纹、阶段、调用结果、预置失败和意外失败。 `checksums.json` 记录两套交接目录和实验源文件的 SHA-256。人工与模型参与量按当次实验填写，机器复跑默认只记录机器段。

初次运行的[原始回执](results.json)与[产物校验值](checksums.json)供对照。机器耗时和窗口截图随环境变化。 [W12 验收与规则拆分](../../docs/project/w12-generation-study.md)
