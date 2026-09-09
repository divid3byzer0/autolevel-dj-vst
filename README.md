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

```
Audio In (32/64-bit Stereo Float)
   │
   ├──► [ITU-R BS.1770-4 Stereo Meter] ──► Gated Histogram (Decaying Memory)
   │                                                  │
   ▼                                                  ▼
[Makeup Gain Rider] ◄────────────────────── [Leveler with Breakdown Freeze & Asymmetric Slew]
   │
   ▼
[6-Band Linkwitz-Riley Crossover]
   ├── Sub       (20 - 120 Hz)     ──► Dynamic Compressor
   ├── Bass      (120 - 400 Hz)    ──► Dynamic Compressor
   ├── Low-Mid   (400 - 1200 Hz)   ──► Dynamic Compressor
   ├── High-Mid  (1200 - 3500 Hz)  ──► Dynamic Compressor
   ├── Presence  (3500 - 8000 Hz)  ──► Dynamic Compressor
   └── Air       (8000 - 20000 Hz) ──► Dynamic Compressor
   │
   ▼
[Summed Bands (Flat Phase / 0 dB Sum)]
   │
   ▼
[Safety Brickwall Limiter] (Protects DACs & Power Amps from Clipping)
   │
   ▼
Audio Out (Clean, Leveled, Punchy Audio to Amps)
```

### 1. Asymmetric Slew-Rate Gain Riding
* **Downward Slew (Fast):** If a new track hits too hot, the leveler cuts gain quickly to protect the sound system and ears.
* **Upward Slew (Slow & Musical):** When a quieter track comes in, the leveler slowly nudges gain upward at **0.75 dB/s**. This prevents audible pumping during song transitions and keeps track dynamics natural.
* **Fast Lock:** On initial sound or manual reset, an 8-second fast-lock window quickly establishes the baseline level.

### 2. Breakdown Freeze
* AutoLevel DJ continuously compares momentary loudness to the integrated track loudness.
* During breakdowns, buildups, or quiet track intros, the gain rider automatically **freezes upward adaptation**. The buildup stays moody and quiet, ensuring the subsequent drop lands with maximum punch.

### 3. Multiband Dynamic Tone Shaper (MBC)
* Rather than using static corrective EQ (which sounds awful when an arrangement naturally lacks bass or drums), tone shaping is performed by a **6-band Linkwitz-Riley (LR4)** dynamic compressor.
* Thresholds dynamically follow a selectable spectral tilt (e.g. pink noise −3.75 dB/octave).
* Quiet bands sit safely under their thresholds and remain 100% untouched. Only unruly sub-bass bursts or harsh treble peaks are transparently reeled in.

### 4. Safety Peak Limiter
* A final zero-latency peak limiter prevents any transient clip from reaching your digital-to-analog converters or over-driving your amplifiers, with a configurable ceiling (e.g., −0.5 dBFS).

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
# Clone the repository
git clone https://github.com/divid3byzer0/autolevel-dj-vst.git
cd autolevel-dj-vst

# Configure build with CMake & Ninja (automatically fetches JUCE 8)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build VST3, AU, Standalone App, and Test Suite
cmake --build build --config Release

# Run the offline DSP verification tests
./build/dsp_test

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

| Control | Range | Default | Description |
| :--- | :--- | :--- | :--- |
| **Target LUFS** | −24 to −4 LUFS | **−9 LUFS** | Master integrated loudness target for club sound systems. |
| **Tone Tilt** | −5.0 to −2.5 dB/oct | **−3.75 dB/oct** | Spectral target balance. Warmer (−4.5) to Brighter (−3.0). |
| **Tone Shaping** | 0% to 100% | **50%** | Multiband compressor depth for dynamic tonal control. |
| **Max Boost** | 0 to 12 dB | **+6 dB** | Maximum upward gain the leveler can apply. |
| **Max Cut** | 0 to 18 dB | **−12 dB** | Maximum downward attenuation for hot tracks. |
| **Slew Speed** | Slow / Normal / Fast | **Normal** | Upward leveling speed (0.5, 0.75, or 1.5 dB/s). |
| **Breakdown Freeze** | On / Off | **On** | Freezes upward gain boost during breakdowns and quiet intros. |
| **Amp Ceiling** | −2.0 to 0.0 dBFS | **−0.5 dBFS** | Brickwall ceiling guarding audio converters and power amps. |
| **Reset Set** | Button | — | Clears integrated loudness history and locks onto incoming track. |

---

## License

MIT License. Created by DistrictNoir.
