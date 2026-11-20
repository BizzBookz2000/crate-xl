# Lattice — pad-based beat production for Windows and macOS

Lattice is an original desktop music app built around a bank-and-pad sampler, a pattern/song/scene sequencer and a mixer. It follows the four-view workflow **Perform → Edit → Sequence → Mix**. "Lattice" is a working codename.

Version 0.1. C++17, built on JUCE 8.

---

## Build it

### Windows: double-click `build.bat`

You need, once:

1. **Visual Studio** 2019, 2022 or 2026 (Community is free) with the **"Desktop development with C++"** workload. The script finds the newest one automatically.
2. **Git** from <https://git-scm.com/download/win>.
3. **CMake** usually comes with Visual Studio's C++ workload ("C++ CMake tools for Windows"). If it doesn't, install it from <https://cmake.org/download/> and tick *"Add CMake to the system PATH"*.

Then double-click `build.bat`. The first build downloads JUCE and takes about 10–20 minutes; later builds are much faster. The app ends up in `dist\Lattice.exe`.

If an older attempt left a `build` folder behind, you can delete it; the script now builds in `build-ninja` or `build-nmake`.

*Optional, ASIO:* download the Steinberg ASIO SDK, then `set ASIO_SDK=C:\path\to\asiosdk` in a Command Prompt and run `build.bat` from that same prompt. Without it the app uses WASAPI, which works fine for most interfaces.

### macOS: double-click `build.command`

You need, once:

1. **Xcode Command Line Tools**: open Terminal and run `xcode-select --install`.
2. **CMake**: `brew install cmake`, or the `.dmg` from cmake.org (the script finds `/Applications/CMake.app` on its own).

Then double-click `build.command`. If macOS says it can't be opened, right-click it and choose **Open**, or run `chmod +x build.command` once in Terminal. The app is built as a universal binary (Apple Silicon + Intel) in `dist/Lattice.app`. It is ad-hoc signed, so it runs on the machine that built it.

The first time you record a sample, macOS asks for microphone permission. Allow it.

### Linux (development only)

Run `./build.sh` after installing the packages listed at the top of that script.

### Headless tests

```
cmake -S . -B build -DLATTICE_BUILD_TESTS=ON
cmake --build build --target LatticeTests
./build/LatticeTests_artefacts/Release/LatticeTests
```

These 30 checks exercise the real engine with no sound card. They cover sample loading, sample-accurate pattern timing, mute, song arrangement, stem export, all 13 effects, session save/load round-trip, slicing, undo/redo, MIDI import, and live recording with auto-quantize.

---

## Using it

**Shortcuts:** Space = play/stop · R = record · F1–F4 = views · Ctrl/Cmd+Z = undo · Ctrl/Cmd+Shift+Z = redo · Ctrl/Cmd+S = save · Ctrl/Cmd+O = open · Ctrl/Cmd+N = new.

### Perform
- Bank tabs **A–H** run along the top. Each bank has 16 pads, with pad 1 at the bottom left.
- **Drag audio files onto pads** to load them. Dropping several files fills consecutive pads.
- **Velocity:** fixed (slider), Y-axis (where you hit the pad), and an option to force a fixed velocity on MIDI input.
- **Repeat:** six rates (1/4 to 1/32, including triplets) that retrigger held pads on the transport grid. **Roll pad:** hold it to roll the selected pad. Left to right is the rate.
- **Wheels:** pitch (±2 st, springs back), mod (opens the pad filter), sustain.
- **Pad mode:** Select (with Copy/Paste/Cut/Clear, which also works across banks), Mute, Solo, Scene (pads launch scenes).
- **Keys:** turns the grid into a keyboard that plays the selected pad chromatically. Scales and chords apply here.
- **Scenes row:** click to launch, or to capture if the slot is empty. Shift-click to capture over an existing scene.

