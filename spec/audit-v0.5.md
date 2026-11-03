# Source audit — caudiok src/ (2026-11-01)

## Re-audit 2026-11-03 (full read of all sources; verification re-run)

Re-checked the earlier fixes and found that two of the audio.c lifecycle fixes
had themselves introduced regressions (full-suite verification on the Linux
host passes, but the playback path could not have worked at all):

| # | File | Finding | Severity | Fix |
|---|------|---------|----------|-----|
| R1 | `audio.c` | **`ap->run` was never initialized to 1** (calloc zeroes it): the worker broke out of its loop on the very first semaphore release — playback was dead on first press of SPACE | critical | `run=1` set in `audio_create` before `CreateThread`; only `audio_free` clears it (matches the v0.5 audit's intended flag split) |
| R2 | `audio.c` | `audio_seek_and_play` called `audio_stop` after the open-check: stop **closed the freshly opened waveOut handle**, so every `waveOutWrite` after the second play would fail silently | critical | play flow is now `audio_open` (stops + opens fresh) → set frame/pos → `playing=1` → release slots; no stop-after-open |
| R3 | `audio.c` | `ap->pos` was never advanced during playback → stop/end returned the cursor to the play-start frame | med | worker sets `pos = frame` after each decoded block |
| R4 | `main.c` | `WM_APP_DONE` was posted at end-of-track but had no handler → waveform cursor never repainted when a track finished | low | WM_APP_DONE case: cursor = `audio_cursor_frame`, invalidate |
| R5 | `dialogs.c` | MP3 multi-file pick also inserted the *directory* entry into the list (single-file check done unconditionally) | low | only insert the dir when no per-file entries were added |
| R6 | `dialogs.c` | `dlg_bg_brush` created a brush per WM_CTLCOLOR* message and never freed it → GDI handle leak per repaint | med | one brush cached per theme, rebuilt on dark/light switch |
| R7 | `waveio.c` | `peaks_build` dereferenced `calloc` results unchecked | low | NULL guards on the Peaks struct and both column arrays |

Verification (2026-11-03, on the host):
* `tests/harness.c` PASS (0 failures)
* `tests/dsp.c` PASS (0 failures; 1 skip — disc fixture not on this host)
* `build.sh` PE32+ GUI build succeeds
* full sources under `-Wall -Wextra`: only pre-existing benign unused-parameter warnings

Line-by-line audit of the shipped sources (dialogs.c/h, main.c, audio.c/h,
waveio.c/h, theme.c, aux.c, dsp/{declick,denoise}.c) plus a template-parity
re-check of every dialog against `spec/dialog-map.json` and a keymap audit
against `spec/keymaps.md`.

## Findings & fixes (all applied this audit)

| # | File | Finding | Severity | Fix |
|---|------|---------|----------|-----|
| 1 | `audio.c` | Worker thread **exited** at end of track (`break` after playing=0) → a second play after the track finished released the semaphore for a dead thread: no sound until app restart | high | worker never exits at end of track: parks on the semaphore, drains leftover scheduled slots |
| 2 | `audio.c` | `audio_stop` set the thread-exit flag → same dead-thread effect on any stop→play cycle after the first | high | flag split: `run` (thread keep-alive, cleared only by `audio_free`) vs `playing` (playback state); `audio_stop` parks the worker via `playing=0` + semaphore drain |
| 3 | `audio.c` | `waveOutPrepareHeader`/`UnprepareHeader` never called (MME contract) | med | prepare in `audio_open`, unprepare in `audio_stop` before close |
| 4 | `audio.c` | `alloc_bufs` freed ring buffers the worker may still be writing on re-open (use-after-free window) | med | buffers are never freed while the thread runs; only re-allocated when a larger size is needed, released once after thread exit |
| 5 | `dialogs.c` | `pick_files` passed a pipe-separated filter (`"...|*.mp3|...|*.*||"`) straight to `OPENFILENAME.lpstrFilter` which needs NUL-separated pairs → broken filter in the MP3-import file picker | med | pipes converted to NULs inside `pick_files` (same scheme already used in doc_open/doc_save_as/1037) |
| 6 | `main.c` | "New Project → Open existing project" (template 414) did nothing when no document was open | low | opens the WAV dialog unconditionally (documented provisional mapping onto the WAV flow, README #9) |
| 7 | `dialogs.c` | Soundcard Delay readout re-read **persisted** registry values instead of the fields being edited | low | reads the live edit texts (1019/1020), clamped the same way |
| 8 | `waveio.c` | duplicated `#include <stdlib.h>` | cosmetic | removed |

## Re-checked, no change needed

* **Dialog template parity** — every implemented template (410/413/423, 414,
  416, 411, 422, 1037, 418, 425 + module param dialogs) re-compared against
  `spec/dialog-map.json`: control ids, rects and style masks verbatim
  (spot-checked hex: `0x50810080`, `0x50818009`, `0x80c000c0`,
  `0x90000840`, `0x40010001` all match the JSON decimals).
* **Keymap parity** — `spec/keymaps.md` scope is honored: only practice-decodable
  keys wired (SPACE, verified in `main.c` `WM_KEYDOWN`); F-key table stays
  provisional pending the behavior-mining pass, no invented bindings.
* **Menu gating** — Process/Export-MP3 enable state follows `g_doc`;
  Enhancer permanently grayed (honest false feature).
* **waveio chunk loop** — pad/align arithmetic is redundant but correct;
  24-bit sign-extension and float→int16 clamps verified by reading.
* **dialog_build sizing** — worst-case item (title + fixed header) stays within
  the `2048 + n·200` byte estimate.

## Known limitations (documented, not defects)

* Master-audio-device races are inherently untestable on the Linux build host;
  fixes #1–#4 are reasoned from the MME contract and will get their real
  behavioral check in the user's VM pass (last step in `spec/pending.md` §5).
* `GetMessageW` returning -1 is not specially handled in `wWinMain` (returns
  the last filled `msg.wParam`); unreachable in practice, noted for honesty.
