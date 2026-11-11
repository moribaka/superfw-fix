# SuperFW fix R88-B（SuperChis）

基于 [SuperFW v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)，构建标识 `88000002`。使用“Hack 加强”应用本版 ROM 适配。

- 《机战 A－添加战斗汉化和语音 1.4 版》新增 NOR 游戏内菜单适配。
- 《龙珠 GT》汉化版、《西格玛星际传说》v1.2、《战斗弹珠人 B 传说》公测版 v3_89、《换装迷宫 3》完美汉化测试版新增 TF、NOR 菜单适配。
- 《横行霸道·复仇》汉化版新增 TF、NOR 游戏内菜单适配。
- 《宝可梦拉撒路》新增 TF、NOR 游戏内菜单适配。
- 《Quake》GBA 非官方移植原型版新增 TF、NOR 游戏内菜单适配。
- 《危险人物－报复 Payback》欧版补充 TF、NOR 游戏内菜单兼容。
- 《杜皮狗之网球公开赛》欧版、《Global Star Sudoku Fever》美版及欧版补充 TF、NOR 游戏内菜单兼容。
- 《X 战警》《雷曼 4 光明之旅》D 商版补充 TF、NOR 游戏内菜单兼容。
- 《甲虫王国 格莱特斯特冠军之路》日版新增 TF 游戏内菜单适配。
- 《神游 Famicom Mini Collection》未发售简体中文版调整 NOR 菜单返回兼容。
- 《神游之密特罗德 融合》简体版补充 NOR 游戏内菜单适配。
- 《阿斯特堡的恶魔》汉化版调整 NOR 游戏内菜单兼容。
- 《口袋妖怪 绿宝石》美化重制 beta0.1、冰魔计划 3.1、幻影之翼 DP、零、Phantom Wing 调整 NOR 资源兼容。
- 《Famicom Mini》《Classic NES Series》本批精确版本补充 TF、NOR 游戏内菜单兼容，逐版本列于附件 ROM 适配明细。

## NOR 重刷范围

仅部分游戏需重刷 NOR。已写入 NOR、需要应用本版新增或调整 NOR 菜单的精确 ROM 版本，需要重新写入对应游戏，包括机战 A 语音 1.4、宝可梦拉撒路、Quake、Payback 及本批 Famicom Mini / Classic NES 适配版本。仅 TF 变化的甲虫王国及黑暗的遗产 Gauntlet 无需因此重刷 NOR。小容量 NOR 分配缩小项可在重新安装时回收空间。

## 使用与构建

将当前游戏进度转存至 TF，再更新固件。固件信息页按 `Down+B+Start` 启用升级，打开 `.fw` 后按 `L+R+Up` 开始更新，完成后重启。

源码构建执行 `make BOARD=chis`，需要 devkitARM、GNU Make、Python 3 和宿主机 C++ 编译器。

本机 mGBA 测试覆盖相应版本的启动、菜单呼出及返回，实卡及完整剧情仍待验收。

感谢 [David Guillen Fandos](https://github.com/davidgfnet/superfw) 与 [ChisBread](https://github.com/ChisBread/SuperChis)。原项目署名和许可随源码保留。
