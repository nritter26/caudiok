/* main.c — Caudiok v0.1 shell: window, menus, waveform view, theme toggle, play
 * Original-behavior basis: Caudiok restoration host; see spec/ + README milestones.
 * Original keymaps were programmatic (no ACCELERATOR resource); menu accelerators
 * here are provisional until Phase-A decompile documents the dispatch (README #6). */
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include "theme.h"
#include "aux.h"
#include "waveio.h"
#include "audio.h"
#include "dialogs.h"
#include "dsp/dsp.h"
#include <stdio.h>
#include <stdlib.h>

#define IDM_OPEN     1001
#define IDM_SAVE_AS  1002
#define IDM_EXIT     1003
#define IDM_TOGGLE_THEME 1004
#define IDM_ABOUT    1005
#define IDM_PLAY_SPACE 1007
#define IDM_STOP_SPACE 1008
/* original Process module command ids (0x44C-region, provisional pending dispatch) */
#define IDM_PROC_DECLICK    1101
#define IDM_PROC_DENOISER   1102
#define IDM_PROC_ENHANCER   1103   /* false feature in this rebuild: stays grayed */
#define IDM_PROC_CALIBRATE  1104
#define IDM_PREFS           1105
#define IDM_SND             1106
/* File/project + import/export dialogs (original menu labels from
 * ui-strings-en.txt ids 3090/3102/3152/3191) */
#define IDM_NEW_PROJECT     1107
#define IDM_IMPORT_CD       1108
#define IDM_IMPORT_MP3      1109
#define IDM_EXPORT_MP3      1110
/* console rack band: original hosts the module rack in the doc-view frame; the
 * rebuild docks a plain bottom band under the waveform (spec/layout-main.md) */
#define CONSOLE_BAND_H 92
#define WM_APP_DONE (WM_USER+7)

static WaveDoc *g_doc = NULL;
static Peaks  *g_pk  = NULL;
static AudioPlayer *g_ap = NULL;
static int32_t   g_cursorFrame = -1;
static double    g_zoom = 1.0;         /* future; v0.1 full-view */
static int       g_scrollPos = 0;      /* future */

#define CLSNAME L"CaudiokMain"

static void app_invalidate(HWND h) { InvalidateRect(h, NULL, TRUE); }

static void set_proc_menu(HWND h);   /* forward: defined with the process handlers */

static void on_pos_update(void *ctx, int32_t frame) { (void)ctx;(void)frame; /* cheap repaint throttling later */ }

static void doc_close(HWND h) {
    if (g_ap) audio_stop(g_ap);
    if (g_pk) { peaks_free(g_pk); g_pk=NULL; }
    if (g_doc){ wave_free(g_doc); g_doc=NULL; }
    g_cursorFrame=-1;
    SetWindowTextW(h, L"Caudiok");
    set_proc_menu(h);
    app_invalidate(h);
}

static void doc_open(HWND h) {
    wchar_t file[MAX_PATH]; file[0]=0;
    wchar_t filter[] = L"WAV files|*.wav|All Files|*.*||";
    wchar_t real_filter[MAX_PATH];
    int o=0;
    for (wchar_t *p=filter; *p; ++p) real_filter[o++] = (*p==L'|')? 0 : *p;
    real_filter[o]=0; real_filter[o+1]=0; (void)o;
    OPENFILENAMEW ofn; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=h;
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH;
    ofn.lpstrFilter=real_filter;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    WaveDoc *nd=NULL;
    WaveErr e=wave_load(file,&nd);
    if (e!=WAVE_OK) {
        const wchar_t *m = e==WAVE_ENOENT? L"Could not open source file."
                         : e==WAVE_EMBEDFMT? L"The file MUST be: 44.1 kHz, 16 bit! (or convertible PCM)"
                         : e==WAVE_ENOMEM?  L"There's not enough memory to complete this operation."
                         : L"Could not open source file.";
        fail_msg(L"Caudiok", L"%s", m);
        return;
    }
    doc_close(h);
    g_doc=nd;
    wchar_t title[MAX_PATH+32];
    _snwprintf(title, MAX_PATH+31, L"Caudiok — %s", nd->path);
    title[MAX_PATH+31]=0;
    SetWindowTextW(h,title);
    set_proc_menu(h);
    app_invalidate(h);
}

