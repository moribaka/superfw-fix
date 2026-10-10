"""Build a self-contained SDRAM cheat importer and position-independent VM."""
import os
import hashlib
import json
import subprocess
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT
OUT=ROOT/'build/cheat-engine'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    mapping=[]
    for lead in range(0x81,0xFF):
        for trail in (*range(0x40,0x7F),*range(0x80,0xFF)):
            try:cp=ord(bytes((lead,trail)).decode('gbk'))
            except UnicodeDecodeError:cp=0
            mapping.append(cp)
    (SOURCE/'res/cheat_gbk.bin').write_bytes(struct.pack('<%dH'%len(mapping),*mapping))
    arm=os.environ.get('CROSS_COMPILE','arm-none-eabi-')
    linker=OUT/'import.ld'
    linker.write_text('''ENTRY(cheat_compile)
SECTIONS {
 . = 0x09000000;
 .entry : { KEEP(*(.entry)) }
 .text : { *(.text*) *(.glue*) }
 .rodata : { *(.rodata*) }
 .vm : ALIGN(4) { __VM_START__ = .; KEEP(*(.vm)); . = ALIGN(4); __VM_END__ = .; }
 .data : { *(.data*) }
 .bss : { *(.bss*) *(COMMON) }
 /DISCARD/ : { *(.comment) *(.ARM.exidx*) *(.ARM.extab*) }
}
ASSERT(SIZEOF(.bss)==0,"Importer must not depend on SDRAM byte-writable globals")
ASSERT(. < 0x09010000,"Importer exceeds reserved 64 KiB")
''')
    files=[SOURCE/'src/cheat_formats'/n for n in ('cheat_import.c','cheat_vm.c','cheat_libc.c','codebreaker.c','gameshark.c','parv3.c')]
    elf=OUT/'cheat-import.elf'
    command=[os.environ.get('CC',arm+'gcc'),'-Os','-mcpu=arm7tdmi','-mthumb','-nostdlib',
        '-fno-builtin','-fno-jump-tables','-ffunction-sections','-fdata-sections','-Wall',
        '-Wl,--gc-sections','-Wl,-Map='+str(OUT/'cheat-import.map'),'-T',str(linker),
        *map(str,files),'-lgcc','-o',str(elf)]
    process=subprocess.run(command,capture_output=True)
    (OUT/'compile.log').write_bytes(process.stdout+process.stderr)
    if process.returncode:raise RuntimeError((process.stdout+process.stderr).decode(errors='replace'))
    for args in (['-O','binary',str(elf),str(SOURCE/'cheat_import.payload')],
                 ['-O','binary','-j','.vm',str(elf),str(OUT/'cheat-vm.bin')]):
        subprocess.run([arm+'objcopy',*args],check=True)
    symbols=subprocess.check_output([arm+'nm','-n',str(elf)],text=True)
    (SOURCE/'cheat_runtime.payload').write_bytes((OUT/'cheat-vm.bin').read_bytes())
    (OUT/'symbols.txt').write_text(symbols)
    rows={p[2]:int(p[0],16) for line in symbols.splitlines() if len(p:=line.split())==3}
    assert rows['cheat_compile']&~1==0x09000000
    bindings={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    report=dict(status='built',overlay_bytes=(SOURCE/'cheat_import.payload').stat().st_size,
        vm_bytes=(OUT/'cheat-vm.bin').stat().st_size,inputs=bindings,
        overlay_sha256=hashlib.sha256((SOURCE/'cheat_import.payload').read_bytes()).hexdigest())
    (OUT/'build.json').write_text(json.dumps(report,indent=2));print(report)

if __name__=='__main__':main()
