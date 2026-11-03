/* theme.h — light/dark palette + Win11 dark title bar, added feature (README #2) */
#ifndef CAUDOK_THEME_H
#define CAUDOK_THEME_H

#include <windows.h>

typedef struct Theme {
    COLORREF bg;            /* client background            */
    COLORREF bg2;           /* panes                        */
    COLORREF text;          /* primary text                 */
    COLORREF textDim;       /* secondary text               */
    COLORREF accent;        /* selection/marker             */
    COLORREF wave;          /* waveform line                */
    COLORREF wavePeak;      /* min/max peak bars            */
    COLORREF waveCursor;    /* play cursor                  */
    COLORREF grid;          /* ruler/grid lines             */
    BOOL     dark;
} Theme;

void theme_set(BOOL dark);            /* switches palette + repaints windows */
BOOL theme_is_dark(void);
const Theme *theme_current(void);
void theme_windows_sync(HWND hwnd);   /* DWM dark title bar sync for one window */

#endif
