# keymaps — original Caudiok keyboard behavior (decompile evidence)

Source of truth: REA/Ghidra analysis of `host exe` (sha256 052eade6…),
decompile bundle `/tmp/opencode/pinnacle/decomp/` (functions directory +
decompile-index.json). All addresses below are as-shipped VAs (image base
0x400000).

## 1. Resource facts (checked directly in the PE, twice)

- **No RT_MENU resources** (top-level resource types: CURSOR 22, BITMAP 599,
  ICON 63, DIALOG 17, STRING 218, RCDATA 2, GROUP_CURSOR 22, GROUP_ICON 7,
  VERSION 1 — `menu_extract.py` walk of the `.rsrc` directory).
- **No RT_ACCELERATOR resource** either.
- Consequence: there is no accelerator table and no compiled menu bar.
  The original's visible UI is built from **DIALOG templates**; keyboard
  handling is **programmatic** in a custom message pump (below).

## 2. The keyboard pipeline

`FUN_005a3d70` (the pump, called by the MDI message loop):

```
GetMessageA(...)
msg.message ∈ {WM_KEYDOWN (0x100), WM_KEYUP (0x101),
               WM_SYSKEYDOWN (0x104), WM_SYSKEYUP (0x105),
               ...windowed range 0xff..0x106}   → key path
0x111 region (WM_COMMAND) and mouse messages → normal MDI dispatch
 FUN_005aa730(msg.lParam, msg.wParam, &ascii, &code)   [key decoder]
 if payload:                                      
   KEYUP/KEYUP-work? → FUN_005a54c0()
   else             → FUN_005a51a0()   [keydown dispatcher]
```

### The key decoder `FUN_005aa730`

- `lParam & 0x1000000` (extended-key bit 24) selects the lookup table:
  - normal keys   → table at `0x00930c3c`
  - extended keys → table at `0x00930d1c`
- Tables are arrays of `(code, vk)` **short pairs**, terminated by code 0.
  Lookup matches the pair whose **vk == wParam** and yields its `code`.
- VK_CAPITAL (0x14) short-circuits (no decode).
- If `DAT_00932330` is set, `ToAscii(vk, scan, keystate=0…) `produces the
  ASCII character for printable keys (skipped for Shift/Ctrl/Alt/Win/Apps/
  NumLock/ScrollLock and similar 14 VKs listed verbatim in the code).
- Codes `0x18..0x21` and `0x26` **suppress** the ASCII char (pure command
  keys = navigation/edit keys defined below).

### Decoded tables (from the file bytes at `.data` VA 0x930c3c / 0x930d1c)

Internal master-alphabet code

NORMAL table `0x930c3c` (code → vk):

| meaning | code | VkKey(s) |
|---|---|---|
| SHIFT | 0x36 | 0xA1 RShift, 0xA0 LShift, 0x10 Shift |
| CTRL | 0x37 | 0x11, 0xA3 RCtl, 0xA2 LCtl |
| ALT | 0x38 | 0xA5 RAlt, 0xA4 LAlt, 0x12 |
| BACKSPACE | 0x01 | 0x08 |
| TAB | 0x02 | 0x09 |
| RETURN | 0x04 | 0x0D |
| PAUSE | 0x05 | 0x13 |
| ESC | 0x06 | 0x1B |
| SPACE | 0x07 | 0x20 |
| DELETE | 0x26 | 0x2E |
| INSERT | 0x18 | 0x2D |
| END | 0x19 | 0x23 |
| DOWN | 0x1A | 0x28 |
| PGDN/NEXT | 0x1B | 0x22 |
| LEFT | 0x1C | 0x25 |
| CLEAR (Num5) | 0x1D | 0x0C, 0x65 (Num5) |
| RIGHT | 0x1E | 0x27 |
| HOME | 0x1F | 0x24 |
| UP | 0x20 | 0x26 |
| PGUP/PRIOR | 0x21 | 0x21 |
| HELP | 0x17 | 0x2F |
| NUMPAD 0..9 | 0x18..0x21 | 0x60..0x69 (emulate INS/END/DOWN/PGDN/LEFT/CLEAR/RIGHT/HOME/UP/PGUP) |
| NUMPAD \* | 0x22 | 0x6A |
| NUMPAD + | 0x23 | 0x6B |
| NUMPAD - | 0x25 | 0x6D |
| NUMPAD . | 0x26 | 0x6E (emulates DEL) |
| F1..F12 | 0x28..0x33 | 0x70..0x7B |
| NUMLOCK | 0x34 | 0x90 |
| SCROLL LOCK | 0x35 | 0x91 |

EXTENDED (bit-24 set) table `0x930d1c` (code → vk):

| meaning | code | VkKey |
|---|---|---|
| NUMPAD RETURN | 0x13 | 0x0D |
| NUMPAD + | 0x27 | 0x6F |
| INSERT | 0x15 | 0x2D |
| END | 0x09 | 0x23 |
| HOME | 0x0A | 0x24 |
| DELETE | 0x16 | 0x2E |
| LEFT | 0x0B | 0x25 |
| UP | 0x0C | 0x26 |
| RIGHT | 0x0D | 0x27 |
| DOWN | 0x0E | 0x28 |
| PGUP/PRIOR | 0x0F | 0x21 |
| PGDN/NEXT | 0x10 | 0x22 |
| SHIFT/CTRL/ALT | 0x36/0x37/0x38 | 0x10/0x11/0x12 |
| APPS (menu) key | 0x3A | 0x5D |

AUX table `0x930db8` (third lookup; pump consumer `FUN_005aa940`):
`(0x3b,0x0E) (0x3c,0x0D) (0x3d,0x0C) (0x3e,0x0B) (0x3f,0x0A) (0x40,0x09)` +
terminator. Pairs VK codes 0x09..0x0E with codes 0x3b..0x40 — consistent
with wheel/auxiliary device emulation (provisional note).

### Behavior proven so far

- **SPACE (code 0x07, vk 0x20)** = play/stop toggle — implemented in the
  rebuild main window (`WM_KEYDOWN`/` VK_SPACE`).
- Modifier codes (shift/ctrl/alt) are consumed by the keydown dispatcher
  (`FUN_005a51a0` checks codes 0x36/0x37/0x38 explicitly up front).
- Arrow/home/end/page keys and DEL are the navigation alphabet used by the
  waveform editor (END/HOME/PGUP/PGDN/LEFT/RIGHT/UP/DOWN + DEL/INS), with
  numpad key aliases precisely as in the tables above.
- Wait-for-key helper `FUN_005aa9f0` implements held-key detection with
  GetAsyncKeyState + the same tables (`space-scrub`-era plumbing).

### Not yet mined (honestly provisional)

- The code → command TABLE that the editor windows executes per code lives
  behind C++ virtual calls (`FUN_005a54c0` / `FUN_005a51a0` dispatch into
  `*(**+0xb8)()` vtable slots). Per-window bindings (which F-key opens
  which effect, etc.) need a vtable-mining pass; the rebuild marks those
  as *provisional* and only wires the proven behaviors above.

## 3. What the rebuild does (and what it legitimately cannot yet)

Because the original carries no menu/accelerator resources, the rebuild
shows **menu mnemonics only** (Alt+F etc.) and does **not** display fake
“Ctrl+O / Ctrl+S” labels. Proven keys wired: SPACE (play/stop). Keyboard
navigation alphabet and F-key bindings are marked provisional pending the
behavior-mining pass; they will be implemented faithfully from that pass,
not invented.
