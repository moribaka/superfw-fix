"""根据 ARM 汇编生成 TF 与 NOR 的原生 VBlank 回调跳板。"""
from pathlib import Path
import os
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
prefix=os.environ.get('CROSS_COMPILE','arm-none-eabi-')
lines=['// 由 tools/build_vblank_bridge.py 根据 vblank_bridge.S 生成。']
with tempfile.TemporaryDirectory(prefix='superfw-vblank-') as temporary:
    folder=Path(temporary)
    for mode in ('sdram','nor'):
        obj=folder/(mode+'.o');binary=folder/(mode+'.bin')
        subprocess.run([prefix+'gcc','-c','-mcpu=arm7tdmi','-DNOR' if mode=='nor' else '-DSDRAM',
                        str(ROOT/'src/vblank_bridge.S'),'-o',str(obj)],check=True)
        subprocess.run([prefix+'objcopy','-O','binary','-j','.text',str(obj),str(binary)],check=True)
        data=binary.read_bytes()
        assert len(data)<=128 and len(data)%4==0
        lines.append('static const uint32_t vblank_bridge_'+mode+'[] = {')
        lines.extend('  0x%08X,'%word for word in struct.unpack('<%dI'%(len(data)//4),data))
        lines.append('};')
(ROOT/'src/vblank_bridge.h').write_text('\n'.join(lines)+'\n',encoding='utf8')
