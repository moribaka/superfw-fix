/* Compact-menu formatting for its audited %s, %d, %u and %% formats.
 * The themed menu and main firmware retain the original nanoprintf.
 * Return value and truncation follow snprintf, including size-zero calls. */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "nanoprintf.h"

typedef struct {char *buffer;size_t size,used;} t_format;
static void put(t_format *out,char value) {
  if(out->used+1<out->size)out->buffer[out->used]=value;
  out->used++;
}
int npf_snprintf(char *buffer,size_t size,const char *format,...) {
  t_format out={buffer,size,0};va_list args;va_start(args,format);
  while(*format) {
    char code=*format++;
    if(code!='%') {put(&out,code);continue;}
    code=*format++;
    if(code=='s') {
      const char *text=va_arg(args,const char*);
      while(*text)put(&out,*text++);
    } else if(code=='d'||code=='u') {
      uint32_t value;
      if(code=='d') {
        int32_t signed_value=va_arg(args,int);
        value=(uint32_t)signed_value;
        if(signed_value<0) {put(&out,'-');value=0U-value;}
      } else value=va_arg(args,unsigned);
      char digits[10];unsigned count=0;
      do {digits[count++]=(char)('0'+value%10);value/=10;} while(value);
      while(count)put(&out,digits[--count]);
    } else if(code=='%')put(&out,'%');
    else {put(&out,'%');if(code)put(&out,code);else break;}
  }
  va_end(args);
  if(size)buffer[out.used<size?out.used:size-1]=0;
  return (int)out.used;
}