/* New Project (original 3090) — that choice point is an exact template 414:
 * the rebuild has no CPF project system yet, so "new" starts an empty workspace
 * and "open existing" provisionally maps onto the WAV-open flow (README #9). */
static void file_new_project(HWND h) {
    int r = dlg_project_prompt(h);
    if (r == 1) { doc_close(h); }
    else if (r == 2) { doc_open(h); }   /* works with no document open too */
}

static void doc_save_as(HWND h) {
    if (!g_doc) return;
    wchar_t file[MAX_PATH]; file[0]=0;
    wchar_t filter[] = L"WAV files|*.wav|All Files|*.*||";
    wchar_t rf[MAX_PATH]; int o=0;
    for (wchar_t *p=filter; *p; ++p) rf[o++] = (*p==L'|')? 0 : *p;
    rf[o]=0; rf[++o]=0; (void)o;
    OPENFILENAMEW ofn; ZeroMemory(&ofn,sizeof(ofn));
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=h;
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.lpstrFilter=rf;
    ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    WaveErr e=wave_save(g_doc,file);
    if (e!=WAVE_OK) fail_msg(L"Caudiok", L"Could not open destination file.");
}

static void play_ctl(HWND h, int start) {
    if (!g_doc) return;
    if (start) {
        int32_t f = g_cursorFrame<0? 0 : g_cursorFrame;
        audio_seek_and_play(g_ap,g_doc,f);
    } else {
        audio_stop(g_ap);
        g_cursorFrame=audio_cursor_frame(g_ap);
        app_invalidate(h);
    }
}

static int32_t frame_from_x(HWND h, int x) {
    RECT r; GetClientRect(h,&r);
    if (r.right<1) return 0;
    int32_t cols = r.right;
    int32_t col = x;
    if (!g_doc) return -1;
    size_t frames = g_doc->pcmBytes/4;
    return (int32_t)((int64_t)frames * col / cols);
}

/* waveform area: full client width, minus the docked console band */
static RECT wave_rect(HWND h) {
    RECT r; GetClientRect(h,&r);
    r.bottom -= CONSOLE_BAND_H;          /* band rides at the bottom edge */
    if (r.bottom < 2) r.bottom = 2;
    return r;
}

