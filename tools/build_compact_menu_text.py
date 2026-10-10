"""Short compact-menu labels; retain every message, language and format argument."""
import json
import re
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'src'

SHORT_ZH={
 'IMENU_CANCEL':'取消','IMENU_FRZRTC':'暂停','IMENU_GOBACK':'返回',
 'IMENU_MAIN0_BACK_GAME':'继续','IMENU_MAIN1_RESET':'复位','IMENU_MAIN2_FLUSH_SAVE':'保存',
 'IMENU_MAIN3_SSTATE':'即时存档','IMENU_MAIN4_RTC':'时钟','IMENU_MAIN5_CHEATS':'金手指',
 'IMENU_MAKEPER':'写入SD','IMENU_MSG_RTCWR':'时钟已更新','IMENU_MSG_SAVEC':'保存成功',
 'IMENU_MSG_SAVEERR':'保存失败','IMENU_PLD_ERR':'存档损坏','IMENU_QC0_NO':'否','IMENU_QC1_YES':'是',
 'IMENU_QLD_ERR':'存档无效','IMENU_QLD_OK':'读取成功','IMENU_RST0_GAME':'重启游戏',
 'IMENU_RST1_DEVICE':'返回固件','IMENU_RST2_DEVSKIP':'返回固件(不保存)',
 'IMENU_RTCSPD':'速度','IMENU_SAVE0_OW':'覆盖保存','IMENU_SAVE1_BKP':'备份保存',
 'IMENU_SAVE2_RST':'保存并退出','IMENU_SAVING_BLOCKED':'正在保存',
 'IMENU_SSTATEP0_SAVE':'保存','IMENU_SSTATEP1_LOAD':'读取','IMENU_SSTATEP2_DEL':'删除',
 'IMENU_SSTATEQ0_SAVE':'保存','IMENU_SSTATEQ1_LOAD':'读取','IMENU_SSTATEQ2_WRITE':'写入SD',
 'IMENU_SSTATE_PN':'SD档 %d','IMENU_SSTATE_QN':'内存档 %d','IMENU_ST_DEL':'删除存档?',
 'IMENU_ST_OVER':'覆盖存档?','IMENU_UPDAT_RTC':'应用','IMENU_WSAV_OK':'存档已创建',
 'IMENU_WSTAF_ERR':'写入失败','IMENU_WSTAF_OK':'写入成功','IMENU_WSTAR_ERR':'读取失败',
}
SHORT_EN={
 'IMENU_MAIN0_BACK_GAME':'Resume','IMENU_MAIN2_FLUSH_SAVE':'Save','IMENU_MAIN4_RTC':'Clock',
 'IMENU_MAKEPER':'Write to SD','IMENU_MSG_RTCWR':'Clock updated','IMENU_MSG_SAVEC':'Saved',
 'IMENU_MSG_SAVEERR':'Save failed','IMENU_PLD_ERR':'State corrupt','IMENU_QLD_ERR':'State invalid',
 'IMENU_QLD_OK':'State loaded','IMENU_RST1_DEVICE':'Firmware menu','IMENU_RST2_DEVSKIP':'Exit without saving',
 'IMENU_SAVE0_OW':'Overwrite save','IMENU_SAVE1_BKP':'Save with backup','IMENU_SAVE2_RST':'Save and exit',
 'IMENU_SAVING_BLOCKED':'Saving...','IMENU_SSTATEQ0_SAVE':'Save state','IMENU_SSTATEQ1_LOAD':'Load state',
 'IMENU_SSTATEQ2_WRITE':'Write to SD','IMENU_SSTATE_PN':'SD slot %d','IMENU_SSTATE_QN':'RAM slot %d',
 'IMENU_UPDAT_RTC':'Apply','IMENU_WSAV_OK':'State saved','IMENU_WSTAF_ERR':'Write failed',
 'IMENU_WSTAF_OK':'State written','IMENU_WSTAR_ERR':'Read failed',
}


def compress(data):
    out=bytearray(struct.pack('<I',len(data)<<8|0x10));pos=0
    while pos<len(data):
        flag_at=len(out);out.append(0);flags=0
        for bit in range(7,-1,-1):
            if pos==len(data):break
            best=0;distance=0;end=pos
            while end>max(0,pos-4096) and pos+3<=len(data):
                found=data.rfind(data[pos:pos+3],max(0,pos-4096),end)
                if found<0:break
                n=3
                while n<18 and pos+n<len(data) and data[found+n]==data[pos+n]:n+=1
                if n>best:best=n;distance=pos-found
                if n==18:break
                end=found
            if best>=3:
                flags|=1<<bit;word=(best-3)<<12|(distance-1)
                out.extend((word>>8,word&255));pos+=best
            else:out.append(data[pos]);pos+=1
        out[flag_at]=flags
    out.extend(bytes((-len(out))&3));return bytes(out)


