import os
import hashlib
import json
import struct
import subprocess
from pathlib import Path

def main():
    root = Path(__file__).resolve().parents[1]
    folder = root / 'build/rtc-custom-payload'
    folder.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('CC',os.environ.get('CROSS_COMPILE','arm-none-eabi-')+'gcc')
    copy = os.environ.get('CROSS_COMPILE','arm-none-eabi-')+'objcopy'
    assembly = folder / 'patches.o'
    subprocess.run([compiler, '-mcpu=arm7tdmi', '-DSUPERCHIS_IO', '-c',
                    str(root / 'src/patches.S'), '-o', str(assembly)], check=True, capture_output=True)
    raw=folder/'patches.bin'
    subprocess.run([copy,'-O','binary','-j','.text',str(assembly),str(raw)],check=True,capture_output=True)
    text=subprocess.check_output([os.environ.get('CROSS_COMPILE','arm-none-eabi-')+'nm','-n',str(assembly)],text=True)
    symbols={p[2]:int(p[0],16) for line in text.splitlines() if len(p:=line.split())==3}
    code=raw.read_bytes()
    sizes = {}
    for name, routine in (('reader', 'patch_rtc_gettimedate'), ('setter', 'patch_rtc_settimedate'), ('reset', 'patch_rtc_reset')):
        size = struct.unpack_from('<I',code,symbols[routine+'_size'])[0]
        sizes[name] = size
    if sizes!={'reader':176,'setter':156,'reset':52}:
        raise ValueError('The shared RTC routines changed their verified layouts')
    output = folder / 'custom.elf'
    subprocess.run([compiler, '-nostdlib', '-Wl,-Ttext=0x08000000',
                    'src/rtc_custom.S', '-o', str(output)], cwd=root, check=True, capture_output=True)
    binary = folder / 'custom.bin'
    subprocess.run([copy, '-O', 'binary', str(output), str(binary)], check=True)
    data = bytearray(binary.read_bytes())
    header = struct.unpack_from('<15I', data)
    if len(data) != header[14] or len(data) > 512 or tuple(header[i] for i in (0,3,5))!=(512,688,844):
        raise ValueError('Custom RTC payload or relocation table exceeds its reserved slot')
    target = root / 'rtc_custom.payload'
    target.write_bytes(data)
    report = dict(local_only=True, status='verified', sizes=sizes, payload_bytes=len(data),
                  sha256=hashlib.sha256(data).hexdigest(), entries=list(header[:10]),
                  relocations=list(struct.unpack_from('<8I', data, header[11])))
    (folder/'build.json').write_text(json.dumps(report,indent=2))
    print(report)


if __name__ == '__main__':
    main()
