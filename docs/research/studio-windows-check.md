# Windows 工作台人工验收记录（EDT-02A）

按 [Windows 工作台验收任务卡](../project/development-tasks.md#edt-02a-windows-工作台验收任务卡) 执行，范围为 `wds_studio.exe` 的键盘操作、创建、打开与错误反馈。以下结果适用于提交 `e327f4d` 的窗口和项目服务。

验收日期：2026-11-14。分支 `task/edt-02a`，提交 `e327f4d`。

## 验收环境

| 项 | 值 |
| --- | --- |
| 系统 | Windows 11 23H2（Build 22631），zh-CN，控制台代码页 936 |
| 显示 | 主屏 2560×1440，系统缩放 150%（`AppliedDPI`=144，显示器 DPI 144） |
| 构建 | `cmake --preset dev`、`cmake --build --preset dev`、`ctest --preset dev --output-on-failure --no-tests=error` |
| 产物 | `build/dev/wds_studio.exe`、`build/dev/wds_cli.exe`（Debug） |
| 一次性目录 | `build/验收 项目`、`build/check-refs`、`build/check-bad-version`、`build/check-missing`、`build/check-cn-id`、`build/check-empty` |

## 执行方式与边界

- 窗口操作由脚本注入完成：Tab 与空格为真实键盘事件（`keybd_event`），按钮为 `BM_CLICK`，编辑框文本为 `WM_SETTEXT`。中文无法用按键输入，因此汉字路径与汉字项目 ID 以文本注入方式填入，创建与打开动作本身仍走真实按钮消息。
- 每个状态用 `GetWindowTextW`、`LB_GETCOUNT`、`LB_GETTEXT` 读取，界面观感以窗口截图核对（`PrintWindow` 渲染，另在物理像素下截屏一次）。截图保存在 `D:\workspace\test\wds-shots\`，属临时证据，不随提交。
- 未修改产品代码，未改系统缩放设置。`PrintWindow` 截图中的标题栏按钮显示为方块是渲染方式造成的，与界面实现无关。

## 结果总览

| 编号 | 检查项 | 结论 |
| --- | --- | --- |
| W-1 | 启动与初始焦点 | 通过 |
| W-2 | Tab 焦点顺序与回绕 | 通过 |
| W-3 | 键盘激活按钮（空格） | 通过 |
| W-4 | 中文与空格路径创建项目 | 通过 |
| W-5 | CLI `project open` 与 `project resources` 核对 | 通过 |
| W-6 | 重复创建同一目录被拒绝 | 通过 |
| W-7 | 打开含引用项目后列表内容 | 通过 |
| W-8 | 打开不存在目录后旧列表清空 | 通过（错误码见 F-1） |
| W-9 | 打开坏清单后旧列表清空 | 通过 |
| W-10 | 空路径提交 | 通过 |
| W-11 | 非 ASCII 项目 ID | 通过（提示可读性见 F-2） |
| W-12 | 关闭并重开同一项目 | 通过 |
| W-13 | 150% 系统缩放表现 | 通过（文字模糊，见 F-4） |
| W-14 | 100% 系统缩放表现 | 未测 |

## 逐项记录

### W-1 启动与初始焦点

步骤：直接运行 `build/dev/wds_studio.exe`，读取窗口线程焦点控件。

预期：窗口出现，焦点落在项目路径输入框。

实际：标题 `WDS Studio | Project Workspace`，外框 700×432，焦点为控件 101（类 `Edit`，内容为空），与代码中 `SetFocus(GetDlgItem(window, folder_input))` 一致。

结论：通过。窗口被其他程序重新激活后焦点先落到框架窗口（控件 ID 0），按一次 Tab 才回到路径框，属 Windows 默认的激活行为。

### W-2 Tab 焦点顺序与回绕

步骤：保持窗口在前台，连续按 Tab 六次，每次读取焦点控件 ID 与类名。

预期：路径输入 → 项目 ID 输入 → Open → Create → 资源列表 → 回到路径输入。

实际：101 `Edit` → 102 `Edit` → 103 `Button`(Open) → 104 `Button`(Create) → 105 `ListBox` → 101 `Edit` → 102 `Edit`，五步一循环，顺序与创建顺序一致。

结论：通过。三个静态标签不参与 Tab，资源列表可获得焦点。

### W-3 键盘激活按钮

步骤：清空路径框，用 Tab 将焦点移到 Create（控件 104），按空格。

预期：按钮被键盘触发，状态行给出缺少路径的反馈。

实际：状态行显示 `Cannot open project: missing_folder at .`，资源列表 0 行。

结论：通过。空格可激活按钮，`IsDialogMessage` 的对话框导航链路正常。

### W-4 中文与空格路径创建项目

步骤：在路径框填入 `D:\project\WDS-worktrees\edt-02a\build\验收 项目`，项目 ID 填 `accept-check`，点击 Create。

预期：在含汉字与空格的路径下创建成功，状态行显示项目 ID、修订号与入口场景，资源列表显示入口场景。

实际：状态行 `accept-check  |  Revision 0  |  Entry scene: intro`；列表 1 行 `intro  [scene]  assets/intro.json`；路径框中的汉字与空格显示正常。磁盘生成 `wds.project.json` 与 `assets/intro.json`，清单为 `format_version` 1、`revision` 0、`entry_scene` `intro`、单条 `intro` 资源。

结论：通过。

### W-5 CLI 核对

步骤：在窗口保持 W-4 结果的同时，运行 `wds_cli project open|resources|validate`，比较修订号、入口场景与资源条目。

预期：CLI 输出与窗口一致。

实际：

- `project open` → `{"code":"ok","entry_scene":"intro","project_id":"accept-check","resource_count":1,"revision":0}`
- `project resources` → `{"code":"ok","project_id":"accept-check","resources":[{"id":"intro","kind":"scene","path":"assets/intro.json","refs":[]}],"revision":0}`
- `project validate` → 同上 open 的结果，退出码 0

控制台代码页 936 与 65001 各测一次，均返回 `ok`；CLI 的 `wmain` 入口按 UTF-8 转换参数，中文路径不受代码页影响。

结论：通过。窗口与 CLI 的修订号、入口场景、资源 ID、类型与路径逐项一致。

### W-6 重复创建同一目录

步骤：保持路径与 ID 不变，再点一次 Create。

预期：拒绝创建，给出已存在的反馈。

实际：状态行 `Cannot open project: already_exists at .`，资源列表 0 行；原项目文件未被改动（后续 W-12 重开仍为修订 0、1 条资源）。

结论：通过。附注：失败时列表被清空，见 F-3。

### W-7 打开含引用项目

步骤：路径框改为 `build/check-refs`（`tests/fixtures/project-v1/valid-with-refs` 的副本），点击 Open。

预期：显示该项目信息与两条资源，引用关系不丢失。

实际：状态行 `demo  |  Revision 0  |  Entry scene: intro`；列表 2 行：`intro  [scene]  assets/intro.json`、`rules  [data]  assets/rules.json`。

结论：通过。

### W-8 打开不存在目录后旧列表清空

步骤：在 W-7 的两行列表之上，把路径改为尚未创建的 `build/check-missing`，点击 Open。

预期：给出打开失败反馈，旧项目的资源列表清空，不残留 `demo` 的行。

实际：状态行 `Cannot open project: unsafe_path at .`，列表 0 行。

结论：通过（列表清空符合任务卡要求）。错误码与规格的差异记入 F-1。

### W-9 打开坏清单后旧列表清空

步骤：把 `tests/fixtures/project-v1/unsupported_version` 复制到 `build/check-bad-version`（`format_version` 为 2），点击 Open。

预期：报告版本不受支持，列表清空。

实际：状态行 `Cannot open project: unsupported_version at /format_version`，列表 0 行。

结论：通过。诊断带错误码与字段位置，可直接定位到清单字段。

### W-10 空路径提交

步骤：清空路径框，点击 Open。

预期：提示缺少路径，不访问文件系统。

实际：状态行 `Cannot open project: missing_folder at .`，列表 0 行。

结论：通过。

### W-11 非 ASCII 项目 ID

步骤：路径改为新目录 `build/check-cn-id`，项目 ID 填 `验收`，点击 Create。

预期：拒绝创建，并说明项目 ID 的合法字符。

实际：状态行 `Cannot open project: invalid_manifest at /project_id`，目录未创建（`Test-Path` 为 False），列表 0 行。

结论：通过（拒绝且未落盘）。提示只给错误码，未说明 ID 规则，记入 F-2。

### W-12 关闭并重开同一项目

步骤：关闭窗口（`WM_CLOSE`，进程退出），重新启动，路径填 `build/验收 项目`，点击 Open。

预期：同一项目可再次打开，修订号与资源与 W-4 一致。

实际：启动后焦点同样在路径框；状态行 `accept-check  |  Revision 0  |  Entry scene: intro`，列表 1 行 `intro  [scene]  assets/intro.json`。

结论：通过。

### W-13 150% 系统缩放

步骤：系统缩放为 150% 时启动窗口，用 DPI 感知进程读取物理像素并截屏。

预期：控件不重叠、不裁剪，文字可读。

实际：`GetDpiForWindow` 返回 96，物理外框 1050×648（=700×432×1.5），进程未声明 DPI 感知，Windows 按位图拉伸整窗。控件位置与逻辑坐标一致，无裁剪、无重叠；文字边缘发虚，与同屏其他 DPI 感知程序相比明显。

结论：通过（可用但有模糊）。成因与修复方向见 F-4。

### W-14 100% 系统缩放

未测。切换系统缩放需注销当前会话，会中断本次验收与用户桌面，未执行；本机常驻 150%，无法在同一次会话内取得 100% 证据。

## 发现

### F-1 不存在目录返回 `unsafe_path`，与读取契约不一致

最小重现：

```powershell
mkdir build/check-empty
./build/dev/wds_cli.exe project open build/check-empty     # missing_manifest at wds.project.json
./build/dev/wds_cli.exe project open build/check-missing    # unsafe_path at .
```

[项目目录与清单](../architecture/project-format.md) 规定“无清单返回 `missing_manifest`，链接、非普通文件或歧义路径返回 `unsafe_path`”。实测空目录正确返回 `missing_manifest`，但不存在目录返回 `unsafe_path`，`open_project` 在进入清单检查前就把非 `ok` 的目录状态统一归为 `unsafe_path`。界面与 CLI 表现一致，属项目服务行为问题，不在界面修复范围，需由实现方复核。

影响：路径拼错时用户看到“路径不安全”，而非“目录里没有清单”，误导排查方向。

### F-2 非 ASCII 项目 ID 被静默转成空串

界面的 ID 转换函数逐字节丢弃大于 127 的字符，`验收` 变成空串，再交给创建逻辑，因此报 `invalid_manifest at /project_id`，而不是“ID 只能用小写字母、数字和连字符”。拒绝行为正确，缺的是可读提示。属界面修复范围。

### F-3 失败时清空已加载的资源列表

`present()` 先执行 `LB_RESETCONTENT` 再判断成败，所以任一失败操作都会清掉当前已加载项目的资源行（W-6、W-8、W-9、W-10、W-11 均为 0 行）。任务卡要求记录该状态，此处记为观察：列表不残留旧数据符合“清空”要求，但用户会失去上次成功项目的可见上下文，是否保留由产品判断。

### F-4 未声明 DPI 感知，高缩放为位图拉伸

工程未提供清单或 `SetProcessDpiAwareness` 调用，`GetDpiForWindow` 返回 96。在 150% 下整窗被拉伸，文字发虚；布局按 96 DPI 逻辑像素固定，未随 DPI 调整字号与间距。建议后续界面任务声明 DPI 感知并按 DPI 布局，属界面修复范围。

### F-5 窗口尺寸固定

窗口样式无 `WS_THICKFRAME`，固定 700×432，资源列表高度 220。资源较多时只能滚动；路径超过约 70 个字符时依赖编辑框横向滚动。当前样例规模下不影响验收，记录为界面改进项。

## 阻塞与未测

- W-14（100% 缩放）未测：切换系统缩放需注销会话，未执行。
- F-1 属项目服务实现，界面不参与修复；按任务卡交实现方复核，本报告不含代码改动。
- F-2、F-4、F-5 属界面修复，按任务卡另行认领。

## 编译器与自动测试

构建与测试在带 MSVC 环境的终端执行，均成功：

| 项 | 结果 |
| --- | --- |
| 编译器 | MSVC 19.44.35228.0（`cl` 19.44.35228） |
| CMake | 3.31.6-msvc6 |
| Ninja | 1.12.1 |
| 配置 / 构建 | 退出码 0 / 0（Debug，`/W4 /WX`，无告警中断） |
| CTest | `ctest --preset dev` 6/6 通过，耗时 2.16 s |

用例：`smoke`、`plugin_host`、`voxel_world`、`platform_project`、`sandbox_rules`、`rename_flow` 全部通过，无跳过。
