# superFW fix（chis ver）

这是面向 superchis 硬件的 SuperFW fix r77 源码仓库。项目基于
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

当前外挂数据库覆盖 1,743 个 ROM 版本，其中 622 个适配 RTC；相同补丁合并为 1,428 条查表记录。固件版本标识为 `77000001`。

## r77 更新

- 将原“Hack 加强”改为“外挂数据库”，只从 TF 卡 `/.superfw/hackpatches.db` 读取记录，固件不再内置 Hack 数据库。
- 选中该模式下的 ROM 时载入数据库；“重新查库”会重新读取文件与当前 ROM 记录。旧 `.hack` 缓存、自动扫描、普通内置数据库和自动模式不再参与该模式的记录选择。
- 文件不可用或 ROM 未收录时使用已有失败提示。普通内置数据库、补丁引擎、无补丁和自动模式保持原有逻辑。
- 沿用 HDB2 ABI revision 75；替换已有操作和已有菜单布局对应的数据库记录，无需重编译固件。数据库本身另行分发。

## r76 更新（历史）

- 新增 Famicom Mini《超级马里奥》FSMJ v1（4 MiB）的专用记录：在启动器解压后替换实际复制到 RAM 的 EEPROM 读写函数，改用 SRAM；修正压缩启动器的 WAITCNT 设置。补丁通过既有数据库操作携带，不增加固件常驻代码。汇编源见 `res/rom-patches/famicom-fsmj-v1.S`。
- 该 ROM 的 TF／NOR 补丁路径已在 mGBA 中完成启动、操作、游戏内最高分保存和冷启动读回；实体卡尚待验证。该记录不启用固件游戏内菜单、RTC 或 DirectSave，其余已有记录保持不变。
- 新增外置 Hack 数据库加载：启动菜单时优先读取 TF 卡 `/.superfw/hackpatches.db`，再使用现有 HDB2 查表流程；没有外置文件时继续使用固件内置数据库。
- 外置数据库使用现有 Hack 数据库 ABI revision 75，不改变已有 ROM 记录格式；可以替换已有补丁操作和已有菜单布局对应的 ROM 记录而不重新编译固件。
- 外置数据库只在菜单启动阶段读入。已经写入 NOR 的游戏仍需重新写入；新的运行时代码、菜单载荷或尚未包含的菜单布局仍需更新固件。

## r73–r75 更新

- r75 修复游戏内菜单进入及返回时显示未完成画面的时序：完整菜单首帧绘制完再显示，恢复游戏调色板与 VRAM 期间保持空白，并在原有 VBlank 同步点恢复游戏显示。切换期间会有短暂空白。
- r73 修正 Craft Sword 3h 的 NOR 图块指针补丁分类，使共享图块指针在 TF 和 NOR 两条路径都生效；保留原生 SRAM、IRQ 和既有菜单布局。
- r74 新增《铸剑物语 3 简单版》的独立记录，适配内置作弊 IRQ 的 VBlank 分发，保留原作弊主体、简单版游戏改动与原生 SRAM。
- 保留 r72 的 RTC 修正和 Apotris 中文版启动映射适配；r75 的 1,742 条 ROM 记录及 622 条 RTC 记录与 r74 相同。

本版是测试固件。Apotris 的 TF／NOR 菜单过渡已逐帧验证，存档导出及冷启动读取通过；六个代表性旧版本的菜单回归及其中四个 RTC 接口回归通过。外挂数据库已通过文件载入、记录更新、全库查表和其它模式选择测试；完整实机验收尚未完成。

使用专用 ROM 适配时，请选择“外挂数据库”模式。NOR 中已经烧入的游戏需要重新烧入，才能应用本次 ROM 补丁变化。

### 从 TF 卡加载 Hack 数据库

选择“外挂数据库”模式下的 ROM 时，固件从 TF 卡 `/.superfw/hackpatches.db` 读取记录。文件使用仓库中的 `HDB2` 格式，ABI revision 75；该模式只使用外挂记录，不回退到旧缓存、扫描或内置数据库。

使用时把对应版本源码中的 `res/hackpatches.db` 复制到 TF 卡的 `/.superfw/hackpatches.db`，保持文件名和目录名不变。

数据库在选中 ROM 或按“重新查库”时读入；替换文件后可重新打开该 ROM 或重新查库。进入游戏后不会从 TF 卡实时读取。已经写入 NOR 的游戏仍需重新写入，才能应用外置数据库中的 ROM 补丁。新的运行时代码、菜单载荷或固件尚未包含的菜单布局仍需更新固件。

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