def decompress(data):
    size=int.from_bytes(data[:4],'little')>>8;out=bytearray();p=4
    while len(out)<size:
        flags=data[p];p+=1
        for bit in range(7,-1,-1):
            if len(out)==size:break
            if flags&(1<<bit):
                word=data[p]<<8|data[p+1];p+=2;count=(word>>12)+3;distance=(word&4095)+1
                for _ in range(count):out.append(out[-distance])
            else:out.append(data[p]);p+=1
    return bytes(out)


def main():
    source=(SRC/'menu_messages.h').read_text(encoding='utf8')
    keys=re.findall(r'^  (IMENU_\w+),',source,re.M);rows=[];packed=[];maximum=0
    for lang,body in re.findall(r'msg_(\w+)\[\] = \{(.*?)\};',source,re.S):
        values=[json.loads(v) for v in re.findall(r'"(?:[^"\\]|\\.)*"',body)]
        assert len(values)==len(keys)
        overrides=SHORT_ZH if lang=='zh' else SHORT_EN if lang=='en' else {}
        entries=[]
        for key,old in zip(keys,values):
            value=overrides.get(key,old)
            if key.startswith('IMENU_SPD'):value=('1/64x','1/8x','1x','12x','96x')[int(key[-1])]
            assert re.findall(r'%(?:\d+\$)?[a-zA-Z]',old)==re.findall(r'%(?:\d+\$)?[a-zA-Z]',value),key
            # The compact formatter preserves exactly these currently used
            # conversions. A future translation must not add an unsupported
            # format silently (including field widths/positional arguments).
            assert not re.sub(r'%(?:[sdu%])','',value).count('%'),(lang,key,value)
            entries.append(value)
        raw=b''.join(value.encode('utf8')+b'\0' for value in entries)
        block=compress(raw)
        assert decompress(block)==raw
        maximum=max(maximum,len(raw));packed.append((lang,block));rows.append((lang,dict(zip(keys,entries))))
    header=[source[:source.index('const char * const msg_en')], '#include <stdint.h>',
            '#define COMPACT_MESSAGE_BYTES %du'%((maximum+3)&~3),
            'static const uint8_t *compact_message_data;',
            'static inline const char *menu_message(unsigned id) {',
            ' const char *p=(const char*)compact_message_data; while(id--){while(*p++); } return p;','}']
    reserve=max(len(block) for _,block in packed)
    config=['/* Generated selected-language window and resident menu size. */',
            '#define MENU_RESIDENT_BYTES 7168',
            '#define MENU_LANGUAGE_RESERVE %d'%reserve]
    (SRC/'menu_compact_config.h').write_text('\n'.join(config)+'\n')
    pool=bytearray(8*len(packed))
    for index,(lang,block) in enumerate(packed):
        struct.pack_into('<II',pool,index*8,len(pool),len(block));pool+=block
    (SRC.parent/'res/menu-languages.bin').write_bytes(pool)
    header.append('extern const uint8_t compact_language_block[];')
    header += ['static void compact_messages_load(void *buffer,unsigned language) {',
        ' const uint8_t *source=compact_language_block;',
        ' uint8_t *out=buffer; unsigned size=(*(const uint32_t*)source)>>8; source+=4;',
        ' while(size){unsigned flags=*source++; for(unsigned bit=0;bit<8 && size;++bit,flags<<=1){',
        '  if(flags&128){unsigned code=(source[0]<<8)|source[1];source+=2;',
        '   unsigned distance=(code&4095)+1,count=(code>>12)+3;',
        '   while(count-- && size){*out=out[-(int)distance];++out;--size;}',
        '  }else{*out++=*source++;--size;}',
        ' }}',
        ' compact_message_data=buffer;','}']
    (SRC/'menu_messages_compact.h').write_text('\n'.join(header)+'\n',encoding='utf8')
    (SRC/'menu_messages_compact.json').write_text(json.dumps(dict(rows),ensure_ascii=False,indent=2),encoding='utf8')
    print('Compact messages:',len(rows),'languages,',len(keys),'actions/statuses,',sum(len(b) for _,b in packed),'packed bytes; decode buffer',maximum)


if __name__=='__main__':main()
