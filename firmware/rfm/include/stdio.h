#ifndef RFM_STDIO_H
#define RFM_STDIO_H
#include <stddef.h>
#include <stdarg.h>
int snprintf(char *out, size_t size, const char *fmt, ...);
int vsnprintf(char *out, size_t size, const char *fmt, va_list ap);
#endif
