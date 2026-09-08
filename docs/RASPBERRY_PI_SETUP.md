# Raspberry Pi 4 Live DJ Black Box Setup Guide: AutoLevel DJ

This guide provides step-by-step instructions to turn a **Raspberry Pi 4 Model B (4GB)** and an **M-Audio Fast Track Pro** into a broadcast-grade, standalone hardware audio processor for live DJ sets.

---

## 1. Physical Hardware & Signal Routing

### Hardware Checklist
- **Raspberry Pi 4 Model B** (4GB RAM recommended) with official 5.1V / 3.0A USB-C power supply.
- **M-Audio Fast Track Pro** USB Audio Interface (2 analog inputs, 4 analog outputs).
- **USB-B to USB-A Cable** (connects Fast Track Pro to any blue USB 3.0 port on the Pi 4).
- **MicroSD Card** (16GB or 32GB Class 10 / A1 speed rating).
- **Audio Cables**:
  - DJ Mixer Master Out -> Fast Track Pro Front Inputs 1 & 2 (1/4 inch TRS or RCA to 1/4 inch TS).
  - Fast Track Pro Outputs 1 & 2 (1/4 inch TRS balanced) -> Front of House / Main PA soundboard.
  - Fast Track Pro Outputs 3 & 4 (RCA unbalanced) -> DJ Booth monitor or direct dry backup feed.

### Front Panel Switch Settings (Fast Track Pro)
- **Front Inputs 1 & 2**:
  - `INST / LINE` buttons: **OUT** (Line level).
  - `PAD` buttons: **OUT** for standard line signals (press IN if your DJ mixer master out is running unusually hot).
  - `MIX` knob: Turn **fully clockwise to PLAYBACK** (so you only hear the processed computer output on Outs 1-2, rather than direct unlevelled input monitoring).
  - `STEREO / MONO` switch: Set to **STEREO**.

---

## 2. Operating System Installation (Patchbox OS)

We recommend **Patchbox OS** (Debian 64-bit with `PREEMPT_RT` real-time kernel) developed by Blokas. It comes pre-packaged with real-time audio optimizations, JACK2, a built-in Wi-Fi Access Point, and MODEP.

