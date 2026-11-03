/* theme.c — palettes designed fresh (no original assets), aligned to Win11 accent cues */
#include "theme.h"
#include "aux.h"

static Theme g_light = {
    .bg=RGB(0xf3,0xf3,0xf3), .bg2=RGB(0xff,0xff,0xff), .text=RGB(0x1a,0x1a,0x1a),
    .textDim=RGB(0x71,0x71,0x71), .accent=RGB(0xc5,0x0f,0x1f),
    .wave=RGB(0x2b,0x57,0x9c), .wavePeak=RGB(0x44,0x86,0xd6),
    .waveCursor=RGB(0xc5,0x0f,0x1f), .grid=RGB(0xd9,0xd9,0xd9), .dark=FALSE,
};
static Theme g_dark = {
    .bg=RGB(0x20,0x21,0x24), .bg2=RGB(0x2b,0x2d,0x31), .text=RGB(0xf2,0xf2,0xf2),
    .textDim=RGB(0x9a,0x9a,0x9a), .accent=RGB(0xe0,0x51,0x5a),
    .wave=RGB(0x74,0xa9,0xe8), .wavePeak=RGB(0xa6,0xc9,0xf2),
    .waveCursor=RGB(0xe0,0x51,0x5a), .grid=RGB(0x3a,0x3c,0x40), .dark=TRUE,
};
static Theme g_cur;
static BOOL  g_darksel = FALSE;

void theme_set(BOOL dark) {
    g_darksel = dark;
    g_cur = dark ? g_dark : g_light;
    HWND fg = GetForegroundWindow();   /* repaint active win32 main; simple broadcast below */
    (void)fg;
    /* repaint every top-level window of this process */
    HWND w = GetTopWindow(NULL);
    DWORD pid = GetCurrentProcessId();
    while (w) {
        DWORD p = 0; DWORD t = 0;
        GetWindowThreadProcessId(w, &p);
        if (p == pid) { theme_windows_sync(w); InvalidateRect(w, NULL, TRUE); }
        w = GetWindow(w, GW_HWNDNEXT);
        (void)t;
    }
}
BOOL theme_is_dark(void) { return g_darksel; }
const Theme *theme_current(void) { return &g_cur; }

void theme_windows_sync(HWND hwnd) {
    typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    DwmSetWindowAttribute_t fn =
        (DwmSetWindowAttribute_t)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!fn) { FreeLibrary(dwm); return; }
    BOOL dark = theme_is_dark() ? TRUE : FALSE;
    /* 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win11 22000+) */
    if (FAILED(fn(hwnd, 20, &dark, sizeof(dark))))
        fn(hwnd, 19, &dark, sizeof(dark));      /* Win10 19041 fallback */
    FreeLibrary(dwm);
}
