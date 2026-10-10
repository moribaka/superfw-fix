/*
 * Copyright (C) 2026 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */


// RTC patches
extern uint16_t patch_rtc_probe[];
extern uint16_t patch_rtc_getstatus[];
extern uint16_t patch_rtc_gettimedate[];
extern uint16_t patch_rtc_settimedate[];
extern uint16_t patch_rtc_reset[];
extern const uint32_t patch_rtc_probe_size;
extern const uint32_t patch_rtc_getstatus_size;
extern const uint32_t patch_rtc_gettimedate_size;
extern const uint32_t patch_rtc_settimedate_size;
extern const uint32_t patch_rtc_reset_size;

// EEPROM patches
extern uint16_t patch_eeprom_read_sram64k[];
extern uint16_t patch_eeprom_write_sram64k[];
extern const uint32_t patch_eeprom_read_sram64k_size;
extern const uint32_t patch_eeprom_write_sram64k_size;

extern uint16_t patch_eeprom_read_directsave[];
extern uint16_t patch_eeprom_write_directsave[];
extern const uint32_t patch_eeprom_read_directsave_size;
extern const uint32_t patch_eeprom_write_directsave_size;

// FLASH patches
extern uint16_t patch_flash_bank_sram[];
extern const uint32_t patch_flash_bank_sram_size;
extern uint16_t patch_flash_read_sram64k[];
extern uint16_t patch_flash_write_sector_sram64k[];
extern uint16_t patch_flash_write_byte_sram64k[];
extern uint16_t patch_flash_erase_sector_sram64k[];
extern uint16_t patch_flash_erase_device_sram64k[];
extern const uint32_t patch_flash_read_sram64k_size;
extern const uint32_t patch_flash_write_byte_sram64k_size;
extern const uint32_t patch_flash_erase_sector_sram64k_size;
extern const uint32_t patch_flash_erase_device_sram64k_size;
extern const uint32_t patch_flash_write_sector_sram64k_size;

extern uint16_t patch_flash_read_sram128k[];
extern uint16_t patch_flash_write_sector_sram128k[];
extern uint16_t patch_flash_write_byte_sram128k[];
extern uint16_t patch_flash_erase_sector_sram128k[];
extern uint16_t patch_flash_erase_device_sram128k[];
extern const uint32_t patch_flash_read_sram128k_size;
extern const uint32_t patch_flash_write_byte_sram128k_size;
extern const uint32_t patch_flash_erase_sector_sram128k_size;
extern const uint32_t patch_flash_erase_device_sram128k_size;
extern const uint32_t patch_flash_write_sector_sram128k_size;

extern uint16_t patch_flash_read_directsave[];
extern uint16_t patch_flash_write_sector_directsave[];
extern uint16_t patch_flash_write_byte_directsave[];
extern uint16_t patch_flash_erase_sector_directsave[];
extern uint16_t patch_flash_erase_device_directsave[];
extern const uint32_t patch_flash_read_directsave_size;
extern const uint32_t patch_flash_write_byte_directsave_size;
extern const uint32_t patch_flash_erase_sector_directsave_size;
extern const uint32_t patch_flash_erase_device_directsave_size;
extern const uint32_t patch_flash_write_sector_directsave_size;

