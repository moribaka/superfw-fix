# r84 主线基底与补丁库

- 上游：[SuperFW](https://github.com/davidgfnet/superfw)。
- 版本：[v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)。
- 提交：[4701c4e92a4c2defc91dcbc2924fa468d672727f](https://github.com/davidgfnet/superfw/commit/4701c4e92a4c2defc91dcbc2924fa468d672727f)。
- 构建标识：`84000001`，目标硬件：SuperChis，固件容量上限 2 MiB。

感谢 David Guillen Fandos（davidgf / davidgfnet）开发并开放 SuperFW，感谢 ChisBread 的 SuperChis 项目。原有署名和许可保留。

## 通用行为

除自有补丁库及其必要适配外，遵循所采用主线的行为。`src/save.c` 与主线原文件一致，开机转存函数与主线一致；DirectSave、SD 驱动、Flash 管理、最近游戏列表等沿用主线。设置解析采用主线取值，不额外改变错误处理或增加失败停载机制。发现主线问题时报告，不自行修正。

## 补丁库及必要适配

内置 Hack 库共 1,964 条，ABI 为 75，默认数据库保持主线原样。保留游戏专用存档与 RTC、补丁查找与回退、网页补丁流、已定义的完整/精简菜单布局及运行所需的导入器和载荷。

晓月大字版专用记录使用 `IRQPATCH_NATIVE_STARTUP`，从 `0xE0` 原生 IRQ 写入处安装菜单，保留头部 `0xB4` 的汉化跳板和 `0xB8` 指针。NOR 固定启动钩子位于固件 `0x00198400`，首次 IRQ 调度位于 `0x00198608`；原共享桥接前缀与既有入口保持。

完整/精简菜单维持已定义 ROM 布局和存储预算。游戏映像仍为单一 32 MiB SDRAM 或 NOR，不增加 ROM overlay、SD 临时 ROM、额外回读或校验流程。

## NOR

仅此前用旧菜单补丁写入 NOR 的上述晓月大字版需重新写入，以恢复已被覆盖的汉化指针；已用修正版写入的无需再次重刷。其他 NOR 游戏无需因本版通用流程对齐而重刷。

## 构建与范围

`make BOARD=chis`。本地检查覆盖实际 ARM 存档往返及代表性游戏菜单，具体结果随交付说明。未进行实体卡刷写、物理总线时序或完整剧情验收。
