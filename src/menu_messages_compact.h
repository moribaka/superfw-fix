enum TranslationID {
  IMENU_CANCEL,
  IMENU_FRZRTC,
  IMENU_GOBACK,
  IMENU_MAIN0_BACK_GAME,
  IMENU_MAIN1_RESET,
  IMENU_MAIN2_FLUSH_SAVE,
  IMENU_MAIN3_SSTATE,
  IMENU_MAIN4_RTC,
  IMENU_MAIN5_CHEATS,
  IMENU_MAKEPER,
  IMENU_MSG_RTCWR,
  IMENU_MSG_SAVEC,
  IMENU_MSG_SAVEERR,
  IMENU_PLD_ERR,
  IMENU_QC0_NO,
  IMENU_QC1_YES,
  IMENU_QLD_ERR,
  IMENU_QLD_OK,
  IMENU_RST0_GAME,
  IMENU_RST1_DEVICE,
  IMENU_RST2_DEVSKIP,
  IMENU_RTCSPD,
  IMENU_SAVE0_OW,
  IMENU_SAVE1_BKP,
  IMENU_SAVE2_RST,
  IMENU_SAVING_BLOCKED,
  IMENU_SPD0,
  IMENU_SPD1,
  IMENU_SPD2,
  IMENU_SPD3,
  IMENU_SPD4,
  IMENU_SSTATEP0_SAVE,
  IMENU_SSTATEP1_LOAD,
  IMENU_SSTATEP2_DEL,
  IMENU_SSTATEQ0_SAVE,
  IMENU_SSTATEQ1_LOAD,
  IMENU_SSTATEQ2_WRITE,
  IMENU_SSTATE_PN,
  IMENU_SSTATE_QN,
  IMENU_ST_DEL,
  IMENU_ST_OVER,
  IMENU_UPDAT_RTC,
  IMENU_WSAV_OK,
  IMENU_WSTAF_ERR,
  IMENU_WSTAF_OK,
  IMENU_WSTAR_ERR,
};

#include <stdint.h>
#define COMPACT_MESSAGE_BYTES 1412u
static const uint8_t *compact_message_data;
static inline const char *menu_message(unsigned id) {
 const char *p=(const char*)compact_message_data; while(id--){while(*p++); } return p;
}
extern const uint8_t compact_language_block[];
static void compact_messages_load(void *buffer,unsigned language) {
 const uint8_t *source=compact_language_block;
 uint8_t *out=buffer; unsigned size=(*(const uint32_t*)source)>>8; source+=4;
 while(size){unsigned flags=*source++; for(unsigned bit=0;bit<8 && size;++bit,flags<<=1){
  if(flags&128){unsigned code=(source[0]<<8)|source[1];source+=2;
   unsigned distance=(code&4095)+1,count=(code>>12)+3;
   while(count-- && size){*out=out[-(int)distance];++out;--size;}
  }else{*out++=*source++;--size;}
 }}
 compact_message_data=buffer;
}
