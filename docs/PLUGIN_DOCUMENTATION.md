# AutoLevel DJ — Full Technical Documentation

This document is the long-term reference for the plugin: how it reproduces autolevel-box, every
parameter and how it maps onto the box's, presets, session compatibility, and a changelog. Where
this document and the code disagree, **the code is the source of truth** — but please update this
doc in the same change if you touch behavior described here.

It complements [README.md](../README.md) (user-facing description, controls, build instructions).
**The DSP itself is documented with the engine**: autolevel-box's `docs/TECHNICAL_DOCUMENTATION.md`
(§2 signal chain, §3 every stage, §4 the parameter store) and autolevel-core's README.

---

## 1. What this plugin is

AutoLevel DJ is the autolevel-box appliance as a VST3 / AU / Standalone plugin (JUCE 8.0.6).
Since v3.0.0 it has no DSP of its own: it builds **autolevel-core**
(`external/autolevel-core`, git submodule), the engine the box builds, and drives it the way the
box does. The goal, set 2026-11-06: the plugin on a Mac sounds exactly like the box, with the
box's settings, minus Bluetooth and the Pi's audio I/O.

## 2. How parity is achieved

Three things, all in `src/PluginProcessor.*`:

1. **Same engine, same parameter store.** The processor owns an `autolevel::AudioEngine` — the
   box's own class: input sanitising, parameter clamping, and the chain
   (AGC → Air Lift → Pre-MBC EQ → 6-band MBC → Post Gain → Low Cut → true-peak limiter).
2. **One host parameter per box parameter.** `kHostIds[]` maps each `AudioEngine::Param` to a
   host parameter ID; ranges and defaults are the box's. Values cross over with
   `toEngineValue()` / `toHostValue()`, the identity except Low Cut's "Off" (host 19.5 ↔
   engine 0). Before each engine block the processor pushes every value with the
   allocation-free `setParam(Param, float)`. Ranges snap to exact decimal floats
   (`decimalRange()`), so 0.85 is the same float the box parses, not 85 × 0.01f.
3. **The box's block size.** The box's JACK callback always processes 256 frames. The Leveler
   updates once per `process()` call (target, slew step, a gain ramp across the block) and its
   fast-lock timer adds whole milliseconds per call, so other block sizes give a different
   result: about −70 dBFS for typical sizes, and under 1 ms per call (48 frames at 48 kHz) the
   fast lock never ends. `processChunked()` therefore collects host audio into 256-frame blocks
   and returns the previous block, processed. This costs **256 frames of latency**.

**Latency** reported to the host: `BOX_BLOCK + AudioEngine::latencyFrames()` = 256 + the
limiter's lookahead (102 frames at 48 kHz) = **358 frames (7.5 ms) at 48 kHz**. Constant:
bypass runs the same path.

**Bypass.** `getBypassParameter()` returns the `bypass` parameter, so a host's bypass button
drives the box's own bypass, which keeps the audio flowing through the limiter's delay line
untouched. `processBlockBypassed()` (hosts that bypass without the parameter) runs the same path
forced to bypass. Either way the latency never changes.

**Not done on purpose.** `releaseResources()` does not reset the engine: the box never clears its
loudness history on its own, so a DAW stopping the transport doesn't either. `prepareToPlay()`
starts fresh (as a box restart does).

### Verification

- `tests/plugin_test.cpp` (built as `plugin_test`, run by CI): the processor against
  `AudioEngine` driven like the box, compared **bit for bit** after the 256-frame delay — at the
  defaults and with every setting changed; at 44.1, 48 and 96 kHz; with host blocks of 1 to 2048
  frames, fixed and ragged. Also: every default and every range end equals the box's; bypass is
  untouched audio at the full latency; presets; session state.
