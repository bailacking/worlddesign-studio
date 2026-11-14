# 二维运动示例：四方汇流

原创示例，用场景 v1 演示四个实体的匀速直线运动。三个动点从不同方向出发，第 30 步同时汇到中轴附近；`anchor` 是静止参照点。项目 ID `scene-editing`，入口场景 `field`，另有一个被入口场景引用的数据资源 `tuning`。

## 组成

| 资源 | 类型 | 文件 | 说明 |
| --- | --- | --- | --- |
| `field` | 场景 | `assets/field.json` | 入口场景，四个实体 |
| `tuning` | 数据 | `assets/tuning.json` | 参数表与 30 步预期位置 |

| 实体 | 起点 | 每步位移 | 走法 |
| --- | --- | --- | --- |
| `anchor` | (0, 0) | 0 | 静止，原点参照 |
| `drifter` | (−240, 40) | vx 8 | 从左向右 |
| `riser` | (120, −150) | vx −4, vy 6 | 从右下方向左上 |
| `glider` | (−180, 150) | vx 6, vy −4 | 从左上方向右下 |

`y` 轴向上为正，所以 `vy` 为正是向上走。

## 体验步骤

1. 在仓库根打开终端（Windows 先进入 MSVC 环境，见开发指南）。
2. 校验项目：`./build/dev/wds_cli.exe project validate examples/scene-editing`，返回 `ok` 与 `resource_count` 2。
3. 看初始位置：`./build/dev/wds_cli.exe project play examples/scene-editing 0 0`，四个实体停在上表起点。
4. 走 30 步：`./build/dev/wds_cli.exe project play examples/scene-editing 0 30`，三个动点汇到中轴。
5. 想看画面就开窗口：运行 `./build/dev/wds_studio.exe`，路径填 `examples/scene-editing`，点 **Open**（资源列表出现 `field` 与 `tuning`），再点 **Play**，画布上的方块标记开始移动；**Stop** 结束。Qt 现代工作台需按开发指南另开 `WDS_BUILD_MODERN_STUDIO` 构建，入口与试玩相同。

## 三项可改参数

改 `assets/field.json` 里对应实体的字段后，直接重跑第 4 步即可看到变化；会话每次启动都从磁盘重新读取。

| 参数 | 当前值 | 改成什么 | 预期效果 |
| --- | --- | --- | --- |
| `drifter.vx` | 8 | 调到 4 | 30 步后只到 x = −120，仍在左半边；调到 0 则停在 (−240, 40) |
| `riser.vy` | 6 | 改成 −6 | 从向上改为向下，30 步后 y = −330；改成 12 则 30 步后 y = 210 |
| `glider.vx` | 6 | 调到 3 | 30 步后 x = −90，斜线变陡；与 `vy` 一起决定斜率 |

## 30 步预期位置（已实测）

入口场景修订 0、试玩 30 步后：

| 实体 | 位置 |
| --- | --- |
| `anchor` | (0, 0) |
| `drifter` | (0, 40) |
| `riser` | (0, 30) |
| `glider` | (0, 30) |

记录其中一条：`drifter` 从 (−240, 40) 出发，`vx` 为 8，30 步后应位于 **(0, 40)**，实测与预期一致。

## 限制

当前试玩只计算整数位置与位移，没有加速度、碰撞和输入；`tuning` 只作为被引用的数据资源存在，运行时不绑定。场景上限 1 MiB、1024 个实体、每会话 600 步。