static void paint_main(HWND h, HDC hdc) {
    const Theme *th=theme_current();
    RECT r=wave_rect(h);
    HBRUSH bg=CreateSolidBrush(th->bg);
    FillRect(hdc,&r,bg); DeleteObject(bg);
    HPEN grid=CreatePen(PS_SOLID,1,th->grid);
    HGDIOBJ og=SelectObject(hdc,grid);
    MoveToEx(hdc,0,r.bottom/2,NULL); LineTo(hdc,r.right,r.bottom/2);
    for (int x=0;x<r.right;x+=64) { MoveToEx(hdc,x,0,NULL); LineTo(hdc,x,r.bottom); }
    SelectObject(hdc,og); DeleteObject(grid);
    if (g_doc) {
        /* lazy peak cache (original precomputed .pk files; whole-file scan here);
         * rebuild when the client width changes so the waveform scale stays honest */
        int cols = r.right > 0 ? r.right : 800;
        if (!g_pk || g_pk->cols != cols) {
            if (g_pk) { peaks_free(g_pk); g_pk = NULL; }
            g_pk = peaks_build(g_doc, cols);
        }
    }
    if (!g_pk) {
        SetTextColor(hdc,th->textDim);
        SetBkMode(hdc,TRANSPARENT);
        HGDIOBJ of=(HGDIOBJ)SelectObject(hdc,GetStockObject(DEFAULT_GUI_FONT));
        const wchar_t *msg = L"open a WAV via File menu — space plays/stops — Ctrl+Alt+T theme (rebuild-only addition)";
        TextOutW(hdc,16,24,msg,(int)wcslen(msg));
        SelectObject(hdc,of);
        return;
    }
    /* waveform peaks (lower half of the wave area, centered) */
    RECT wr={0,r.bottom/2,r.right,r.bottom};
    HPEN wm=(HPEN)CreatePen(PS_SOLID,1,th->wave);
    HPEN wp=(HPEN)CreatePen(PS_SOLID,1,th->wavePeak);
    SelectObject(hdc,wp);
    int mid=wr.bottom/2+wr.top;
    for (int x=0;x<wr.right && x<g_pk->cols;x++) {
        int32_t mnv=g_pk->mins[x], mxv=g_pk->maxs[x];
        int top=mid-(*(&mxv)) * (wr.bottom-wr.top)/2/SHRT_MAX;
        int bot=mid-(*(&mnv)) * (wr.bottom-wr.top)/2/SHRT_MAX;
        MoveToEx(hdc,x,top,NULL); LineTo(hdc,x,bot);
    }
    SelectObject(hdc,wm);
    DeleteObject(wp); DeleteObject(wm);
    /* cursor: spans the wave area only */
    if (g_cursorFrame>=0 && g_doc) {
        size_t frames=g_doc->pcmBytes/4;
        int cx=(int)((int64_t)g_cursorFrame*r.right/(int64_t)(frames?frames:1));
        HPEN cur=(HPEN)CreatePen(PS_SOLID,1,th->waveCursor);
        HGDIOBJ oc=SelectObject(hdc,cur);
        MoveToEx(hdc,cx,0,NULL); LineTo(hdc,cx,r.bottom);
        SelectObject(hdc,oc); DeleteObject(cur);
    }
    /* docked console band background */
    RECT cf; GetClientRect(h,&cf);
    RECT band={0,r.bottom,cf.right,cf.bottom};
    HBRUSH bb=CreateSolidBrush(th->bg2);
    FillRect(hdc,&band,bb); DeleteObject(bb);
    SetTextColor(hdc,th->textDim);
    SetBkMode(hdc,TRANSPARENT);
    HGDIOBJ of=(HGDIOBJ)SelectObject(hdc,GetStockObject(DEFAULT_GUI_FONT));
    TextOutW(hdc,12,band.top+8,L"CONSOLE",(int)wcslen(L"CONSOLE"));
    SelectObject(hdc,of);
}

static void about(HWND h) {
    MessageBoxW(h,
      L"Caudiok — learning re-implementation of a 2000s restoration host.\n\n"
      L"No original code or assets. v0.2: shell, WAV open/save, waveform, playback,\n"
      L"De-Click, De-Noiser, calibration tone generator, light/dark.\n"
      L"Phase-A analysis artifacts & build steps: see README.",
      L"About Caudiok",MB_OK|MB_ICONINFORMATION);
}

/* ---- Process handlers (original module names from extracted string ids 3257/3259) ---- */

static void set_proc_menu(HWND h) {
    HMENU m = GetMenu(h);
    int on = g_doc ? MF_ENABLED : MF_GRAYED;
    if (m) {
        EnableMenuItem(m, IDM_PROC_DECLICK,  MF_BYCOMMAND | on);
        EnableMenuItem(m, IDM_PROC_DENOISER, MF_BYCOMMAND | on);
        /* Enhancer is not implemented in this rebuild: stays grayed, visible (honest placeholder) */
        EnableMenuItem(m, IDM_PROC_ENHANCER, MF_BYCOMMAND | MF_GRAYED);
        /* Export-as-MP3 dialog needs audio (dialog itself honors no-encoder state) */
        EnableMenuItem(m, IDM_EXPORT_MP3, MF_BYCOMMAND | on);
    }
    /* the console rack mirrors the menu state on its module buttons */
    HWND b;
    if ((b=GetDlgItem(h, IDM_PROC_DECLICK)))  EnableWindow(b, g_doc!=NULL);
    if ((b=GetDlgItem(h, IDM_PROC_DENOISER))) EnableWindow(b, g_doc!=NULL);
    if ((b=GetDlgItem(h, IDM_PROC_ENHANCER))) EnableWindow(b, FALSE);   /* honest: never enabled */
}

