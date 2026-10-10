"""Pack linked menus and derive offsets; retain checked-in r70 layouts."""
import json
import struct
import subprocess
from pathlib import Path
import os

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT
OUT = ROOT / 'build/menu-assets'
COMPACT_RUNTIME_RESERVE = 84708
FULL_RUNTIME_RESERVE = 213052


def generate():
    OUT.mkdir(parents=True, exist_ok=True)
    payload = (SRC/'ingamemenu.payload').read_bytes()
    symbols_text = subprocess.check_output(
        [os.environ.get('CROSS_COMPILE','arm-none-eabi-')+'nm', '-n', str(SRC/'ingamemenu.elf')], text=True)
    symbols = {p[2]: int(p[0], 16) for line in symbols_text.splitlines() if len(p := line.split()) == 3}
    nor_defines = {
        'MENU_NOR_ENTRY': 0x09000000+symbols['scatter_nor_entry']-0x02000000,
        'MENU_NOR_CHEATS': 0x09000000+symbols['scatter_nor_cheats']-0x02000000,
        'MENU_NOR_HOOK_HANDLER': symbols['ingame_menu_entrypoint_nocheats']-0x02000000,
        'MENU_NOR_EXPLICIT_ENTRY': symbols['ingame_menu_entry']-0x02000000,
    }
    (SRC/'src/menu_nor_offsets.h').write_text('\n'.join('#define %s 0x%X'%kv for kv in nor_defines.items())+'\n')
    arm=os.environ.get('CROSS_COMPILE','arm-none-eabi-')
    subprocess.run([
        os.environ.get('CC',arm+'gcc'),'-Isrc','-nostdlib','-Wl,-Ttext=0','src/ingame_nor_scatter.S','-o','ingame_nor_scatter.elf'],cwd=SRC,check=True,capture_output=True)
    subprocess.run([
        arm+'objcopy','-O','binary','ingame_nor_scatter.elf','ingame_nor_scatter.payload'],cwd=SRC,check=True)
    bridge_names=subprocess.check_output([arm+'nm','-n',str(SRC/'ingame_nor_scatter.elf')],text=True)
    bridge={p[2]:int(p[0],16) for line in bridge_names.splitlines() if len(p:=line.split())==3}
    resident = (symbols['scatter_resident_end'] - 0x02000000 + 1023) & ~1023
    payload=bytearray(payload)
    payload.extend(bytes(53308-len(payload)))
    struct.pack_into('<I',payload,64,FULL_RUNTIME_RESERVE)
    for name,value in [('menu_storage_size',53308),('menu_data_offset',symbols['__EWRAM_SIZE__']),('menu_packed_size',0)]:
        struct.pack_into('<I',payload,symbols[name]-0x02000000,value)
    (SRC/'ingamemenu.payload').write_bytes(payload)
    (SRC/'ingamemenu.head').write_bytes(payload[:resident])
    (SRC/'ingamemenu.body').write_bytes(payload[resident:])
    (SRC/'ingamemenu.size.S').write_text('.word %d\n'%len(payload))
    full_payload=payload
    compact_raw=(SRC/'ingamemenu.compact.raw').read_bytes()
    compact_names=subprocess.check_output([os.environ.get('CROSS_COMPILE','arm-none-eabi-')+'nm','-n',str(SRC/'ingamemenu.compact.elf')],text=True)
    compact_symbols={p[2]:int(p[0],16) for line in compact_names.splitlines() if len(p:=line.split())==3}
    assert resident==7168
    for name in ('scatter_layout','scatter_borrowed','spill_header_addr','nor_exit_bridge','capacity_context','ingame_menu_entry','scatter_resident_end'):
        assert symbols[name]==compact_symbols[name],name
    ewram=compact_symbols['__EWRAM_SIZE__'];iwram=compact_symbols['__IWRAM_SIZE__']
    packdir=OUT/'code-pack'
    packdir.mkdir(parents=True,exist_ok=True)
    packer=packdir/('apultra.exe' if os.name=='nt' else 'apultra');source=SRC/'tools/apultra.cc'
    if not packer.exists() or packer.stat().st_mtime_ns<source.stat().st_mtime_ns:
        subprocess.run([os.environ.get('HOST_CXX','g++'),'-std=c++20','-O2','-static',str(source),'-o',str(packer)],check=True)
    sample=packdir/'menu-code.bin';encoded=packdir/'menu-code.apl'
    sample.write_bytes(compact_raw[resident:ewram])
    subprocess.run([str(packer),str(sample),str(encoded)],check=True,capture_output=True)
    packed=encoded.read_bytes();packed+=bytes((-len(packed))&3)
    compact_payload=bytearray(compact_raw[:resident]+packed+compact_raw[ewram:ewram+iwram])
    compact_payload.extend(bytes((-len(compact_payload))&3))
    scratch=struct.unpack_from('<I',compact_raw,64)[0]-len(compact_raw)
    for name,value in [('menu_storage_size',len(compact_payload)),('menu_data_offset',resident+len(packed)),('menu_packed_size',len(packed))]:
        struct.pack_into('<I',compact_payload,compact_symbols[name]-0x02000000,value)
    struct.pack_into('<I',compact_payload,64,len(compact_payload)+scratch)
    compact_payload.extend(bytes(28388-len(compact_payload)))
    struct.pack_into('<I',compact_payload,64,COMPACT_RUNTIME_RESERVE)
    struct.pack_into('<I',compact_payload,compact_symbols['menu_storage_size']-0x02000000,28388)
    (SRC/'ingamemenu.compact.payload').write_bytes(compact_payload)
    image_fields={name:compact_symbols[name]-0x02000000 for name in ('menu_storage_size','menu_data_offset','menu_packed_size','compact_language_block')}
    info=dict(codec='aplib',raw=len(compact_raw),stored=len(compact_payload),resident=resident,ewram=ewram,iwram=iwram,packed=len(packed),scratch=scratch,fields=image_fields)
    (OUT/'packed-menu.json').write_text(json.dumps(info,indent=2))
    print('Compact menu packing',info)
    (SRC/'ingamemenu.compact.head').write_bytes(compact_payload[:resident])
    (SRC/'ingamemenu.compact.body').write_bytes(compact_payload[resident:])
    (SRC/'ingamemenu.compact.size.S').write_text('.word %d\n'%len(compact_payload))
    fields = {n: symbols[n]-0x02000000 for n in ('scatter_layout', 'scatter_borrowed', 'spill_header_addr', 'nor_exit_bridge', 'capacity_context', 'ingame_menu_entry')}
    metadata = ['/* Linked menu offsets, regenerated with every menu build. */', '#define MENU_SCATTER_RESIDENT_SIZE %du' % resident]
    metadata += ['#define MENU_%s_OFFSET %du'%(name.upper(),value) for name,value in image_fields.items()]
    for name,value in fields.items(): metadata.append('#define MENU_%s_OFFSET %du'%(name.upper(),value))
    metadata.append('#define MENU_NOR_EXIT_OFFSET %du'%bridge['mirror_exit'])
    (SRC/'src/menu_layout_offsets.h').write_text('\n'.join(metadata)+'\n')

if __name__ == '__main__': generate()
