# AutoLevel DJ (VST3 / AU / Standalone)

**Intelligent real-time master bus loudness leveling and multiband dynamic tone shaping for live DJ sets.**

Designed to sit on the **master output** of your DJ software or mixer before the signal reaches your power amplifiers and sound system. It automatically rides gain and tames frequency imbalances across vastly different tracks so you can focus entirely on mixing, track selection, and transitions.

---

## The Problem in Live DJ Sets

1. **Vastly Different Mastering Standards:** Tracks in a DJ set span different eras and genres (e.g. 70s disco, 90s vinyl house, modern brickwall EDM). RMS/LUFS can vary wildly from −14 LUFS to −6 LUFS, forcing the DJ to constantly adjust trim gain.
2. **Transition Swells & Pumping:** Two tracks playing together during a blend naturally sum higher in energy (+2 to +3 dB). Fast compressors or broadcast processors pump and suck the life out of drops and buildups.
3. **Breakdown Drop Deflation:** A 45-second acoustic or percussion-less breakdown can trick normal AGC/levelers into boosting gain by several dB. When the drop arrives, it clips hard or blasts the audience.
4. **Spectral Mismatches:** A 90s track might sound thin in the sub-bass compared to a modern club track, or high-hats might be piercingly harsh at high sound system volumes.

---

## How AutoLevel DJ Solves This

