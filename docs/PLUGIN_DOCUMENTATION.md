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
[3] BandEQ  (only when "EQ Position" = Before MBC)
  ▼
[4] MultibandCompressor (MBC) — 6-band Linkwitz-Riley crossover, per-band soft-knee
  │     compression (user attack/release, Peak<->RMS detector, thresholds on the tonal
  │     target curve), phase-corrected recombination (no makeup gain — see [5])
  ▼
[4b] BandEQ  (only when "EQ Position" = After MBC, the default)
  ▼
[5] Post-MBC Gain — simple manual trim (dB), applied only if |gain| > 0.01 dB; the only
  │     makeup stage for the MBC's gain reduction since auto-makeup was removed (2026-09-29)
  ▼
[6] HighPassFilter — 4th-order (24 dB/oct) Butterworth low cut, 20–50 Hz
  ▼
[7] SafetyLimiter — "Lookahead" Off: 1ms attack / 60ms release, 20:1 ratio, plus a hard
  │     sample clamp at the ceiling (zero latency). 1 ms / 2 ms: lookahead true-peak-aware
  │     brickwall that does not distort when pushed (adds exactly that much latency) — §3.8
  ▼
Output (stereo float) → also feeds a lock-free 3-buffer "visual state" the UI thread reads at 60Hz
```

This exact order matters: Post-Gain and the HPF both run *after* the MBC but *before* the
limiter, so manual trim and subsonic cleanup are still caught by the final safety stage. The
Band EQ runs at exactly one of its two spots ([3] or [4b]), chosen by the "EQ Position" switch —
always after the AGC, always before Post Gain and the limiter. (Stage [3] used to be the
DynamicAirLift, removed 2026-10-08 — §3.4.)

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
concern from steady-state reactivity. This is **distinct from "MBC Attack / Release"** (§3.5), which
govern the multiband compressor's per-band ballistics, not the AGC gain rider. It's also
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

### 3.4 Dynamic Bass Lift / Air Lift — removed

Both adaptive "lift" stages are gone. **Bass Lift** (`DynamicBassLift.h`, the "Sub Weight"
control) was removed on 2026-09-11; **Air Lift** (`DynamicAirLift.h`, the "Air Exciter" /
"AIR: OFF LOW MED HIGH" control, a sidechain-driven dynamic high shelf above ~6.5 kHz with a
sibilance ducker) on 2026-10-08, at the owner's request. Nothing replaces them automatically;
the manual Band EQ (§3.5.2) is the way to add bass or air now. See the changelog (§8) for what
each removal touched and, in the 2026-09-11 entries, how Air Lift used to work.

### 3.5 MultibandCompressor / MBC (`src/dsp/MultibandCompressor.h`, `BandSplitter.h`, `Bands.h`, `Biquad.h`)

The most involved module. Splits the signal into 6 bands via a tree of 4th-order
Linkwitz-Riley (LR4) crossovers at 120 / 400 / 1200 / 3500 / 8000 Hz (the tree lives in
`BandSplitter.h`; the Band EQ does not use it, §3.5.2),
compresses each band independently (6 dB soft-knee, per-band attack/release), then sums the
bands back together.

**Bands and ballistics:**

| # | Name | Range | Attack | Release |
|---|---|---|---|---|
| 0 | Sub | 20–120 Hz | 2 × MBC Attack | 2 × MBC Release |
| 1 | Bass | 120–400 Hz | MBC Attack | MBC Release |
| 2 | Low-Mid | 400–1200 Hz | MBC Attack | MBC Release |
| 3 | High-Mid | 1200–3500 Hz | MBC Attack | MBC Release |
| 4 | Presence | 3500–8000 Hz | MBC Attack | MBC Release |
| 5 | Air | 8000–20000 Hz | MBC Attack | MBC Release |

**Attack / Release are user parameters** (`mbc_attack` 1–100 ms, default 15; `mbc_release`
20–1000 ms, default 200). They replaced the old Slow/Normal/Fast "MBC Speed" preset (§8). The
defaults equal the old Normal preset, so a session that never touches them sounds as before. The
Sub band keeps its long-standing 2× factor (30 ms / 400 ms at the defaults) — broadcast chains
run the bass slower. Changing them while audio runs only swaps filter coefficients: the
envelopes keep going, so turning the knob never snaps the gain reduction to zero
(`testMbcLiveBallisticsChange`). **This is a completely different control from the AGC's
"Slew Speed"** (§3.3, §4) — it only affects how quickly each band's compressor reacts, not how
fast the overall gain rider moves.

**Level detector — Peak <-> RMS (`mbc_detector`, 0..1, default 0 = Peak).** Each band's detector
is a blend: `level = (1 − m)·peak + m·rms`, where `peak` is the stereo-linked `max(|L|,|R|)` and
`rms` is `sqrt(2 · mean-square)` of the same linked signal, averaged over a one-pole 30 ms window.
The attack/release ballistics are then applied to the blended level, as before. Two properties
worth knowing:

- **Sine-calibrated.** The `2·` makes a steady tone read the same amplitude in both modes, so
  switching detector does not change how a steady bass note is treated; the two differ only on
  how *peaky* the material is. On real music RMS reads well below peak, so **RMS mode compresses
  noticeably less at the same threshold** (raise Compression, or lower Target LUFS which lowers
  the threshold, to compensate). Measured on a high-crest-factor burst signal at 1 ms attack:
  Low-Mid GR −11.3 dB (peak) / −9.5 dB (50%) / −7.6 dB (RMS).
- **Ballistics already smooth the peak path.** With the 15 ms default attack a 1 ms burst barely
  moves the peak envelope either, so Peak and RMS sound closest at slow attack settings and
  furthest apart at fast ones.

At 0 (Peak) the code path is bit-identical to before the detector existed
(`testMbcRmsDetector`; and a bit-for-bit comparison of old vs new compressor output at default
settings on a 6 s test signal was run when this was added — see §8). The RMS average is tracked
even at 0, so moving the slider off Peak never starts from a stale value; only the `sqrt` is
skipped. The limiter stays a pure peak detector — it must.

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

**No makeup gain (auto-makeup removed 2026-09-29, §8):** the MBC's output level drops by
whatever gain reduction it applies; the Post Gain parameter (§3.6) is where that is made up by
hand.

### 3.5.2 BandEQ (`src/dsp/BandEQ.h`)

Six gain controls on the MBC's six bands — `eq_sub`, `eq_bass`, `eq_lowmid`, `eq_highmid`,
`eq_presence`, `eq_air`, each ±12 dB — and an `eq_position` switch (Before MBC / After MBC,
default After) that puts the EQ either just ahead of the MBC or just behind it. Always after the
AGC, always before Post Gain, the Low Cut and the limiter. In the UI each band's fader sits
beside that band's gain-reduction meter in the Tone Shaper card, and the switch is in that
card's header (where the Air Lift buttons were).

**Filter shapes.** The four middle bands are RBJ peaking filters centred on the band's geometric
centre (219 / 693 / 2049 / 5292 Hz) and as wide in octaves as the band itself. Sub is a low shelf
cornered at 120 Hz and Air a high shelf cornered at 8 kHz, so they cover everything beyond their
outer edge, as the compressor's outer bands do. The slider value is (nearly) the gain you get at
the band centre: exact for the four bells, 92–97% for the two shelves' centres
(`testBandEqFlatAndPerBand`). Neighbouring bands overlap smoothly — a +9 dB Bass boost leaks
+1.7 dB into Low-Mid's centre.

*Why not the compressor's LR4 split with a gain per band?* Tried first. It is literally "the same
bands", but a −9 dB cut only reached −5.6 dB at the Presence centre (neighbours' skirts leak
back in), and an untouched EQ was a phase-rotating allpass chain rather than a passthrough. The
bell/shelf version is accurate and, at 0 dB, an **exact passthrough** — every band is the
identity filter, so the EQ is bit-transparent until moved (asserted sample-for-sample).

**Smoothing.** Gains glide in dB with a ~15 ms time constant and the filters are retuned every
16 samples, so moving a band never zippers (`testBandEqGainSmoothing`). A band returning to 0 dB
snaps onto the exact passthrough once within 0.005 dB.

**Before vs After.** After MBC is the default: the compressor never sees the EQ, so what you set
is what you hear. Before MBC feeds the compressor the EQ'd signal — boosting Presence +9 dB makes
that band's compressor work ~6 dB harder in the test (`testEqPositionRouting`) and nets less
output level than the same boost after. Switching position live can click if the EQ is boosted
hard (the filters keep their state, but the signal they see changes under them); at flat settings
it is inaudible.

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

Two engines, picked by the **Limiter Lookahead** parameter (`limiter_lookahead`: Off / 1 ms /
2 ms, default Off).

**Off — the original limiter, zero latency, bit-identical to before** (checked sample for
sample, audio and GR meter, on 8 s of hot noise with transients, odd block sizes and ceiling
changes). 1ms attack / 60ms release, 20:1 ratio on the stereo-linked peak envelope, **plus** a
hard per-sample clamp to `±ceilingLin`. Measured: a steady sine pushed only 3 dB over the
ceiling comes out at ~2–3% THD and ~4.5–5.7% at +12 dB, at every frequency from 25 Hz to 5 kHz —
the 1 ms attack lets the front of every peak through and the clamp squares it off. It also only
sees sample peaks: an inter-sample-over test signal leaves it at **+2.7 dBFS true peak**.

**1 ms / 2 ms — lookahead brickwall.** Latency is exactly the lookahead (48 / 96 samples at
48 kHz, 44 / 88 at 44.1 kHz; `SafetyLimiter::lookaheadSamples`). Per sample:

1. **Detector:** stereo-linked sample peak plus a 4× true-peak estimate (polyphase
   Blackman-windowed sinc, 4 phases × 12 taps, each phase normalised to unity DC). It runs on
   the detector path only — the audio is never resampled — and it is referenced to the sample
   6 ago (the interpolator's own delay), estimating the peaks between that sample and the next.
2. **Required gain** `ceiling / peak` (1 when under), then its **minimum over the smoothing
   window plus a 20 ms hold** (monotonic queue, O(1)). The hold means successive half-cycles of
   a bass note do not let the gain bounce between them — that bounce is what makes a plain fast
   limiter distort bass. 20 ms covers 25 Hz; it was 10 ms first, which left 0.17% THD at 30 Hz
   and 1.2% at 20 Hz (now 0.00% and 0.02%), at the cost of ~10 ms slower recovery after a burst.
3. **Release:** instant down, 60 ms one-pole up.
4. **Smoothing:** two cascaded moving averages whose combined span is the window
   (`lookahead − 6 + 1` samples), so the gain glides down in an S-curve.
5. **Apply** to the audio delayed by the lookahead.

Because every term of the moving average is a minimum taken over a window that contains the
delayed sample, the applied gain is never above what that sample needs: the ceiling is held by
the gain alone. The hard clamp is kept as a last resort and counted
(`SafetyLimiter::getClampCount()`); on the hot-noise-plus-transients test it is **0**. Measured:
THD on sines pushed +3 / +12 dB is ~10⁻⁶ % from 25 Hz to 5 kHz in both modes; the inter-sample
test comes out at −0.13 dBFS true peak against a −0.3 ceiling (the 12-tap estimator under-reads
by ~0.17 dB at fs/4). 1 ms and 2 ms measure the same on these signals; 2 ms ramps the gain down
over twice the time, which is gentler on dense real transients.

**Switching live** fades out over 2 ms, swaps engines, and fades back in once audio emerges
from the new delay line (≤ ~6 ms dip, no click — `testLimiterLookaheadSwitching`). Before the
first block after `prepare()` a mode change applies at once (that is the host restoring the
saved setting). `reset()` (the "Reset Set" button) clears only the Off engine's envelope and
the meter, as before — emptying the lookahead delay line mid-song would drop audio.

**Latency reporting / bypass.** `PluginProcessor` reports `latencySamples(mode)` in
`prepareToPlay`, and re-reports it via `AsyncUpdater` (message thread) when the parameter
changes. Both bypasses keep that latency: the plugin's own Bypass button (`EngineParameters::bypass`)
and the host's bypass (`processBlockBypassed`) pass the audio through the lookahead delay
untouched (`AutoLevelEngine::processBypassed`), so toggling bypass never shifts the audio in
time. With Off, both are still exact no-ops. Hosts differ in whether they re-compensate a
latency change during playback; changing the setting between sets is safest.

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
| `slew_speed` | Slew Speed | Choice | Slow / Normal / Fast | Normal | `Leveler` steady-state gain slew rate (§3.3) — **not** `mbc_attack` / `mbc_release` below, a different stage entirely. Added 2026-09-11 (§8) |
| `compression_amount` | Compression | Float | 0 to 1 (0.01 step) | 0.50 | `MultibandCompressor` ratio (1.0–4.0) and enable gate (`>= 0.02`) |
| `tone_slope` | Tone Slope ("Tone Tilt") | Float | −3.0 to 0.0 dB/oct (0.1 step) | −1.5 dB/oct | `MultibandCompressor` threshold tilt |
| `target_profile` | Target Profile | Choice | Pink Noise (Linear) / Modern Mix (Contoured) | Modern Mix | `MultibandCompressor` threshold contour. Note: `TargetProfile::CUSTOM` exists in the DSP enum but has no 3rd UI choice — unreachable from the plugin |
| `mbc_attack` | MBC Attack | Float | 1 to 100 ms (log-skewed, 15 ms at centre of travel) | 15 ms | `MultibandCompressor` attack, bands 1–5 (Sub runs at 2×) — §3.5. Added 2026-10-08, replaces `mbc_speed` |
| `mbc_release` | MBC Release | Float | 20 to 1000 ms (log-skewed, 200 ms at centre) | 200 ms | `MultibandCompressor` release, bands 1–5 (Sub runs at 2×). Added 2026-10-08 |
| `mbc_detector` | Detector | Float | 0 (Peak) to 1 (RMS), 0.01 step | 0 = Peak | `MultibandCompressor` level-detector blend — §3.5. Added 2026-10-08 |
| `eq_sub` … `eq_air` | EQ Sub / Bass / Low-Mid / High-Mid / Presence / Air | Float ×6 | −12 to +12 dB (0.1 step) | 0 dB | `BandEQ` band gains — §3.5.2. Added 2026-10-08 |
| `eq_position` | EQ Position | Choice | Before MBC / After MBC | After MBC | Where `BandEQ` sits relative to the MBC. Added 2026-10-08 |
| `post_mbc_gain` | Post Gain | Float | −12 to +12 dB (0.1 step) | 0 dB | Post-MBC manual trim |
| `hpf_freq` | Low Cut | Float | 20 to 50 Hz (0.5 step) | 30 Hz | `HighPassFilter` cutoff (always enabled, see §3.7) |
| `limiter_lookahead` | Lookahead (header) | Choice | Off / 1 ms / 2 ms | Off | `SafetyLimiter` engine and the plugin's latency (0 / 1 / 2 ms) — §3.8. Added 2026-10-08 |
| `ceiling_db` | Limiter Ceiling ("Amp Ceiling") | Float | −3.0 to 0.0 dBFS (0.1 step) | −0.3 dBFS | `SafetyLimiter` ceiling |
| `freeze_breakdowns` | Freeze Breakdowns | Bool | On/Off | On | `Leveler` breakdown-freeze enable (threshold itself is hardcoded at 7 LU, see §5) |
| `bypass` | Bypass | Bool | On/Off | Off | Whole-engine bypass (`AutoLevelEngine::process` early-returns, leaving audio untouched) |

---

### 3.3.1 Seed gain (`Leveler::seedGain`, `AutoLevelEngine::seedGain`)

Added 2026-09-18. Preloads the leveller's gain from an **external** estimate of how loud the
source is, so the correction starts at the right value instead of slewing to it over the first
several seconds of a track.

The plugin does not call this — a DJ mixer's master bus has no metadata to seed from. It exists
for hosts that know a track's loudness *before* playing it: the AutoLevel Android player uses a
Subsonic `replayGain.trackGain` tag, converting it with `trackLufs = -18.0 - trackGain`
(ReplayGain 2.0 references −18 LUFS) and seeding `targetLUFS - trackLufs`.

Behaviour:

- Sets current gain, target gain **and** the smoothed linear gain, so there is no ramp from
  unity. That is why it must only be called at a boundary with no audio flowing; mid-stream it
  would be an audible step. `AutoLevelEngine::seedGain` defers it to the audio thread for the
  same reason `reset()` is deferred, and applies it *after* a pending reset so a seed requested
  alongside one survives.
- Leaves the fast-lock timer at zero. Nothing has been measured yet, so the leveller should
  still be free to move quickly once it has been.
- Clamped to ±24 dB (`Leveler::SEED_LIMIT_DB`), independent of Max Boost / Max Cut. Those are
  parameters and are applied by `update()` once a measurement exists; the hard bound only stops
  a nonsensical tag blasting the output before then.
- **Is not authoritative.** The next `update()` recomputes the target from what is actually
  heard, so a wrong seed costs a brief settle rather than being trusted for the whole track.
  There is a test feeding a loud signal after a +12 dB seed and asserting the gain ends up
  negative.

## 5. Things that exist in the DSP layer but aren't exposed as parameters

These are implemented and unit-tested in `src/dsp/`, but `PluginProcessor.cpp` never sets them
from any `AudioProcessorValueTreeState` parameter, so they always run at their hardcoded
`EngineParameters` struct default:

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
./build/dsp_test                     # same tests, built via CMake this time
cmake --build build --target install_plugins   # installs VST3+AU to ~/Library/Audio/Plug-Ins
```

