# WDS 插件 SDK 源码样例

源码样例通过 C++ 插件宿主启用和停用插件。原生模块样例另外构建两份配色动态库和一份旧版动态库，检查宿主可依次加载、替换、诊断旧版并恢复原配色。

先从 WDS 构建目录安装源码 SDK。

```sh
cmake --install build/dev --component PluginSDK --prefix build/sdk
cmake -S build/sdk/share/wds/plugin-sdk -B build/sdk-demo
cmake --build build/sdk-demo --config Release
ctest --test-dir build/sdk-demo -C Release --output-on-failure
```

也可直接将 `examples/plugin-sdk` 作为 CMake 源目录。原生模块使用 `palette_abi.h` 中的 C 接口，版本函数返回 ABI 版本，配色函数收发 `uint32_t` 表示的 24 位 RGB。样例直接加载测试传入的本机动态库路径，在获取和调用配色函数之前检查版本。宿主组合结束后释放库句柄。 [插件架构](https://github.com/bailacking/worlddesign-studio/blob/main/docs/architecture/plugins.md)

`wds_native_sdk_demo` 提供单个配色模块的安装槽。使用 `--install DIRECTORY LIBRARY` 安装或更新可信的本机配色模块，`--preview DIRECTORY` 查看配色结果，`--remove DIRECTORY` 卸载并恢复内置配色。更新先验证临时副本，再替换已安装模块。ABI 不匹配时保留原模块。独立的 `sdk_native_store` 回归覆盖安装、更新、拒绝不兼容更新和卸载。
