# DSP engine parity — evidence and status (v0.5)

Purpose: record what the decompile of the original's DSP actually is, so the
rebuild's processors use the same algorithm and parameter space. Evidence class
is marked per claim: **[D]** = exact constant/function mined from the binaries,
**[A]** = architectural (from call structure), **[P]** = verified-equivalent in
careful testing.

## 1. Where the restoration DSP lives

* The module roster is in `host exe` (`0x495a20`), registered as config keys
  `kDeclick, kDecrack, kDenoise, kDeRumbler, kAzimuth, kPitcher, kStereoSpread,
  kLoudMax, kEqualizer, kSurround, kSoundMorph, kTubeSim, kHall, kCarSim,
  kDeEsser, kDeBuzz, kFPDenoise, kMultiBandComp, kSpectralizer, kPuncher` — 20
  slots `{0..8, 0xd..0x14, 0x17, 0x18}` in the rack list.
* The DSP implementations ship in `SddSpp*.dll` ("Audio Algorithms by Spectral
  Design", string `0x51c518`). `0x515160` loads exactly ONE of the four
  (`SDDSPP.dll`/`SDDSPPII.dll`/`SDDSPPIII.dll`/`SDDSPPIV.dll`) chosen by CPU
  capability probing — identical algorithms, different instruction builds.
  `Puncher.dll` (`DSPPuncher`) is a separate module; `lame_enc.dll` handles MP3.
* The DLL exposes **107 ordinal-only exports**; `host exe` resolves ordinals
  1..0x6a into a function-pointer table at `DAT_00abd7a0` and calls table[1]
  (init) first. Decompiled at `/tmp/opencode/pinnacle/decomp-sddsp{1,4}`
  (Ghidra-project analyzed via headless, then REA).

## 2. Confirmed engine parameters [D]

* The offline module constructors allocate FFT blocks with
  `FUN_1002e000(this, n, 0x400, 0xac44, 3)` / `FUN_1004ba90(…, 0x400, 0x472c4400)`
  → **block/FFT size 1024 samples at 44 100 Hz** for the restoration modules
  (`0x10080ac0`, `0x10081020`; init ordinals 3/4).
* Reconstruction gain range: `DAT_100dafe0 = { 20.0, −20.0, 0.9 }` → the
  spectral gain path operates with **±20 dB range and a 0.9 scaling**; the
  module-init gain call uses `DAT_100d91d0 = −20.0`
  (`(−20 − param) * 1.0` = default *−20 dB offset* at init, `0x100d91d0..d4`).
  → our denoiser default of 20 dB reduction is the original's unit range (not
  invented). [D]
* Per-module raw parameter default (the nine restore modules) is **0.5**
  (written as the integer `500000` — `0x484eb0` initializes `in_ECX[0x4f0+0..8]`).
  This corresponds to the "Medium" tier in the Preferences dialog. [D]
* Preferences tiers (template 425, spec/layout-main.md): restoration threshold
  tiers **High −20 dB / Medium −30 dB / Normal −40 dB**, AutoMarker/AutoStop
  2/5/10 s, Surround distance 100…1000 cm, Units Meter/Inch, EQ A/B presets. [D]
* `NoiseFingerprints.dat` = `FpDfd` magic + 0x64 + padding, payload zeroed in the
  boxed copy: fingerprint-learned denoise has **no baked-in noise prints**. [D]

## 2.1 Provenance of the DSP payloads (round‑2 mining) [D]

Each `SddSppiv.dll` (SDDSPPIV 2.0.0.20, Spectral Design GmbH 2002) carries the
original build banner strings (Intel C++ 5.0.1, VC6 flavor). From them, the
original source layout is documented:

* Algorithm libraries included in the build: `Spectralizer, MultiBandComp,
  FPDeNoise, DeEsser, DeBuzz, SoundMorph, Reverb, Tube, Car, ParamEstimater,
  AlgoUtils, Speaker, SurroundIt, MatrixEncoder, DeNoiser, DeClicker,
  DeScratcher, Enhance, DeCrackle, StereoSpread, BassBoost, Limiter,
  Normalize, Azimuth, DeRumpler, Pitcher, Lopt, Equalizer, TrackAnalyse` —
  this is the *complete* shipped module set (29 algorithm folders), matching
  the rack roster of §1.
* DeNoiser algorithm files: `Denoise.cpp`, **`EMSR.CPP`, `LOOKEMSR.CPP`** —
  the original per‑bin gain shaping is an EMSR class spectral‑reduction with a
  precomputed lookup variant. The decomp lead pointing at `0x10093490` is
  **wrong**: that family is the MSVC CRT `_matherr`/DOMAIN/SINGULARITY handler,
  unrelated to DSP.
