# 桌面工作台

## 第一版交付

工作台采用可调整的编辑器布局。顶部是菜单、项目名称与修订、保存、撤销和运行控制。左侧为场景树，中央为场景视口，右侧为属性检查器，底部为资源和输出页签。面板可以隐藏和恢复，布局随工作台设置保存。首轮使用清晰的深色界面，突出选中、修改中、失败和运行中状态，保留键盘焦点提示与原生交互。

第一条正式创作路径是打开项目、选中实体、修改一个真实属性、保存并试玩。窗口内能判断改动是否已保存、试玩用了哪个修订、失败发生在哪个资源。后续加入可撤销的视口操作、资源导入和独立导出。原生 Win32 工作台继续作为已有功能入口，新工作台完成同一条创作路径后再切换默认入口。

## 首版技术路线

首版采用 **Qt 6 Widgets**，这是可替换的编辑器前端。用 `QMainWindow` 与停靠组件实现面板，场景视口独立于窗口布局。先交付 Windows x64 构建；最低 Qt 6.8，实际使用的补丁版、安装来源与构建命令由 WKB-01 记录。本地已从 Qt 下载仓库获取 Qt 6.8.3 MSVC 2022 x64 SDK。可在独立构建目录编译并运行现代工作台。核心和 CLI 沿用 C++20 与现有默认构建。使用 `WDS_BUILD_MODERN_STUDIO` 显式启用，Qt 目标的运行库配置与所选 SDK 一致。编辑器发行任务负责 Qt 运行库、许可材料及第三方声明。 [停靠组件](https://doc.qt.io/qt-6/qdockwidget.html) · [高 DPI](https://doc.qt.io/qt-6/highdpi.html) · [Windows 工具链](https://doc.qt.io/qt-6.8/windows.html) · [Qt 开源分发说明](https://www.qt.io/development/open-source-lgpl-obligations)

首版 UI 异步调用同一构建的 `wds_cli` 进行项目查询和写入。请求完成前禁止切换项目和提交另一条改动。项目数据由 C++ 项目服务验证和落盘。二维视口直接读取共享 C++ 会话的逐步状态，正式图形与输入适配由 SIM-01 和 WKB-08 继续推进。

## 模块边界

| 模块 | 读取 | 交互与状态 |
| --- | --- | --- |
| 工作区 | 项目 ID、路径、修订及可用操作 | 打开、创建、关闭、恢复布局。真实动作启用状态由当前项目决定 |
| 场景树 | 入口场景实体及稳定 ID | 选择和搜索。实体层级由场景数据模型提供后接入 |
| 属性 | 当前实体及字段类型、当前值、范围与默认值 | 修改后先保留草稿，提交时带预期修订。失败或冲突保留草稿并定位字段 |
| 资源 | 项目资源 ID、类型、路径、引用 | 搜索与选择。导入、移动和删除经共享操作执行 |
| 视口 | 编辑态场景或指定修订的试玩会话 | 选中态联动，编辑输入与游戏输入分别路由，退出试玩归还编辑焦点 |
| 输出 | 操作、运行及导出诊断 | 记录时间、级别、资源、字段和修订。能定位来源时提供跳转 |

当前 Windows 构建已接入工作台主题、场景树和资源面板。资源区显示 ID、类型和路径，支持搜索与筛选。资源引用查看归入 PRD-04。

**设置 → 界面语言**可即时切换简体中文与英文，工作台会记住选择。切换时保留正在编辑的项目、属性草稿与过滤条件。项目 ID、资源路径、修订和诊断码使用源值。

## 首批可并行交付的组件

首批组件以 `src/studio/` 为根。WKB-01 将它们纳入可选的 `wds_studio_modern` 目标。面板使用命名空间 `wds::studio`。调用方传入的 JSON 已由工作台桥接层确认基本结构，组件自己保存所需显示数据，项目切换时传空数组清空。用户主动选择时回调一次当前稳定 ID，清空选择传空字符串。更新模型和从其他面板同步选中时不触发回调。过滤时保留逻辑选中 ID，清除过滤后恢复可见选中。

- **WKB-02 主题**。唯一产物为 `src/studio/theme/workbench.qss`。以 Qt palette 和控件状态组织深色样式，覆盖工具栏、菜单、停靠标题、树、列表、页签、输入及状态栏。使用系统字体、控件原生尺寸和焦点边框。选中、悬停、禁用、键盘焦点可区分，资源路径保持可读。WKB-01 负责加载。
- **WKB-04 场景树**。唯一可改文件为 `src/studio/scene_tree.hpp` 与 `src/studio/scene_tree.cpp`。实现 `SceneTreePanel final : public QWidget`。构造函数参数为 `std::function<void(QString)> onSelect, QWidget* parent = nullptr`。提供 `void setEntities(const QJsonArray& entities)` 与 `void setSelectedId(const QString& id)`。每条实体读取 `id`，按输入顺序列出，支持大小写不敏感的搜索和键盘选择。无实体或无匹配时显示对应状态。
- **WKB-05 资源面板**。唯一可改文件为 `src/studio/resource_panel.hpp` 与 `src/studio/resource_panel.cpp`。实现 `ResourcePanel final : public QWidget`，构造函数参数同上。提供 `void setResources(const QJsonArray& resources)` 与 `void setSelectedId(const QString& id)`。每条资源读取现有 `project resources` 的 `id`、`kind`、`path`，按 ID 或路径搜索，支持按类型过滤。长路径有完整提示，空结果和空项目分别显示。

组件可用 Qt 自带控件与普通 C++ 回调实现，首批无需自定义 Qt 信号或网络依赖。集成时使用实际项目数据，能完成上述路径即交付；涉及行为的改动沿用现有构建检查，出现回归再补针对性用例。 [实施任务与分工](../project/parallel-delivery.md)
