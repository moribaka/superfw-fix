# superFW fix（chis ver）

这是面向 superchis 硬件的 SuperFW fix r70 源码仓库。项目基于
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

当前 r70 数据库包含 1,740 条 ROM 记录，固件版本标识为 `70000001`。

## 构建

需要 ARM GNU 工具链、GNU Make 4.3 或更新版、Python 3 和主机 C++ 编译器。构建脚本全部位于本仓库的 `tools/`。常用构建命令为：

```text
make BOARD=chis
```

当前 Chis 构建配置引用的内置模拟器二进制需另外提供在 `emu/`；它们不随此源码仓库发布。菜单、金手指和 RTC payload 由源码生成，不上传已编译 payload。

测试脚本保留在本机测试目录，不随源码仓库发布；测试使用的 ROM、BIOS、SAV 和仿真产物也保留在本机。

## 已知原版启动异常

以下两个条目在原版 ROM 中也会启动异常，因此没有把它们标记为 r70 修复回归：

- 究极绿宝石 I
- 激进红 v2.1 MOD

## 许可与署名

SuperFW 上游代码按其原有 GPL 许可发布；各第三方源代码目录中的许可文件和署名
随对应代码保留。感谢 superFW 上游作者 David Guillen Fandos（davidgf）以及
superchis 项目作者 ChisBread 和我。

---

# superFW fix (chis ver) — English

This is the r70 source repository for SuperFW fix targeting superchis hardware. It is based on [superFW](https://github.com/davidgfnet/superfw), retaining the upstream firmware's core build system, menu, save, RTC, and patch framework while adding compatibility fixes for Chinese hack games and Pokémon modifications, a compact menu, and ROM patch database changes.

Project links:

- [SuperFW upstream source](https://github.com/davidgfnet/superfw)
- [SuperFW documentation](https://superfw.davidgf.net/)
- [superchis project](https://github.com/ChisBread/SuperChis)

Thanks to SuperFW upstream author David Guillen Fandos (davidgf), and to the superchis project authors ChisBread and me. Since the entire firmware was completed by AI, I feel too embarrassed to merge it upstream. :)

## Repository contents

The repository directly contains the firmware source, build scripts, resources, linker scripts, and FatFs. Build-required resources, patch databases, and menu assets remain under `res/` and in the source tree.

This source repository does not contain ROMs, BIOS files, user saves, mGBA emulator binaries, emulation artifacts, private acceptance reports, release-page HTML, firmware release packages, or machine-specific configuration. Firmware release assets are maintained separately.

The r70 database contains 1,740 ROM records and the firmware version identifier is `70000001`.

## Build

ARM GNU tools, GNU Make 4.3 or newer, Python 3, and a host C++ compiler are required. Build scripts reside in this repository under `tools/`. A common build command is:

```text
make BOARD=chis
```

Bundled emulator binaries referenced by the current Chis build configuration must be provided separately under `emu/`; they are not published in this source repository. Menu, cheat, and RTC payloads are generated from source rather than checked in as compiled payloads.

Test scripts remain in the local testing directory and are not published in the source repository. The ROMs, BIOS files, SAV files, and emulation artifacts used by those tests also remain local.

## Known startup failures in the original ROMs

The following two entries also fail to start in their original ROMs, so they are not marked as r70 regressions or r70 fixes:

- 究极绿宝石 I
- 激进红 v2.1 MOD

## Licensing and attribution

The SuperFW upstream code is distributed under its original GPL license. License files and attribution notices in third-party source directories remain with their corresponding code. Thanks to SuperFW upstream author David Guillen Fandos (davidgf), and to the superchis project authors ChisBread and me.