1. **Download Patchbox OS**:
   - Download the 64-bit image from [Blokas Patchbox OS](https://blokas.io/patchbox-os/).
2. **Flash the SD Card**:
   - Use **Raspberry Pi Imager** or **BalenaEtcher** to flash the image to your MicroSD card.
3. **First Boot**:
   - Insert the MicroSD card into the Pi 4, connect the Fast Track Pro to USB, and power on the Pi.
   - The Pi will automatically initialize and broadcast a local Wi-Fi Hotspot named:
     `Patchbox-XXXX` (where XXXX is a unique ID).
   - Default Wi-Fi Password: `blokaslabs`
4. **Accessing the Pi**:
   - Once your phone, tablet, or laptop is connected to the Pi Wi-Fi network:
     - **Web Interface**: Open `http://patchbox.local` (or `http://172.24.1.1`) in your browser.
     - **SSH Terminal**: `ssh patch@patchbox.local` (Password: `blokaslabs`).

---

## 3. Configuring M-Audio Fast Track Pro (24-Bit / 4-Output Mode)

By default, Linux loads the Fast Track Pro in generic USB Audio Class 1 mode (16-bit, 2 inputs, 2 outputs). To unlock **24-bit audio depth**, **2 inputs**, and **all 4 analog outputs** simultaneously at 44.1 kHz or 48 kHz, configure the ALSA kernel driver.

### Option A: Automatic Configuration
Run the included setup script on your Pi:
```bash
cd autolevel-dj-vst
sudo ./scripts/setup_fasttrackpro.sh
```

### Option B: Manual Configuration
Create or edit `/etc/modprobe.d/fasttrackpro.conf`:
```bash
sudo nano /etc/modprobe.d/fasttrackpro.conf
```
Add the following line:
```ini
# device_setup=0x1 enables 24-bit, 44.1/48kHz, 2 in / 4 out
options snd_usb_audio vid=0x0763 pid=0x2012 device_setup=0x1 index=1
```
Save and exit (`Ctrl+O`, `Enter`, `Ctrl+X`).

Unplug the Fast Track Pro USB cable and plug it back in (or run `sudo reboot`).

### Verify 24-Bit Mode
Check ALSA playback devices:
```bash
aplay -l
```
You should see `FastTrackPro` with subdevices. Run:
```bash
cat /proc/asound/card1/pcm0p/sub0/hw_params
```
When playing, it will report `format: S24_3BE` or `S24_LE` (24-bit PCM).

---

## 4. Installing AutoLevel DJ on the Pi

### Method A: Download Precompiled Release (Fastest)
1. Download `autolevel-dj-linux-arm64.zip` from the GitHub repository Releases page.
2. Unzip the bundle into your plugins directory:
   ```bash
   mkdir -p ~/.lv2 ~/.vst3
   unzip autolevel-dj-linux-arm64.zip -d ~/.lv2/
   # If running MODEP, also copy to MODEP directory:
   sudo cp -R ~/.lv2/AutoLevel\ DJ.lv2 /var/modep/lv2/
   sudo chown -R modep:modep /var/modep/lv2/AutoLevel\ DJ.lv2
   ```

### Method B: Build Locally on the Raspberry Pi
If you want to compile with maximum CPU optimization directly on your hardware:
```bash
git clone https://github.com/divid3byzer0/autolevel-dj-vst.git
cd autolevel-dj-vst
./scripts/build_pi.sh
```
This script installs all necessary packages, configures CMake with `-O3 -mcpu=cortex-a72`, compiles VST3, LV2, and Standalone binaries, runs the DSP test suite, and installs everything into `~/.lv2` and `~/.vst3`.

---

## 5. Web Hosting & Real-Time Wireless Control

You have two primary hosting options: **MODEP** (recommended for touchscreens and phones) and **Carla**.

### Setting Up MODEP (MOD Emulation Project)

1. Activate MODEP in Patchbox OS:
   ```bash
   patchbox module activate modep
   ```
2. Open your browser on your phone or laptop at `http://patchbox.local`.
3. You will see a virtual pedalboard canvas.
4. Click or tap **Plugins** at the bottom, find **AutoLevel DJ** under Utility/Dynamics, and drag it onto the pedalboard.
5. Connect the virtual patch cables:
   - **Hardware Input 1 (Left)** -> AutoLevel DJ **Input L**
   - **Hardware Input 2 (Right)** -> AutoLevel DJ **Input R**
   - AutoLevel DJ **Output L** -> **Hardware Output 1 (PA Left)**
   - AutoLevel DJ **Output R** -> **Hardware Output 2 (PA Right)**
   - *(Optional Dry Backup / Booth Split)*:
     Connect Hardware Input 1 & 2 directly to **Hardware Output 3 & 4**. This gives you an uncompressed, zero-latency analog safety feed directly to the DJ booth monitor or club engineer!
6. Click the gear icon / preset button and click **Save as Default Pedalboard**. Now whenever the Pi powers up, this routing is loaded instantly.

---

## 6. Live DJ Gig Walkthrough & Parameter Tweaks

With the Pi running inside your DJ bag or booth rack:
1. **Power up**: Connect USB-C power to the Pi. In 15 to 20 seconds, the hotspot is live and audio starts streaming.
2. **Connect**: On your phone or iPad, select Wi-Fi network `Patchbox-XXXX`.
3. **Open Browser**: Navigate to `http://patchbox.local`.
4. **Key AutoLevel DJ Controls**:
   - **Target LUFS**: Default is -14.0 LUFS. For high-energy club sets, set to -12.0 LUFS or -10.0 LUFS. For lounge/chillout, set to -16.0 LUFS.
   - **Level Response**: Default is 0.85 (fast transparent adaptation). If playing eclectic sets with drastic volume swings between older and newer tracks, keep at 0.85 to 0.95.
   - **Tone Mode**:
     - *Modern*: Enhances deep sub punch (+1.5 dB) and sparkling air (+1.5 dB) while smoothing out boxy upper bass and harsh presence.
     - *Linear*: Neutral broadcast curve.
   - **Dynamic Bass Lift**: Lifts weak, thin, or vintage 70s/80s tracks with up to +4.5 dB adaptive low-end expansion. Automatically stops expanding on modern, bass-heavy tracks to avoid mud.
   - **Dynamic Air Lift**: Smoothly restores top-end breath and crispness on compressed MP3s or dull recordings without harsh sibilance.
   - **Bypass Rule**: If you toggle Bass Lift or Air Lift to OFF, the audio stream through those stages is 100% bit-identical to the source signal.

---

## 7. Fail-Safe Gig Tips

- **Dedicated Power**: Always use the official Raspberry Pi 5.1V 3A power supply. Do not power the Pi from a laptop USB port.
- **Audio Interface Power**: The Fast Track Pro can be powered via USB, but for high-volume club environments, using an optional 9V DC external adapter on the Fast Track Pro provides additional analog headroom.
- **Dry Split Security**: By routing Inputs 1-2 straight to Outputs 3-4 in ALSA/JACK, you always have a completely untouched analog backup line available on the soundboard.
- **No Internet Required**: The built-in Wi-Fi AP operates entirely offline and autonomously.