This is the r77 source repository for SuperFW fix targeting superchis hardware. It is based on [superFW](https://github.com/davidgfnet/superfw), retaining the upstream firmware's core build system, menu, save, RTC, and patch framework while adding compatibility fixes for Chinese hack games and Pokémon modifications, a compact menu, and ROM patch database changes.

Project links:

- [SuperFW upstream source](https://github.com/davidgfnet/superfw)
- [SuperFW documentation](https://superfw.davidgf.net/)
- [superchis project](https://github.com/ChisBread/SuperChis)

Thanks to SuperFW upstream author David Guillen Fandos (davidgf), and to the superchis project authors ChisBread and me. Since the entire firmware was completed by AI, I feel too embarrassed to merge it upstream. :)

## Repository contents

The repository directly contains the firmware source, build scripts, resources, linker scripts, and FatFs. Build-required resources, patch databases, and menu assets remain under `res/` and in the source tree.

This source repository does not contain ROMs, BIOS files, user saves, mGBA emulator binaries, emulation artifacts, private acceptance reports, release-page HTML, firmware release packages, or machine-specific configuration. Firmware release assets are maintained separately.

The external database covers 1,743 ROM versions, including 622 RTC adaptations, deduplicated into 1,428 lookup records; the firmware version identifier is `77000001`.

## r77 changes

- Rename Hack enhanced to External DB and read ROM records only from `/.superfw/hackpatches.db` on TF. The firmware no longer embeds the Hack database.
- Load the database when selecting a ROM in this mode; Reload record reads the file again. This mode does not use old `.hack` caches, scanning, the normal built-in database or Auto fallback.
- The existing failure prompt handles an unavailable file or an unlisted ROM. Built-in database, Patch engine, No patching and Auto retain their existing behavior.
- Keep HDB2 ABI revision 75. Distribute the database separately; record updates using existing operations and layouts do not require rebuilding firmware.

## r76 changes (historical)

- Add external Hack database loading: when the menu starts, the firmware first reads `/.superfw/hackpatches.db` from the TF card and uses the existing HDB2 lookup path. Without the external file, the built-in database remains active.
- The external database uses the existing Hack database ABI revision 75, so existing ROM record formats are unchanged. Existing patch operations and existing menu-layout records can be replaced without rebuilding the firmware.
- The external database is loaded during menu startup only. Games already written to NOR must be rewritten; new runtime code, menu payloads, or menu layouts not already present in the firmware still require a firmware update.

## r73–r75 changes

- r75 fixes incomplete frames during in-game menu entry and game restoration. Reveal the full menu after its first frame is drawn, keep the display blank while restoring game palette and VRAM, and restore game display at the existing VBlank synchronization point. A brief blank transition is expected.
- r73 corrects the NOR patch classification for Craft Sword 3h shared tile pointers, applying those pointers on both TF and NOR paths while retaining native SRAM, IRQ integration, and the existing menu layout.
- r74 adds an independent record for the easy version of Summon Night: Swordcraft Story 3, integrating its built-in cheat IRQ through VBlank dispatch while retaining the original cheat body, easy-mode modifications, and native SRAM.
- Retain r72 RTC fixes and Chinese Apotris startup mapping support. All 1,742 ROM records and 622 RTC records are unchanged between r74 and r75.

This is test firmware. Apotris TF/NOR menu transitions have been verified frame by frame, with save export and cold-start loading passing. Menu regressions passed for six representative earlier ROM versions, including four applicable RTC interface regressions. The external database passed file loading, record replacement, full lookup and other-mode selection tests; complete physical-hardware acceptance remains pending.

Select External DB mode to use the dedicated ROM adaptations. Rewrite existing NOR game copies to apply the ROM patch changes in this release.

### Loading the Hack database from the TF card

When a ROM is selected in External DB mode, the firmware reads `/.superfw/hackpatches.db` from TF. The file uses HDB2 ABI revision 75. This mode uses only the external record, without a cache, scanner or built-in database fallback.

To use it, copy `res/hackpatches.db` from the matching source revision to `/.superfw/hackpatches.db` on the TF card, keeping the exact file name and directory.

The database is loaded when selecting a ROM or pressing Reload record. After replacing the file, reopen the ROM or reload its record. It is not read live after a game starts. Games already written to NOR must be rewritten to apply ROM patches from the external database. New runtime code, menu payloads, or menu layouts not already present in the firmware still require a firmware update.

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
