# superFW fix（chis ver）

这是面向 superchis 硬件的 SuperFW fix r79 源码仓库。项目基于
[superFW](https://github.com/davidgfnet/superfw)，保留上游固件的核心构建方式、
菜单、存档、RTC 和补丁框架，并加入面向中文 hack 游戏及宝可梦改版的兼容性修复、
精简菜单和 ROM 补丁数据库改动。

项目链接：

- [SuperFW 上游源码](https://github.com/davidgfnet/superfw)
- [SuperFW 文档](https://superfw.davidgf.net/)
- [superchis 项目](https://github.com/ChisBread/SuperChis)

感谢 superFW 上游作者 David Guillen Fandos（davidgf），以及 superchis 项目作者
ChisBread 和我。由于整个固件是由 AI 完成的，所以不好意思合并到主线。:)

## 仓库内容

仓库直接包含固件源码、构建脚本、资源、链接脚本和 FatFs。构建必需的资源文件、补丁数据库和菜单资源保留在 `res/` 及源码树中。

本源码仓库不包含 ROM、BIOS、用户存档、mGBA 模拟器二进制、仿真产物、私人验收
报告、发布页 HTML、固件发布包或本机配置。固件发布物另行维护。

当前内置 Hack 数据库包含 1,962 条查表记录；新增记录保留原有记录和默认数据库，固件版本标识为 `79000004`。

## r79 更新

- 新增《口袋妖怪灵魂金独立普通版》专用 Hack 记录。该独立改版虽然使用 `BPEE` 和 `FLASH1M_V103` 标记，但实际 Flash 探测和读取入口不同；新增补丁补上独立 Flash ID 探测、Flash1M SRAM 读取、擦除与写入路径，并保留 RTC 操作。
- 新增 Diablo [Bangoo] v0.9.4 的双 bank SRAM 存档适配和 WAITCNT 处理，RTC 不启用；完整游戏内菜单仍只在有经过验证的 ROM 空间时启用。
- Hack 库未命中时回退到默认内置数据库；两个库都未命中时先提示用户，再以无补丁方式运行，并关闭依赖补丁的菜单、RTC 和 DirectSave。
- 加入网页补丁的流式处理路径，NOR 和 SDRAM 都按单一 32 MiB 游戏映像处理，不使用 ROM overlay 或异构游戏存储。
- 当前本地 mGBA 已验证新增 ROM 的启动、菜单、存档和 RTC 接口；完整实体卡刷写验收仍需单独进行。

## r78 历史更新

- 恢复固件内置 Hack 数据库，选择“Hack 加强”即可使用，无需向 TF 卡复制 `hackpatches.db`。
- 保留 Famicom Mini《超级马里奥》FSMJ v1 的专用修复，以及已有游戏、存档和 RTC 适配。该补丁的汇编源见 `res/rom-patches/famicom-fsmj-v1.S`。
- 使用内置库查找当前 ROM；未收录时回退到普通内置数据库，普通库也未收录时才显示已有失败提示。其他补丁模式保持原有逻辑。
- 构建同时生成 `superfw.gba` 和机内升级用 `superfw.fw`，两个文件内容相同。

## 机内升级

从 [r79 发布页](https://github.com/moribaka/superfw-fix/releases/tag/r79) 下载 `superfw-chis-r79.fw`，只适用于 SuperChis（Chis），不适用于 SuperCard SD/Lite。

1. 将当前游戏存档保存到 TF 卡，再把 `.fw` 文件复制到卡上。刷写会覆盖卡带固件，中途断电可能导致无法启动。
2. 启动现有 SuperFW，用 L/R 切换到“信息”页面，同时按 `Down+B+Start` 启用固件升级。
3. 返回文件浏览器，选中 `.fw`，按 A 打开，再按 `L+R+Up` 开始刷写。
4. 等待固件更新完成后重启。使用专用游戏修复时选择“Hack 加强”。

升级窗口可能显示基础版本 `0.21`；本版为 r79，构建标识 `79000004`。数据库随固件更新，NOR 中已有的游戏需要重新写入才能应用新的 ROM 补丁。`.fw` 保留原项目的型号、文件长度和摘要格式。

本版是测试固件，完整实机刷写及游戏验收尚未完成。

## r73–r75 更新

- r75 修复游戏内菜单进入及返回时显示未完成画面的时序：完整菜单首帧绘制完再显示，恢复游戏调色板与 VRAM 期间保持空白，并在原有 VBlank 同步点恢复游戏显示。切换期间会有短暂空白。
- r73 修正 Craft Sword 3h 的 NOR 图块指针补丁分类，使共享图块指针在 TF 和 NOR 两条路径都生效；保留原生 SRAM、IRQ 和既有菜单布局。
- r74 新增《铸剑物语 3 简单版》的独立记录，适配内置作弊 IRQ 的 VBlank 分发，保留原作弊主体、简单版游戏改动与原生 SRAM。
- 保留 r72 的 RTC 修正和 Apotris 中文版启动映射适配；r75 的 1,742 条 ROM 记录及 622 条 RTC 记录与 r74 相同。

本版是测试固件。Apotris 的 TF／NOR 菜单过渡已逐帧验证，存档导出及冷启动读取通过；六个代表性旧版本的菜单回归及其中四个 RTC 接口回归通过。完整实机验收尚未完成。

## r72 更新

- RTC 数据库修正 30 个版本：补全 16 个未适配版本和 12 个接口遗漏版本，纠正参宿六两个版本的状态及日期读取接口；共 622 个 RTC 版本。
- 支持特殊日期读取函数位于 2 字节对齐的 Thumb 入口，保留游戏原有日期和昼夜后处理。
- 新增 Apotris 中文版精确记录，保留原生 SRAM，适配 IRQ 和游戏内菜单；跳过启动时切换 SDRAM 的检测，保持 NOR／SDRAM 当前映射。NOR 菜单使用布局 712。
- r70 原有非 RTC 操作保留；此前模拟人生 2、菜单容量、资源、存档与 IRQ 适配继续保留。

软件 RTC 仍按游戏读取推进，使用每个游戏的 `.rtc` 保存时间与速度，通过游戏内菜单调整；运行时不持续同步系统时间，无电池 RTC 时离线不走时。

本版是测试固件。RTC 接口和 Apotris 的 SDRAM／NOR 菜单及存档流程已完成本地模型验证，完整实机验收尚未完成。

## 构建

需要 ARM GNU 工具链、GNU Make 4.3 或更新版、Python 3 和主机 C++ 编译器。构建脚本全部位于本仓库的 `tools/`。常用构建命令为：

```text
make BOARD=chis
```

当前 Chis 构建配置引用的内置模拟器二进制需另外提供在 `emu/`；它们不随此源码仓库发布。菜单、金手指和 RTC payload 由源码生成，不上传已编译 payload。

测试脚本保留在本机测试目录，不随源码仓库发布；测试使用的 ROM、BIOS、SAV 和仿真产物也保留在本机。

## 已知原版启动异常

以下两个条目在原版 ROM 中也会启动异常，因此没有把它们标记为本次修复或回归：

- 究极绿宝石 I
- 激进红 v2.1 MOD

## 许可与署名

SuperFW 上游代码按其原有 GPL 许可发布；各第三方源代码目录中的许可文件和署名
随对应代码保留。感谢 superFW 上游作者 David Guillen Fandos（davidgf）以及
superchis 项目作者 ChisBread 和我。

---

# superFW fix (chis ver) — English

This is the r79 source repository for SuperFW fix targeting superchis hardware. It is based on [superFW](https://github.com/davidgfnet/superfw), retaining the upstream firmware's core build system, menu, save, RTC, and patch framework while adding compatibility fixes for Chinese hack games and Pokémon modifications, a compact menu, and ROM patch database changes.

Project links:

- [SuperFW upstream source](https://github.com/davidgfnet/superfw)
- [SuperFW documentation](https://superfw.davidgf.net/)
- [superchis project](https://github.com/ChisBread/SuperChis)

Thanks to SuperFW upstream author David Guillen Fandos (davidgf), and to the superchis project authors ChisBread and me. Since the entire firmware was completed by AI, I feel too embarrassed to merge it upstream. :)

## Repository contents

The repository directly contains the firmware source, build scripts, resources, linker scripts, and FatFs. Build-required resources, patch databases, and menu assets remain under `res/` and in the source tree.

This source repository does not contain ROMs, BIOS files, user saves, mGBA emulator binaries, emulation artifacts, private acceptance reports, release-page HTML, firmware release packages, or machine-specific configuration. Firmware release assets are maintained separately.

The built-in Hack database contains 1,962 lookup records; existing records and the ordinary built-in database are preserved. The firmware version identifier is `79000004`.

## r79 changes

- Add a dedicated Hack record for the standalone Pokémon SoulGold ROM. Although it carries the `BPEE` and `FLASH1M_V103` markers, its Flash detection and read entrypoints differ from the original game; the dedicated patch supplies the Flash ID path, Flash1M-to-SRAM reads, erase/write handlers, and RTC operations.
- Add the Diablo [Bangoo] v0.9.4 dual-bank SRAM save adaptation and WAITCNT handling; RTC remains disabled, and the full in-game menu is only enabled where verified ROM space exists.
- Fall back from a Hack database miss to the ordinary built-in database; when both databases miss, show a prompt before launching the original ROM without patch-dependent menu, RTC or DirectSave features.
- Add streaming web-patch processing while keeping each game in one 32 MiB SDRAM or NOR image without ROM overlay or heterogeneous game storage.
- The new ROM's startup, menu, save and RTC interfaces were verified locally with mGBA; complete physical-cartridge flashing acceptance remains separate.

## r78 historical changes

- Embed the Hack database in the firmware again. Select Hack enhanced; no TF database file is required.
- Retain the Famicom Mini Super Mario FSMJ v1 fix and existing game, save and RTC adaptations. Its assembly source is `res/rom-patches/famicom-fsmj-v1.S`.
- Look up the selected ROM in the built-in Hack database; unlisted ROMs fall back to the ordinary built-in database, and use the existing failure prompt only when neither database has a record. Other patch modes retain their existing behavior.
- The build produces byte-identical `superfw.gba` and `superfw.fw` files.

## Updating on the device

Download `superfw-chis-r79.fw` from the [r79 release](https://github.com/moribaka/superfw-fix/releases/tag/r79). This image targets SuperChis (Chis), not SuperCard SD/Lite.

1. Save the current game to TF and copy the `.fw` file onto the card. Flashing replaces the cartridge firmware; loss of power can prevent booting.
2. Boot the existing SuperFW, open Info with L/R, and press `Down+B+Start` to enable flashing.
3. Open the `.fw` in the file browser and press `L+R+Up` to flash.
4. Restart after completion and select Hack enhanced for dedicated ROM fixes.

The update window may show the base version `0.21`; this release is r79 with identifier `79000004`. The database is updated with the firmware. Rewrite existing NOR games to apply new ROM patches. The `.fw` retains the upstream variant, length and checksum format.

This is test firmware; complete physical flashing and game acceptance remain pending.

## r73–r75 changes

- r75 fixes incomplete frames during in-game menu entry and game restoration. Reveal the full menu after its first frame is drawn, keep the display blank while restoring game palette and VRAM, and restore game display at the existing VBlank synchronization point. A brief blank transition is expected.
- r73 corrects the NOR patch classification for Craft Sword 3h shared tile pointers, applying those pointers on both TF and NOR paths while retaining native SRAM, IRQ integration, and the existing menu layout.
- r74 adds an independent record for the easy version of Summon Night: Swordcraft Story 3, integrating its built-in cheat IRQ through VBlank dispatch while retaining the original cheat body, easy-mode modifications, and native SRAM.
- Retain r72 RTC fixes and Chinese Apotris startup mapping support. All 1,742 ROM records and 622 RTC records are unchanged between r74 and r75.

This is test firmware. Apotris TF/NOR menu transitions have been verified frame by frame, with save export and cold-start loading passing. Menu regressions passed for six representative earlier ROM versions, including four applicable RTC interface regressions. Complete physical-hardware acceptance remains pending.

## r72 changes

- Correct 30 RTC records: 16 missing adaptations, 12 incomplete interface sets, and two Saiph status/date ABI fixes; 622 RTC versions in total.
- Support halfword-aligned Thumb entries for strided date readers while retaining game calendar and day/night processing.
- Add the exact Chinese Apotris version with native SRAM, IRQ integration and in-game menus. Skip the startup SDRAM-switching probe to retain the active cartridge mapping; use layout 712 for the NOR menu.
- Preserve r70 non-RTC operations and preceding menu, resource, save and IRQ adaptations.

The software RTC advances on game reads, persists each game's time and speed in its `.rtc` file, and is adjusted through the in-game menu. It does not continuously synchronize with system time or advance offline without a battery-backed RTC.

This is test firmware. RTC interfaces and Apotris SDRAM/NOR menu and save flows have been verified in the local model; complete physical-hardware acceptance remains pending.

## Build

ARM GNU tools, GNU Make 4.3 or newer, Python 3, and a host C++ compiler are required. Build scripts reside in this repository under `tools/`. A common build command is:

```text
make BOARD=chis
```

Bundled emulator binaries referenced by the current Chis build configuration must be provided separately under `emu/`; they are not published in this source repository. Menu, cheat, and RTC payloads are generated from source rather than checked in as compiled payloads.

Test scripts remain in the local testing directory and are not published in the source repository. The ROMs, BIOS files, SAV files, and emulation artifacts used by those tests also remain local.

## Known startup failures in the original ROMs

The following two entries also fail to start in their original ROMs, so they are not marked as fixes or regressions in this update:

- 究极绿宝石 I
- 激进红 v2.1 MOD

## Licensing and attribution

The SuperFW upstream code is distributed under its original GPL license. License files and attribution notices in third-party source directories remain with their corresponding code. Thanks to SuperFW upstream author David Guillen Fandos (davidgf), and to the superchis project authors ChisBread and me.
