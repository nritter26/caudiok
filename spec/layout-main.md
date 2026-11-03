# Layout evidence — dialog templates + window architecture (Caudiok)

Source of truth: 17 `RT_DIALOG` templates extracted verbatim from the disc payload
(`spec/dialog-*.bin`), parsed by `analysis-toolset/dialog_dump.py` into
`spec/dialog-map.json`. Resource coordinates are in **dialog units**, the
template font is `MS Sans Serif 8pt` (EN builds; JA builds use `ＭＳ ゴシック 9pt`,
which is why the JP template ids come in pairs with different pixel extents).

## Template format quirks (documented for reproduction)

* Header layout is **classic** `DLGTEMPLATE` (style, exStyle, cdit, x y cx cy,
  menu?, class?, title?; DS_SETFONT = `style & 0x0040`), but every string field is
  stored **UTF-16 wide**: title, `DS_SETFONT` face, item class, item title are all wide.
  `sz_Or_Ord`: `0x0000` = none, `0xFFFF` + WORD = ordinal, else wide string.
* Items (classic order): `style, exStyle, x, y, cx, cy, WORD id, class, title,
  WORD extraCount` and the next item is DWORD-aligned **except after the very last
  item**, where alignment padding does not exist in the dump. Same for the
  post-font alignment when `cdit == 0`.
* Common-control classes appear by name (`SysListView32`, `msctls_progress32`,
  `msctls_updown32`), stock classes by ordinal (`0x80` BUTTON, …).

## Window architecture from the binary (evidence: functions 0x5a5b40, 0x5a9440)

The **main console is not a dialog template**. `0x5a5b40` registers six window
classes and creates the frame programmatically:

| class name                  | wnd proc    | purpose (observed) |
|-----------------------------|-------------|--------------------|
| `FrameWindowClass`          | `0x5a9440`  | main frame; creates `MDICLIENT` (`style 0x52300001`) |
| `HostDocWindowClass`   | `0x649500`  | waveform document windows (MDI children) |
| `HostFullScreenWindowClass` | `0x713170` | fullscreen playback view |
| `HostPanelWindowClass` | `0x6f8700`  | floating tool panels (`NCCALCSIZE`/`NCPAINT` custom) |
| `HostPopupWindowClass` | `0x712e40`  | popup helper |
| `HostModalWindowClass` | `0x5d5240`  | modal helper |

* Frame: `CreateWindowExA(WS_EX_WINDOWEDGE, class, title,
  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, …)` with geometry restored
  from saved config; early-startup fallback is a near-fullscreen rectangle
  (`FUN_005a1260(0,0, w, h)` from system metrics minus small insets).
* The console content itself is a `USConsole` control (string at `0x93f4d4`); the
  rack look is drawn (`UBitmaplider/USlider`, `bmToolButton`), so **pixel geometry
  of console contents lives in skin bitmaps + code**, not in RT_DIALOG. Layout
  parity strategy for the console: mirror the *structure* (frame + MDI doc area +
  transport/tool console panel, positions of panel vs doc area) rather than skin
  bitmaps, with a 2026-styled render of the same placement.

## Console rack structure — mined from the binary (evidence: 0x8114e0, 0x8114e0 helpers, 0x6f8700)

The console is **not** laid out from static coordinates in code: rack content is
constructed as a vertical stack of *command panels*, each panel rendering a
run of **tool buttons cut from bitmap strip resources**, one strip column per
command. Mined facts [D, decompile `host exe` /tmp/userspace evidence]:

* Command registration chain: `FUN_008114e0` walks the app command list
  (`FUN_005e85c0`, iterator `+ 0x2c?` step 8? — walks `**?` vtable pair 4/8) and
  instantiates one `CButton`-facade item per command via `operator_new(100)`+
  `FUN_0061ad20(this, 0, this+0x28)`, where `this+0x28` is the console panel that
  the button belongs to.
* Button art: `bmToolButton` (normal commands) or `bmOptionToolButton`
  (mode/option commands, decided per command by its state vtable `piVar4[5]+0x6c`)
  loaded through the UBitmap cache (`FUN_00609d80` → `FUN_006c2d30(name)`).