* DeClicker algorithm files: `ARInterpolator.cpp, BigClickDetector.cpp,
  ClickDetectPlusNEw.cpp, DeClicker.cpp, CSlidingMean.cpp,
  emphasis.cpp, LinearInterpolator.cpp, MonoDeClickAlgo.cpp,
  RunningMedianFilter.cpp`.
* Exports are ordinal‑only (no names in the export table, no RTTI type
  descriptors, no debug directory survive stripping) — algorithm classes can
  only be tied to code by this provenance + constant shapes.

Round‑2 negative results (do not re‑mine):
* `SddSppiv 0x10093490`‑family = CRT math‑error dispatcher (not a gain curve).
* `SddSppiv 0x100441b0` = ARInterpolator‑class multi‑biquad filter cascade
  (`this+0x30` selects 2 or 3 stages per channel), not a dead‑band detector.
* `SddSppiv 0x1006dfd0` = one of the comp/EQ family constructors (attack/release
  30 dB tier, 10 kHz/6 kHz/−15 dB shelf/band taps).

## 3. De-Click (original architecture) [A→D]

From the DLL construction/dispatch (functions `0x10038740`, `0x10041190`,
`0x100441b0`):

* The engine builds a **992-sample repair template** (`0x3e0` floats, plus
  a 32‑sample tail) — round 2 read the exact template builder `0x10038740`:
  a 1024‑float static shape (`0x3e0` main body + 0x3e0/4 = 992‑sample guard and
  a 32‑sample rise tail), assembled from tabulated index ramps — taps literally
  `31..1` (`.data1 0x100cff30…b8`, ascending) and `1023..992`
  (`0x100cfe9c…8`, descending phase 2) — with:
  * slope scale **1/64** (`0.015625` at `0x100cffb8`),
  * fractional anchors at positions **0, 224, 480, 736, 992 of 1023** (the
    `−224 / −480 / −736 / −992` offsets at `0x100cff2c/28/24/20`), i.e. five
    linear segments interpolated fractionally over 1023, gaps of 64–224
    samples between anchors,
  * phase 2 anchors `992/224` (the −992/−224 dupes) + record writer sentinel
    `_DAT_100cfe98/9c = 0.0` (template start = 0) and tail scalar 0.625 at
    `0x100cfea0` (second‑phase **target mask**, see below).
* Serialization [D]: anomaly counters use a max-gap of **200 samples**
  (`iVar7 < 200`); events append `(start = runPos − sourceOff − 4,
  length = run + 4, channel)` triplets (`10038740` tail). The final record
  writer's post‑guard (source‑tail check before commit, `if post_serv_cycle`
  invariant) is what pending §2.3 called "click‑record writer" — the burst
  detector never commits records past the source end and applies a
  200‑sample max-gap.
* Phase 2 burst [D]: second‑phase mask threshold = 0.625 → the engine marks a
  burst sample as *target* when `shape[·]·0.625` exceeds the target error
  buffer — this is the "dead‑band" analogue: masked (unrepaired) samples are
  those whose template shape × 0.625 stays under the error buffer.
* Class [A]: slope‑connected anomaly window over the 1024‑shape template with
  linear ramp edges; taps are straight linear index ramps — consistent with
  'rest's' linear‑guard repair.

Round‑3 (this file v0.5) closed the level‑system shape:
* The template's **five anchors are block levels, one per 256‑sample block**
  of the 1024‑sample window (`0x10039140`): the detector computes 4 level
  values per event and stores them at record offsets `+0x00/+0x08/+0x10/+0x18`
  (stride‑8 floats, `L0..L3`), which the template builder reads as the
  anchor levels for segments 0/224/480/736/992.
* The level formula [D‑shape, scales runtime]: each level is
  `exp((pos·k + b)·s1·s2)` via `0x10041190` → libm `exp` (`0x1008f7b0`,
  Intel CRT poly; its error path is the CRT `_matherr` at `0x10093490` —
  confirming that old lead was a red herring). `s1`/`s2` are the runtime
  doubles at `0x100d1050/58` (`.data`, zero/NaN in the file) and `k`/`b` live
  in the detector object — **statically unrecoverable**, honest limit reached.
* The level detector itself [D]: per 0x400‑sample block, `nspsb*` SIMD helpers
  (`nspsbThresh2/Thresh1` → `nspsbLn1` → `nspsbMpy1(4.356594)` →
  `nspsbNormalize` → `nspsbFloatToInt(..., 0x20, 0x100)`) produce a 256‑int
  tap list; a 5‑tap running sum finds the click position where the cumulative
  sum first exceeds `(total·3)>>4` (= 18.75 % of the block energy).