CI (`.github/workflows/build-and-release.yml`) builds macOS (Universal VST3/AU/Standalone) and
Windows x64/x86 (VST3/Standalone) on every version tag push or manual dispatch, runs `dsp_test`
on each platform, and publishes a GitHub Release with zipped artifacts per platform.

The test suite (`tests/dsp_test.cpp`, 28 tests) exercises `AutoLevelEngine` and every DSP
submodule directly — K-weighting calibration, tone-profile thresholds, LR4 crossover flatness,
EBU R128 loudness accuracy across levels, MBC attack/release (including a regression test that
`prepare()` doesn't leave stale cached ballistics) and the Peak/RMS detector, the Band EQ
(bit-exact flat, per-band accuracy, smoothing, pre/post routing), the lookahead limiter
(exact latency, THD, ceiling, true peak, live switching, bypass latency), breakdown-freeze
sensitivity, NaN/Inf sanitization, and the lock-free visual-state buffer. It does **not**
exercise `PluginProcessor`/`PluginEditor` (those need a full JUCE build and a host/GUI
environment) — as of 2026-09-11 a full CMake+JUCE build was verified to compile cleanly with
zero warnings in project code, but no live-host or GUI interaction testing has been done.

---

## 7. Known unreachable/dead code (intentionally left in place)

- `TargetProfile::CUSTOM` (§5) — UI can't select it.
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
