# W8 礁灯投递验收

交付截止为北京时间 2026 年 11 月 20 日 22:00。验收包含本机的 Windows 工作台构建及冒烟、另一位创作者的接手流程，以及第二台 Windows 机器运行独立成品。

## 第二台机器试玩

把「礁灯投递」Windows x64 压缩包在第二台机器完整解压，双击 **Play.exe**。需要 Windows 10 或更新版本的 x64 桌面及支持 OpenGL 3.3 的驱动。

1. 看到信使、隔墙、红色浅礁和绿色信站后，用 **WASD** 或方向键开始。倒计时从第一次移动开始。
2. 向下再向右进入浅礁，看到 **Delivery failed.** 后按 **Enter**，确认信使回到出生点。
3. 向上绕过隔墙，到绿色信站上方再向下，看到 **Delivered!**。按 **Esc** 退出。

记下 Windows 版本、两种结局和重开结果。如启动失败，保留完整错误提示。

## 创作者接手

完整解压创作者练习包。双击 **Start-Studio.cmd** 直接打开 **Project**，也可以打开 **Studio/wds_studio_modern.exe** 后在项目菜单选 **Project** 目录。系统提示缺少 Visual C++ 运行库时，可安装微软的 x64 版本。 [Microsoft Visual C++ x64 运行库](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)

1. 把包内的 **handoff-notes.txt** 拖进工作台。确认类型为文本、导入预览显示 **3 行**，提交后在资源面板点开 `handoff-notes` 查看文字。
2. 在场景树选中 `courier`，把 **X 位置**从 `-223` 改为 `-200`，点击 **保存属性**。从原始修订 0 开始，两次操作后应为修订 2。
3. 关闭并重开 **Project**。确认文本资源仍可查看，信使 **X 位置**仍为 `-200`。按 **F5** 可试跑，按 **Shift+F5** 停止。
4. 用 **项目 → 导出 Windows x64** 选一个项目外的父文件夹和新目录，导出后双击该文件夹内的 **Play.exe**。确认出生点略向右。发布锁 `wds.release.json` 的修订应为 2。

回传是否独立完成四步和实际导出修订。若有卡点，附界面提示或截图即可。

## 签收记录

| 验收项 | 结果 |
| --- | --- |
| Windows 工作台构建与冒烟 | Windows 11 x64 本机通过，W1–W16 集成分支也通过 Windows CI。 [GitHub Actions](https://github.com/bailacking/worlddesign-studio/actions/runs/44298731361/job/134782505726) |
| 本机创作者流程复跑 | 自动操作工作台完成导入、修改 X、保存、重开和修订 2 导出，播放器自检通过 |
| 本机独立成品 | 从压缩包解压到临时目录，系统 PATH 仅含 System32。退出、胜负和原地重玩通过 |
| 另一位创作者完成接手 | 待人工回传 |
| 第二台 Windows 机器试玩 | 待人工回传 |

[周计划](50-week-plan.md) · [发布包规格](../architecture/platforms.md#windows-x64-发布包) · [示例](../../examples/reef-delivery/README.md)