- Against the box **as built** (autolevel-core `tools/box_render`, compiled with the box's
  `-O3 -ffast-math -fno-finite-math-only`): `plugin_test --render` output differs by at most
  −138 dBFS on a 60 s program (below 24-bit resolution), and is identical to a render built
  without fast-math. The difference is the compiler's rounding, not the processing; the Pi build
  (GCC, ARM) rounds its own way too.
- `auval -v aufx AlDj DstN`: passes.

## 3. Parameter reference

| Host ID | Box ID (`AudioEngine`) | Range | Default | Notes |
|---|---|---|---|---|
| `target_lufs` | `target_lufs` | −20 … −6 LUFS, 0.5 | −14 | Also sets MBC thresholds (target − 18 dB) |
| `max_boost` | `max_boost` | 0 … 18 dB, 0.5 | 12 | Not on the box's web UI; in its presets/API |
| `max_cut` | `max_cut` | 0 … 18 dB, 0.5 | 12 | As above |
| `level_response` | `level_response` | 0 … 1, 0.01 | 0.85 | Loudness memory half-life |
| `slew_speed` | `slew_speed` | Slow / Normal / Fast | Normal | Choice index = box value |
| `freeze_breakdowns` | `freeze_breakdowns` | bool | on | |
| `breakdown_threshold` | `breakdown_threshold_lu` | 3 … 15 LU, 0.5 | 7 | New in v3; UI buttons 5 / 7 / 9 as on the box |
| `air_lift` | `air_exciter` | Off / Low / Med / High | Off | New in v3 |
| `eq_sub` … `eq_air` | `pre_eq_band_0` … `_5` | ±12 dB, 0.1 | 0 | v2 IDs kept; now the box's pre-MBC bells |
| `compression_amount` | `compression_amount` | 0 … 1, 0.01 | 0.5 | Ratio 1 + amount × 4 (up to 5:1) |
| `tone_slope` | `tone_slope` | −3 … 0 dB/oct, 0.1 | 0 | v2 default was −1.5 |
| `contour_sub` … `contour_air` | `custom_band_0` … `_5` | ±12 dB, 0.1 | 0 | New in v3; replaces Target Profile |
| `mbc_attack` | `mbc_attack_ms` | 1 … 100 ms, 0.1, skewed | 15 | Sub ×2 |
| `mbc_release` | `mbc_release_ms` | 20 … 1000 ms, 1, skewed | 200 | Sub ×2; v2 allowed 5000 |
| `mbc_release_mode` | `mbc_auto_release` | Manual / Auto | Manual | Choice index = box bool |
| `mbc_detector` | `mbc_detector_rms` | 0 … 1, 0.01 | 0 (Peak) | |
| `post_mbc_gain` | `post_mbc_gain` | ±24 dB, 0.1 | 0 | v2 was ±12 |
| `hpf_freq` | `hpf_freq` | Off (19.5), 20 … 50 Hz, 0.5 | 30 | Host 19.5 = engine 0 = Off |
| `ceiling_db` | `ceiling_db` | −3 … 0 dBFS, 0.1 | −0.3 | |
| `bypass` | `bypass` | bool | off | Host bypass parameter |

Removed in v3: `target_profile` (Modern Mix = contour +1.5, −2.5, +0.5, +1.0, −2.0, +1.5),
`eq_position`, `limiter_lookahead`. Version hints: v2 IDs keep 1, new IDs use 3.

## 4. Presets

`PRESETS` in the header loads and saves **box-format JSON** — byte for byte what the box's
`AudioEngine::getStateJson()` writes, so a file moves freely between the box's preset folder
(`~/.local/share/autolevel-box/presets/` on the Pi) and the plugin (default folder
`~/Documents/AutoLevel/Presets`).

