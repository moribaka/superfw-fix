# SuperFW fix r84（SuperChis）

基于 [SuperFW 主线 v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)，保留自有 Hack 补丁库及运行这些补丁必需的适配。

**感谢主线作者 David Guillen Fandos（davidgf / davidgfnet）开发并开放 [SuperFW](https://github.com/davidgfnet/superfw)。**感谢 ChisBread 的 [SuperChis](https://github.com/ChisBread/SuperChis) 项目。上游和第三方代码的原署名、许可随源码保留。

- [SuperFW 上游源码](https://github.com/davidgfnet/superfw)
- [SuperFW 文档](https://superfw.davidgf.net/)
- [SuperChis 项目](https://github.com/ChisBread/SuperChis)

基础版本 `0.22`，构建标识 `84000001`，仅适用于 SuperChis（Chis）。内置 Hack 库共 1,964 条，选择“Hack 加强”即可使用，无需另放数据库文件。

## 本版内容

- 通用存档、开机转存、备份轮换、覆盖和设置解析遵循主线 v0.22，不加入额外的转存失败停载或自行改变主线错误处理的流程。
- 保留《晓月圆舞曲[简大字]》专用记录：保留汉化版头部文字跳板，从原生 IRQ 写入处安装游戏内菜单，解决背包项目返回 KONAMI 标志的问题；TF 与 NOR 均可使用菜单。
- 保留 Monomyth v1.0、既有游戏存档与 RTC 专用记录、已定义菜单布局和补丁库加载适配。默认数据库保持原样。
- 游戏仍运行于单一 32 MiB SDRAM 或 NOR，不使用 SD 临时 ROM、整 ROM 备份或 ROM overlay。

目标《晓月》ROM：8 MiB，`A2CJ` v0，SHA256 为 `f0a7e31ff9c728a9a7b313bb9a31eb102d3496783d4e1d335585b47f466e5615`。

## NOR 是否需要重刷

**仅部分游戏需重刷：如果这份《晓月大字版》此前使用会覆盖汉化头部的旧菜单补丁写入 NOR，需要重新写入这一份游戏。已经使用修正版补丁写入的无需再次重刷；其他 NOR 游戏无需因本版恢复主线流程而重刷。**

旧补丁已改写 NOR 游戏的汉化函数指针，更新固件不能自动恢复这些游戏字节。TF 启动该游戏无需改 ROM，重新加载并选择“Hack 加强”即可应用专用记录。

## 机内升级

1. 将当前游戏进度转存到 TF，然后把 `superfw-chis-r84.fw` 复制到卡上。
2. 固件信息页按 `Down+B+Start` 启用升级；返回文件浏览器，打开 `.fw`，按 `L+R+Up` 开始刷写。
3. 等待完成后重启。刷写会覆盖固件，中途不要断电。
4. 使用专用游戏适配时选择“Hack 加强”。升级界面显示基础版本 `0.22`、构建标识 `84000001`。

## 构建

需要 ARM GNU 工具链、GNU Make 4.3 或更新版、Python 3 和主机 C++ 编译器：

```text
make BOARD=chis
```

生成内容相同的 `superfw.gba` 和 `superfw.fw`。源码包包含构建资源、补丁数据库、链接脚本及原有内置模拟器资源；不包含游戏 ROM、BIOS、用户存档、本机测试工具或私人报告。

## 已知反馈

《水银》实卡旧档读取差异、《黄金太阳》显示错误数据但可读的现象仍未定位，本版不声称修复。快速读取开启时部分游戏的偶发卡死反馈也尚未定位。GBC《宝可梦 黄》游戏内保存后，可先按 L+R 打开模拟器菜单提交缓存，再重启转存；本版没有修改模拟器核心。

本地检查使用 mGBA 和固件实际 ARM 代码，验证范围见交付说明；未进行实体卡 TF/NOR 总线时序或完整剧情验收。

## 许可

SuperFW 原有代码遵循其 GPL 许可；各第三方目录中的许可及署名随对应代码保留。
