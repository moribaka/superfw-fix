# SuperFW fix r88（SuperChis）

基于 [SuperFW 主线 v0.22](https://github.com/davidgfnet/superfw/releases/tag/v0.22)，构建标识 `88000001`，仅适用于 SuperChis。内置 1,965 条 Hack 补丁库，保留既有游戏与完整/精简菜单适配。

感谢 David Guillen Fandos（davidgf / davidgfnet）开发并开放 [SuperFW](https://github.com/davidgfnet/superfw)，感谢 ChisBread 的 [SuperChis](https://github.com/ChisBread/SuperChis) 项目。原有署名、GPL 和第三方许可随源码保留。[主线文档](https://superfw.davidgf.net/)。

## 兼容性更新

- 《欢乐足球 球队球员名全汉化版 v1.3》（BIXJ v0）：调整游戏内存档兼容，解决提示保存成功但重新启动没有进度的问题。请使用“Hack 加强”补丁模式启动。

## NOR 是否需要重刷

**仅已写入 NOR 的《欢乐足球 球队球员名全汉化版 v1.3》需重新写入一次**，以替换游戏 ROM 中旧的存档适配。其他 NOR 游戏无需因本次更新重刷；从 TF 直接启动该游戏时，更新固件并选择“Hack 加强”即可应用。

历史例外：此前用旧版菜单适配写入 NOR 的《晓月圆舞曲[简大字]》仍需重新写入，以应用该游戏的汉化兼容修正。已使用修正版写入的无需再次重刷。

## 欢乐足球验证范围

上述版本已通过本机 SRAM、DirectSave 的新建保存、覆盖保存及独立冷启动读档测试，实卡仍待确认。

## 升级

1. 将当前游戏进度转存到 TF，将 `superfw-chis-r88.fw` 复制到卡上。
2. 固件信息页按 `Down+B+Start` 启用升级，返回文件浏览器打开 `.fw`，按 `L+R+Up` 开始刷写。
3. 刷写会覆盖固件，过程中不要断电。完成后重启，确认构建标识 `88000001`。

源码构建：`make BOARD=chis`。源码包不含用户 ROM、BIOS、存档或本地排查工具。[下载 r88 固件和完整源码](https://github.com/moribaka/superfw-fix/releases/tag/r88)。