### Edit
- Pad selector and name; **Load sample**, **Record** (samples your audio input onto the pad), **Clear**.
- **Waveform:** drag the start/end markers (and the loop marker when Loop is on). The mouse wheel zooms, Shift+wheel scrolls, double-click fits the whole sample.
- **Sample tools:** Normalize, Reverse, Trim, Fade in, Fade out. These work on the start–end region and never change your original file. **Slice to pads** chops the region into 2/4/8/16 pads that share one buffer.
- **Parameters:** tune, fine, gain, pan, level; AHDSR envelope; LP/HP/BP filter; an LFO aimed at pitch, cutoff, volume or pan; two sends; trigger mode (one shot / hold); polyphony 1–16; choke groups 1–8; loop; reverse.
- **Lo-fi modes:** Hi-fi, Vintage 12-bit 26 kHz, Vintage 12-bit 32 kHz, Crunch 8-bit 22 kHz.
- **Bank instrument:** swap the bank's sampler for a VST3 instrument. Pads then send notes starting at the "MIDI note of pad 1".

### Sequence
- **Pattern:** each bank has any number of patterns (1–16 bars). Click a cell to add a note, click a note to delete it, drag to paint. The mouse wheel over a note sets its velocity, and clicking a row name auditions that pad. You can also New, Duplicate, Rename, Delete, Clear, Quantize, Launch, and Import MIDI.
- **Recording:** press Rec (or R). Pad hits and MIDI go into the bank's playing pattern, quantized to the setting in the top bar.
- **Song:** an arranger with one row per bank. Click to place the bank's selected pattern, drag to move, right-click or Delete to remove, double-click to edit. Drag on the ruler to set the loop. Switch the transport to **Song** to play the arrangement.
- **Scenes:** each scene stores one pattern (or Stop) per bank. Launching switches every bank on the next bar. **Add to song** appends the scene to the end of the arrangement.

### Mix
- Strips for the 8 banks, 2 AUX returns (AUX 1 has a reverb, AUX 2 a delay) and the main output. Each strip has 4 insert slots, sends, pan, a fader, a peak meter, mute and solo.
- **Pads** view: per-pad level, pan, sends, mute and solo for the selected bank.
- Click an insert slot to add a built-in effect or a VST3 effect. The panel below edits its parameters (Bypass / Remove / Plugin window).
- **Built-in effects:** Sculpt EQ (6-band), Dynamics (compressor / limiter / expander), Echo, Dual Echo, Space (reverb), Swirl (chorus / flanger / vibrato), Heat (overdrive), Grit (bit crusher), Contour (filter + LFO), Orbit (auto-pan), Wide (stereo widener), Chop (slicer), Gate.
- **MIDI learn:** right-click any knob or fader, choose *MIDI learn*, then move a control on your hardware.

### File menu
New / Open / Save / Save As (`.ltsession`) · Export mixdown or mixdown + stems (24-bit WAV, faster than real time) · Audio & MIDI settings · Scan VST3 plugins · Add a VST3 folder · Restore last autosave.

The session autosaves every 2 minutes. Samples you recorded or edited are written to a `"<session name> Samples"` folder next to the session file.

### MIDI input
All connected MIDI inputs are enabled automatically, including ones you plug in later. Notes go to the selected bank: note 36 (C1) plays pad 1 by default, or the selected pad plays chromatically in Keys mode. Pitch bend, mod wheel (CC1) and sustain (CC64) are supported, and every other CC can be MIDI-learned.

---

## What v0.1 does not do yet

These are planned in the blueprint but not built yet:

- **Banks and pads:** 8 banks × 16 pads, not 128 × 128; 16-pad grid only (no 64-pad mode).
- **Sampler:** one sample per pad; no multi-layer pads and no key/velocity zone mapping. No disk streaming (samples load into memory, up to 10 minutes each). No tempo-synced time-stretch or pitch-shift.
- **Sequencer:** no automation lanes. Recording works in Pattern mode only. Song and scene playback can't be mixed per track; the whole transport is either Pattern or Song.
- **Audio:** no multi-track audio recording. You can sample audio onto pads.
- **Effects and plugins:** no MIDI output to external gear, no plugin delay compensation, and VST3 plugins are scanned in-process (a crashing plugin can crash the scan).
- **Mixer:** no macro knobs.
- **Files:** MIDI-learn mappings aren't saved with the session; no sound-pack format yet.

## Licensing notes

- JUCE 8 is dual-licensed: AGPLv3, or a commercial licence. Releasing Lattice closed-source requires the commercial licence; releasing it open source means using AGPLv3.
- The VST3 SDK (bundled with JUCE) has been MIT-licensed since VST 3.8.
- The ASIO SDK has its own Steinberg licence.
- All built-in effects, the UI design and the code are original.
