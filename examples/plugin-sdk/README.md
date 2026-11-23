# WDS 插件 SDK 源码样例

样例通过 C++ 插件宿主启用和停用插件，更换两个配色提供方，并检查旧契约版本会产生诊断。兼容提供方可重新组合，预览恢复到原配色。

先从 WDS 构建目录安装源码 SDK。

```sh
cmake --install build/dev --component PluginSDK --prefix build/sdk
cmake -S build/sdk/share/wds/plugin-sdk -B build/sdk-demo
cmake --build build/sdk-demo --config Release
ctest --test-dir build/sdk-demo -C Release --output-on-failure
```

也可直接将 `examples/plugin-sdk` 作为 CMake 源目录。样例随宿主源码构建，使用同一编译器与 C++20 标准。ABI、运行时动态加载和第三方扩展签名见[插件架构](https://github.com/bailacking/worlddesign-studio/blob/main/docs/architecture/plugins.md)。
