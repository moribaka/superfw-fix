/* SuperFW bounded cheat importer. SPDX-License-Identifier: GPL-3.0-or-later
 * Decoders in this directory retain their mGBA MPL-2.0 notices.
 * The overlay runs before ROM loading, entirely in a reserved SDRAM arena. */
#include "../cheats.h"
#include "mgba_shim.h"
#undef malloc
#undef snprintf

typedef struct {
  struct GBACheatSet set;
  struct mCheat ops[CHEAT_OP_LIMIT + 1];
  struct mCheatPatch patches[CHEAT_PATCH_LIMIT + 1];
  uint32_t failed, offset, groups, start, lines, active, legacy, format;
  uint32_t next_disabled, lib_count, directive_version;
  struct {
    uint32_t name, code, name_size, code_size, disabled, line;
  } lib[1000];
  uint32_t raw_count;
  uint32_t raw[2 * CHEAT_OP_LIMIT];
  char title[1024];
} Workspace;
_Static_assert(sizeof(Workspace) < CHEAT_WORK_SIZE, "Import arena capacity");
extern const uint8_t __VM_START__[], __VM_END__[];

static void byte_put(void *base, unsigned offset, unsigned value) {
  uint16_t *p = (uint16_t *)((uintptr_t)base + (offset & ~1u));
  unsigned shift = (offset & 1) * 8;
  *p = (*p & ~(255u << shift)) | ((value & 255) << shift);
}
static void copy_bytes(void *to, const void *from, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    byte_put(to, i, ((const uint8_t *)from)[i]);
}
static void clear_bytes(void *to, unsigned n) {
  for (unsigned i = 0; i < n; i += 2)
    ((uint16_t *)to)[i / 2] = 0;
}
static unsigned length(const char *s) {
  unsigned n = 0;
  while (s[n])
    n++;
  return n;
}
static bool equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}
static bool starts(const char *s, const char *p) {
  while (*p)
    if (*s++ != *p++)
      return false;
  return true;
}
static bool space(unsigned c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool utf8(const char *p, unsigned n) {
  for (unsigned i = 0; i < n;) {
    unsigned c = (uint8_t)p[i++], k = 0, v = 0, min = 0;
    if (c < 128)
      continue;
    if (c >= 0xC2 && c <= 0xDF) {
      k = 1;
      v = c & 31;
      min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      k = 2;
      v = c & 15;
      min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      k = 3;
      v = c & 7;
      min = 0x10000;
    } else
      return false;
    while (k--) {
      if (i >= n || ((uint8_t)p[i] & 0xC0) != 0x80)
        return false;
      v = (v << 6) | ((uint8_t)p[i++] & 63);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
      return false;
  }
  return true;
}
static bool title_decode(char *out, const char *p, unsigned n) {
  if (n >= 1024)
    return false;
  if (utf8(p, n)) {
    for (unsigned i = 0; i < n; i++)
      out[i] = p[i];
    out[n] = 0;
    return true;
  }
  const uint16_t *gbk = (const uint16_t *)0x09D60000;
  unsigned q = 0;
  for (unsigned i = 0; i < n; i++) {
    unsigned a = (uint8_t)p[i];
    if (a < 128) {
      if (q >= 1023)
        return false;
      out[q++] = a;
      continue;
    }
    if (a < 0x81 || a > 0xFE || ++i >= n)
      return false;
    unsigned b = (uint8_t)p[i];
    if (b < 0x40 || b == 0x7F || b > 0xFE)
      return false;
    unsigned v = gbk[(a - 0x81) * 190 + b - 0x40 - (b > 0x7F)];
    if (!v || q + 3 >= 1024)
      return false;
    if (v < 0x800) {
      out[q++] = 0xC0 | (v >> 6);
      out[q++] = 0x80 | (v & 63);
    } else {
      out[q++] = 0xE0 | (v >> 12);
      out[q++] = 0x80 | ((v >> 6) & 63);
      out[q++] = 0x80 | (v & 63);
    }
  }
  out[q] = 0;
  return true;
}
static int digit(unsigned c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c |= 32;
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
static const char *hex(const char *p, uint32_t *v, unsigned n) {
  uint32_t value = 0;
  for (unsigned i = 0; i < n; i++) {
    int d = digit((uint8_t)*p++);
    if (d < 0)
      return 0;
    value = (value << 4) | d;
  }
  *v = value;
  return p;
}
const char *hex32(const char *p, uint32_t *v) { return hex(p, v, 8); }
const char *hex16(const char *p, uint16_t *v) {
  uint32_t x;
  const char *q = hex(p, &x, 4);
  if (q)
    *v = x;
  return q;
}

int GBACheatAddressIsReal(uint32_t a) {
  unsigned region = a >> 24, offset = a & 0xFFFFFF;
  if (region == 2)
    return offset < 0x40000 ? 32 : -64;
  if (region == 3)
    return offset < 0x8000 ? 32 : -64;
  if (region == 4)
    return offset < 0x400 ? 16 : -128;
  if (region >= 5 && region <= 15)
    return -8;
  return -192;
}
static bool auto_add(struct GBACheatSet *s, uint32_t a, uint32_t b) {
  if (!s->gsaVersion) {
    int best = INT_MIN;
    unsigned version = 0;
    for (unsigned i = 1; i <= 4; i++) {
      uint32_t x = a, y = b;
      if (i & 1)
        GBACheatDecryptGameShark(&x, &y,
                                 i < 3 ? GBACheatGameSharkSeeds : GBACheatProActionReplaySeeds);
      int score =
          i < 3 ? GBACheatGameSharkProbability(x, y) : GBACheatProActionReplayProbability(x, y);
      // Match mGBA's tie order: encrypted GSA, encrypted PAR, raw GSA, raw PAR.
      unsigned order = i == 1 ? 0 : i == 3 ? 1 : i == 2 ? 2 : 3;
      unsigned oldorder = version == 1 ? 0 : version == 3 ? 1 : version == 2 ? 2 : 3;
      if (score > best || (score == best && order < oldorder)) {
        best = score;
        version = i;
      }
    }
    if (best < 0)
      return false;
    GBACheatSetGameSharkVersion(s, version);
  }
  return s->gsaVersion < 3 ? GBACheatAddGameShark(s, a, b) : GBACheatAddProActionReplay(s, a, b);
}

static unsigned add_codes(Workspace *w, const char *p) {
  struct GBACheatSet *s = &w->set;
  while (*p) {
    while (space(*p) || *p == '+')
      p++;
    if (!*p)
      break;
    uint32_t a, b = 0;
    const char *q = hex32(p, &a);
    if (!q)
      return CheatSyntax;
    if (*q == ':') {
      q++;
      unsigned digits = 0;
      while (digit((uint8_t)q[digits]) >= 0 && digits < 9)
        digits++;
      if (digits != 2 && digits != 4 && digits != 8)
        return CheatSyntax;
      q = hex(q, &b, digits);
      if (s->incompleteCheat != COMPLETE || s->incompletePatch)
        return CheatIncomplete;
      struct mCheat *c = mCheatListAppend(&s->d.list);
      *c = (struct mCheat){
          .type = CHEAT_ASSIGN, .width = digits / 2, .address = a, .operand = b, .repeat = 1};
    } else {
      while (space(*q) || *q == '+')
        q++;
      unsigned digits = 0;
      while (digit((uint8_t)q[digits]) >= 0 && digits < 9)
        digits++;
      if (digits != 4 && digits != 8)
        return CheatSyntax;
      q = hex(q, &b, digits);
      if (!s->cbMaster && digits == 4 && (a >> 28) == 5 && !b && s->incompleteCheat == COMPLETE)
        return CheatRange;
      if (!(digits == 4 ? GBACheatAddCodeBreaker(s, a, b) : auto_add(s, a, b)))
        return CheatUnsupported;
    }
    if (w->failed)
      return CheatCapacity;
    if (*q && !space(*q) && *q != '+')
      return CheatSyntax;
    p = q;
    w->lines++;
  }
  return CheatOK;
}

static void reset_group(Workspace *w) {
  w->set.d.list.size = 0;
  w->set.d.romPatches.size = 0;
  w->set.incompleteCheat = COMPLETE;
  w->set.incompletePatch = 0;
  w->set.currentBlock = COMPLETE;
  w->set.remainingAddresses = 0;
  w->lines = 0;
  w->raw_count = 0;
}
static unsigned begin_group(t_cheat_import *r, Workspace *w, const char *title) {
  unsigned n = length(title), slen = (n + 4) & ~3u;
  if (n >= sizeof(w->title) || w->offset + sizeof(t_cheathdr) + slen > r->output_size)
    return CheatCapacity;
  w->start = w->offset;
  t_cheathdr h = {.slen = slen, .enabled = w->legacy ? 0 : !w->next_disabled};
  w->next_disabled = 0;
  copy_bytes(r->output + w->offset, &h, sizeof(h));
  w->offset += sizeof(h);
  clear_bytes(r->output + w->offset, slen);
  copy_bytes(r->output + w->offset, title, n);
  w->offset += slen;
  reset_group(w);
  if (w->format == 2 && w->directive_version)
    GBACheatSetGameSharkVersion(&w->set, w->directive_version);
  w->active = 1;
  return CheatOK;
}
static unsigned validate_ops(Workspace *w) {
  struct GBACheatSet *s = &w->set;
  if (s->incompleteCheat != COMPLETE || s->incompletePatch)
    return CheatIncomplete;
  if (s->currentBlock != COMPLETE) {
    // PAR blocks without ELSE/ENDIF are closed at the group end.
    if (s->currentBlock >= s->d.list.size)
      return CheatIncomplete;
    struct mCheat *c = &w->ops[s->currentBlock];
    unsigned n = s->d.list.size - s->currentBlock - 1;
    if (c->repeat)
      c->negativeRepeat = n - c->repeat;
    else
      c->repeat = n;
    s->currentBlock = COMPLETE;
  }
  if (s->hook)
    return CheatHook;
  if (s->d.romPatches.size)
    return CheatROMPatch;
  uint32_t cost = 0;
  for (unsigned i = 0; i < s->d.list.size; i++) {
    struct mCheat *c = &w->ops[i];
    if (c->type > CHEAT_NEVER || (c->width != 1 && c->width != 2 && c->width != 4)) {
      if ((c->type == CHEAT_NEVER || c->type == CHEAT_IF_BUTTON) && c->width == 0)
        continue;
      return CheatUnsupported;
    }
    if (c->type >= CHEAT_IF_EQ)
      continue;
    if (!c->repeat || c->repeat > 4096 || cost > 4096 - c->repeat)
      return CheatRange;
    cost += c->repeat;
    if (c->type != CHEAT_ASSIGN_INDIRECT) {
      uint32_t last = c->address + (c->repeat - 1) * (uint32_t)c->addressOffset;
      uint32_t region = c->address >> 24;
      if (region >= 8 && region <= 13)
        return CheatROMPatch;
      if (region < 2 || region > 7 || (last >> 24) != region || (c->address & (c->width - 1)) ||
          (last & (c->width - 1)))
        return CheatRange;
    }
  }
  return w->failed ? CheatCapacity : CheatOK;
}

static unsigned legacy_payload(Workspace *w, uint8_t *out, unsigned capacity) {
  unsigned cnt = w->raw_count;
  if (!cnt)
    return CheatIncomplete;
  if ((cnt + 1) * 8 > capacity)
    return CheatCapacity;
  for (unsigned i = 0; i < cnt; i++) {
    uint32_t a = w->raw[2 * i], v = w->raw[2 * i + 1];
    unsigned op = a >> 28;
    unsigned extra = op == 4 ? 1 : op == 5 ? (v + 2) / 3 : 0;
    if (op == 9)
      return CheatUnsupported;
    if ((op == 4 || op == 5) && (!v && op == 5))
      return CheatRange;
    if (extra >= cnt - i || (extra + 1) * 8 > 255)
      return CheatIncomplete;
    if (op == 4 && !((uint16_t)w->raw[2 * i + 2]))
      return CheatRange;
    if (op == 13 && ((a & 0xFFFFFFF) >> 4) > 2)
      return CheatUnsupported;
    t_cheat_predec h = {op ? op * 2 : 2, (extra + 1) * 8, v, a & 0xFFFFFFF};
    copy_bytes(out + i * 8, &h, 8);
    for (unsigned j = 0; j < extra; j++) {
      uint32_t words[2] = {w->raw[2 * (i + j + 1)], w->raw[2 * (i + j + 1) + 1]};
      if (op == 5) {
        words[0] = __builtin_bswap32(words[0]);
        words[1] = __builtin_bswap16(words[1]);
      }
      copy_bytes(out + (i + j + 1) * 8, words, 8);
    }
    i += extra;
  }
  clear_bytes(out + cnt * 8, 8);
  return (cnt + 1) * 8;
}
static unsigned end_group(t_cheat_import *r, Workspace *w) {
  if (!w->active)
    return CheatOK;
  bool legacy = w->legacy;
  if (legacy) {
    bool encrypted = w->set.cbMaster != 0;
    for (unsigned i = 0; i < w->raw_count && !encrypted; i++) {
      unsigned op = w->raw[2 * i] >> 28;
      if (op == 9)
        encrypted = true;
      else if (op == 4)
        i++;
      else if (op == 5)
        i += (w->raw[2 * i + 1] + 2) / 3;
    }
    if (encrypted) {
      w->legacy = 0;
      for (unsigned i = 0; i < w->raw_count; i++) {
        if (!GBACheatAddCodeBreaker(&w->set, w->raw[2 * i], w->raw[2 * i + 1]))
          return CheatUnsupported;
        if (w->failed)
          return CheatCapacity;
        w->lines++;
      }
    }
  }
  unsigned n;
  if (w->legacy) {
    n = legacy_payload(w, r->output + w->offset, r->output_size - w->offset);
    if (n < 16)
      return n;
  } else {
    unsigned e = validate_ops(w);
    if (e)
      return e;
    if (!w->lines)
      return CheatIncomplete;
    n = 12 + w->set.d.list.size * sizeof(struct mCheat);
    if (n > 65535 || n > r->output_size - w->offset)
      return CheatCapacity;
    uint32_t h[3] = {CHEAT_VM_MAGIC, w->set.d.list.size, 0};
    copy_bytes(r->output + w->offset, h, 12);
    copy_bytes(r->output + w->offset + 12, w->ops, n - 12);
  }
  ((t_cheathdr *)(r->output + w->start))->codelen = n;
  w->offset += n;
  w->groups++;
  w->active = 0;
  w->legacy = legacy;
  return CheatOK;
}

// This reader never mutates SDRAM input, handles CRLF/BOM/EOF, and bounds lines.
static int next_line(t_cheat_import *r, unsigned *at, char *line, unsigned size) {
  if (*at >= r->input_size)
    return 0;
  unsigned n = 0;
  bool longline = false;
  while (*at < r->input_size) {
    unsigned c = (uint8_t)r->input[(*at)++];
    if (c == '\n')
      break;
    if (!c)
      return -1;
    if (n + 1 < size)
      line[n++] = c;
    else
      longline = true;
  }
  while (n && space((uint8_t)line[n - 1]))
    n--;
  line[n] = 0;
  return longline ? -1 : 1;
}

static unsigned decimal(const char **p) {
  unsigned n = 0;
  if (**p < '0' || **p > '9')
    return UINT_MAX;
  while (**p >= '0' && **p <= '9') {
    if (n > 10000)
      return UINT_MAX;
    n = n * 10 + *(*p)++ - '0';
  }
  return n;
}
static bool quoted(char *to, const char *p, unsigned n) {
  while (n && space(*p)) {
    p++;
    n--;
  }
  if (!n || *p != '"')
    return false;
  p++;
  n--;
  unsigned q = 0;
  while (n && *p != '"') {
    unsigned c = (uint8_t)*p++;
    n--;
    if (c == '\\') {
      if (!n)
        return false;
      c = *p++;
      n--;
      if (c == 'n')
        c = '\n';
      else if (c == 'r')
        c = '\r';
      else if (c == 't')
        c = '\t';
      else if (c != '"' && c != '\\')
        return false;
    }
    if (q >= 1023)
      return false;
    to[q++] = c;
  }
  if (!n || *p != '"')
    return false;
  p++;
  n--;
  while (n && space(*p)) {
    p++;
    n--;
  }
  to[q] = 0;
  return !n;
}
static unsigned lib_field(t_cheat_import *r, Workspace *w, const char *p, unsigned input_at,
                          const char *line, unsigned lineno) {
  if (!starts(p, "cheat"))
    return CheatSyntax;
  p += 5;
  if (*p == 's') {
    p++;
    while (space(*p))
      p++;
    if (*p++ != '=')
      return CheatSyntax;
    while (space(*p))
      p++;
    unsigned n = decimal(&p);
    if (n > 1000 || *p || w->lib_count)
      return CheatSyntax;
    w->lib_count = n;
    return CheatOK;
  }
  unsigned index = decimal(&p);
  if (index >= 1000 || *p++ != '_')
    return CheatSyntax;
  const char *field = p;
  while (*p && *p != '=' && !space(*p))
    p++;
  unsigned flen = p - field;
  while (space(*p))
    p++;
  if (*p++ != '=')
    return CheatSyntax;
  while (space(*p))
    p++;
  unsigned offset = input_at + (p - line), size = length(p);
  if (flen == 4 && starts(field, "desc")) {
    w->lib[index].name = offset;
    w->lib[index].name_size = size;
  } else if (flen == 4 && starts(field, "code")) {
    w->lib[index].code = offset;
    w->lib[index].code_size = size;
    w->lib[index].line = lineno;
  } else if (flen == 6 && starts(field, "enable")) {
    if (!equal(p, "true") && !equal(p, "false"))
      return CheatSyntax;
    w->lib[index].disabled = equal(p, "false");
  } else
    return CheatUnsupported;
  return CheatOK;
}
static unsigned lib_compile(t_cheat_import *r, Workspace *w) {
  char text[1024];
  if (!w->lib_count)
    return CheatIncomplete;
  for (unsigned i = 0; i < w->lib_count; i++) {
    if (w->lib[i].name_size) {
      if (!quoted(text, r->input + w->lib[i].name, w->lib[i].name_size))
        return CheatSyntax;
    } else {
      text[0] = 'C';
      text[1] = 'h';
      text[2] = 'e';
      text[3] = 'a';
      text[4] = 't';
      text[5] = 0;
    }
    w->next_disabled = w->lib[i].disabled;
    unsigned e = begin_group(r, w, text);
    if (e)
      return e;
    if (!w->lib[i].code_size || !quoted(text, r->input + w->lib[i].code, w->lib[i].code_size))
      return CheatSyntax;
    r->error_line = w->lib[i].line;
    e = add_codes(w, text);
    if (e)
      return e;
    e = end_group(r, w);
    if (e)
      return e;
  }
  return CheatOK;
}
static unsigned ez_line(t_cheat_import *r, Workspace *w, char *p) {
  if (*p == '[') {
    char *q = p + 1;
    while (*q && *q != ']')
      q++;
    if (*q != ']' || q[1])
      return CheatSyntax;
    char title[1024];
    if (!title_decode(title, p + 1, q - p - 1))
      return CheatSyntax;
    copy_bytes(w->title, title, length(title) + 1);
    return CheatOK;
  }
  char *eq = p;
  while (*eq && *eq != '=')
    eq++;
  if (!*eq)
    return CheatSyntax;
  char title[1024];
  unsigned n = length(w->title);
  if (!n)
    return CheatSyntax;
  for (unsigned i = 0; i <= n; i++)
    title[i] = w->title[i];
  if (eq - p != 2 || p[0] != 'O' || p[1] != 'N') {
    char sub[1024];
    if (!title_decode(sub, p, eq - p))
      return CheatSyntax;
    unsigned m = length(sub);
    if (n + m + 3 > sizeof(title))
      return CheatCapacity;
    title[n++] = ':';
    title[n++] = ' ';
    for (unsigned i = 0; i <= m; i++)
      title[n + i] = sub[i];
  }
  w->next_disabled = 1;
  unsigned e = begin_group(r, w, title);
  if (e)
    return e;
  p = eq + 1;
  uint32_t address = 0, value = 0;
  unsigned digits = 0;
  while (1) {
    if (space(*p)) {
      p++;
      continue;
    }
    int d = digit((uint8_t)*p);
    if (d >= 0) {
      if (++digits > 8)
        return CheatSyntax;
      value = (value << 4) | d;
      p++;
      continue;
    }
    unsigned c = *p;
    if (c != ',' && c != ';' && c)
      return CheatSyntax;
    if (!digits)
      return CheatSyntax;
    if (!address) {
      if (c != ',' || value >= 0x48000)
        return CheatRange;
      address = value < 0x40000 ? 0x02000000 + value : 0x03000000 + value - 0x40000;
    } else {
      if (value > 255)
        return CheatRange;
      struct mCheat *op = mCheatListAppend(&w->set.d.list);
      *op = (struct mCheat){
          .type = CHEAT_ASSIGN, .width = 1, .address = address++, .operand = value, .repeat = 1};
      w->lines++;
      if (w->failed)
        return CheatCapacity;
      if (c == ';')
        address = 0;
    }
    value = 0;
    digits = 0;
    if (!c)
      break;
    p++;
    if (c == ';' && !*p)
      break;
  }
  return end_group(r, w);
}

int cheat_compile(t_cheat_import *r) __attribute__((section(".entry")));
int cheat_compile(t_cheat_import *r) {
  r->error = CheatOK;
  r->error_line = 0;
  if (r->work_size < sizeof(Workspace) || r->output_size < 4 || r->input_size > CHEAT_INPUT_MAX) {
    r->error = CheatCapacity;
    return -1;
  }
  Workspace *w = r->work;
  clear_bytes(w, sizeof(*w));
  clear_bytes(r->output, 4);
  w->offset = 4;
  w->set.d.list = (struct mCheatList){w->ops, 0, &w->failed};
  w->set.d.romPatches = (struct mCheatPatchList){w->patches, 0, &w->failed};
  reset_group(w);
  unsigned at = 0, lineno = 0, e = 0;
  char line[1024];
  if (r->input_size >= 3 && (uint8_t)r->input[0] == 0xEF && (uint8_t)r->input[1] == 0xBB &&
      (uint8_t)r->input[2] == 0xBF)
    at = 3;
  while (1) {
    unsigned input_at = at;
    int read = next_line(r, &at, line, sizeof(line));
    if (!read)
      break;
    lineno++;
    if (read < 0) {
      e = CheatSyntax;
      break;
    }
    char *p = line;
    while (space((uint8_t)*p))
      p++;
    if (!*p)
      continue;
    if (!w->format) {
      uint32_t first;
      const char *probe = hex32(p, &first);
      w->format =
          (*p == '#' || *p == '!' || (probe && (*probe == ':' || space(*probe) || *probe == '+')))
              ? 2
              : 1;
      w->legacy = w->format == 1;
      if (*p == '[') {
        unsigned look = at;
        char next[1024];
        while (next_line(r, &look, next, sizeof(next)) > 0) {
          const char *q = next;
          while (space(*q))
            q++;
          if (!*q)
            continue;
          while (*q && *q != '=')
            q++;
          if (*q == '=') {
            w->format = 4;
            w->legacy = 0;
          }
          break;
        }
      }
      if (starts(p, "cheats")) {
        const char *q = p + 6;
        while (space(*q))
          q++;
        if (*q == '=') {
          w->format = 3;
          w->legacy = 0;
        }
      }
    }
    if (w->format == 3) {
      e = lib_field(r, w, p, input_at, line, lineno);
      if (e)
        break;
      continue;
    }
    if (w->format == 4) {
      if (equal(p, "[GameInfo]"))
        break;
      e = ez_line(r, w, p);
      if (e)
        break;
      continue;
    }
    if (w->format == 2) {
      if (*p == '!') {
        p++;
        while (space((uint8_t)*p))
          p++;
        if (equal(p, "disabled")) {
          w->next_disabled = 1;
          continue;
        }
        int version = equal(p, "GSAv1")       ? 1
                      : equal(p, "GSAv1 raw") ? 2
                      : equal(p, "PARv3")     ? 3
                      : equal(p, "PARv3 raw") ? 4
                                              : 0;
        if (equal(p, "reset")) {
          w->directive_version = 0;
          continue;
        }
        if (!version) {
          e = CheatUnsupported;
          break;
        }
        w->directive_version = version;
        continue;
      }
      if (*p == '#') {
        e = end_group(r, w);
        if (e)
          break;
        p++;
        while (space((uint8_t)*p))
          p++;
        e = begin_group(r, w, p);
        if (e)
          break;
        continue;
      }
      if (!w->active) {
        e = begin_group(r, w, "");
        if (e)
          break;
      }
      e = add_codes(w, p);
      if (e)
        break;
    } else {
      if (!w->active) {
        e = begin_group(r, w, p);
        if (e)
          break;
        continue;
      }
      uint32_t first;
      const char *probe = hex32(p, &first);
      if (!probe || (!space(*probe) && *probe != '+')) {
        e = end_group(r, w);
        if (e)
          break;
        e = begin_group(r, w, p);
        if (e)
          break;
        continue;
      }
      while (*p) {
        while (space(*p) || *p == '+')
          p++;
        if (!*p)
          break;
        if (w->raw_count >= CHEAT_OP_LIMIT) {
          e = CheatCapacity;
          break;
        }
        uint32_t a, b;
        const char *q = hex32(p, &a);
        if (!q) {
          e = CheatSyntax;
          break;
        }
        while (space(*q) || *q == '+')
          q++;
        q = hex(q, &b, 4);
        if (!q || (*q && !space(*q) && *q != '+')) {
          e = CheatSyntax;
          break;
        }
        w->raw[2 * w->raw_count] = a;
        w->raw[2 * w->raw_count + 1] = b;
        w->raw_count++;
        p = (char *)q;
      }
      if (e)
        break;
    }
  }
  if (!e && w->format == 3)
    e = lib_compile(r, w);
  if (!e)
    e = end_group(r, w);
  if (!e && !w->groups)
    e = CheatIncomplete;
  if (e) {
    clear_bytes(r->output, 4);
    r->error = e;
    if (!r->error_line)
      r->error_line = lineno;
    return -1;
  }
  unsigned vm_size = __VM_END__ - __VM_START__, enhanced = 0;
  unsigned off = 4, enabled = 0;
  for (unsigned i = 0; i < w->groups; i++) {
    t_cheathdr *h = (t_cheathdr *)(r->output + off);
    enabled |= h->enabled;
    off += sizeof(*h) + h->slen;
    uint32_t *body = (uint32_t *)(r->output + off);
    if (body[0] == CHEAT_VM_MAGIC) {
      body[2] = w->offset - off;
      enhanced++;
    }
    off += h->codelen;
  }
  if (enhanced && vm_size > r->output_size - w->offset) {
    clear_bytes(r->output, 4);
    r->error = CheatCapacity;
    return -1;
  }
  if (enhanced)
    copy_bytes(r->output + w->offset, __VM_START__, vm_size);
  else
    vm_size = 0;
  *(uint32_t *)r->output = w->groups | (enabled ? CHEAT_ENABLED_FLAG : 0);
  return w->offset + vm_size;
}
