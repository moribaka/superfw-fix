# r88 源码基底与 ROM 兼容

- 上游：[SuperFW](https://github.com/davidgfnet/superfw)。
- 基础版本：[v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)。
- 上游提交：`4701c4e92a4c2defc91dcbc2924fa468d672727f`。
- 硬件：SuperChis，固件上限 2 MiB。
- 构建标识：`88000001`。

感谢 David Guillen Fandos（davidgf / davidgfnet）和 ChisBread。原有许可、源码署名和第三方声明保留。

## 本版 ROM 兼容

《欢乐足球 球队球员名全汉化版 v1.3》（BIXJ v0）新增游戏内存档适配，处理提示保存成功但重新启动没有进度的问题。使用“Hack 加强”补丁模式。

本次对应的 ROM 为 16 MiB，SHA256：`79bc6cf662c1aef458fab8cc4294dfd37a2184ab3c76c057980db72eaa170f07`。已完成本机 SRAM、DirectSave 的新建保存、覆盖保存和独立冷启动读档测试，实卡仍待确认。

内置加强库现有 1,965 条记录；此前 1,964 条记录内容保留。该 ROM 的适配源码位于 `res/rom-patches/football-v13-compare.S`。

## 构建与使用

执行 `make BOARD=chis`。游戏使用单一 32 MiB SDRAM 或 NOR，保留既有 ROM 与完整/精简菜单适配。安装与升级方式见 README。

## NOR 重刷范围

仅已经写入 NOR 的上述《欢乐足球》v1.3 需重新写入一次，以更新游戏内的存档适配。其他 NOR 游戏无需因本次更新重刷；从 TF 直接启动该 ROM 时，更新固件并选择“Hack 加强”即可应用。

更早旧版《晓月圆舞曲[简大字]》仍按其历史交付范围重写；已经使用修正版写入的无需重复操作。
