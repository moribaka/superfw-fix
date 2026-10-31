# r81 主线基底

- 上游项目：[SuperFW 主线](https://github.com/davidgfnet/superfw)。
- 上游版本：[v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)。
- 上游提交：[4701c4e92a4c2defc91dcbc2924fa468d672727f](https://github.com/davidgfnet/superfw/commit/4701c4e92a4c2defc91dcbc2924fa468d672727f)。
- Hack 增强来源：r79 源码提交 `e5864dad023826c1b2020a048dccf0cff2e57e90`。
- 基础版本号：`0x00000016`，构建标识：`81000001`。
- 目标硬件：SuperChis，固件容量上限 2 MiB。

源码以 v0.22 完整主线为起点，迁移原有 Hack 功能。感谢上游作者 David Guillen Fandos（davidgf / davidgfnet）开发并开放 SuperFW。

## 保留的增强

内置 Hack 数据库保留原有 1,962 条查表记录，并加入 Monomyth v1.0 一条，共 1,963 条；数据库 ABI 仍为 75，默认数据库保持不变。
保留专用存档和 RTC、完整／精简菜单及已定义 ROM 布局、网页补丁流式处理、查库回退和未命中提示。

金手指保留原有多格式导入器和 VM；它们取代了主线的旧格式解析器，不能混用两套表头单位。

RTC 保留 r79 的 176 字节日期读取实现及相应重定位。这套实现已经包含年份、日期、闰年和星期修正，另外支持无符号时间换算和固定返回值，供专用接口和六档调速继续使用。

## 与主线衔接

- DirectSave 保留主线两个高字节左移 24 位的修复，以及新的字节写入实现。
- 存档覆盖使用主线临时文件流程，同时保留原有 RTC 持久化。
- 设置解析沿用主线恢复分隔符的修复；布尔 UI 设置上限采用 2，使保存值 1 能正常载入。
- 浏览器隐藏规则采用主线实现，函数使用 `static inline` 以匹配当前 C 编译方式。
- 完整菜单使用 `-fno-inline-functions-called-once`，精简菜单使用 `-fno-tree-sra`，使编译后的菜单继续适配原预留布局；存储长度仍为 53,308／28,388 字节。

游戏仍使用单一 32 MiB SDRAM 或 NOR 映像。未增加 ROM 备份、SD 临时 ROM、ROM overlay、修复后复核或 NOR 烧入后回读。

## 构建

执行 `make BOARD=chis`，生成内容相同的 `superfw.gba` 和 `superfw.fw`。
沿用主线已有的内置模拟器资源；源码同时包含原增强版构建所需的 `emu/sfc-patches.bin`。

本地回归包括补丁测试、两类 RTC 接口和代表性游戏菜单。实体卡刷写、物理 SD／NOR 操作以及完整游戏流程不在本次本地验收范围内。
