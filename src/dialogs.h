/* dialogs.h — dialogs rebuilt from the original's measured templates
 * (spec/dialog-map.json) via runtime DLGTEMPLATE construction: identical
 * control ids, rects and titles; visual styling modernized (theme). */
#ifndef CAUDOK_DIALOGS_H
#define CAUDOK_DIALOGS_H

#include <windows.h>

/* persisted settings (registry HKCU\Software\Caudiok5) */
int  st_get_int(const wchar_t *name, int def);
void st_set_int(const wchar_t *name, int val);

/* original template dialogs (exact geometry) */
BOOL prefs_show(HWND owner);      /* template 425 */
BOOL soundcard_show(HWND owner);  /* template 418; enumerates MME devices */

/* template-inventory dialogs (spec/dialog-map.json): verbatim geometry, honest
 * grayed execution where the feature is not in this build (MP3/CD) */
BOOL dlg_confirm_ok(HWND owner, const wchar_t *title, int variant);  /* 410/413 (0) or 423 (1) */
int  dlg_project_prompt(HWND owner);            /* 414: 0 cancel, 1 new, 2 open-existing */
BOOL dlg_blankpanel_show(HWND owner);           /* 416: 0-item module shell */
BOOL dlg_importcd_show(HWND owner);             /* 411 */
BOOL dlg_mp3import_show(HWND owner);            /* 422 */
BOOL dlg_mp3export_show(HWND owner, const wchar_t *sourcePath);  /* 1037 */

/* module parameter dialogs (this rebuild's interface layer; tiers/units are
 * the original's own evidence-verified values, see spec/dsp-parity.md) */
BOOL declick_params_show(HWND owner, float *sensitivity, float *widthMs);
BOOL denoiser_params_show(HWND owner, int *reductionDb, int *learnPct);

/* sound card config getters for audio playback */
int  snd_play_dev(void);      /* WAVE_MAPPER or device index */
int  snd_play_bufs(void);     /* 4..10 */
int  snd_play_bufsize(void);  /* 16384..131072, step 16384 */

#endif
