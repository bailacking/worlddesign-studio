# 礁灯信使 · W4 对照样机

同一个小场景可以用 Qt 栅格和 raylib OpenGL 启动。用方向键或 WASD 收集三盏灯，走到右边出口。Tab 在两套图形间切换，R 重新开始。在 raylib 版本用空格试听拾取音、M 切换循环音乐、V 查看三维模型。Tab 会同时切换当前样本。音频开关初始为关，收集仍触发拾取音。

[结果与工期](../../docs/project/w4-benchmarks.md) · [已采集的度量](results.json)

## 从源码构建

使用 Windows x64 的 MSVC 开发环境、Qt 6.8 Widgets 和 Python。本次用 Qt 6.8.3、Pillow 12.3.0。Pillow 只用于试验资源生成。构建配置从上游取带 SHA-256 校验的 raylib 5.5 源码。

```powershell
python examples/w4-lab/assets.py build/w4/assets
python scripts/w4_content_study.py build/w4/content

$w4Qt = 'C:/Qt/6.8.3/msvc2022_64'
cmake -S examples/w4-lab -B build/w4/native -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$w4Qt"
cmake --build build/w4/native
& "$w4Qt/bin/windeployqt.exe" ./build/w4/native/w4_qt.exe
```

运行任一程序时传入生成的资源目录，或把 `assets` 目录放在程序旁边。

```powershell
./build/w4/native/w4_qt.exe build/w4/assets
./build/w4/native/w4_raylib.exe build/w4/assets
```

## 复现度量

在独立终端依次测量两边。Qt 命令在可用时也可用 `-platform offscreen` 指定平台插件。

```powershell
./build/w4/native/w4_qt.exe --bench build/w4/assets build/w4/frame-qt.png
./build/w4/native/w4_raylib.exe --bench build/w4/assets build/w4/frame-raylib.png
```

固定的右移序列测试三次收集、路线和 A/B 切换。两行 JSON 分别给出脚本输入开始到绘制提交、以及加上整帧读回的 P50 和 P95；raylib 额外保存两张模型截图。按正常运行入口可试声音和键盘手感。样机资产从脚本再生，仓库保存输入、来源和采样记录。
