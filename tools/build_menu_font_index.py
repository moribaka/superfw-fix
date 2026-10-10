"""Index the existing in-game translations for compact menu font relocation."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src/menu_messages.h').read_text(encoding='utf-8')
rows = []
lines = ['// Generated from menu_messages.h; glyphs remain in the existing font pack.']
for name, body in re.findall(r'msg_(\w+)\[\] = \{(.*?)\};', source, re.S):
    text = ''.join(json.loads(value) for value in re.findall(r'"(?:[^"\\]|\\.)*"', body))
    chars = sorted({ord(c) for c in text if ord(c) > 127})
    assert len(chars) < 256 and all(c < 65536 for c in chars)
    lines.append('static const uint16_t menu_chars_' + name + '[] = {' + ','.join(map(str, chars or [0])) + '};')
    rows.append((name, len(chars)))
lines.append('static const struct { const uint16_t *chars; unsigned count; } menu_charsets[] = {')
lines += ['  {menu_chars_' + name + ', ' + str(count) + '},' for name, count in rows]
lines.append('};')
(root / 'src/fonts/menu_font_chars.h').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('Menu font character sets', rows)

compact=json.loads((root/'src/menu_messages_compact.json').read_text(encoding='utf8'))
lines=['/* Generated compact-menu glyph subsets; original font artwork retained. */']
rows=[]
for name,entries in compact.items():
    chars=sorted({ord(c) for text in entries.values() for c in text if ord(c)>127})
    assert len(chars)<256 and all(c<65536 for c in chars)
    lines.append('static const uint16_t menu_compact_chars_'+name+'[] = {'+','.join(map(str,chars or [0]))+'};')
    rows.append((name,len(chars)))
lines.append('static const struct { const uint16_t *chars; unsigned count; } menu_compact_charsets[] = {')
lines+=[' {menu_compact_chars_'+name+', '+str(count)+'},' for name,count in rows]
lines.append('};')
(root/'src/fonts/menu_compact_font_chars.h').write_text('\n'.join(lines)+'\n',encoding='utf8')
print('Compact menu font character sets',rows)
