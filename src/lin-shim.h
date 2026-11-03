/* lin-shim.h — minimal Windows-shim so waveio compiles/runs headlessly on Linux.
 * Only what waveio uses; nothing else leaks in. */
#ifndef CAUDOK_LIN_SHIM_H
#define CAUDOK_LIN_SHIM_H
#define MAX_PATH 260
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static inline FILE *caudiok_wfopen(const wchar_t *path, const wchar_t *mode) {
    char p[MAX_PATH + 1], m[16];
    size_t n = wcstombs(p, path, MAX_PATH);
    size_t mn = wcstombs(m, mode, 15);
    if (n == (size_t)-1 || mn == (size_t)-1 || n > MAX_PATH) return NULL;
    p[n] = 0; m[mn] = 0;
    return fopen(p, m);
}
#define _wfopen caudiok_wfopen
#define _ftelli64(f) ftello(f)
#endif