* Placement idiom: buttons are strip columns — one strip per button gap after
  measurement `FUN_007d7b10(bmp,&w)`, placed at horizontal offset
  `(strip_width - w) * command_index` (`FUN_005a14d0((local_30-local_38)*idx,0)`),
  i.e. left-to-right ribbon per panel. Split vs full-width flags read from
  `piVar4[3] & 2` choose a docked full-width (`operator_new(0x5c)` type) vs
  half/option gutter variant (`FUN_005a1260(w,h,w,h)` sizing then +0xf8
  `<RECT>`, +0x110 activate).
* `bmOnOffButton` exists as a separate on/off (toggle) state strip — used by the
  toggling commands (`0x7d8540`).
* Skin resources mined from the decompile: `bmToolButton`, `bmOptionToolButton`,
  `bmOnOffButton`, `bmInspectorBack / bmInspectorBackR`, `bmExpandBitmap /`
  `bmExpandIcon`, `bmTrimIcon`, `bmLineIcon`, `bmDrawIcon`, `bmArrowIcon` (each
  referenced exactly twice per build in the toolbar/console construction).
* Panel windows (`HostPanelWindowClass`, wndproc `0x6f8700`): custom
  `NCCALCSIZE` insets the client by the title-strip height — top and bottom each
  shifted by `GetSystemMetrics(SM_CXFRAME) − 2·GetSystemMetrics(SM_CXBORDER)`
  (formula as coded: `pts += SM(0x20); pts −= 2·SM(5)` on both `top` and
  `bottom`) — and `NCPAINT` painted two `CreateSolidBrush(syscolor)` fills plus
  `DrawEdge(.., 5 /*EDGE_ETCHED*/, 0x0f /*BF_RECT|FLAT*/)` — a small etched
  tool-palette border. Panel identity strings: class scheme `s_Console_0093f248`,
  window type `USConsole` (`s_USConsole_0093f4d4`, RTTI checkers `0x7679e0/0x767a50`).
* Frame evidence (own facts, wndproc `0x5a9440`): WM_CREATE creates the MDI area
  first (`CreateWindowExA(MDICLIENT, style 0x52300001, 0,0,0,0)`), subclasses the
  client (`SetWindowLongA(-4, 0x5a9320)`); WM_USER+43 (0x2b) redraws skinned
  console background via the theme engine with `GetSysColor(0x0f) / (0x0d)` fills.

**Placement map for a 2026 rebuild**: one bottom (or floating) panel per command
band, band = unidirectional left→right horizontal row of module buttons, each
button sized by its own bitmap-width (strip column), state-shown via the same
strip's alternate column (normal vs option/onoff). This is what
`src/main.c`'s docked console band implements structurally; the skin bitmaps
themselves are NOT reused (see ground rules).

## Control id parity for the console band (rebuild mapping)

| registered per `FUN_008114e0` | rebuild id | note |
|---|---|---|
| module buttons flagged `piVar4[3] & 2` | `IDM_PROC_*` in the Process menu | already in the v0.2 menu band |
| toggle commands via `bmOnOffButton` | (v0.3) handled as stay-checked modules | |
| `bmInspectorBack…` panels | inspector pane — not rebuilt yet (grayed slot) | |

## Dialog inventory (17 templates)

| id | size (DLU) | items | title | class notes |
|----|-----------|-------|-------|-------------|
| 410/413 | 186×95 | 2 | "Dialog" | plain OK/Cancel message boxes |
| 411/412 | 241×253 / 233×206 | 10 | "Importing Audio tracks from CD" | EN/JP pair; combo, `SysListView32`, progress, path edit + `...`, status static, "Add to Project" checkbox, Update |
| 414/415 | 270×90 | 5 | "Pinnacle Caudiok" | new-project prompt: question static + New/Open/Cancel |
| 416 | 332×213 | 0 | (empty) | placeholder shell, 0 items |
| 418/419 | 311×119 | 15 | "Soundcard" | Playback + Recording group boxes; per side: device combo, Buffers (4...10), Buffer size (bytes), Delay (read-only); OK/Cancel |
| 422/424 | 243×226 | 8 | "Importieren von MPEG1-Layer3-Dateien" / (JP) | MP3 import: file list, "add to project" checkbox, dest dir edit + `...`, Konvertieren/Abbruch, explicit `MP3-Dateien` button |
| 423 | 137×45 | 2 | "Pinnacle Caudiok" | tiny OK/Cancel |
| 425/426 | 281×172 / 259×172 | 24 | "Preferences" | EN/JP pair — see map below |
| 1037/1041 | 356×222 | 18 | "Exportieren als MPEG1-Layer3-Dateien" / "Export as MPEG1 Layer 3 Files" | MP3 export: file list, CRC/Copyright/Original/Intensity checks, 3 quality radios, bitrate combo, progress, path edit + `...`, Konvertieren/Abbruch, time display |
| `IDEMPTYTEMPLATE` | 9×5 | 0 | | framework placeholder |

