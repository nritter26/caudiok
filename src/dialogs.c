/* dialogs.c — dialogs rebuilt from the original's measured RT_DIALOG templates
 * (spec/dialog-map.json: classic-header, UTF-16, DS_SETFONT MS Sans Serif 8pt).
 * Geometry, control ids and styles are verbatim template measurements; styling
 * is modernized via the palette. Persistence lives in HKCU\Software\Caudiok5
 * (neutral registry key). */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <math.h>
#include "dialogs.h"
#include "audio.h"
#include "theme.h"

/* ---------------- persistence ---------------- */

static const wchar_t *REGKEY = L"Software\\Caudiok5";

static HKEY reg_open_w(void) {
    HKEY k; RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0,
                            KEY_READ|KEY_WRITE, NULL, &k, NULL);
    return k;
}

int st_get_int(const wchar_t *name, int def) {
    HKEY k = reg_open_w(); if (!k) return def;
    DWORD v=0, t=0, cb=sizeof(v);
    LONG r = RegQueryValueExW(k, name, NULL, &t, (BYTE*)&v, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || t != REG_DWORD) return def;
    return (int)v;
}

void st_set_int(const wchar_t *name, int val) {
    HKEY k = reg_open_w(); if (!k) return;
    DWORD v = (DWORD)val;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

/* ---------------- runtime DLGTEMPLATE builder ---------------- */
/* Encodes the measured template layout: DLGTEMPLATE header (style, exStyle=0,
 * cdit, x/y=0, cx/cy DLU), no menu, no class, wide title, DS_SETFONT 8pt
 * "MS Sans Serif"; items as classic DLGITEMTEMPLATE (style, exStyle=0, rect,
 * WORD id, class ordinal/string, title, extra=0) with DWORD alignment. */

typedef struct {
    WORD   id;
    short  x, y, cx, cy;
    DWORD  style;
    WORD   clsOrd;                 /* 0x80.. button/edit/static/... ; 0 = by string */
    const wchar_t *clsStr;         /* e.g. L"msctls_updown32" (must be wide: template
                                    * strings are UTF-16 per spec/layout-main.md quirk) */
    const wchar_t *text;
} DlgItemDef;

typedef struct {
    WORD   cx, cy;                 /* dialog size in DLU */
    DWORD  style;                  /* verbatim template DS_|WS_ mask */
    const wchar_t *title;
    const DlgItemDef *items;
    int    n;
} DlgDef;

#define CLS_BUTTON   0x80
#define CLS_EDIT     0x81
#define CLS_STATIC   0x82
#define CLS_COMBOBOX 0x85

static LPBYTE w16(LPBYTE p, WORD v) { memcpy(p, &v, 2); return p + 2; }
static LPBYTE w32(LPBYTE p, DWORD v) { memcpy(p, &v, 4); return p + 4; }
static LPBYTE awide(LPBYTE p, const wchar_t *s) {   /* zero-terminated wide string */
    size_t n = (lstrlenW(s) + 1) * 2;
    memcpy(p, s, n); return p + n;
}
static LPBYTE a4(LPBYTE p) { uintptr_t q = (uintptr_t)p; return (LPBYTE)((q + 3) & ~(uintptr_t)3); }
static LPBYTE wname(LPBYTE p, WORD ord) { p = w16(p, 0xFFFF); return w16(p, ord); }
static LPBYTE wtext(LPBYTE p, const wchar_t *s) {
    if (!s || !*s) return w16(p, 0x0000);       /* sz_Or_Ord: none */
    return awide(p, s);
}

static HGLOBAL dialog_build(const DlgDef *d) {
    int bytes = 2048;
    for (int i = 0; i < d->n; i++) bytes += 200;
    HGLOBAL g = GlobalAlloc(GMEM_ZEROINIT, bytes);
    if (!g) return NULL;
    LPBYTE p = (LPBYTE)GlobalLock(g);
    p = w32(p, d->style);            /* DLG style (inc. DS_MODALFRAME|DS_SETFONT) */
    p = w32(p, 0);                   /* extended style */
    p = w16(p, (WORD)d->n);          /* cdit */
    p = w16(p, 0); p = w16(p, 0);    /* x, y */
    p = w16(p, d->cx); p = w16(p, d->cy);
    p = w16(p, 0x0000);              /* menu: none */
    p = w16(p, 0x0000);              /* dialog class: default */
    p = wtext(p, d->title);
    p = w16(p, 8);                   /* DS_SETFONT point size (template value) */
    p = awide(p, L"MS Sans Serif");  /* template face */
    for (int i = 0; i < d->n; i++) {
        const DlgItemDef *it = &d->items[i];
        p = a4(p);
        p = w32(p, it->style);
        p = w32(p, 0);               /* item extended style */
        p = w16(p, (WORD)it->x); p = w16(p, (WORD)it->y);
        p = w16(p, (WORD)it->cx); p = w16(p, (WORD)it->cy);
        p = w16(p, it->id);
        if (it->clsOrd) p = wname(p, it->clsOrd);
        else            p = awide(p, it->clsStr ? it->clsStr : L"");
        p = wtext(p, it->text);
        p = w16(p, 0);               /* extraCount */
    }
    GlobalUnlock(g);
    return g;
}

static HBRUSH dlg_bg_brush(HWND h) {
    /* cache one brush per theme: WM_CTLCOLOR* fires on every paint pass, and
     * creating a brush per message leaks GDI handles until reset */
    static HBRUSH cached = NULL;
    static int    cached_dark = -1;
    int dark = theme_is_dark() ? 1 : 0;
    if (!cached || cached_dark != dark) {
        if (cached) DeleteObject(cached);
        cached = CreateSolidBrush(theme_current()->bg2);
        cached_dark = dark;
    }
    return cached;
}

static LPVOID dpi_hdr(void) { return NULL; }
/* (kept intentionally simple: template DLU math baselined by the original font) */

/* ---------------- Preferences dialog: original template 425 ---------------- */
/* rect 281x172 DLU, DS bits 0x80c808c0, 24 items. Control ids verbatim. */

static const DlgItemDef PREFS_ITEMS[] = {
    { 1,    163, 147, 50, 14, 0x50010001, CLS_BUTTON, NULL, L"&OK" },
    { 2,    221, 147, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
    { 1055,  19,  51,124, 10, 0x50010004, CLS_BUTTON, NULL, L"Normal  (-40 dB)" },
    { 1056,  19,  36,124, 10, 0x50010004, CLS_BUTTON, NULL, L"Medium  (-30 dB)" },
    { 1057,  19,  22,124, 10, 0x50010004, CLS_BUTTON, NULL, L"High  (-20 dB)" },
    { 1059,   8,   6,145, 64, 0x50020007, CLS_BUTTON, NULL, L" Threshold AutoMarker/AutoStop " },
    { 1060, 161,   6,112, 64, 0x50020007, CLS_BUTTON, NULL, L" Threshold AutoMarker " },
    { 1061, 173,  22, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"  2 Seconds" },
    { 1062, 173,  36, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"  5 Seconds" },
    { 1063, 173,  51, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"10 Seconds" },
    { 1064, 161,  74,112, 64, 0x50020007, CLS_BUTTON, NULL, L" Threshold AutoStop " },
    { 1065, 173,  90, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"  2 Seconds" },
    { 1066, 173, 104, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"  5 Seconds" },
    { 1067, 173, 119, 85, 10, 0x50010004, CLS_BUTTON, NULL, L"10 Seconds" },
    { 1068,   8,  74,145, 42, 0x50020007, CLS_BUTTON, NULL, L" Surround Distance " },
    { 1070,  19,  90, 42, 14, 0x50812082, CLS_EDIT,   NULL, L"" },
    { 1071,  62,  90, 10, 14, 0x500000a6, 0, L"msctls_updown32", L"Spin1" },
    { 1075,  73,  90, 71, 12, 0x5002020c, CLS_STATIC, NULL, L" cm (100...1000)" },
    { 1072,   8, 120, 98, 45, 0x50020007, CLS_BUTTON, NULL, L" Surround Measuring Units " },
    { 1073,  19, 132, 52, 10, 0x50010004, CLS_BUTTON, NULL, L"Meter" },
    { 1074,  19, 147, 52, 10, 0x50010004, CLS_BUTTON, NULL, L"Inch" },
    { 1076, 110, 120, 43, 45, 0x50020007, CLS_BUTTON, NULL, L" EQ " },
    { 1077, 117, 132, 30, 10, 0x50010c04, CLS_BUTTON, NULL, L"A" },
    { 1078, 117, 147, 30, 10, 0x50010c04, CLS_BUTTON, NULL, L"B" },
};
static const DlgDef PREFS_DEF = { 281, 172, 0x80c808c0, L"Preferences", PREFS_ITEMS, 24 };

static void check_radio(HWND h, WORD id, BOOL on) {
    SendDlgItemMessageW(h, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static BOOL radio_group(HWND h, const WORD *ids, int n, int sel) {
    for (int i = 0; i < n; i++) check_radio(h, ids[i], i == sel);
    return TRUE;
}

static int radio_which(HWND h, const WORD *ids, int n) {
    for (int i = 0; i < n; i++)
        if (SendDlgItemMessageW(h, ids[i], BM_GETCHECK, 0, 0) == BST_CHECKED) return i;
    return 0;
}

/* radio triples */
static WORD RAD_TH[]  = { 1055, 1056, 1057 };   /* normal/medium/high */
static WORD RAD_AM[]  = { 1061, 1062, 1063 };   /* 2/5/10 s */
static WORD RAD_AS[]  = { 1065, 1066, 1067 };
static WORD RAD_SU[]  = { 1073, 1074 };         /* meter/inch */
static WORD RAD_EQ[]  = { 1077, 1078 };         /* preset A/B */

static INT_PTR CALLBACK prefs_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        radio_group(h, RAD_TH, 3, st_get_int(L"prefsThresholdTier", 1));
        radio_group(h, RAD_AM, 3, st_get_int(L"prefsAutoMarkSec", 1));
        radio_group(h, RAD_AS, 3, st_get_int(L"prefsAutoStopSec", 1));
        radio_group(h, RAD_SU, 2, st_get_int(L"prefsSurroundMeasure", 0));
        radio_group(h, RAD_EQ, 2, st_get_int(L"eqPreset", 0));
        int rad = st_get_int(L"prefsSurroundRadius", 350);
        if (rad < 100) rad = 100;
        if (rad > 1000) rad = 1000;
        HWND up = GetDlgItem(h, 1071);
        SendMessageW(up, UDM_SETRANGE, 0, MAKELPARAM(1000, 100));
        SendMessageW(up, UDM_SETBUDDY, (WPARAM)GetDlgItem(h, 1070), 0);
        SendMessageW(up, UDM_SETPOS, 0, MAKELPARAM(rad, 0));
        /* recording group mirrors the original: recording buffers are display
         * state in this build (no recorder yet) — grayed identically */
        // (nothing enabled yet to fake record config beyond what exists)
        return TRUE; }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)w; HWND ctl = (HWND)l;
        const Theme *t = theme_current();
        if (m == WM_CTLCOLORSTATIC) {
            SetTextColor(dc, t->text);
            SetBkColor(dc, t->bg2);
            return (INT_PTR)dlg_bg_brush(h);
        }
        if (m == WM_CTLCOLORBTN) {
            HBRUSH b = (HBRUSH)GetStockObject(NULL_BRUSH);
            return (INT_PTR)b;
        }
        if (m == WM_CTLCOLORLISTBOX) {
            SetTextColor(dc, t->text);
            SetBkColor(dc, t->bg2);
            return (INT_PTR)dlg_bg_brush(h);
        }
        SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 1055: case 1056: case 1057: radio_group(h, RAD_TH, 3, (int)LOWORD(w) - 1055); return TRUE;
        case 1061: case 1062: case 1063: radio_group(h, RAD_AM, 3, (int)LOWORD(w) - 1061); return TRUE;
        case 1065: case 1066: case 1067: radio_group(h, RAD_AS, 3, (int)LOWORD(w) - 1065); return TRUE;
        case 1073: case 1074:            radio_group(h, RAD_SU, 2, (int)LOWORD(w) - 1073); return TRUE;
        case 1077: case 1078:            radio_group(h, RAD_EQ, 2, (int)LOWORD(w) - 1077); return TRUE;
        case 1: {   /* OK: persist */
            st_set_int(L"prefsThresholdTier",  radio_which(h, RAD_TH, 3));
            st_set_int(L"prefsAutoMarkSec",    radio_which(h, RAD_AM, 3));
            st_set_int(L"prefsAutoStopSec",    radio_which(h, RAD_AS, 3));
            st_set_int(L"prefsSurroundMeasure",radio_which(h, RAD_SU, 2));
            st_set_int(L"eqPreset",            radio_which(h, RAD_EQ, 2));
            int rad = SendMessageW(GetDlgItem(h, 1071), UDM_GETPOS, 0, 0);
            st_set_int(L"prefsSurroundRadius", rad);
            EndDialog(h, 1); return TRUE; }
        case 2:
            EndDialog(h, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL prefs_show(HWND owner) {
    HGLOBAL tmpl = dialog_build(&PREFS_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, prefs_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---------------- Soundcard dialog: original template 418 ---------------- */
/* rect 311x119 DLU, DS bits 0x80c808c0, 15 items; ids verbatim. */

static const DlgItemDef SOUND_ITEMS[] = {
    { 1,     26,  98, 50, 14, 0x50010001, CLS_BUTTON, NULL, L"OK" },
    { 2,     83,  98, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
    { 1017,  19,  16,122, 150,0x50210003, CLS_COMBOBOX, NULL, L"" },
    { 1018,   7,   4,145, 87, 0x50000007, CLS_BUTTON, NULL, L"Playback" },
    { 1019, 103,  33, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 1020, 103,  52, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 1021,  20,  35, 74, 10, 0x5002020c, CLS_STATIC, NULL, L"Buffers (4...10):" },
    { 1022,  20,  55, 74, 10, 0x5002020c, CLS_STATIC, NULL, L"Buffer size (bytes):" },
    { 1023,  20,  75,118, 10, 0x50020000, CLS_STATIC, NULL, L"Delay:" },
    { 1024, 159,   4,145, 87, 0x50000007, CLS_BUTTON, NULL, L"Recording" },
    { 1025, 171,  16,122, 150,0x50210003, CLS_COMBOBOX, NULL, L"" },
    { 1026, 172,  35, 74, 10, 0x5002020c, CLS_STATIC, NULL, L"Buffers (4...10):" },
    { 1027, 172,  55, 74, 10, 0x50020200, CLS_STATIC, NULL, L"Buffer size (bytes):" },
    { 1028, 255,  33, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 1029, 255,  52, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
};
static const DlgDef SOUND_DEF = { 311, 119, 0x80c808c0, L"Soundcard", SOUND_ITEMS, 15 };

static void sound_fill_device_combo(HWND h, BOOL out, HWND box) {
    ComboBox_ResetContent(box);
    int idx = ComboBox_AddString(box, L"Default (system)");
    ComboBox_SetItemData(box, idx, (DWORD_PTR)0xFFFFFFFF);   /* WAVE_MAPPER */
    if (out) {
        UINT n = waveOutGetNumDevs();
        for (UINT i = 0; i < n; i++) {
            WAVEOUTCAPSW c; ZeroMemory(&c, sizeof(c));
            if (waveOutGetDevCapsW(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
            int ix = ComboBox_AddString(box, c.szPname);
            ComboBox_SetItemData(box, ix, (DWORD_PTR)i);
        }
    } else {
        UINT n = waveInGetNumDevs();
        for (UINT i = 0; i < n; i++) {
            WAVEINCAPSW c; ZeroMemory(&c, sizeof(c));
            if (waveInGetDevCapsW(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
            int ix = ComboBox_AddString(box, c.szPname);
            ComboBox_SetItemData(box, ix, (DWORD_PTR)i);
        }
    }
}

static int sound_find_device(HWND h, HWND box, int stored) {
    int n = ComboBox_GetCount(box);
    for (int i = 0; i < n; i++)
        if ((LPARAM)(int)ComboBox_GetItemData(box, i) == stored) return i;
    return 0;
}

static int clamp_buffers(HWND h, int id) {
    wchar_t t[32]; GetDlgItemTextW(h, id, t, 32);
    int v = _wtoi(t);
    if (v < 4) v = 4;
    if (v > 10) v = 10;
    _snwprintf(t, 31, L"%d", v); t[31] = 0;
    SetDlgItemTextW(h, id, t);
    return v;
}

static int clamp_bufsize(HWND h, int id) {
    wchar_t t[32]; GetDlgItemTextW(h, id, t, 32);
    int v = _wtoi(t);
    /* ring values step 16384, bounded 16k..128k like the original's byte fields */
    if (v < 16384) v = 16384;
    if (v > 131072) v = 131072;
    v = (v / 16384) * 16384;
    _snwprintf(t, 31, L"%d", v); t[31] = 0;
    SetDlgItemTextW(h, id, t);
    return v;
}

static void sound_update_delay(HWND h) {
    int rate = st_get_int(L"sndSampleRate", 44100);
    if (rate <= 0) rate = 44100;
    /* live values: the fields being edited, not the persisted ones */
    wchar_t t[32];
    GetDlgItemTextW(h, 1019, t, 32); int bufs = _wtoi(t); if (bufs < 4) bufs = 4; if (bufs > 10) bufs = 10;
    GetDlgItemTextW(h, 1020, t, 32); int size = _wtoi(t); if (size < 16384) size = 16384; if (size > 131072) size = 131072;
    /* effective latency = buffers * bufferBytes / (44100 * 4 bytes/frame) */
    double ms = (double)bufs * (double)size * 1000.0 / ((double)rate * 4.0);
    wchar_t tb[64]; _snwprintf(tb, 63, L"Delay: %d ms", (int)lround(ms)); tb[63] = 0;
    SetDlgItemTextW(h, 1023, tb);
}

static INT_PTR CALLBACK sound_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        sound_fill_device_combo(h, TRUE,  GetDlgItem(h, 1017));
        sound_fill_device_combo(h, FALSE, GetDlgItem(h, 1025));
        ComboBox_SetCurSel(GetDlgItem(h, 1017),
                           sound_find_device(h, GetDlgItem(h, 1017), st_get_int(L"idPlayDevice", -1)));
        ComboBox_SetCurSel(GetDlgItem(h, 1025),
                           sound_find_device(h, GetDlgItem(h, 1025), st_get_int(L"idRecDevice", -1)));
        wchar_t t[32];
        _snwprintf(t, 31, L"%d", st_get_int(L"sndPlayBufs", 4));     t[31]=0; SetDlgItemTextW(h, 1019, t);
        _snwprintf(t, 31, L"%d", st_get_int(L"sndPlayBufSize",16384));t[31]=0; SetDlgItemTextW(h, 1020, t);
        _snwprintf(t, 31, L"%d", st_get_int(L"sndRecBufs", 4));      t[31]=0; SetDlgItemTextW(h, 1028, t);
        _snwprintf(t, 31, L"%d", st_get_int(L"sndRecBufSize",16384));t[31]=0; SetDlgItemTextW(h, 1029, t);
        sound_update_delay(h);
        /* recording hardware is not implemented in this rebuild (feature-honest):
         * controls mirror the original's layout but stay disabled */
        EnableWindow(GetDlgItem(h, 1025), FALSE);
        EnableWindow(GetDlgItem(h, 1026), FALSE);
        EnableWindow(GetDlgItem(h, 1027), FALSE);
        EnableWindow(GetDlgItem(h, 1028), FALSE);
        EnableWindow(GetDlgItem(h, 1029), FALSE);
        return TRUE; }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        if (m == WM_CTLCOLORBTN) return (INT_PTR)GetStockObject(NULL_BRUSH);
        SetTextColor(dc, t->text);
        SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE && (LOWORD(w) == 1019 || LOWORD(w) == 1020)) sound_update_delay(h);
        switch (LOWORD(w)) {
        case 1: {
            int sel = ComboBox_GetCurSel(GetDlgItem(h, 1017));
            if (sel >= 0) st_set_int(L"idPlayDevice", (int)ComboBox_GetItemData(GetDlgItem(h, 1017), sel));
            st_set_int(L"sndPlayBufs",   clamp_buffers(h, 1019));
            st_set_int(L"sndPlayBufSize",clamp_bufsize(h, 1020));
            int selR = ComboBox_GetCurSel(GetDlgItem(h, 1025));
            if (selR >= 0) st_set_int(L"idRecDevice", (int)ComboBox_GetItemData(GetDlgItem(h, 1025), selR));
            st_set_int(L"sndRecBufs",    clamp_buffers(h, 1028));
            st_set_int(L"sndRecBufSize", clamp_bufsize(h, 1029));
            EndDialog(h, 1); return TRUE; }
        case 2:
            EndDialog(h, 0); return TRUE;
        }
        break;
    }
    (void)dpi_hdr;
    return FALSE;
}

BOOL soundcard_show(HWND owner) {
    HGLOBAL tmpl = dialog_build(&SOUND_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, sound_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---------------- module parameter dialogs (rebuild interface layer) ---------------- */
/* Sensitivity tiers mirror the original's module parameters (default 0.5 =
 * "Medium −30 dB"; decoded evidence in spec/dsp-parity.md §3. */

static const DlgItemDef DCLICK_ITEMS[] = {
    { 1,    141,  70, 50, 14, 0x50010001, CLS_BUTTON, NULL, L"&OK" },
    { 2,    197,  70, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
    { 201,    8,   6,178, 64, 0x50020007, CLS_BUTTON, NULL, L" Sensitivity " },
    { 202,   15,  20,124, 10, 0x50010004, CLS_BUTTON, NULL, L"High  (-20 dB)" },
    { 203,   15,  34,124, 10, 0x50010004, CLS_BUTTON, NULL, L"Medium  (-30 dB)" },
    { 204,   15,  48,124, 10, 0x50010004, CLS_BUTTON, NULL, L"Normal  (-40 dB)" },
    { 210,    8,  74, 76, 12, 0x50000007, CLS_BUTTON, NULL, L"Repair width (ms):" },
    { 211,   88,  74, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 212,  130,  76, 78,  9, 0x5002020c, CLS_STATIC, NULL, L"(2 ... 22.5)" },
};
static const DlgDef DCLICK_DEF = { 254, 92, 0x80c808c0, L"De-Click", DCLICK_ITEMS, 9 };

static WORD RAD_DC[] = { 202, 203, 204 };

static INT_PTR CALLBACK dclick_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        radio_group(h, RAD_DC, 3, st_get_int(L"declickTier", 1));
        wchar_t t[32];
        _snwprintf(t, 31, L"%d", st_get_int(L"declickWidthMs10", 112)); t[31]=0;
        SetDlgItemTextW(h, 211, t);
        return TRUE; }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        if (m == WM_CTLCOLORBTN) return (INT_PTR)GetStockObject(NULL_BRUSH);
        SetTextColor(dc, t->text);
        SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 202: case 203: case 204: radio_group(h, RAD_DC, 3, (int)LOWORD(w) - 202); return TRUE;
        case 1: {
            st_set_int(L"declickTier", radio_which(h, RAD_DC, 3));
            wchar_t t[32]; GetDlgItemTextW(h, 211, t, 32);
            int w10 = _wtoi(t);
            if (w10 < 20) w10 = 20;
            if (w10 > 225) w10 = 225;
            st_set_int(L"declickWidthMs10", w10);
            EndDialog(h, 1); return TRUE; }
        case 2:
            EndDialog(h, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL declick_params_show(HWND owner, float *sensitivity, float *widthMs) {
    HGLOBAL tmpl = dialog_build(&DCLICK_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, dclick_proc, 0);
    GlobalFree(tmpl);
    if (r) {
        static const float sensTiers[3] = { 1.0f, 0.5f, 0.0f };   /* High/Medium/Normal */
        int tier = st_get_int(L"declickTier", 1);
        *sensitivity = sensTiers[tier];
        *widthMs = st_get_int(L"declickWidthMs10", 112) / 10.0f;
    }
    return r != 0;
}

static const DlgItemDef DNOISE_ITEMS[] = {
    { 1,    141,  70, 50, 14, 0x50010001, CLS_BUTTON, NULL, L"&OK" },
    { 2,    197,  70, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
    { 301,    8,   6,178, 26, 0x50020007, CLS_BUTTON, NULL, L" Reduction " },
    { 302,   15,  14, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 303,   57,  16, 96,  9, 0x5002020c, CLS_STATIC, NULL, L"dB (0 ... 20)" },
    { 310,    8,  36,178, 26, 0x50020007, CLS_BUTTON, NULL, L" Noise print learn " },
    { 311,   15,  44, 38, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 312,   57,  46,120,  9, 0x5002020c, CLS_STATIC, NULL, L"% of first region (5 ... 50)" },
};
static const DlgDef DNOISE_DEF = { 254, 92, 0x80c808c0, L"De-Noiser", DNOISE_ITEMS, 8 };

static INT_PTR CALLBACK dnoise_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        wchar_t t[32];
        _snwprintf(t, 31, L"%d", st_get_int(L"denoiseDb", 20)); t[31]=0; SetDlgItemTextW(h, 302, t);
        _snwprintf(t, 31, L"%d", st_get_int(L"denoiseLearnPct", 10)); t[31]=0; SetDlgItemTextW(h, 311, t);
        return TRUE; }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        if (m == WM_CTLCOLORBTN) return (INT_PTR)GetStockObject(NULL_BRUSH);
        SetTextColor(dc, t->text);
        SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 1: {
            wchar_t t[32];
            GetDlgItemTextW(h, 302, t, 32);
            int db = _wtoi(t);
            if (db < 0) db = 0;
            if (db > 20) db = 20;
            st_set_int(L"denoiseDb", db);
            GetDlgItemTextW(h, 311, t, 32);
            int pct = _wtoi(t);
            if (pct < 5) pct = 5;
            if (pct > 50) pct = 50;
            st_set_int(L"denoiseLearnPct", pct);
            EndDialog(h, 1); return TRUE; }
        case 2:
            EndDialog(h, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL denoiser_params_show(HWND owner, int *reductionDb, int *learnPct) {
    HGLOBAL tmpl = dialog_build(&DNOISE_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, dnoise_proc, 0);
    GlobalFree(tmpl);
    if (r) {
        *reductionDb = st_get_int(L"denoiseDb", 20);
        *learnPct    = st_get_int(L"denoiseLearnPct", 10);
    }
    return r != 0;
}

/* ---------------- dialogs mined from spec/dialog-map.json (template inventory) ----------------
 * Control ids, rects (DLU) and styles are VERBATIM template measurements from
 * spec/dialog-map.json; titles follow the EN-disc text and are neutralized where
 * the template carried original branding ("Pinnacle Caudiok" -> "Caudiok",
 * ground rule 3 in README). Features whose execution the rebuild cannot yet do
 * (CD expansion, MP3 encode/decode) show their real dialogs but the converting
 * functions stay grayed — never faked (ground rule 4). JA geometry variants
 * (412/415/419/424/426/1041) are documented pairs in spec/layout-main.md, mapped
 * when i18n lands. */

/* ---- 410/413/423 family: tiny OK/Cancel confirm shells (186x95 xx2 / 137x45) ---- */

static const DlgItemDef CONFIRM_ITEMS[] = {
    { 1, 129,  7, 50, 14, 0x50010001, CLS_BUTTON, NULL, L"OK" },
    { 2, 129, 24, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
};
static const DlgDef CONFIRM_DEF = { 186, 95, 0x80c800c0, L"Dialog", CONFIRM_ITEMS, 2 };
static const DlgItemDef CONFIRM_MIN_ITEMS[] = {
    { 1, 57,  6, 50, 14, 0x40010001, CLS_BUTTON, NULL, L"OK" },
    { 2, 57, 23, 50, 14, 0x40000000, CLS_BUTTON, NULL, L"Cancel" },
};
static const DlgDef CONFIRM_MIN_DEF = { 137, 45, 0x80c000c0, L"Caudiok", CONFIRM_MIN_ITEMS, 2 };

static INT_PTR CALLBACK confirm_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        SetTextColor(dc, t->text); SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 1: EndDialog(h, 1); return TRUE;
        case 2: EndDialog(h, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL dlg_confirm_ok(HWND owner, const wchar_t *title, int variant) {
    DlgDef d = variant == 1 ? CONFIRM_MIN_DEF : CONFIRM_DEF;
    d.title = title;
    HGLOBAL tmpl = dialog_build(&d);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, confirm_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---- 414: "new project or open existing one" prompt (body = original string 3116;
 *      template's &Cancel button is petrified hidden verbatim, so Esc is not wired) ---- */

static const DlgItemDef PROJPROMPT_ITEMS[] = {
    { 1,    5, 64, 93, 16, 0x50010001, CLS_BUTTON, NULL, L"&New Project" },
    { 1012,104, 64, 93, 16, 0x50010000, CLS_BUTTON, NULL, L"&Open existing project" },
    { 1011, 9, 11,251, 45, 0x40020201, CLS_STATIC,  NULL, L"Do you want to create a new project or open an existing one?" },
    { 65535,4, 0,263, 63, 0x40000027, CLS_BUTTON,  NULL, L"" },
    { 2,  215, 64, 50, 16, 0x40010000, CLS_BUTTON,  NULL, L"&Cancel" },
};
static const DlgDef PROJPROMPT_DEF = { 270, 90, 0x80c008c0, L"Caudiok", PROJPROMPT_ITEMS, 5 };

static INT_PTR CALLBACK projprompt_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        SetTextColor(dc, t->text); SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case 1: EndDialog(h, 1); return TRUE;      /* new */
        case 1012: EndDialog(h, 2); return TRUE;   /* open existing */
        case 2: EndDialog(h, 0); return TRUE;
        }
        break;
    }
    return FALSE;
}

int dlg_project_prompt(HWND owner) {
    HGLOBAL tmpl = dialog_build(&PROJPROMPT_DEF);
    if (!tmpl) return 0;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, projprompt_proc, 0);
    GlobalFree(tmpl);
    return (int)r;   /* 0 cancel, 1 new, 2 open-existing */
}

/* ---- 416: empty 332x213 module shell (0 items by template — framework placeholder) ---- */

static const DlgDef BLANK_DEF = { 332, 213, 0x90000840, L"", NULL, 0 };

static INT_PTR CALLBACK blank_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CTLCOLORDLG: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND:
        if (LOWORD(w) == 2) { EndDialog(h, 0); return TRUE; }
        break;
    default:
        break;
    }
    (void)l;
    return FALSE;
}

BOOL dlg_blankpanel_show(HWND owner) {
    HGLOBAL tmpl = dialog_build(&BLANK_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, blank_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---- helpers shared by the import/export dialogs ---- */

static HWND list_from_def(HWND h, WORD id) { return GetDlgItem(h, id); }

static void list_add_file(HWND lv, const wchar_t *name) {
    LVITEMW it; ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT; it.iItem = 0x7fffffff; it.pszText = (LPWSTR)name;
    ListView_InsertItem(lv, &it);
}

static BOOL pick_files(HWND owner, const wchar_t *filter, wchar_t *out /* MAX_PATH*n */) {
    /* filters arrive pipe-separated ("...|*.mp3|All|*.*||"); OPENFILENAME needs
     * NUL-separated pairs — convert in place (same scheme as doc_open/main.c) */
    wchar_t rf[512]; int o = 0;
    for (const wchar_t *p = filter; *p && o < 510; ++p) rf[o++] = (*p == L'|') ? 0 : *p;
    rf[o] = 0; rf[o + 1] = 0;
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFile = out; ofn.nMaxFile = 8192;
    ofn.lpstrFilter = rf;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
    return GetOpenFileNameW(&ofn) != FALSE;
}

static BOOL pick_folder(HWND owner, wchar_t *path) {
    BROWSEINFOW bi; ZeroMemory(&bi, sizeof(bi));
    wchar_t disp[MAX_PATH]; disp[0] = 0;
    bi.hwndOwner = owner; bi.lpszTitle = L"Select destination directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;   /* no COM init dependency */
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return FALSE;
    BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok && path[0];
}

/* ---- 411: "Importing Audio tracks from CD"
 *      original's own honest no-ASPI status text (string 3128) shown in static 1007;
 *      Save/Update stay disabled — there is no CD read path in this rebuild ---- */

static const DlgItemDef CDIMPORT_ITEMS[] = {
    { 1,    176, 216, 59, 14, 0x50010001, CLS_BUTTON, NULL, L"&Save" },
    { 2,    176, 234, 59, 14, 0x50010000, CLS_BUTTON, NULL, L"&Cancel" },
    { 1000,   7,   7,228, 60, 0x50210003, CLS_COMBOBOX, NULL, L"" },
    { 1002,   7,  27,228,149, 0x50818009, 0, L"SysListView32", L"List1" },
    { 1003,   7, 200,228, 10, 0x50000000, 0, L"msctls_progress32", L"Progress1" },
    { 1005,   7, 235,140, 12, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 1006, 151, 235, 16, 12, 0x50010000, CLS_BUTTON, NULL, L"..." },
    { 1007,   7, 216,159, 12, 0x5002128c, CLS_STATIC, NULL, L"" },
    { 1008,   7, 183,166, 11, 0x50010d03, CLS_BUTTON, NULL, L"Add to Project" },
    { 1013, 185, 180, 50, 14, 0x50010000, CLS_BUTTON, NULL, L"&Update" },
};
static const DlgDef CDIMPORT_DEF = { 241, 253, 0x84c008c0, L"Importing Audio tracks from CD", CDIMPORT_ITEMS, 10 };

static void cd_fill_drives(HWND h) {
    HWND cb = list_from_def(h, 1000);
    wchar_t v = L'A';
    for (int i = 0; i < 26; i++, v++) {
        wchar_t drv[4] = { v, L':', L'\\', 0 };
        if (GetDriveTypeW(drv) != DRIVE_CDROM) continue;
        int ix = ComboBox_AddString(cb, drv);
        ComboBox_SetItemData(cb, ix, (DWORD_PTR)v);
    }
    if (ComboBox_GetCount(cb) == 0) {
        ComboBox_AddString(cb, L"(no CD drive)");
    }
    ComboBox_SetCurSel(cb, 0);
}

static INT_PTR CALLBACK cdimport_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    switch (m) {
    case WM_INITDIALOG: {
        cd_fill_drives(h);
        /* original's own message when no ASPI layer is present (string 3128) —
         * this rebuild targets that same reality (no CD read path) */
        SetDlgItemTextW(h, 1007,
            L"Can't find 'Wnaspi32.dll'...    Importing tracks from CD is not possible!");
        wchar_t dir[MAX_PATH]; dir[0] = 0;
        DWORD cb = sizeof(dir)*2 - 2, t;
        if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"cdDestDir", RRF_RT_REG_SZ, &t, dir, &cb)
                == ERROR_SUCCESS && dir[0])
            SetDlgItemTextW(h, 1005, dir);
        /* CD read path absent in this rebuild (same reality as the original without
         * wnaspi32): Save + Update stay disabled, matching the status above */
        EnableWindow(GetDlgItem(h, 1), FALSE);
        EnableWindow(GetDlgItem(h, 1013), FALSE);
        return TRUE; }
    case WM_NOTIFY:
        return FALSE;   /* list is display-only here */
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        SetTextColor(dc, t->text); SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 1006) {
            wchar_t dir[MAX_PATH]; dir[0] = 0;
            if (pick_folder(h, dir)) {
                SetDlgItemTextW(h, 1005, dir);
                HKEY kk; if (RegCreateKeyW(HKEY_CURRENT_USER, REGKEY, &kk) == ERROR_SUCCESS)
                    { RegSetValueExW(kk, L"cdDestDir", 0, REG_SZ, (const BYTE*)dir, (lstrlenW(dir)+1)*2); RegCloseKey(kk); }
            }
            return TRUE;
        }
        switch (id) {
        case 2: EndDialog(h, 0); return TRUE;
        case 1: EndDialog(h, 1); return TRUE; }
        break; }
    }
    return FALSE;
}

BOOL dlg_importcd_show(HWND owner) {
    HGLOBAL tmpl = dialog_build(&CDIMPORT_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, cdimport_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---- 422: "Importing MPEG1-Layer3-Files"
 *      file list fills from a real MP3 multi-file pick; Convert/add-to-project
 *      stay grayed until an MP3 decode lands (decision D1, see spec/pending.md) ---- */

static const DlgItemDef MP3IMPORT_ITEMS[] = {
    { 1,     6, 207, 55, 14, 0x50010001, CLS_BUTTON, NULL, L"Konvertieren" },
    { 2,   159, 207, 55, 14, 0x50010000, CLS_BUTTON, NULL, L"Abbruch" },
    { 1030,  6,   6,231,151, 0x50818009, 0, L"SysListView32", L"List1" },
    { 1031,  8, 162,110, 10, 0x50010003, CLS_BUTTON, NULL, L"Add to project" },
    { 1036,  8, 176, 95, 10, 0x50020200, CLS_STATIC, NULL, L"Destination Directory" },
    { 1032,  6, 187,210, 14, 0x58810080, CLS_EDIT,   NULL, L"" },
    { 1033, 219, 187, 18, 14, 0x50010000, CLS_BUTTON, NULL, L"..." },
    { 1034,182, 161, 55, 14, 0x50010000, CLS_BUTTON, NULL, L"MP3-Dateien" },
};
static const DlgDef MP3IMPORT_DEF = { 243, 226, 0x86c40840, L"Importing MPEG1-Layer3-Files", MP3IMPORT_ITEMS, 8 };

static void mp3import_set_dest(HWND h) {
    wchar_t dir[MAX_PATH]; DWORD cb = sizeof(dir)*2 - 2, t;
    if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"mp3ImportDir", RRF_RT_REG_SZ, &t, dir, &cb)
            == ERROR_SUCCESS && dir[0])
        SetDlgItemTextW(h, 1032, dir);
}

static void persist_modpath(HWND h) {
    wchar_t dir[MAX_PATH];
    if (!GetDlgItemTextW(h, 1032, dir, MAX_PATH)) return;
    if (!dir[0]) return;
    HKEY kk; if (RegCreateKeyW(HKEY_CURRENT_USER, REGKEY, &kk) == ERROR_SUCCESS) {
        RegSetValueExW(kk, L"mp3ImportDir", 0, REG_SZ, (const BYTE*)dir, (lstrlenW(dir)+1)*2);
        RegCloseKey(kk);
    }
}

static INT_PTR CALLBACK mp3import_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    switch (m) {
    case WM_INITDIALOG: {
        mp3import_set_dest(h);
        EnableWindow(GetDlgItem(h, 1), FALSE);
        EnableWindow(GetDlgItem(h, 1031), FALSE);
        return TRUE; }
    case WM_DESTROY:
        persist_modpath(h);
        return FALSE;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        SetTextColor(dc, t->text); SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 1034) {
            wchar_t buf[8192]; buf[0] = 0;
            if (pick_files(h, L"MPEG1-Layer3 files|*.mp3|All Files|*.*||", buf)) {
                HWND lv = list_from_def(h, 1030);
                wchar_t *dir = buf, *p = buf + lstrlenW(buf) + 1;
                int added = 0;
                while (*p) {
                    wchar_t full[MAX_PATH + 4]; _snwprintf(full, MAX_PATH + 3, L"%s\\%s", dir, p); full[MAX_PATH + 3] = 0;
                    list_add_file(lv, full);
                    added++;
                    p += lstrlenW(p) + 1;
                }
                if (!added) list_add_file(lv, dir);   /* single-file pick: dir IS the file */
            }
            return TRUE;
        }
        if (id == 1033) {
            wchar_t dir[MAX_PATH]; dir[0] = 0;
            if (pick_folder(h, dir)) SetDlgItemTextW(h, 1032, dir);
            return TRUE;
        }
        if (id == 2) { EndDialog(h, 0); return TRUE; }
        if (id == 1) { EndDialog(h, 1); return TRUE; }
        break; }
    }
    return FALSE;
}

BOOL dlg_mp3import_show(HWND owner) {
    HGLOBAL tmpl = dialog_build(&MP3IMPORT_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner, mp3import_proc, 0);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---- 1037: "Export as MP3 file" (original title 3192; template geometry 356x222).
 *      Options are real and persisted; Convert stays grayed until a LAME-derived
 *      encoder lands (D1(a) pending). Disabled quality radio semantics: template
 *      has BN_AUTORADIOBUTTON? — specs: ids 1049/1050/1051 verbatim checked-group. ---- */

static const DlgItemDef MP3EXPORT_ITEMS[] = {
    { 1038,  7,   9,249,149, 0x50818009, 0, L"SysListView32", L"List1" },
    { 65535,263,   5, 86,153, 0x50000007, CLS_BUTTON, NULL, L"" },
    { 1046,271,  42, 70, 10, 0x50010003, CLS_BUTTON, NULL, L"CRC" },
    { 1047,271,  55, 70, 10, 0x50010003, CLS_BUTTON, NULL, L"Copyright" },
    { 1048,271,  68, 70, 10, 0x50010003, CLS_BUTTON, NULL, L"Original" },
    { 1049,271, 115, 70, 10, 0x50010009, CLS_BUTTON, NULL, L"Normal Quality" },
    { 1050,271, 128, 70, 10, 0x50010009, CLS_BUTTON, NULL, L"Better Quality" },
    { 1051,271, 141, 70, 10, 0x50010009, CLS_BUTTON, NULL, L"Highest Quality" },
    { 1040,  7, 164,249, 11, 0x50000000, 0, L"msctls_progress32", L"Progress1" },
    { 1043,  7, 181,318, 14, 0x50810080, CLS_EDIT,   NULL, L"" },
    { 1044,331, 181, 17, 14, 0x50010000, CLS_BUTTON, NULL, L"..." },
    { 1039,  7, 201,213,117, 0x50210003, CLS_COMBOBOX, NULL, L"" },
    { 1,   230, 201, 55, 14, 0x50010001, CLS_BUTTON, NULL, L"Convert" },
    { 2,   293, 201, 55, 14, 0x50010000, CLS_BUTTON, NULL, L"Cancel" },
    { 1052,271,  81, 70, 10, 0x50010003, CLS_BUTTON, NULL, L"Intensity" },
    { 1053,271, 101, 70, 12, 0x5002020c, CLS_STATIC, NULL, L"Quality/Speed" },
    { 1054,264, 164, 85, 12, 0x50020201, CLS_STATIC, NULL, L"0:00:00" },
    { 1079,269,  16, 74,100, 0x50210003, CLS_COMBOBOX, NULL, L"" },
};
static const DlgDef MP3EXPORT_DEF = { 356, 222, 0x84c008c0, L"Export as MP3 file", MP3EXPORT_ITEMS, 18 };

static WORD RAD_MP3Q[] = { 1049, 1050, 1051 };
static const int MP3_RATES[] = { 32,40,48,56,64,80,96,112,128,160,192,224,256,320 };

static void mp3_export_persist(HWND h) {
    st_set_int(L"mp3Crc",      SendDlgItemMessageW(h, 1046, BM_GETCHECK, 0, 0) == BST_CHECKED);
    st_set_int(L"mp3Copyright",SendDlgItemMessageW(h, 1047, BM_GETCHECK, 0, 0) == BST_CHECKED);
    st_set_int(L"mp3Original", SendDlgItemMessageW(h, 1048, BM_GETCHECK, 0, 0) == BST_CHECKED);
    st_set_int(L"mp3Intensity",SendDlgItemMessageW(h, 1052, BM_GETCHECK, 0, 0) == BST_CHECKED);
    st_set_int(L"mp3QualitySel", radio_which(h, RAD_MP3Q, 3));
    int cur = ComboBox_GetCurSel(GetDlgItem(h, 1079));
    if (cur >= 0 && cur < 14) st_set_int(L"mp3Bitrate", MP3_RATES[cur]);
    wchar_t path[MAX_PATH];
    if (GetDlgItemTextW(h, 1043, path, MAX_PATH) && path[0]) {
        HKEY kk; if (RegCreateKeyW(HKEY_CURRENT_USER, REGKEY, &kk) == ERROR_SUCCESS) {
            RegSetValueExW(kk, L"mp3Dest", 0, REG_SZ, (const BYTE*)path, (lstrlenW(path)+1)*2);
            RegCloseKey(kk);
        }
    }
}

static INT_PTR CALLBACK mp3export_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    (void)l;
    switch (m) {
    case WM_INITDIALOG: {
        /* bitrate combo: standard MPEG1-Layer3 set */
        HWND bc = GetDlgItem(h, 1079);
        for (int i = 0; i < 14; i++) {
            wchar_t t[16]; _snwprintf(t, 15, L"%d kBit/s", MP3_RATES[i]); t[15] = 0;
            int ix = ComboBox_AddString(bc, t);
            ComboBox_SetItemData(bc, ix, (DWORD_PTR)MP3_RATES[i]);
        }
        int rate = st_get_int(L"mp3Bitrate", 192);
        int cur = 7;
        for (int i = 0; i < 14; i++) if (MP3_RATES[i] == rate) cur = i;
        ComboBox_SetCurSel(bc, cur);
        SendDlgItemMessageW(h, 1046, BM_SETCHECK, st_get_int(L"mp3Crc", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(h, 1047, BM_SETCHECK, st_get_int(L"mp3Copyright", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(h, 1048, BM_SETCHECK, st_get_int(L"mp3Original", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(h, 1052, BM_SETCHECK, st_get_int(L"mp3Intensity", 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        radio_group(h, RAD_MP3Q, 3, st_get_int(L"mp3QualitySel", 2));
        wchar_t path[MAX_PATH]; DWORD cb = sizeof(path)*2 - 2, t;
        if (RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"mp3Dest", RRF_RT_REG_SZ, &t, path, &cb)
                == ERROR_SUCCESS && path[0]) {
            SetDlgItemTextW(h, 1043, path);
            ComboBox_AddString(GetDlgItem(h, 1039), path);
            ComboBox_SetCurSel(GetDlgItem(h, 1039), 0);
        }
        else if (l) {                            /* caller-provided default: doc path */
            const wchar_t *src = (const wchar_t *)l;
            SetDlgItemTextW(h, 1043, src);
            ComboBox_AddString(GetDlgItem(h, 1039), (LPWSTR)src);
            ComboBox_SetCurSel(GetDlgItem(h, 1039), 0);
        }
        /* encoder pending (D1/a): convert + progress + length stay honest grayed */
        EnableWindow(GetDlgItem(h, 1), FALSE);
        EnableWindow(GetDlgItem(h, 1040), FALSE);
        EnableWindow(GetDlgItem(h, 1038), FALSE);
        return TRUE; }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        const Theme *t = theme_current();
        if (m == WM_CTLCOLORBTN) return (INT_PTR)dlg_bg_brush(h);
        SetTextColor(dc, t->text); SetBkColor(dc, t->bg2);
        return (INT_PTR)dlg_bg_brush(h); }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 1044) {
            wchar_t path[MAX_PATH]; GetDlgItemTextW(h, 1043, path, MAX_PATH);
            wchar_t filter[] = L"MPEG1-Layer3 files|*.mp3|All Files|*.*||";
            wchar_t rf[64]; int o = 0;
            for (wchar_t *p = filter; *p; ++p) rf[o++] = (*p == L'|') ? 0 : *p;
            rf[o] = 0; rf[++o] = 0;
            OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn)); path[MAX_PATH-1] = 0;
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = h;
            ofn.lpstrFile = path; ofn.nMaxFile = MAX_PATH; ofn.lpstrFilter = rf;
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(h, 1043, path);
                HWND cb = GetDlgItem(h, 1039);
                int ix = ComboBox_FindStringExact(cb, 0, path);
                if (ix < 0) { ix = ComboBox_AddString(cb, path); }
                ComboBox_SetCurSel(cb, ix);
                mp3_export_persist(h);
            }
            return TRUE;
        }
        switch (id) {
        case 1046: case 1047: case 1048: case 1052:
            mp3_export_persist(h); return TRUE;
        case 1049: case 1050: case 1051:
            radio_group(h, RAD_MP3Q, 3, id - 1049); mp3_export_persist(h); return TRUE;
        case 1: EndDialog(h, 1); return TRUE;
        case 2: EndDialog(h, 0); return TRUE;
        }
        break; }
    }
    return FALSE;
}

BOOL dlg_mp3export_show(HWND owner, const wchar_t *sourcePath) {
    /* sourcePath (doc path) pre-fills destination 1043 as lParam when no
     * destination has been stored yet */
    HGLOBAL tmpl = dialog_build(&MP3EXPORT_DEF);
    if (!tmpl) return FALSE;
    INT_PTR r = DialogBoxIndirectParamW(GetModuleHandleW(NULL), tmpl, owner,
                                        mp3export_proc, (LPARAM)sourcePath);
    GlobalFree(tmpl);
    return r != 0;
}

/* ---------------- sound getters for audio.c ---------------- */

int snd_play_dev(void)     { return st_get_int(L"idPlayDevice", -1); }
int snd_play_bufs(void)    { int v = st_get_int(L"sndPlayBufs", 4);    if (v < 4) v = 4;  if (v > 10) v = 10;       return v; }
int snd_play_bufsize(void) { int v = st_get_int(L"sndPlayBufSize", 16384);
                             if (v < 16384) v = 16384; if (v > 131072) v = 131072;
                             v = (v / 16384) * 16384; return v; }