* Mask factor: the multiply constant at `0x100cfea0` reads **625.0** in the
  binary (not 0.625 — the round‑2 note was off by ×1000 semantics); the
  comparison `sample·625 ≤ max(pow‑threshold, sample·625)` is scale‑invariant
  on the template side only. Round‑2 doc corrected.
* Ported into the kernel [D→P]: record window `(start−4, run+4)` and
  **max‑gap 200** run merge now live in `src/dsp/declick.c` (`repair_run`);
  tests still pass with the same ≥70 % gates (spike 66.4 M→117 k,
  burst 4.0 G→1.15 M — the wider window trades some residual for exact
  original geometry).
* Still open: the runtime scales `0x100d1050/58`, the detector‑object fields
  `k/b`, and the DeCrack split (≤3‑flagged analog).

Rebuild mapping (`src/dsp/declick.c`):
* [D→P] threshold now maps sensitivity µ→ dB tier (−40…−20 dB) as
  `t = 32768·10^(dB/20)·0.5` (half amplitude because `d` is a second difference;
  equivalence verified by the spike/burst fixture suite).
* [D] guard ring radius default 11.2 ms = half of the original 992-sample
  template (44.1 kHz).
* [A] leftover: the original ALSO fits Schmitt-style dead-band extensions and
  cross-channel masking inside the big `0x100441b0` handler — our burst
  extension and ring guard cover the same role; exact coefficients of that
  dead-band have not yet been isolated. Marked for round 2.

## 4. Denoise [D + P]

* [D] block 1024 @ 44.1 kHz, ±20 dB gain scale (above).
* [D] round-3: the **DeNoiser init is `0x1008a040`** (families `FUN_100720xx`
  / `FUN_100732xx` are the core). Mined facts:
  * reduction mapping: `gain_db = DAT_100dafe0(20.0)·p + DAT_100dafe4(−20.0)`
    → **parameter p∈[0,1] maps to 0…20 dB reduction** (`20·p − 20`, sign
    convention = reduction depth); the original constructor default p=0.5
    therefore equals **10 dB**, not the 20 dB our UI defaults to (our default
    is the documented UI choice; p-mapping is the original's).
  * two `0.9` ratios: `FUN_10072d10(this, 0.9)` sets the per-bin slope ratio
    `this+0x84`, `FUN_100731f0(this, 0.9)` a second threshold — these are the
    `0.9` in the §2 gain-range table `{20, −20, 0.9}`.
  * **the per-bin curve is a log-domain spectral expander** [D-shape]: per
    band (`FUN_10072b30`/`FUN_10072d10`), mean of the band's log values is
    taken (`FUN_1008f7e0/810` = `log/exp` helpers), then per bin
    `v > mean → v = mean + (limit − 0.9)·(v − mean)`, then `exp(v)` — an
    upper-tail clamp of bins toward their band mean. The clamp `limit`
    constants (`_DAT_100d7a08/1c`) and the time-scale `_DAT_100d7a10/20` are
    runtime-initialized (`.data`, zeros in the file) — **exact values are
    statically unrecoverable**; this is the honest limit for the EMSR curve.
  * `FUN_10072020(fft, sr, 0x400, 44100.0)` re-confirms block 1024 @ 44.1 kHz.
* [P] our two-pass spectral subtraction (learn frames = frames FULLY inside the
  print region, gain `max(1 − 2·|N|/|S|, 10^(−20 dB/20))`, Hann² WOLA with
  measured Σw² normalization) matches the original's spectral-subtraction class
  and the ±20 dB gain range. The original's exact per-bin curve is the
  log-domain expander above with runtime scales — our Wiener-style curve stays
  marked provisional (curve class now known, constants not).
* The learn/apply print semantics (sample-based fraction 0.10 default in our
  shell) are our rebuild's interface choice, not the original's; the original's
  default learn ratio is unknown (the UI learn button drives the print region).
  Documented in keymaps/report dialogs accordingly. (UI reduction default:
  original's p=0.5 → 10 dB; we default to 20 dB — consider aligning, harmless.)

## 5. Not yet mined (follow‑up rounds)

1. DeNoise EMSR per-bin lookup curve — **shape now mined** (log-domain
   spectral expander, §4); the runtime scales `0x100d1050/58`, `0x100d7a08/1c`,
   `0x100d7a10/20` and detector-object `k/b` are set at run time only. Only a
   behavior run (user's VM) can pin them — do not re-mine statically.
2. DeCrack vs DeClick split parameters (burst ≤3-flagged analog; the record
   format `(start−4, run+4, channel)` is now [D] in §3, split constants still
   open).
3. Azimuth (stereo alignment) — lateral-correlation delay estimator parameters.
4. Puncher.dll — 4 exports, grayed under standing policy unless user opts in.
5. Rack parameter ranges for the grayed modules (roster of §2.1 documents all
   shipped algos; per-toggle value ranges still open).
