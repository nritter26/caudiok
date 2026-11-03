/* aux.c — Windows-only helper (message box); headless Linux harness does not build it. */
#include "aux.h"
#include <stdio.h>

void fail_msg(const wchar_t *title, const wchar_t *fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    MessageBoxW(NULL, buf, title, MB_OK | MB_ICONERROR);
}