static void show_proc_report(HWND h, const wchar_t *title, const ProcReport *rep) {
    wchar_t buf[512];
    _snwprintf(buf, 511,
               L"%s complete\n\n"
               L"Clicks repaired: %d\n"
               L"Crackles repaired: %d\n"
               L"Frames processed: %d\n",
               title, rep->clicksRepaired, rep->cracklesRepaired, rep->framesProcessed);
    buf[511] = 0;
    MessageBoxW(h, buf, title, MB_OK|MB_ICONINFORMATION);
}

/* persisted module parameters (defaults = original constructor values:
 * De-Click "Medium −30 dB" = 0.5, repair width 11.2 ms = half of the original's
 * 992-sample template; De-Noiser −20 dB reduction, 10 % learn — spec/dsp-parity.md) */

static const float DECLICK_SENS[3] = { 1.0f, 0.5f, 0.0f };   /* High/Medium/Normal */

static void proc_declick_run(HWND h, float sens, float widthMs) {
    if (!g_doc) return;
    audio_stop(g_ap);
    ProcReport rep; ZeroMemory(&rep, sizeof(rep));
    int rc = dsp_declick((int16_t*)g_doc->pcmData, g_doc->pcmBytes/4, sens, widthMs, &rep);
    if (rc != 0) { fail_msg(L"De-Click", L"There's not enough memory to complete this operation."); return; }
    show_proc_report(h, L"De-Click", &rep);
    if (g_pk) { peaks_free(g_pk); g_pk = NULL; }
    app_invalidate(h);
}

static void proc_declick(HWND h) {
    int tier = st_get_int(L"declickTier", 1);
    if (tier < 0) tier = 0; if (tier > 2) tier = 2;
    float w = st_get_int(L"declickWidthMs10", 112) / 10.0f;   /* resolution 0.1 ms */
    if (w < 2.0f) w = 2.0f; if (w > 22.5f) w = 22.5f;
    proc_declick_run(h, DECLICK_SENS[tier], w);
}

static void proc_denoiser_run(HWND h, int db, float learnRatio) {
    if (!g_doc) return;
    audio_stop(g_ap);
    ProcReport rep; ZeroMemory(&rep, sizeof(rep));
    /* noise print is learned from the first learnRatio fraction of the file */
    int rc = dsp_denoise((int16_t*)g_doc->pcmData, g_doc->pcmBytes/4, g_doc->sampleRate, db, learnRatio, &rep);
    if (rc != 0) { fail_msg(L"De-Noiser", L"There's not enough memory to complete this operation."); return; }
    show_proc_report(h, L"De-Noiser", &rep);
    if (g_pk) { peaks_free(g_pk); g_pk = NULL; }
    app_invalidate(h);
}

static void proc_denoiser(HWND h) {
    int db = st_get_int(L"denoiseDb", 20);
    if (db < 0) db = 0; if (db > 20) db = 20;
    float learn = st_get_int(L"denoiseLearnPct", 10) / 100.0f;
    if (learn < 0.05f) learn = 0.05f; if (learn > 0.50f) learn = 0.50f;
    proc_denoiser_run(h, db, learn);
}

/* 1104: play the 1 kHz calibration tone generated to the original disc's
 * measured spec (44.1k/16/stereo, L=R, 30 s + endpoint frame) */
