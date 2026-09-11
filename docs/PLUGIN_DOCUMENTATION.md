# AutoLevel DJ — Full Technical Documentation

This document is the long-term reference for this project: what the plugin does, how every
piece of the signal chain works, what every parameter actually does (and its real vs.
documented range/default), and a changelog of anything altered outside of normal feature work
(bug fixes, calibration corrections, doc corrections). It exists so that returning to this
project after a long gap — or handing it to someone else — doesn't require re-deriving any of
this from the source.

It complements, but does not replace, [README.md](../README.md) (user-facing pitch/build
instructions) and the inline comments in the source itself. Where this document and the code
disagree, **the code is the source of truth** — but please update this doc in the same change
if you touch behavior described here.

---

## 1. What this plugin is

AutoLevel DJ is a VST3 / AU / Standalone audio plugin (JUCE 8, C++20) meant to sit on the
**master output** of a DJ mixer/software, before the signal reaches the PA / power amps. It is
the VST sibling of two other AutoLevel projects (see the parent memory notes if you maintain
all three): an Android app that loudness-matches a phone's music player session, and
`autolevel-box`, a headless Raspberry Pi daemon doing the same job in hardware. All three share
DSP lineage — fixes are ported between them (see `git log` for "port v2.0 DSP fixes from
autolevel-box").

The problem it solves: a DJ set strings together tracks mastered decades and genres apart
(disco at −14 LUFS vs. modern EDM at −6 LUFS), and naive automatic gain control either pumps
during blended transitions or blows up gain during a quiet breakdown right before a drop. This
plugin's job is to even out perceived loudness across tracks and to shape tone consistently,
without those two failure modes.

---

## 2. Signal chain

Every audio block passes through these stages in this exact order (see
`AutoLevelEngine::process()` in [`src/dsp/AutoLevelEngine.h`](../src/dsp/AutoLevelEngine.h)):

```
Input (stereo float)
  │
  ▼
[0] Input sanitizer — clamps to ±8.0, zeroes NaN/Inf (bit-pattern check, not std::isnan/isinf)
  │
  ▼
[1] LoudnessMeter — ITU-R BS.1770-4 K-weighting + EBU R128 gated integration
  │     produces: momentary (400ms), short-term (3s), integrated (gated whole-history) LUFS
  ▼
[2] Leveler (AGC) — computes target gain from (targetLUFS - integratedLUFS), clamped to
  │     [-maxCutDb, +maxBoostDb], asymmetric slew-rate limited, with breakdown-freeze
  ▼
[3] DynamicBassLift — dynamic low-shelf upward expansion (< 100 Hz), 0 added distortion
  ▼
[4] DynamicAirLift — dynamic high-shelf upward expansion (> 6.5 kHz) + sibilance ducking
  ▼
[5] MultibandCompressor (MBC) — 6-band Linkwitz-Riley crossover, per-band soft-knee
  │     compression, phase-corrected recombination, adaptive auto-makeup gain
  ▼
[6] Post-MBC Gain — simple manual trim (dB), applied only if |gain| > 0.01 dB
  ▼
[7] HighPassFilter — 4th-order (24 dB/oct) Butterworth low cut, 20–50 Hz
  ▼
[8] SafetyLimiter — 1ms attack / 60ms release, 20:1 ratio, plus a hard sample clamp at the
  │     ceiling as a last-resort safety net
  ▼
Output (stereo float) → also feeds a lock-free 3-buffer "visual state" the UI thread reads at 60Hz
```

This exact order matters: Bass/Air Lift run *before* the MBC so the MBC can "polish" whatever
they add; Post-Gain and the HPF both run *after* the MBC but *before* the limiter, so manual
trim and subsonic cleanup are still caught by the final safety stage.

### Why the DSP core has no JUCE dependency

Everything under `src/dsp/` is plain C++20 (`<cmath>`, `<array>`, `<atomic>`, …) with zero JUCE
includes. This is deliberate: `tests/dsp_test.cpp` links and runs `src/dsp/*` directly, without
fetching or building JUCE, so the DSP correctness suite is fast to run and has no external
dependency:

```bash
clang++ -std=c++20 -O0 -g -Isrc -UNDEBUG -o dsp_test tests/dsp_test.cpp
./dsp_test
```

`src/PluginProcessor.*` and `src/PluginEditor.*` are the only JUCE-dependent files — they're
thin glue: parameter management (`AudioProcessorValueTreeState`) and the GUI. If you add new
DSP behavior, prefer putting the math in `src/dsp/` (testable, portable) and keep
`PluginProcessor.cpp` limited to reading parameters and calling into the engine.

---

## 3. DSP modules, in detail

### 3.1 Input sanitizer (inline in `AutoLevelEngine::process`)

Runs first, unconditionally (as long as the block isn't bypassed/empty). For every sample, it
reads the raw bit pattern and checks whether the exponent field is all-ones (`0x7f800000`),
which is true for both NaN and ±Inf regardless of mantissa. Anything matching becomes `0.0f`;
everything else is clamped to `[-8.0, 8.0]`. This exists because a misbehaving upstream plugin
or device can and does occasionally emit NaN/Inf, and letting that reach an IIR filter's state
variables would corrupt them permanently (a single NaN in a biquad's `z1`/`z2` propagates
forever). Verified in `tests/dsp_test.cpp::testInputSanitizerAntiNan`.

### 3.2 LoudnessMeter (`src/dsp/LoudnessMeter.h`)

Implements ITU-R BS.1770-4 K-weighting (`KWeightingFilter`, a cascaded high-shelf + high-pass
biquad pair, coefficients per the standard reference formula) followed by EBU R128-style
gated loudness integration:

- **Momentary** — mean power of the last 4× 100ms steps (400ms window), converted to LUFS.
- **Short-term** — mean power of the last 30× 100ms steps (3s window).
- **Integrated** — a running 0.1-LUFS-resolution histogram (`-100` to `+15` LUFS, 1151 bins)
  with EBU R128's two-stage gating: an absolute gate at −70 LUFS, then a relative gate at
  (ungated mean − 10 LU). This is a *running*, causal estimate — it only gets more accurate as
  more of the track plays, same as any real-time loudness meter; there is no "look ahead."
- The momentary/short-term ring buffers are zero-initialized, so for the first ~400ms
  (momentary) / ~3s (short-term) after `reset()`, those two readings ramp up from an
  artificially low value rather than reporting "not enough data yet." This only affects the UI
  readout during that window — it does **not** cause a false breakdown-freeze trigger (verified
  by direct test: the momentary/integrated delta stays negative during ramp-up, never crossing
  the freeze threshold in the wrong direction).
- `setLevelResponse(0..1)` maps to a histogram decay half-life (`halfLifeForResponse`):
  `0.0` → infinite memory (whole-track average, "Track Hold"), `1.0` → 4 second half-life. This
  is the **"Level Response"** UI parameter. It controls how fast the *measurement* (the
  integrated LUFS estimate) forgets old material — **not** how fast the applied gain moves once
  a new target is computed (that's the Leveler's slew rate, see 3.3).

### 3.3 Leveler / AGC (`src/dsp/Leveler.h`)

Computes `rawDesired = targetLUFS - integratedLUFS`, clamps it to `[-maxCutDb, +maxBoostDb]`,
then moves the *applied* gain toward that target with an **asymmetric slew rate**:

| State | Upward rate | Downward rate |
|---|---|---|
| Fast lock (first 8s after `reset()`) | 4.0 dB/s (fixed, all speeds) | 16.0 dB/s (4×, fixed, all speeds) |
| Steady state, Slew Speed = Slow | 0.5 dB/s | 1.0 dB/s (2×) |
| Steady state, Slew Speed = Normal (default) | 0.75 dB/s | 1.5 dB/s (2×) |
| Steady state, Slew Speed = Fast | 1.5 dB/s | 3.0 dB/s (2×) |

Downward moves are always faster than upward — a track coming in "too hot" gets pulled down
quickly (protects the room/ears), while a quiet track gets boosted slowly and musically (avoids
audible pumping on transitions). The **"Slew Speed"** parameter (`LevelerParams::speed`,
`LevelerSpeed::{SLOW,NORMAL,FAST}`) only affects the *steady-state* rate — the 8-second
"fast lock" window after `reset()` always uses its own fixed 4.0/16.0 dB/s rates regardless of
this setting, since fast-lock is about establishing an initial baseline quickly, a different
concern from steady-state reactivity. This is **distinct from "MBC Speed"** (§3.5), which
governs the multiband compressor's per-band ballistics, not the AGC gain rider. It's also
distinct from **"Level Response"** (§3.2), which controls how fast the *measurement* (integrated
LUFS estimate) reacts — Slew Speed controls how fast the *applied gain* chases whatever that
measurement currently says. The "fast lock" window only starts counting once
`blocksIntegrated >= 5` (~500ms of valid, gated audio) and only resets via `Leveler::reset()`
(wired to the "Reset Set / Integration" button, or `prepareToPlay`) — it does **not**
automatically reset at the start of every new track, only on manual reset or plugin
(re)initialization.

**Breakdown freeze:** if `integratedLUFS - momentaryLUFS > breakdownThresholdLU` (currently
hardcoded at 7 LU — not user-adjustable, see §5), upward gain movement is frozen (downward is
still allowed) until the momentary level recovers. This is what stops the leveler from boosting
gain into a quiet breakdown right before a drop.

The actual gain is applied to audio via `Leveler::processBlock()`, which linearly interpolates
the *linear* gain across the block sample-by-sample (not a step change), avoiding zipper noise.

### 3.4 DynamicBassLift / DynamicAirLift (`src/dsp/DynamicBassLift.h`, `DynamicAirLift.h`)

A pair of "Dolby Duo"-style dynamic shelving filters — pure linear EQ (RBJ cookbook low-shelf /
high-shelf biquads, S=1 slope), with the *shelf gain itself* driven dynamically by a sidechain
energy-ratio detector, rather than any nonlinear harmonic generation:

- **Bass Lift**: compares sub-100Hz energy (`LP(100Hz)`) to a genuine ~100Hz-1kHz bandpass mid
  anchor (`LP(1000Hz) - LP(100Hz)`). Bass-deficient material (vintage/thin tracks) gets up to
  +2.5/+4.5/+6.5 dB of low-shelf boost (Low/Med/High mode); already bass-heavy material is left
  alone (ratio-based deficit calculation clamps toward 0). Correctly discriminating but
  conservative in practice: even a signal with *zero* content below 100Hz only reaches ~60% of
  a mode's ceiling, because the single-pole 100Hz detector is fairly leaky and picks up real
  bass-guitar/kick-body energy from the adjacent 100-250Hz range as "some bass present." Not
  changed as of 2026-09-11 — flagged as a possible future candidate for a different approach
  entirely (e.g. a harmonic-generator style enhancer) rather than retuning this one further.
- **Air Lift**: same idea at the top end (>6.5kHz vs. a genuine ~1-3kHz bandpass mid anchor,
  `LP(3000Hz) - LP(1000Hz)`), plus a dedicated **sibilance auto-ducker** (1ms attack / 40ms
  release on a crest-factor detector) that pulls the lift back when a transient (a harsh "S" or
  cymbal hit) would otherwise get boosted. **Bug fixed 2026-09-11**: the mid anchor used to be a
  plain `LP(2000Hz)` with no subtraction — i.e. *everything below 2kHz* — which for any real
  track vastly outweighs the air band above, so the lift read close to its ceiling almost
  regardless of actual brightness (verified: dark/moderately-bright/very-bright synthetic
  signals all measured 3.4-3.7 dB, essentially flat). Fixed to a real bandpass matching Bass
  Lift's technique; re-verified on the same three signals: 3.40/1.73/0.00 dB — now properly
  tracks brightness. See `testDynamicAirLift`'s 4th case (added in the same fix) for the
  realistic-ratio regression test that would have caught this — the original "bright" test case
  used a signal with *more* energy at 12kHz than at 2kHz, a ratio no real track has, extreme
  enough to pass despite the bug.
- Both report `getLiftDb()` for UI metering, and are fully bypassed (zero state touched, exact
  bit-identical passthrough) when their mode is `OFF` — verified in
  `testDynamicBassLift`/`testDynamicAirLift`.
- Backward-compatibility note: `EngineParameters` has *both* a newer `bassLift`/`airLift` field
  and an older `subWeight`/`airWeight` field (type-aliased to the same enums). The engine uses
  whichever is non-OFF, preferring the new field
  (`AutoLevelEngine.h`: `effBass = (params.bassLift != OFF) ? params.bassLift : params.subWeight`).
  **`PluginProcessor.cpp` only ever sets `subWeight`/`airWeight`** (from the "Sub Weight"/"Air
  Exciter" combo boxes) — `bassLift`/`airLift` are effectively dead fields in the shipped
  plugin, kept for any code/tests that construct `EngineParameters` directly with the newer
  names.

### 3.5 MultibandCompressor / MBC (`src/dsp/MultibandCompressor.h`, `Bands.h`, `Biquad.h`)

The most involved module. Splits the signal into 6 bands via a tree of 4th-order
Linkwitz-Riley (LR4) crossovers at 120 / 400 / 1200 / 3500 / 8000 Hz, compresses each band
independently (6 dB soft-knee, per-band attack/release), then sums the bands back together.

**Bands:**

| # | Name | Range | Attack | Release |
|---|---|---|---|---|
| 0 | Sub | 20–120 Hz | 30 ms (Normal) | 400 ms (Normal) |
| 1 | Bass | 120–400 Hz | 15 ms | 200 ms |
| 2 | Low-Mid | 400–1200 Hz | 15 ms | 200 ms |
| 3 | High-Mid | 1200–3500 Hz | 15 ms | 200 ms |
| 4 | Presence | 3500–8000 Hz | 15 ms | 200 ms |
| 5 | Air | 8000–20000 Hz | 15 ms | 200 ms |

"MBC Speed" (Slow/Normal/Fast) scales all six pairs at once: Slow doubles both times, Fast
roughly halves them (see `MultibandCompressor::updateSpeed()`). **This is a completely
different control from the AGC's "Slew Speed"** (§3.3, §4) — it only affects how quickly each
band's compressor envelope reacts, not how fast the overall makeup gain rider moves. Historical
note: the README used to document a "Slew Speed" control that, at the time, didn't actually
exist anywhere (this MBC Speed control was the only "speed" knob that shipped) — a real,
separate AGC Slew Speed parameter was added on 2026-09-11 (§8) specifically to make that
documented behavior true.

**Phase-corrected recombination:** naively summing an LR4 low-pass and high-pass from the same
crossover does *not* reconstruct flat — it sums to a 2nd-order allpass at the crossover
frequency, not unity. A tree of several such crossovers (as used here) would otherwise produce
audible notches (one was measured at 5.13 dB at 1200 Hz before this was fixed — see `git log`
for "Fix 6-band crossover phase alignment"). Each band is now passed through the allpasses of
whichever crossovers its own path *didn't* go through
(`MultibandCompressor::AP_COMPENSATION` table + `AllpassLR4`/`Biquad::Type::Allpass`), which
cancels this out. `tests/dsp_test.cpp::testLR4CrossoverSummation` verifies flat magnitude
reconstruction across 14 test frequencies spanning all 6 bands to better than 0.10 dB (measured
worst case: ~3×10⁻⁷ dB — i.e. essentially exact).

**Per-band thresholds** (`Bands::thresholdsFor`) are computed from: a tone-slope tilt (power
per octave, "Tone Slope"/"Tone Tilt" parameter) + a bandwidth term (so wider bands get a higher
threshold, correcting for the fact they naturally carry more energy) + an optional profile
contour offset (`MODERN_MIX` applies genre-tuned per-band offsets; `PINK_NOISE` applies none;
`CUSTOM` would apply arbitrary user offsets but **is not reachable from the actual plugin UI**
— the "Target Profile" combo box only offers 2 of the 3 `TargetProfile` enum values). The whole
curve is then re-centered so its average equals `baseThresholdDb`, preserving overall
calibration regardless of slope/contour.

**`baseThresholdDb` and the Target-LUFS coupling (important — see §5):**
`mbcParams.baseThresholdDb = params.targetLUFS - 15.0f`. This means the MBC's absolute
threshold level tracks the AGC's Target LUFS parameter. `Bands::MBC_THRESHOLD_DB` (−24 dBFS) is
the "canonical" calibration point this formula is designed to hit — but only when Target LUFS
is at −9. At the plugin's actual shipped default (Target LUFS = −14), `baseThresholdDb` comes
out to **−29 dBFS**, not −24 — the MBC is measurably more aggressive at default settings than
the "exactly matches Android Shaper.kt" calibration the rest of the code (constants, tests)
assumes. This was found and the misleading comment fixed on 2026-09-11 (§8); the numeric
*behavior* itself was intentionally left unchanged (targetLUFS = −14 is confirmed correct) — if
recalibrating the MBC to track −24 dBFS at the real default is ever wanted, change the `15.0f`
offset to `10.0f` (`-14 - 10 = -24`), understanding that this changes live audio behavior for
every preset that doesn't override Target LUFS.

**Auto-makeup:** a 6-band weighted average of each band's current gain reduction
(weights: Sub 0.10, Bass 0.20, Low-Mid 0.25, High-Mid 0.25, Presence 0.15, Air 0.05 — biased
toward the perceptually dominant midrange) drives a smoothed makeup gain, recomputed every
32-sample sub-block (sub-millisecond adaptation, ~0.67ms latency at 48kHz between "true" gain
reduction and makeup catching up — an intentional smoothing tradeoff, not a bug). **Not exposed
as a plugin parameter** — always on (`EngineParameters::mbcAutoMakeup` defaults to `true` and
`PluginProcessor.cpp` never sets it from any APVTS parameter, because none exists for it).

### 3.6 Post-MBC Gain

A single manual trim (`postMbcGainDb`, ±12 dB), applied as a flat linear multiply, skipped
entirely (not even a unity multiply) when `|dB| ≤ 0.01` to save a pass over the buffer.

### 3.7 HighPassFilter (`src/dsp/HighPassFilter.h`)

4th-order Butterworth high-pass (two cascaded 2nd-order sections, Q₁≈0.5412, Q₂≈1.3066 — the
correct pole pair for a maximally-flat 4th-order Butterworth, not two identical 2nd-order
sections), 20–50 Hz cutoff. Verified against theoretical response (−3.01 dB at cutoff, ~−24 dB
one octave below) in `testHighPassFilter`. There is no separate on/off control in the UI — the
parameter range (20–50 Hz) never allows a value that would disable it
(`hpfEnabled = hpfCutoffHz >= 20.0f` is always true given that range), so this filter is
effectively always active. This matches the shipped UI (only a frequency knob, no toggle), not
a bug.

### 3.8 SafetyLimiter (`src/dsp/SafetyLimiter.h`)

1ms attack / 60ms release, 20:1 ratio soft limiter on the stereo-linked peak envelope, **plus**
a hard per-sample clamp to `±ceilingLin` as an absolute last-resort safety net for anything the
smoothed limiter doesn't fully catch (e.g. a single-sample transient inside the attack window).
The reported gain-reduction meter value has an "instant attack, ~16 dB/s release" smoothing
applied purely for legible metering — it does not affect the audio path.

### 3.9 Visual state (lock-free triple buffer)

`AutoLevelEngine` publishes a full `EngineVisualState` snapshot at the end of every
`process()` call into one of 3 buffers (`m_visualStates`), never the one currently marked
"published" (`m_activeVisualIndex`, `memory_order_release`/`_acquire`). Because the writer
cycles through all 3 buffers in a fixed rotation (index sequence 1→2→0→1→2→0…), the buffer it
writes to was last published *two full audio blocks ago* — far more headroom than the UI's 60Hz
`getVisualState()` read (a plain struct copy) could ever need, so there is no meaningful race
even though nothing here uses a mutex. This scheme replaced an earlier 2-buffer version that
could race (see `git log`, "removing mutexes on realtime audio path").

`reset()` (called from the UI's "Reset Set" button) does **not** touch DSP state directly —
it just sets an atomic flag that `process()` consumes and services (via `doReset()`) at the top
of the *next* audio block, specifically so filter/measurement state is only ever mutated from
the audio thread.

---

## 4. Parameter reference

All parameters are declared in `AutoLevelDJAudioProcessor::createParameterLayout()`
([`src/PluginProcessor.cpp`](../src/PluginProcessor.cpp)) and are standard
`AudioProcessorValueTreeState` parameters — automatable and saved/restored with the plugin
state (`getStateInformation`/`setStateInformation`, XML via `ValueTree`).

| Parameter ID | UI Label | Type | Range | Default | Consumed by |
|---|---|---|---|---|---|
| `target_lufs` | Target LUFS | Float | −24 to −4 LUFS (0.5 step) | **−14 LUFS** | `Leveler` target, and (via `- 15.0f`) `MultibandCompressor` base threshold — see §3.5 |
| `max_boost` | Max Boost | Float | 0 to 18 dB (0.5 step) | 12 dB | `Leveler` upward clamp |
| `max_cut` | Max Cut | Float | 0 to 18 dB (0.5 step) | 12 dB | `Leveler` downward clamp |
| `level_response` | Level Response | Float | 0 to 1 (0.01 step) | 0.85 | `LoudnessMeter` histogram decay half-life (§3.2) — **not** the leveler's slew rate |
| `slew_speed` | Slew Speed | Choice | Slow / Normal / Fast | Normal | `Leveler` steady-state gain slew rate (§3.3) — **not** `mbc_speed` below, a different stage entirely. Added 2026-09-11 (§8) |
| `compression_amount` | Compression | Float | 0 to 1 (0.01 step) | 0.50 | `MultibandCompressor` ratio (1.0–4.0) and enable gate (`>= 0.02`) |
| `tone_slope` | Tone Slope ("Tone Tilt") | Float | −3.0 to 0.0 dB/oct (0.1 step) | −1.5 dB/oct | `MultibandCompressor` threshold tilt |
| `target_profile` | Target Profile | Choice | Pink Noise (Linear) / Modern Mix (Contoured) | Modern Mix | `MultibandCompressor` threshold contour. Note: `TargetProfile::CUSTOM` exists in the DSP enum but has no 3rd UI choice — unreachable from the plugin |
| `mbc_speed` | MBC Speed | Choice | Slow / Normal / Fast | Normal | `MultibandCompressor` attack/release ballistics (§3.5) |
| `sub_weight` | Bass (Dynamic Bass Lift) | Choice | Off / Low / Medium / High | Off | `DynamicBassLift` mode |
| `air_exciter` | Air (Dynamic Air Lift) | Choice | Off / Low / Medium / High | Off | `DynamicAirLift` mode |
| `post_mbc_gain` | Post Gain | Float | −12 to +12 dB (0.1 step) | 0 dB | Post-MBC manual trim |
| `hpf_freq` | Low Cut | Float | 20 to 50 Hz (0.5 step) | 30 Hz | `HighPassFilter` cutoff (always enabled, see §3.7) |
| `ceiling_db` | Limiter Ceiling ("Amp Ceiling") | Float | −3.0 to 0.0 dBFS (0.1 step) | −0.3 dBFS | `SafetyLimiter` ceiling |
| `freeze_breakdowns` | Freeze Breakdowns | Bool | On/Off | On | `Leveler` breakdown-freeze enable (threshold itself is hardcoded at 7 LU, see §5) |
| `bypass` | Bypass | Bool | On/Off | Off | Whole-engine bypass (`AutoLevelEngine::process` early-returns, leaving audio untouched) |

---

## 5. Things that exist in the DSP layer but aren't exposed as parameters

These are implemented and unit-tested in `src/dsp/`, but `PluginProcessor.cpp` never sets them
from any `AudioProcessorValueTreeState` parameter, so they always run at their hardcoded
`EngineParameters` struct default:

- **`mbcAutoMakeup`** (default `true`, always on) — see §3.5.
- **`breakdownThresholdLU`** (default `7.0f` LU) — the sensitivity of breakdown-freeze
  detection. `Leveler` supports a configurable threshold (tested with 5/7/9 LU sensitivity in
  `testBreakdownFreezeSensitivity`) but the plugin only exposes a plain On/Off toggle, always
  using 7 LU ("Normal" sensitivity) when on.
- **`TargetProfile::CUSTOM`** and its `customOffsetsDb` array — implemented and tested
  (`testCustomToneProfile`), but the "Target Profile" combo box only offers 2 choices, so this
  path is dead code from the plugin's actual entry point today.

If any of these get exposed as real parameters in the future, add them to §4's table and remove
them from this list.

---

## 6. Build & test

```bash
# DSP-only correctness suite (no JUCE needed, seconds to build/run):
clang++ -std=c++20 -O0 -g -Isrc -UNDEBUG -o dsp_test tests/dsp_test.cpp
./dsp_test

# Full plugin build (fetches JUCE 8.0.6 via CMake FetchContent — first run is slow):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/dsp_test                     # same 18 tests, built via CMake this time
cmake --build build --target install_plugins   # installs VST3+AU to ~/Library/Audio/Plug-Ins
```

CI (`.github/workflows/build-and-release.yml`) builds macOS (Universal VST3/AU/Standalone) and
Windows x64/x86 (VST3/Standalone) on every version tag push or manual dispatch, runs `dsp_test`
on each platform, and publishes a GitHub Release with zipped artifacts per platform.

The test suite (`tests/dsp_test.cpp`, 18 tests) exercises `AutoLevelEngine` and every DSP
submodule directly — K-weighting calibration, LR4 crossover flatness, EBU R128 loudness
accuracy across levels, MBC speed ballistics (including a regression test that `prepare()`
doesn't silently revert a selected Slow/Fast speed back to Normal), breakdown-freeze
sensitivity, NaN/Inf sanitization, and the lock-free visual-state buffer. It does **not**
exercise `PluginProcessor`/`PluginEditor` (those need a full JUCE build and a host/GUI
environment) — as of 2026-09-11 a full CMake+JUCE build was verified to compile cleanly with
zero warnings in project code, but no live-host or GUI interaction testing has been done.

---

## 7. Known unreachable/dead code (intentionally left in place)

- `TargetProfile::CUSTOM` (§5) — UI can't select it.
- `EngineParameters::bassLift` / `airLift` fields (§3.4) — `PluginProcessor.cpp` only ever sets
  the older `subWeight`/`airWeight` aliases.
- `LoudnessMeter::m_blockSamples`, `m_gatedMs` — computed/incremented, never read.
- Several struct-level defaults that are always overwritten by a caller before use, kept only
  so directly-constructed instances (e.g. in tests) have a sane value:
  `LevelerParams::targetLUFS`, `MBCParams::toneSlopeDbPerOctave`,
  `MBCParams::baseThresholdDb` (defaults to `Bands::MBC_THRESHOLD_DB`). **If you ever change a
  "live" default (the one in `EngineParameters` or the APVTS parameter), check whether these
  dead struct defaults should track it too** — see §8, this is exactly the kind of drift that
  caused the Target LUFS documentation to go stale.

---

## 8. Changelog

Entries below cover changes made *outside* of normal feature commits (the git log is
authoritative for feature history) — specifically, bug-hunt findings and their fixes, and
documentation corrections. Keep this updated any time you fix something similar.

### 2026-09-11 — Bug hunt + parameter/documentation corrections

A structured bug hunt (build the DSP suite standalone, run all 18 tests, do a full JUCE build,
then verify specific hypotheses with small standalone test programs rather than reporting
guesses) found:

1. **MBC threshold calibration drift (real defect, quantified).**
   `AutoLevelEngine.h`'s `mbcParams.baseThresholdDb = params.targetLUFS - 15.0f` carried a
   comment claiming this is "Exact −24 dBFS at default −9 LUFS" — but the actual shipped
   default Target LUFS had been changed to −14 LUFS (in `EngineParameters`, the APVTS parameter
   default, and `PluginProcessor.cpp`'s fallback value) without updating this relationship.
   Verified by direct test: at the real default, a −24 dBFS reference tone (the canonical
   calibration point used elsewhere, `Bands::MBC_THRESHOLD_DB`) which should sit exactly at
   threshold (0.0 dB gain reduction) instead receives −0.438 dB GR, and typical −14 to −9 dBFS
   program material receives ~3 dB *more* gain reduction than the documented calibration
   intends. **Decision (per project owner): −14 LUFS is the correct, intentional default — fix
   the stale comment/documentation, not the number.** Done: comment corrected in
   `AutoLevelEngine.h`, dead `LevelerParams::targetLUFS` default aligned to −14, this doc's §3.5
   added to explain the relationship for anyone touching it later.
2. **Tone Slope ("Tone Tilt") range narrowed** from −6.0…0.0 dB/oct to **−3.0…0.0 dB/oct**
   (default unchanged at −1.5 dB/oct) — per project owner, anything steeper than −3 dB/oct
   isn't useful. Updated: `PluginProcessor.cpp`'s parameter definition, `Bands::MIN_TONE_SLOPE`/
   `DEFAULT_TONE_SLOPE` (dead constants, updated for consistency), doc comments in
   `AutoLevelEngine.h` and `MultibandCompressor.h`, and the README table.
3. **Max Boost (0–18 dB, default 12 dB) and Amp Ceiling (−3.0–0.0 dBFS, default −0.3 dBFS)**
   were already correct in code — only the README's Controls Reference table was stale (it said
   0–12 dB/+6 dB and −2.0–0.0 dBFS/−0.5 dBFS respectively). README corrected to match the code
   that actually ships.
4. **Added a real "Slew Speed" (Slow/Normal/Fast) parameter** (`slew_speed`, §4) controlling the
   `Leveler`'s steady-state gain-change rate — 0.5/0.75/1.5 dB/s upward, always 2× that
   downward; Normal is bit-exact with the original hardcoded 0.75/1.5 dB/s behavior, so
   existing sessions/presets that don't touch this parameter are unaffected. The 8-second
   fast-lock window (§3.3) deliberately keeps its own fixed 4.0/16.0 dB/s rates regardless of
   this setting. This makes the README's control table (previously misdescribing "MBC Speed" as
   the AGC slew control) actually true. New DSP-level regression test:
   `testLevelerSlewSpeed` in `tests/dsp_test.cpp`. New UI: a "SLEW: SLOW/NORMAL/FAST" segmented
   row in Card 2 (AGC Gain Correction), below the Breakdown Freeze toggle — verified visually
   via a Standalone app screenshot (no layout overlap/clipping).
5. Ruled out (tested, not bugs): loudness-meter ramp-up dilution falsely triggering
   breakdown-freeze (disproven — delta never crosses the false-positive direction); MBC
   bypass/re-enable leaving crossover filter state stale (disproven — no measurable glitch,
   settling time is sub-millisecond at these crossover frequencies).

All 18 tests in `tests/dsp_test.cpp` pass after these changes; a full JUCE CMake build (VST3/AU/
Standalone) was re-verified clean after each round of changes.

### 2026-09-11 (later) — Fixed DynamicAirLift's non-discriminating mid anchor

Reported: Air Lift engages strongly on almost every track regardless of era/brightness, while
Bass Lift feels barely perceptible even on genuinely bass-deficient material. Investigated by
comparing both algorithms' actual code against their own doc comments, then verifying with
synthetic test signals (not just the existing clean two-tone unit tests) closer to real program
material.

- **Bass Lift: not a bug.** Confirmed it correctly discriminates (0dB on a signal with real
  sub-bass vs. positive lift on a genuinely deficient one) — just conservative, per §3.4. Left
  unchanged; the project owner is considering a different approach entirely (e.g. a bass
  harmonic generator) rather than retuning this one further.
- **Air Lift: real bug, fixed.** Its mid anchor was `LP(2000Hz)` with no subtraction —
  "everything below 2kHz" — instead of the documented 1-3kHz bandpass. Since real music's energy
  is always dominated by content below 2kHz regardless of genre/era, the air/mid ratio was
  always low and the lift always read near its ceiling. Verified on three synthetic signals
  (dark / moderately bright / very bright): old code gave 3.41/3.67/3.51 dB (flat); fixed code
  gives 3.40/1.73/0.00 dB (properly tracks brightness). Fixed to a real `LP(3000)-LP(1000)`
  bandpass, matching Bass Lift's own (correct) technique. Added a 4th case to
  `testDynamicAirLift` using a realistic amplitude ratio (air quieter than mid, unlike the
  existing extreme "bright" case) that asserts the lift actually drops with brightness — verified
  this new test fails against the old code (identical 4.13/4.13 dB) and passes against the fix.

All 19 assertions across 18 test functions in `tests/dsp_test.cpp` pass; a full JUCE CMake build
was re-verified clean.
