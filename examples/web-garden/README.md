# 风园浏览器试玩

浏览器展示与 Windows 版相同的[风园试验场](../w25-lab/README.md)。两种启动方式都使用 WDS C++ 回放引擎计算坐标、库存、作物与烹饪产物。

## 独立浏览器构建

在已启用 Emscripten SDK 4.0.23 的终端，从仓库根目录构建。

```sh
emcmake cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --target wds_web_garden
python -m http.server 8765 --bind 127.0.0.1 --directory build/wasm
```

打开 [风园](http://127.0.0.1:8765/web-garden/)。`build/wasm/web-garden` 是可部署到静态 HTTP 服务的完整目录。WDS 回放引擎和风园项目文件嵌在 WebAssembly 模块中。部署时让服务器以 `application/wasm` 提供 `.wasm` 文件。 [Emscripten 文件打包文档](https://emscripten.org/docs/porting/files/packaging_files.html)

## 使用桌面构建预览

在仓库根目录，用 Python 3.10 或更新版本启动本机预览。

```powershell
python examples/web-garden/serve.py --cli build/native/wds_cli.exe
```

在 Linux 或 macOS 上把 `--cli` 指向本机构建中的 `wds_cli`。在浏览器打开服务打印的地址。移动可以用方向键或地图下方的按钮。采集种子、香草和树皮，制作炉具，前往种植地播种。往返潮滩后收获，在厨房选择香草和根茎，分别入锅，再点烹饪。

桌面预览服务监听本机回环地址。两种启动方式都在浏览器内存中保留最多 256 个成功动作，重新开始会清空会话。
