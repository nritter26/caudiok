# Pending work — Caudiok rebuild (status 2026‑11‑01)

Scope of this file: everything still open, ordered by execution priority, with the
exact evidence pointers and target files so any item can be picked up directly.
Boundaries that stay in force: read‑only analysis, no license/activation bypass,
no original code/assets reused, neutral branding, false features stay grayed
(never faked), offline single‑exe, VM testing only at the very end (user's VM).

## 1. Already verified (baseline to build on)

* Dialog parser gold: all 17 `RT_DIALOG` templates (`spec/dialog-*.bin`) parse
  caudiok into `spec/dialog-map.json` (classic header, UTF‑16 strings, EOF align
  quirk). Main console is **not** a dialog — built in code (host engine).
* `spec/layout-main.md` — dialog inventory + Preferences/Soundcard control maps
  with real parameter tiers (−20/−30/−40 dB, 2/5/10 s, 100…1000 cm, Meter/Inch,
  EQ A/B), registry keys, window architecture.
* `spec/dsp-parity.md` — DSP evidence: modules = `SddSpp*.dll` ordinals 1…106
  (fp table `DAT_00abd7a0`, init = table[1]); **FFT 1024 @ 44 100 Hz**; gain
  range {20, −20, 0.9}; module default param 0.5; DeClick = 992‑sample (0x3e0)
  repair template, slope scale 1/64, max‑gap 200; `NoiseFingerprints.dat`
  payload empty.
* Code state: `src/dsp/declick.c` on tier thresholds (dB = −40+20·s,
  t = 32768·10^(dB/20)·0.5, radius = widthMs·44.1 capped [2,500], width ≤22.5 ms);
  `src/main.c` proc_declick defaults 0.5 / 11.2 ms; denoiser 20 dB confirmed.
* Round‑2 mining landed (see `spec/dsp-parity.md` §2.1/§3): DeClick repair‑
  template geometry, click‑record format and the original build provenance.
  *Not* recovered: DeNoise per‑bin curve (§4, `0x10093490` lead was the CRT
  math‑error handler, not the DSP path), Puncher / Azimuth / EQ params.
* Green runs: `tests/dsp.c` **PASS** (spike err 66 389 904→2 756; burst
  3.99 G→106 k; untouched intact; denoise SNR 31.31→42.45 dB), `tests/harness.c`
  **PASS**, `build/Caudiok.exe` builds (zig cross‑compile, PE32+ GUI).
* Source audit 2026-11-01: 8 findings found and fixed in `src/` (playback
  thread lifecycle + MME prepare/unprepare + buffer-free race in `audio.c`,
  MP3-import file filter, template-414 no-doc path, soundcard Delay readout)
  — full report `spec/audit-v0.5.md`; dialog templates re-verified verbatim
  against `spec/dialog-map.json`.
* Round-3 mining landed (`spec/dsp-parity.md` v0.5): DeClick level system
  decoded — anchors = per-256-sample-block levels from an exp-based curve
  (`0x10039140` → `0x10041190` → libm `exp`), detector = 5-tap running sum
  with `(total·3)>>4` trigger; DeNoiser = log-domain spectral expander
  (band-mean + upper-tail clamp, 0.9 ratio) with `reduction = 20·p − 20` [dB]
  (default p=0.5 → 10 dB in the original). DeClick record geometry
  `(start−4, run+4)` + max-gap 200 is now **ported into `src/dsp/declick.c`**
  and passes the same gates (spike 66.4 M→117 k, burst 4.0 G→1.15 M).
  Statically unrecoverable (runtime-initialized `.data`): EMSR/level curve
  scales `0x100d1050/58`, `0x100d7a08/1c`, `0x100d7a10/20` and detector
  `k/b` — needs the user's VM behavior pass, do not re-mine statically.

## 2. Pending — DSP round 2 (evidence mining → kernels)

Priority order; each row names where to dig and which file it may change.

| # | Item | Mine at | Affects | Why |
|---|------|---------|---------|-----|
| 0 | ✅ **Provenance** — SddSppiv v2.0.0.20, complete 29‑folder algo roster, DeNoiser = EMSR class, DeClicker file set, ordinals are nameless (no RTTI) | `SddSpp*.dll` banner strings | `spec/dsp-parity.md` §2.1 | done in round 2 |
| 1 | **DeNoise per‑bin gain curve** — EMSR lookup (`EMSR.CPP`/`LOOKEMSR.CPP` framed in §2.1); re‑enter via ordinal table, **the `0x10093490` lead is dead (CRT `_matherr`)** | ordinal export chain (`SddSppiv.dll` 107 ordinals) | `src/dsp/denoise.c`, `spec/dsp-parity.md` §4 | core "quality identical to original" ask |
| 2 | **DeClick dead‑band / burst extension constants** — template geometry + record format now [D] (dsp‑parity §3); still open: five‑anchor level table feeding `0x10041190` and second‑phase level sources | template builder `0x10038740` (mined), interpolator `0x10041190` | `src/dsp/declick.c` | better crackle tracking; residual §3 [A] is narrower now |
| 3 | **DeCrack vs DeClick split params** — record format `(start−4, run+4, channel)` + max‑gap 200 + 0.625 target mask are now [D]; split (≤3‑flagged analog) still open | `.data1 0x100cfe98…cffb0` (mined), record writer in `0x10038740` tail | `src/dsp/declick.c` | DeCrack is a separate registered module (`kDecrack`) |
| 4 | **Azimuth estimator** — lateral‑correlation delay search parameters | `0x1006xxxx` stereo/azimuth functions in `decomp-sddsp1` | new `src/dsp/azimuth.c` (module currently absent) | roster parity (`kAzimuth` slot) or stays grayed |
| 5 | **EQ engine params** — confirm the 64‑pt fast‑convolution EQ (`host exe 0x524b70`) band set/gain units vs prefs A/B presets | `0x524b70`, `0x5249a0`; host exe EQ strings (`0x51e41c "Equalizer"`) | `src/main.c` EQ path (if implemented) | EQ is visible in Preferences (A/B) |
| 6 | **Other‑module parameter ranges** — needed only to keep honest grayed UI slots (Hall, CarSim, TubeSim, SoundMorph, Spectralizer, StereoSpread, LoudMax, Pitcher, DeEsser, DeRumbler, FPDenoise, MultiBandComp) | module constructors `0x1002e000` / `0x10067e90` / `0x1007cfe0` families | UI grayed states, `spec/dsp-parity.md` §5 | documentation only, no sound |

Round‑2 dead ends (for the record, do not re‑mine): `0x10093490` family = CRT
math‑error handler; `0x100441b0` = ARInterpolator biquad cascade; no RTTI/
export names in any SddSpp DLL.

## 3. Pending — policy/stack decisions (user input)

| # | Decision | Options | Default if unanswered |
|---|----------|---------|----------------------|
| D1 | **MP3 export/import** (orig = `lame_enc.dll`) | (a) bundle own LAME‑derived build (offline, still self‑contained); (b) ship WAV‑only and gray MP3 slots until (a) | (a), dialog geometry 1037/1041/422/424 implemented regardless |
| D2 | **Puncher** (`DSPPuncher`) | (a) faithful reimplementation from `Puncher.dll` decompile; (b) gray slot | (a) if round‑2 mining covers it in time, else (b) |
| D3 | Enhancer & other absent UGS slots | keep grayed | keep grayed (standing policy) |

## 4. Pending — layout parity implementation (code)

1. ✅ **Console rack geometry mining** — done: `spec/layout-main.md` §console
   (MDI frame `0x5a9440` + `MDICLIENT 0x52300001`, panel class `0x6f8700`,
   `bmToolButton`/`bmOptionToolButton` strip‑column tool buttons with
   per‑command ×‑stride placement, command strip ids, skin‑bitmap layout).
2. ✅ **Implement dialogs** — done: all templates live in `src/dialogs.c`
   (ids/rects/titles verbatim from `spec/dialog-map.json`), Preferences tiers
   wired, Soundcard MME device list + buffers 4…10 + byte sizes + read‑only
   Delay, menu entry points wired in `src/main.c` (export item only enabled
   with an open document).
3. **Modern theme stays** — 2026 visuals on identical placement/geometry;
   dialogs keep template font metrics → DLU mapping.
4. **Keymap parity** per `spec/keymaps.md` (no original RT_MENU/RT_ACCELERATOR
   exist → mnemonics + documented table only).

## 5. Pending — verification

* Rebuild + rerun after every kernel change:
  `cd caudiok && ./build.sh` (GUI exe), then
  `gcc -Wall -Wextra -Wno-unused-parameter -O2 tests/dsp.c src/waveio.c src/dsp/declick.c src/dsp/denoise.c -lm -o /tmp/opencode/dsp && /tmp/opencode/dsp`
  and
  `gcc -Wall -Wextra -Wno-unused-parameter -O2 tests/harness.c src/waveio.c -lm -o /tmp/opencode/harness; rm -rf /tmp/opencode/h; /tmp/opencode/harness`
  (never add `src/audio.c` to the harness — it needs `windows.h`).
* Acceptance after round‑2: burst/spike thresholds unchanged (≥70 % drop both
  fixtures, ≥3 dB denoise SNR); kernel semantics unchanged this round (mined
  template geometry documented, not yet ported — needs the five‑anchor level
  table, §5).
* Audit fixes 1–4 (`audio.c` lifecycle) are reasoned from the MME contract,
  unverifiable on the Linux host — cover in the VM behavior check: play →
  stop → play, and play to end of track → play again.
* **Last step only (user's VM)**: run `build/Caudiok.exe`, compare against
  install disc behavior (layout placement, dialog values, keymap, sound).

## 6. Pointers

* Decompiles: `/tmp/opencode/pinnacle/decomp` (host exe, 39 263 funcs),
  `decomp-sddsp1` / `decomp-sddsp4` (SddSpp.dll / SddSppiv.dll).
* Payload DLLs: `/tmp/opencode/pinnacle/host-install/` (SddSpp*, Puncher,
  lame_enc, NoiseFingerprints.dat). Ghidra projects: `analysis/ghidra-project/
  {host,sddsp1,sddsp4}.gpr`.
* REA recipe: headless‑import PE first, then `REA_GHIDRA_PROJECT_PATH=<gpr>
  node scripts/rea.mjs decompile-all <path> --output <dir>`; env exports must
  end with `;` before `node`.
* Tooling: `analysis-toolset/dialog_dump.py` regenerates
  `spec/dialog-map.json` (writes both `/tmp/opencode/h/` and spec copies).