- **Load** (`loadPresetJson`): parsed by the box's own `loadStateFromJson` into a fresh engine
  (box defaults first, then the preset's fields, clamped; pre-2026-10-03 `mbc_speed` migrated),
  then written to the host parameters, so the result is automatable and saved with the session.
  The preset's `bypass` is ignored: loading a preset never bypasses the plugin.
- **Save** (`getPresetJson`): the current host parameters through the box's serialiser.
- **Reset to Default**: every parameter to its default = the box's "Default" preset.

## 5. Sessions and v2 compatibility

State is the APVTS tree as XML (`stateVersion` 3). On load, parameters the saved state lacks
are filled in with their defaults *in the tree* before `replaceState()` (no
`setValueNotifyingHost`, so restoring a session doesn't mark it edited). A v2 session therefore
keeps every setting that still exists — clamped to the box's ranges — ignores removed ones, and
gets the box defaults for the new ones.

## 6. UI

The v2 layout and look are kept (`src/PluginEditor.*`, fixed 840×700 canvas scaled to the
window). Changes for v3: header **PRESETS** (replaces the lookahead switch); **Air Lift** row and
live lift readout in the Tonal Target card (replaces the profile buttons); breakdown sensitivity
**5LU / 7LU / 9LU** beside the freeze toggle; Tone Shaper header **FADERS: CONTOUR / PRE-EQ**
choosing what the six band faders edit (replaces the EQ position switch), like the box's canvas
modes. The tone curve and the badges under each meter show the contour.
`plugin_test --snapshot out.png` renders the editor to an image after 12 s of audio.

## 7. Build & test

```bash
git submodule update --init --recursive          # if cloned without --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/dsp_test      # autolevel-core's engine tests
./build/plugin_test   # parity, mapping, presets, state
```

CI (`.github/workflows/build-and-release.yml`) checks out the submodule and runs both. Because
autolevel-core is private, the workflow needs a `CORE_REPO_TOKEN` secret: a fine-grained token
with read access to `divid3byzer0/autolevel-core`.

Engine changes are made in autolevel-core, then picked up here by moving the submodule
(`cd external/autolevel-core && git pull`, commit the new pointer) — the same in autolevel-box.

---

## 8. Changelog

### 2026-11-06 — v3.0.0: the autolevel-box engine, settings and presets

The plugin's own DSP (copied from the box in September and drifted since) is replaced by
autolevel-core, the engine the box now also builds. See §2-§6 for the design; in short:

- Sound is the box's: MBC up to 5:1 with thresholds 18 dB under Target LUFS, the box's RMS
  window and auto release, Air Lift, the pre-MBC bell EQ, the 8× true-peak limiter, flat
  tone slope by default. Verified bit for bit against the box engine.
- 256-frame block collection for identical Leveler behaviour; latency 358 frames at 48 kHz.
- Parameters mirror the box's (§3); presets are box JSON (§4); v2 sessions load (§5).
- Removed with the old DSP: Target Profile, EQ Position, Limiter Lookahead, the zero-latency
  SafetyLimiter, BandEQ shelves, `seedGain`, configurable max ratio.

### Before v3

The entries below describe the plugin's own DSP, which v3 replaced. Kept as history; file and
class names in them refer to code that no longer exists here, and their section references
(§3.3, §3.5, §3.8, §4 and so on) point to the pre-v3 layout of this document, which v3 rewrote:
see this file's git history before commit `010ffad`. Values in them (ranges, defaults, ratios)
are the values of their day; §3 has the current ones.


### 2026-10-09 — MBC auto release; release range up to 5000 ms

- `mbc_release` range 20–1000 → **20–5000 ms**. Still log-skewed with 200 ms at the centre of
  travel, so the default sits where it did; but the knob's travel above the centre now spans to
  5000, so **host automation recorded on this parameter in v2.2.0 maps to different values**
  (automation is stored normalised). Sub band's 2× is now capped at 5000 ms. Values up to
  1000 ms behave exactly as before.
- New `mbc_release_mode` (Manual / Auto, default Manual) and a RELEASE: MANUAL / AUTO switch in
  the Tone Shaper header where the Air Lift buttons used to be — §3.5.
- Manual mode bit-identical to v2.2.0 (compressor output compared sample for sample at 37, 200
  and 1000 ms release). New `testMbcLongReleaseAndAutoRelease`; 29 tests pass. JUCE build clean,
  Standalone inspected.

### 2026-10-08 (later) — Lookahead limiter, selectable Off / 1 ms / 2 ms

New `limiter_lookahead` parameter and header switch; full description in §3.8. **Default Off**,
which is the old limiter bit for bit and zero latency, so existing sessions are unchanged until
the setting is changed. Differences from the proposal in the previous entry, decided on
measurements: the hold is **20 ms**, not ~10 (10 ms left 1.2% THD at 20 Hz); the release is a
single 60 ms stage, not dual — with the hold in place steady tones measure ~10⁻⁶ % THD, so a
second stage had nothing to fix; and the last resort stays the hard clamp rather than a
soft-clip, because the gain alone already holds the ceiling (clamp hit count 0 on hot noise) and
a soft-clip would add distortion to exactly the material the clamp would ever touch.

Also: `AutoLevelEngine::processBypassed` / `latencySamples`, `PluginProcessor::processBlockBypassed`
(JUCE asserts a plugin with latency overrides it) and latency reporting through `AsyncUpdater`.

Tests: `testLimiterLookaheadLatency`, `testLimiterLookaheadNoDistortion`,
`testLimiterLookaheadCeilingAndTruePeak`, `testLimiterLookaheadSwitching`, `testBypassKeepsLatency`
— 28 pass. Sanitizer (ASan + UBSan) engine stress at 44.1 / 48 / 96 / 192 kHz with random
settings, lookahead and bypass switched constantly, random block sizes: never over the ceiling,
never non-finite. Full JUCE build clean, Standalone inspected. Not done: a live host run
(including how a DAW reacts to the latency changing), `auval`.

### 2026-10-08 — Air Lift removed; Peak/RMS detector; user attack/release; Band EQ

Four changes requested by the owner.

**1. Dynamic Air Lift removed** (the owner's "tone shaper": the stage that dynamically added air;
its bass counterpart was already gone since 2026-09-11). Deleted `src/dsp/DynamicAirLift.h`, the
`AirLiftMode`/`AirWeight` types, `EngineParameters::{airLift,airWeight}`, the four visual-state
air fields, the `air_exciter` parameter, the "AIR:" buttons and LED meter in the Tone Shaper
card header, the "AIR +" band label, and `testDynamicAirLift`. It defaulted to Off, so default
sound is unchanged; sessions that had it on lose it (the stale value is ignored on load). The
tonal target (Tone Slope, Pink Noise / Modern Mix, per-band thresholds, curve card) is **kept**.

*(A first pass the same day misread the request and removed the tonal target instead; that was
fully reverted before anything was committed. Bands.h, the threshold maths and the two
tone-profile tests are back exactly as they were.)*

**2. Peak <-> RMS detector** (`mbc_detector`) — §3.5.

**3. MBC speed presets replaced by Attack / Release knobs** (`mbc_attack`, `mbc_release`) — §3.5.
`MBCSpeed`, `MultibandCompressor::updateSpeed` and the `mbc_speed` parameter are gone. Defaults
equal the old Normal preset. The old Slow/Fast presets were 2× / 0.5× Normal, i.e. attack 30/7.5
ms and release 400/100 ms — all inside the new ranges.

**4. Band EQ** (`eq_*`, `eq_position`) — §3.5.2. The compressor's crossover tree was extracted
into `BandSplitter.h` for a first EQ attempt that reused it; that attempt was dropped (see
§3.5.2), so the extraction now has a single user. It is behaviour-neutral (see Verification) and
left in place; folding it back into `MultibandCompressor.h` would also be fine.

**UI.** The editor grew from 840×660 to 840×700 (still aspect-locked and scalable). The Tone
Shaper card is 40 px taller and each band column now holds the GR meter and, beside it, the
band's EQ fader; the EQ Before/After switch replaced the MBC Speed buttons in its header. The
controls card is a 6×2 grid: row 1 Target LUFS, Max Boost, Max Cut, Level Response, Post Gain,
Low Cut; row 2 Compression, Tone Slope, Detector, MBC Attack, MBC Release, Limiter Ceiling. The
decorative "Safety Limiter / 24 dB/oct Sub Cut / Clip-Free Output" badge was dropped to make room.

**Verification.** `dsp_test`: 23 tests pass (`testMbcSpeedBallistics` → `testMbcAttackRelease`;
added `testMbcLiveBallisticsChange`, `testMbcRmsDetector`, `testBandEqFlatAndPerBand`,
`testBandEqGainSmoothing`, `testEqPositionRouting`; removed `testDynamicAirLift`). The
compressor was compared against the committed (pre-change) code on a 6 s tonal-plus-noise
stereo signal at default settings: **bit-identical output**; the untouched
`testMbcHasNoMakeupGain` and `testMaxCompressionRatio` reproduce their committed numbers exactly
(−8.64 / −9.81 dB; −6.97 / −7.75 dB). Engine stress run (random extreme settings, random block
sizes, ASan + UBSan): output always finite and ≤ the ceiling. Full JUCE build
(VST3/AU/Standalone) clean with no project warnings; the Standalone was launched and inspected
visually. Not done: a live-host run, and `auval`.

**Limiter:** measured here, then implemented the same day — see the next entry.

### 2026-09-29 — Removed MBC auto-makeup (ported from autolevel-box)

Mirrors autolevel-box commit `40befa9`. Auto-makeup was one broadband gain on the recombined
MBC output — a weighted average of the six bands' gain reduction (0.10/0.20/0.25/0.25/0.15/0.05,
clamped 0–12 dB) — so it never changed the band-to-band balance that is the tone shaping. The
owner removed it anyway: Post Gain already covers makeup by hand, and it is one fewer automatic
gain stage moving the output level on its own.

It was never a plugin parameter here (always on), so only the DSP changed: `MBCParams::autoMakeup`,
`EngineParameters::mbcAutoMakeup`, the two `EngineVisualState` makeup fields and
`MultibandCompressor::getAutoMakeupGainDb()` are gone, and the 32-sample makeup sub-block loop
is now a plain per-sample loop. `PluginProcessor`/`PluginEditor` never referenced any of it.

**Behavior change:** output after the MBC is now quieter by however much it compresses, until
Post Gain is raised. Host session state is unaffected (no parameter was removed).

Tests: `testMbcAutoMakeupGain` replaced by `testMbcHasNoMakeupGain` (heavy compression on a
1 kHz tone: output drops by band 2's own gain reduction within 1.5 dB — −8.6 dB against a
−9.8 dB GR; confirmed to **fail** against the previous code, which measured −4.3 dB). Full JUCE
build (VST3/AU/Standalone) clean.

### 2026-09-19 — Configurable max compression ratio

`MBCParams::maxRatio` / `EngineParameters::maxCompressionRatio` set the ratio reached at
`compressionAmount = 1.0`. Both default to `Bands::MAX_RATIO` (4.0), so the plugin and every
existing host are bit-identical; only a host that raises it sees a change. Added for the
AutoLevel Android player, which wanted a firmer ceiling at 100%. `testMaxCompressionRatio`
asserts the default is unchanged and that a higher ratio produces more gain reduction on the
same signal (4:1 → −6.97 dB worst band, 6:1 → −7.75 dB).

### 2026-09-18 — Seed gain

Added `Leveler::seedGain(float db)` and `AutoLevelEngine::seedGain(float db)` so a host that
already knows a track's loudness can start the correction at the right value rather than
converging to it. See §3.3.1. No change to any existing behaviour or parameter: the plugin does
not call it, and an engine that never seeds behaves exactly as before. Six assertions added to
`tests/dsp_test.cpp` (`testSeedGain`) covering application, the advantage over converging from
zero, the measurement overriding a wrong seed, the hard clamp, and output safety.

Motivated by the AutoLevel Android player (`autolevel-player`), which consumes this DSP core as
a submodule and has ReplayGain tags for every track in its library.

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
   `AutoLevelEngine.h`, dead `LevelerParams::targetLUFS` default aligned to −14, and a section
   added to the (pre-v3) doc explaining the relationship.
   **Superseded:** the offset is now **18 dB**, not 15 (owner's decision of 2026-10-03, made in
   the box and arriving here with autolevel-core in v3.0.0). The engine computes
   `params.targetLUFS - Bands::MBC_THRESHOLD_BELOW_TARGET_DB` (= 18), so the base threshold is
   −32 dBFS at the default −14 LUFS, and `Bands::MBC_THRESHOLD_DB` (−24) is no longer reached at
   any Target LUFS the UI allows (−20 to −6). The relationship is now in §3 (`target_lufs`). The
   −15 above is historical.
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
  sub-bass vs. positive lift on a genuinely deficient one) — just conservative. Left unchanged
  at the time; Bass Lift was later removed entirely (see the 2026-09-11 entry near the end of
  this changelog), so this describes behavior that no longer exists in the codebase.
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

