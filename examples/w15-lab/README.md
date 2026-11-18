# W15 运行时试验

## 构建和复跑

使用 CMake、Ninja、Qt 6.8 的 Widgets 与 Network 模块。复跑脚本还需要 Python 3.10 或更新版本。Windows 需从已加载 MSVC 的终端构建，Qt 的 `bin` 目录加入 PATH。

```powershell
cmake --preset dev -B build/w15 -DWDS_BUILD_W15_LAB=ON "-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64"
cmake --build build/w15 --target wds_cli wds_w15_server wds_w15_layers
ctest --test-dir build/w15 -R '^w15_runtime_lab$' --verbose
```

回归会新建副本和临时端口，写出红石回放、两客户端报告和三张换装 PNG。Windows 实测摘要在 [results.json](results.json)。先测试方块线路，再启动服务器和两个真实本机 TCP 客户端，最后运行分层渲染对照。

## 单独试用

- 红石请求使用 `wds_cli project request examples/ore-furnace`，以 UTF-8 标准输入送入 [circuit.json](circuit.json)。
- 服务端试验程序用 `wds_w15_server SURFACE_PROJECT CAVERN_PROJECT SAVE_FILE [PORT]` 启动。两个项目可从「矿炉起步」复制，需共享物品定义且都是修订 0。省略端口时选择空闲端口，启动后在终端输出端口号。
- 分层预览用 `wds_w15_layers examples/w15-lab/assets` 启动。发布试用目录可省略参数，程序自动读取同级 `assets`。用下拉框切换提灯与行囊、栏杆前后。 `--compare ASSETS OUTPUT` 可导出帧供对照。

角色本体和提灯沿用 W4 的原创素材，行囊使用单独的小图和挂点。 [W4 素材说明](../w4-lab/README.md) · [协议信息与验收记录](../../docs/project/w15-runtime-lab.md)