static uint8_t *g_calHead = NULL;   /* underlying allocation (hdr prefix owned here) */
static WaveDoc *g_calDoc  = NULL;
static void proc_calibrate(HWND h) {
    audio_stop(g_ap);
    if (!g_calDoc) {
        size_t bytes = 0;
        uint8_t *wav = dsp_calibration_tone(1000, 44100, 30, &bytes);
        if (!wav) { fail_msg(L"Calibrate audio hardware", L"There's not enough memory to complete this operation."); return; }
        WaveDoc *d = (WaveDoc *)calloc(1, sizeof(WaveDoc));
        if (!d) { free(wav); return; }
        d->sampleRate = 44100; d->channels = 2; d->bytesPerSample = 2;
        d->pcmData = wav + 44;          /* wave_free() is never used on this doc */
        d->pcmBytes = bytes - 44;
        wcscpy(d->path, L"<1 kHz calibration tone>");
        g_calDoc = d; g_calHead = wav;
    }
    audio_seek_and_play(g_ap, g_calDoc, 0);
}

static void build_menu(HWND h, HINSTANCE hi) {
    (void)hi;
    HMENU bar=CreateMenu();    HMENU mFile=CreatePopupMenu();
    HMENU mProc=CreatePopupMenu();
    HMENU mView=CreatePopupMenu();
    HMENU mHelp=CreatePopupMenu();
    AppendMenuW(mFile,MF_STRING,IDM_OPEN,L"&Open WAV...");
    AppendMenuW(mFile,MF_STRING,IDM_SAVE_AS,L"&Save As...");
    AppendMenuW(mFile,MF_SEPARATOR,0,0);
    AppendMenuW(mFile,MF_STRING,IDM_NEW_PROJECT,L"New Project...");
    AppendMenuW(mFile,MF_STRING,IDM_IMPORT_CD,L"Import Tracks from CD...");
    AppendMenuW(mFile,MF_STRING,IDM_IMPORT_MP3,L"Import MP3 file...");
    AppendMenuW(mFile,MF_STRING,IDM_EXPORT_MP3,L"Export audiotrack as MP3 file...");
    AppendMenuW(mFile,MF_SEPARATOR,0,0);
    AppendMenuW(mFile,MF_STRING,IDM_EXIT,L"E&xit");
    /* module names verbatim from the original's own UI strings; Enhancer is
     * a false feature here and stays grayed (README #5) */
    AppendMenuW(mProc,MF_STRING|MF_GRAYED,IDM_PROC_DECLICK,L"De-Click...");
    AppendMenuW(mProc,MF_STRING|MF_GRAYED,IDM_PROC_DENOISER,L"De-Noiser...");
    AppendMenuW(mProc,MF_STRING|MF_GRAYED,IDM_PROC_ENHANCER,L"Enhancer...");
    AppendMenuW(mProc,MF_SEPARATOR,0,0);
    AppendMenuW(mProc,MF_STRING,IDM_PROC_CALIBRATE,L"Calibrate audio hardware...");
    AppendMenuW(mView,MF_STRING,IDM_TOGGLE_THEME,L"Toggle &Theme (Light/Dark)\tCtrl+Alt+T");
    AppendMenuW(mHelp,MF_STRING,IDM_ABOUT,L"About Caudiok...");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)mFile,L"&File");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)mProc,L"&Process");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)mView,L"&View");
    AppendMenuW(bar,MF_POPUP,(UINT_PTR)mHelp,L"&Help");
    SetMenu(h,bar);
    set_proc_menu(h);   /* owns export-MP3/doc gating since v0.3 */
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        HINSTANCE hi=(HINSTANCE)GetWindowLongPtrW(h,GWLP_HINSTANCE);
        build_menu(h,hi);
        return 0; }
    case WM_DESTROY:
        if (g_ap) audio_stop(g_ap);
        PostQuitMessage(0); return 0;
    case WM_SIZE: app_invalidate(h); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc=BeginPaint(h,&ps);
        HDC mem=CreateCompatibleDC(hdc);
        RECT c; GetClientRect(h,&c);
        HBITMAP bm=CreateCompatibleBitmap(hdc,c.right?c.right:1,c.bottom?c.bottom:1);
        HGDIOBJ ob=SelectObject(mem,bm);
        paint_main(h,mem);
        BitBlt(hdc,0,0,c.right,c.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,ob); DeleteObject(bm); DeleteObject(mem);
        EndPaint(h,&ps); return 0; }
    case WM_APP_DONE:   /* end of track: park cursor at the last decoded frame */
        g_cursorFrame=audio_cursor_frame(g_ap);
        app_invalidate(h);
        return 0;
    case WM_LBUTTONDOWN:
        g_cursorFrame=frame_from_x(h,GET_X_LPARAM(l));
        app_invalidate(h); return 0;
    case WM_KEYDOWN:
        if (w==VK_SPACE && !(l & 0x40000000)) { play_ctl(h, !audio_is_playing(g_ap)); return 0; }
        break;
    case WM_HOTKEY:
        if (w==1) { BOOL d=!theme_is_dark(); theme_set(d); }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDM_OPEN: doc_open(h); return 0;
        case IDM_SAVE_AS: doc_save_as(h); return 0;
        case IDM_EXIT: DestroyWindow(h); return 0;
        case IDM_TOGGLE_THEME: { BOOL d=!theme_is_dark(); theme_set(d); app_invalidate(h); return 0; }
        case IDM_ABOUT: about(h); return 0;
        case IDM_PROC_DECLICK:   proc_declick(h);   return 0;
        case IDM_PROC_DENOISER:  proc_denoiser(h);  return 0;
        case IDM_PROC_CALIBRATE: proc_calibrate(h); return 0;
        case IDM_PROC_ENHANCER:  /* false feature: menu item is grayed; no handler */ return 0;
        case IDM_NEW_PROJECT:    file_new_project(h); return 0;
        case IDM_IMPORT_CD:      dlg_importcd_show(h); return 0;
        case IDM_IMPORT_MP3:     dlg_mp3import_show(h); return 0;
        case IDM_EXPORT_MP3:
            if (g_doc) dlg_mp3export_show(h, g_doc->path);
            return 0;
        default: break;
        }
        break;
    case WM_SIZING: case WM_ERASEBKGND: return 1;
    default: break;
    }
    return DefWindowProcW(h,m,w,l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, PWSTR cmd, int show) {
    (void)hp; (void)cmd;
    InitCommonControls();            /* SysListView32/progress for import-export dialogs */
    LoadLibraryW(L"comctl32.dll");
    WNDCLASSEXW wc; ZeroMemory(&wc,sizeof(wc));
    wc.cbSize=sizeof(wc);
    wc.style=CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc=WndProc;
    wc.hInstance=hi;
    wc.hCursor=LoadCursorW(hi,IDC_ARROW);
    wc.lpszClassName=CLSNAME;
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,CLSNAME,L"Caudiok",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,860,480,NULL,NULL,hi,0);
    if (!hwnd) { fail_msg(L"Caudiok", L"window create failed"); }
    theme_set(FALSE);
    theme_windows_sync(hwnd);
    g_ap=audio_create(hwnd,WM_APP_DONE);
    if (g_ap) audio_set_pos_cb(g_ap,on_pos_update,0);
    RegisterHotKey(hwnd,1,MOD_CONTROL|MOD_ALT,'T');
    ShowWindow(hwnd,show);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg,0,0,0)>0) {
        if (!TranslateAcceleratorW(hwnd,NULL,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    UnregisterHotKey(hwnd,1);
    if (g_ap) audio_free(g_ap);
    if (g_pk) peaks_free(g_pk);
    if (g_doc) wave_free(g_doc);
    /* calibration tone owns a raw header allocation; never wave_free'd */
    if (g_calHead) free(g_calHead);
    if (g_calDoc)  free(g_calDoc);
    return (int)msg.wParam;
}
