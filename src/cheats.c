/*
 * Copyright (C) 2025 David Guillen Fandos <david@davidgf.net>
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

#include <stdint.h>
#include <stdbool.h>
#include "cheats.h"
#include "util.h"
#include "fatfs/ff.h"
#ifdef __GBA__
#include "common.h"
#include "supercard_driver.h"
#else
static uint32_t host_input[CHEAT_INPUT_MAX/4];
static uint32_t host_work[CHEAT_WORK_SIZE/4];
#endif
#pragma GCC optimize ("Os")
unsigned cheat_error_line,cheat_error_code;

int open_read_cheats(uint8_t *buffer,unsigned capacity,const char *filename) {
  cheat_error_line=0;cheat_error_code=CheatFile;
  FIL file;
  if(f_open(&file,filename,FA_READ)!=FR_OK)return -1;
#ifdef __GBA__
  uint8_t *input=(uint8_t*)0x09100000;
  void *work=(void*)0x09200000;
#else
  uint8_t *input=(uint8_t*)host_input;
  void *work=host_work;
#endif
  uint32_t chunk[128];unsigned length=0;int result=-1;
  while(1) {
    UINT count;
    if(f_read(&file,chunk,sizeof(chunk),&count)!=FR_OK)break;
    if(count>CHEAT_INPUT_MAX-length){cheat_error_code=CheatCapacity;break;}
#ifdef __GBA__
    set_supercard_mode(MAPPED_SDRAM,true,false);
#endif
    memcpy32(input+length,chunk,(count+3)&~3u);
#ifdef __GBA__
    set_supercard_mode(MAPPED_SDRAM,true,true);
#endif
    length+=count;
    if(count==sizeof(chunk))continue;
    result=0;break;
  }
  if(f_close(&file)!=FR_OK)result=-1;
  if(result<0)return -1;
  t_cheat_import request={ (const char*)input,length,buffer,capacity,work,CHEAT_WORK_SIZE,0,0 };
#ifdef __GBA__
  set_supercard_mode(MAPPED_SDRAM,true,false);
  const void *asset=get_vfile_ptr("CHTC");int size=get_vfile_size("CHTC");
  if(!asset||size<=0||size>65536)result=-1;
  else {
    memcpy32((void*)0x09000000,asset,(size+3)&~3u);
    result=((int(*)(t_cheat_import*))0x09000001)(&request);
  }
  set_supercard_mode(MAPPED_SDRAM,true,true);
#else
  result=cheat_compile(&request);
#endif
  cheat_error_line=request.error_line;
  cheat_error_code=result<0?(request.error?request.error:CheatFile):CheatOK;
  return result;
}