## Preferences dialog (425 EN) — control map with placement

Groups/tabs absent: horizontal, left `Threshold AutoMarker/AutoStop` (8,6,145,64)
and right `Threshold AutoMarker` (161,6,112,64); below `Threshold AutoStop`
(161,74,112,64), `Surround Distance` (8,74,145,42), `Surround Measuring Units`
(8,120,98,45), ` EQ ` (110,120,43,45). Buttons `&OK` (163,147,50,14),
`Cancel` (221,147,50,14).

| id | control | rect | meaning |
|----|---------|------|---------|
| 1055–1057 | RADIO | (19,51/36/22) 124×10 | threshold **Normal (-40 dB)** / **Medium (-30 dB)** / **High (-20 dB)** (order bottom→top) |
| 1059/1060/1064 | GROUPBOX | — | AutoMarker + AutoStop / AutoMarker / AutoStop |
| 1061–1063, 1065–1067 | RADIO, 85×10 | y 22/36/51, y 90/104/119 | AutoMarker & AutoStop duration: **2 / 5 / 10 s** |
| 1070 | EDIT | (19,90) 42×14 | surround distance (numeric) |
| 1071 | `msctls_updown32` | (62,90) 10×14 | spin (range **100…1000**) |
| 1075 | STATIC | (73,90) 71×12 | `cm (100...1000)` |
| 1073/1074 | RADIO | (19,132/147) | measuring units **Meter / Inch** |
| 1077/1078 | RADIO (A/B) | (117,132/147) | **Equalizer preset A / B** |

Settings persisted in registry (`<vendor>\<product>` + keys
`prefsSurroundRadius`, `prefsSurroundMeasure`, `eqFromHost15`,
`prefsThresholdDehummerFreq` with 50 Hz / 60 Hz, `prefsIgnoreCDSize`).

> These are the original's numeric tiers: restoration threshold tiers
> −20/−30/−40 dB, auto marker/stop 2/5/10 s, surround room distance 100…1000 cm,
> meter/inch units, EQ presets A/B. Our rebuild's parameter defaults must map onto
> the same tier values (expressed in our own UI/units) — not invented numbers.

## Soundcard dialog (418 EN)

Playback group (7,4,145,87) and Recording group (159,4,145,87); each side:
device COMBOBOX at (19/171,16,122,~150), `Buffers (4...10):` static, EDIT
(103/255,33,38,12), `Buffer size (bytes):` static, EDIT (103/255,52,38,12),
`Delay:` static (20,75) — delay is displayed, not editable; OK (26,98) /
Cancel (83,98). Registry keys `idPlayDevice`, `idRecDevice`, `mme_numBuff_play`,
`mme_numBuff_rec`, `mme_buffSize_play`, `mme_buffSize_rec` (defaults shown as
"16384"/"32768" strings in code).

## Parity plan

1. **Dialogs above are the exact placement source** — the rebuild re-uses the same
   control ids, rects (DLU) and grouping; visual styling is re-done for 2026
   (segment instead of classic radios/checkboxes, same geometry).
2. **Main console**: same *architecture* (non-modal rack + MDI waveform area,
   transport behavior, keyboard behavior per `keymaps.md`); geometry re-checked
   from the panels' creation code before main.c implements docks.
3. False/absent original functions (Enhancer/other plugin slots) stay grayed —
   registration evidence (`kDeClick…kPuncher` key table, SDDSPP ordinal loader)
   documents exactly why.