AutoLevel DJ runs the same engine as the **autolevel-box** Raspberry Pi appliance
([autolevel-core](https://github.com/divid3byzer0/autolevel-core), a git submodule here), with the
same settings and presets, so the plugin on your Mac sounds exactly like the box.

```
Audio In (stereo float)
   │
   ├──► [ITU-R BS.1770-4 / EBU R128 meter] ──► gated, decaying loudness history
   │                                                  │
   ▼                                                  ▼
[Leveler (AGC)] ◄──────────────────────── target gain, breakdown freeze, asymmetric slew
   │
   ▼
[Dynamic Air Lift] (Off / Low / Med / High)
   │
   ▼
[Pre-MBC EQ] (6 bells on the compressor's bands)
   │
   ▼
[6-Band Linkwitz-Riley Crossover, phase-corrected]
   ├── Sub       (20 - 120 Hz)     ──► Dynamic Compressor
   ├── Bass      (120 - 400 Hz)    ──► Dynamic Compressor
   ├── Low-Mid   (400 - 1200 Hz)   ──► Dynamic Compressor
   ├── High-Mid  (1200 - 3500 Hz)  ──► Dynamic Compressor
   ├── Presence  (3500 - 8000 Hz)  ──► Dynamic Compressor
   └── Air       (8000 - 20000 Hz) ──► Dynamic Compressor
   │
   ▼
[Post Gain] ──► [Low Cut 20-50 Hz] ──► [True-Peak Lookahead Limiter]
   │
   ▼
Audio Out
```

### 1. Asymmetric Slew-Rate Gain Riding
* **Downward Slew (Fast):** If a new track hits too hot, the leveler cuts gain quickly to protect the sound system and ears.
* **Upward Slew (Slow & Musical):** When a quieter track comes in, the leveler slowly nudges gain upward (0.5 / 0.75 / 1.5 dB/s for Slow / Normal / Fast). This prevents audible pumping during song transitions.
* **Fast Lock:** On initial sound or manual reset, an 8-second fast-lock window quickly establishes the baseline level.

### 2. Breakdown Freeze
* AutoLevel DJ continuously compares momentary loudness to the integrated track loudness.
* During breakdowns, buildups, or quiet intros, the gain rider **freezes upward adaptation**, so the drop lands with full punch.
* **Sensitivity (5 / 7 / 9 LU):** how far below the integrated level counts as a breakdown.

### 3. Dynamic Air Lift
* A distortion-free dynamic high shelf above 6.5 kHz: lifts the top end of dull or vintage material (up to +6.5 dB) and backs off on bright modern tracks, with sibilance ducking.

### 4. Pre-MBC EQ
* Six bell filters on the compressor's bands, ±12 dB each, ahead of the compressor so its dynamics rein your shaping back in. Flat by default.

### 5. Multiband Dynamic Tone Shaper (MBC)
* A **6-band Linkwitz-Riley (LR4)** dynamic compressor, ratio up to **5:1**, thresholds **18 dB below your Target LUFS**.
* Thresholds follow a **tone slope** (0 to −3 dB/octave, flat by default) plus a **per-band contour**: raise a band's threshold to leave it more open, lower it to rein it in.
* **Ballistics:** Attack 1–100 ms, Release 20–1000 ms; the Sub band runs at twice both.
* **Auto release:** a second, slow detector runs alongside, so short hits recover at your Release time but dense passages don't pump.
* **Peak or RMS:** the Detector knob blends the level detector from pure peak to pure RMS.

### 6. True-Peak Limiter
* A 2 ms lookahead brickwall with an 8× oversampled true-peak detector, 25 ms hold and ~80 ms release: holds the ceiling, inter-sample peaks included, without clipping. Untouched audio passes bit-for-bit.

### Latency
The plugin reports **358 samples at 48 kHz** (7.5 ms) to the host, which compensates automatically: 256 samples to run the engine in the box's 256-sample blocks (needed for identical output, since the Leveler updates once per block) plus the limiter's 2.1 ms lookahead. Bypass keeps the same latency.

---

## Formats & Deployment

AutoLevel DJ is available for **macOS** (Apple Silicon & Intel Universal) and **Windows** (x64 / x86):

1. **VST3 & AU Plugin:**
   * Load directly onto the Master track inside **Ableton Live**, **Logic Pro**, **FL Studio**, **Bitwig Studio**, **Cubase**, or any VST3/AU host DAW.
2. **Standalone Application:**
   * For DJ software without native plugin hosting (e.g. **Traktor Pro**, **Rekordbox**, **Serato DJ Pro**, **VirtualDJ**):
   * Route your DJ software master output into a virtual loopback device (such as **BlackHole** or Rogue Amoeba **Loopback** on macOS, or **VB-Cable** on Windows).
   * Open the Standalone app, select the loopback device as Input, and your physical audio interface / DAC as Output.

---

## Building from Source

### Requirements
* **macOS:** Xcode Command Line Tools (`xcode-select --install`), CMake (`brew install cmake`), Ninja (`brew install ninja`)
* **Windows:** Visual Studio 2022 with C++ Desktop Development workload, CMake, Ninja

### Build Commands (macOS)

```bash
# Clone the repository, with the engine submodule
git clone --recursive https://github.com/divid3byzer0/autolevel-dj-vst.git
cd autolevel-dj-vst
# (already cloned without --recursive?  git submodule update --init --recursive)

# Configure build with CMake & Ninja (automatically fetches JUCE 8)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build VST3, AU, Standalone App, and Test Suite
cmake --build build --config Release

# Run the engine tests and the plugin tests (bit-for-bit parity with the box)
./build/dsp_test
./build/plugin_test

# Install plugins to ~/Library/Audio/Plug-Ins/
cmake --build build --target install_plugins
```

### Build Commands (Windows x64 & x86)

```powershell
# For 64-bit Windows (x64) - Standard for modern DAWs
cmake -B build-x64 -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64 --config Release

# For 32-bit Windows (x86) - Legacy hosts
cmake -B build-x86 -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86 --config Release

# Run tests
.\build-x64\Release\dsp_test.exe
.\build-x64\Release\plugin_test.exe

# Install plugin to %COMMONPROGRAMFILES%\VST3
cmake --build build-x64 --target install_plugins --config Release
```

Compiled plugins will be located in:
* **macOS:**
  * `build/AutoLevelDJ_artefacts/Release/VST3/AutoLevel DJ.vst3`
  * `build/AutoLevelDJ_artefacts/Release/AU/AutoLevel DJ.component`
  * `build/AutoLevelDJ_artefacts/Release/Standalone/AutoLevel DJ.app`
* **Windows:**
  * `build/AutoLevelDJ_artefacts/Release/VST3/AutoLevel DJ.vst3`
  * `build/AutoLevelDJ_artefacts/Release/Standalone/AutoLevel DJ.exe`

---

## Pre-built Releases

Pre-compiled binary packages for macOS and Windows are published under [GitHub Releases](https://github.com/divid3byzer0/autolevel-dj-vst/releases).

---

## Controls Reference

Ranges and defaults are the box's.

| Control | Range | Default | Description |
| :--- | :--- | :--- | :--- |
| **Target LUFS** | −20 to −6 LUFS | **−14 LUFS** | Integrated loudness target. Also sets the compressor thresholds (18 dB below). |
| **Max Boost** | 0 to 18 dB | **+12 dB** | Maximum upward gain the leveler can apply. |
| **Max Cut** | 0 to 18 dB | **12 dB** | Maximum downward attenuation for hot tracks. |
| **Level Response** | 0 to 100% | **85%** | How quickly the loudness measurement forgets older material. |
| **Slew Speed** | Slow / Normal / Fast | **Normal** | AGC gain-change speed: 0.5 / 0.75 / 1.5 dB/s upward (2× that downward). |
| **Breakdown Freeze** | On / Off | **On** | Holds the gain during breakdowns and quiet intros instead of boosting them. |
| **Breakdown Sensitivity** | 3 to 15 LU (buttons: 5 / 7 / 9) | **7 LU** | How far below the integrated level counts as a breakdown. |
| **Air Lift** | Off / Low / Med / High | **Off** | Dynamic high-shelf lift for dull material. |
| **Pre-EQ Sub … Air** | −12 to +12 dB | **0 dB** | Six bells ahead of the compressor (faders, PRE-EQ mode). |
| **Compression** | 0 to 100% | **50%** | Compressor depth: ratio 1:1 to 5:1. |
| **Tone Slope** | −3.0 to 0.0 dB/oct | **0.0** | Tilt of the compressor's tonal target. 0 = flat, lower = darker. |
| **Contour Sub … Air** | −12 to +12 dB | **0 dB** | Per-band compressor threshold offsets (faders, CONTOUR mode). The old Modern Mix profile is +1.5, −2.5, +0.5, +1.0, −2.0, +1.5. |
| **Detector** | Peak (0%) to RMS (100%) | **Peak** | What the compressor listens to. |
| **MBC Attack** | 1 to 100 ms | **15 ms** | How fast the compressor clamps down. Sub runs at 2×. |
| **MBC Release** | 20 to 1000 ms | **200 ms** | How fast it lets go. Sub runs at 2×. |
| **Release Mode** | Manual / Auto | **Manual** | Auto = program-dependent release. |
| **Post Gain** | −24 to +24 dB | **0 dB** | Trim after the compressor (it has no automatic makeup gain). |
| **Low Cut** | Off, 20 to 50 Hz | **30 Hz** | 24 dB/oct Butterworth high-pass ahead of the limiter. |
| **Limiter Ceiling** | −3.0 to 0.0 dBFS | **−0.3 dBFS** | True-peak ceiling. |
| **Bypass** | On / Off | **Off** | The box's bypass; your DAW's bypass button drives it too. Keeps the latency. |
| **Reset Set** | Button | — | Clears the loudness history and locks onto the incoming track (settings kept). |
| **Presets** | Button | — | Load / save presets in the box's JSON format (the same files the box saves), or reset to the defaults. Saved to `~/Documents/AutoLevel/Presets` by default. |

### Upgrading from v2
v3 opens v2 projects. Settings that still exist carry over (Target LUFS, Max Boost/Cut, Level Response, Slew Speed, Compression, Tone Slope, Attack, Release, Release Mode, Detector, EQ bands, Post Gain, Low Cut, Ceiling, Freeze, Bypass); values outside the box's ranges are clamped (e.g. Release above 1000 ms). Target Profile, EQ Position and Limiter Lookahead are gone; the new settings start at the box defaults. The sound changes to the box's.

---

## License

MIT License. Created by DistrictNoir.