### 2026-09-11 (later still) — Removed Bass Lift entirely

`autolevel-box` (this plugin's sibling, sharing the same `src/dsp/` core) went through several
same-day redesigns of its own Bass Lift — shelf EQ, then a harmonic exciter, then a sub-harmonic
synthesizer — before the project owner asked to drop the adaptive-detector approach entirely and
replace it with a plain manual 6-band EQ ahead of the MBC. The project owner then asked to port
that same change here, then reconsidered mid-port and asked for **only** the removal, not the
new EQ: "don't port the eq to the vst, just remove the sub lift."

Deleted `src/dsp/DynamicBassLift.h` outright (not just unwired) and removed every reference:
`AutoLevelEngine.h` (`#include`, the `bassLift`/`subWeight` params and their `SubWeight` type
alias, the `activeBassLift`/`bassLiftDb`/`activeSubWeight`/`subInjectedLevel` visual-state
fields, the `m_bassLift` member and its `prepare()`/`process()`/`doReset()` calls, the Stage 1.5
comment), `PluginProcessor.h/.cpp` (`ID_SUB_WEIGHT`, `m_subWeightParam`, the "Sub Weight"
`AudioParameterChoice`, its `processBlock()` mapping), and `PluginEditor.h/.cpp` (the
`m_subWeightBox`/label/four buttons/attachment, the `MultibandMeterRack`'s `subWeight` parameter
and "SUB +" band-name highlight logic, the header's real-time Bass Lift activity LED meter in
the Tone Shaper card, and the `timerCallback()` sync for those buttons). `DynamicAirLift.h` and
everything around Air Lift were not touched - confirmed by a repo-wide grep before and after,
which turned up only one pre-existing historical comment in `DynamicAirLift.h`'s own doc comment
that references `DynamicBassLift`'s technique, deliberately left alone.

Nothing replaces Bass Lift here - no new EQ, no new control. The plugin's window layout is
unchanged (removing the "Bass" segmented buttons and LED meter just left the same blank header
space in the Tone Shaper card that Air Lift's own controls already had room around).

All 18 test functions in `tests/dsp_test.cpp` pass (down from 19 assertions across 18 functions
- `testDynamicBassLift` removed, nothing added in its place, matching the "just remove" scope);
full JUCE CMake build verified clean; the Standalone app was launched and screenshotted to
confirm the Tone Shaper header now shows only "AIR:" and "SPEED:" controls with no layout
artifacts where "BASS:" used to be.
