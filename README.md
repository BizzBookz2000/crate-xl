# Crate XL

Copyright © ILLAHTAINMENT. All rights reserved.

Crate XL is a pad sampler, sequencer and mixer for Windows and macOS. It's built around the classic pad-production workflow: **Performance → Editor → Sequencer → Mixer**, with 128 banks of 128 pads, multi-layer sampling, a song / scenes / pattern sequencer, a full mixer with unlimited effects, VST3 hosting (and Audio Units on macOS), and MIDI learn.

On first launch Crate XL builds a small starter kit: an original drum kit plus keys and bass sounds, with a groove pattern. You can play straight away.

---

## Building it

### Windows: `build.bat`

1. Install **Visual Studio** (Community is free; 2019, 2022 and 2026 all work) and tick the **Desktop development with C++** workload.
2. Install **Git** (https://git-scm.com/download/win).
3. Double-click **`build.bat`**.

The script finds your Visual Studio automatically and uses its CMake and Ninja. If CMake is missing, add "C++ CMake tools for Windows" in the Visual Studio Installer. The first build downloads JUCE and takes 10–20 minutes. When it's done, the app is in `dist\`.

For ASIO drivers, set `ASIO_SDK=C:\path\to\asiosdk` before running the script. The ASIO SDK from Steinberg is a free download.

### macOS: `build.command`

1. Install the **Xcode Command Line Tools**: `xcode-select --install`.
2. Install **CMake**: `brew install cmake`, or download it from cmake.org.
3. Double-click **`build.command`**. If macOS won't open it, right-click it and choose **Open**.

You get a universal app (Apple Silicon + Intel) in `dist/`, ad-hoc signed so it runs on the Mac that built it.

### Renaming the app

The name is set in one place, at the top of `CMakeLists.txt`:

```cmake
set(CRATE_PRODUCT_NAME "Crate XL" CACHE STRING "Application name")
```

You can also pass `-DCRATE_PRODUCT_NAME="My Name"` when configuring. The build scripts copy the newest app they find.

---

## What's in it

### Performance view

- 16-pad and 64-pad grids, eight pages to reach all 128 pads, and the **Station** layout: eight big pads, eight volume faders, quick sound knobs (tune, decay, cutoff, reso, attack, saturate, pan, gain, lo-fi, filter type) and a 16-step sequencer for the selected pad.
- Bank tabs A–H across 16 bank groups (128 banks).
- Left panel:
  - **Velocity**: fixed velocity, pad Y-axis velocity, fixed MIDI velocity.
  - **Roll**: hold and slide to change the roll rate; height sets velocity.
  - **Wheels**: pitch, mod, aftertouch and sustain.
  - **X/Y**: four X/Y macro pads.
- Note repeat with six programmable rates (straight, triplets, swing).
- **Keys** mode turns the pads into a chromatic keyboard for the selected pad. It follows the scale and root, with octave shifting. There's also a 128-key on-screen keyboard.
- **Select** mode with copy / paste / cut / clear / colour / rename for pads. **Mute** and **Solo** modes. **Scene** mode turns the pads into scene launchers.
- Chords & Scales: 15 scales and 16 chord types.
- Computer keyboard: `Z X C V / A S D F / Q W E R / 1 2 3 4` play the 16 pads.

### Editor view

- Pad selector, channel settings (volume, pan, sends, choke group, link group, pad note, keys mode, output, MIDI setup), and the layers & samples list.
- **Sampler** page:
  - Zoomable waveform with start / end / loop markers, zero-crossing snap and loop crossfade.
  - Loop modes: off / forward / alternate. Layer trigger: one shot / hold / on release.
  - Disk streaming, live stretch to tempo, reverse, polyphony, legato and glide.
  - Tune, fine tune, gain, pan, saturation and lo-fi modes (12-bit 26 kHz, 12-bit 32 kHz, 8-bit 22 kHz).
  - AHD / AHDSR amplitude envelope.
  - Filters: low-pass 12/24, high-pass 12/24, band-pass, notch.
- **Slicing**: slice mode (click the ruler to add slices, drag to move, right-click to remove). Auto-slice by equal parts, by beat grid or by transients. Save slices to pads (with choke group and an auto-generated pattern), to one layer across keys, to multiple layers, or to files.
- **Audio Editor**: select, cut, copy, paste, delete, trim, normalize, gain, fade in/out, reverse, silence, remove DC, time-stretch, pitch-shift, stretch to tempo, set start/end and loop from the selection, and undo.
- **Recorder**: pick the input device (CoreAudio / WASAPI / DirectSound; with ASIO the input follows the driver), then sample from any stereo pair or mono channel by its real name, or resample the main output, a bank or an AUX. Threshold start, and "move to next pad" for quick multi-sampling.
- **Layer FX**: four effects per layer.
- **Modulations**: a modulation matrix with two LFOs (six shapes, Hz or tempo-synced, mono or poly), a mod envelope, a 32-step modulator, velocity, key track, wheels, aftertouch, random and the 16 macros. Destinations are pitch, cutoff, resonance, volume, pan, sample start, saturation and LFO rates.
- **Mapping**: key × velocity zone map with drag-to-edit, root key learn and spread keys.
- **Plugin**: host a VST3 (or AU on macOS) instrument on a pad, with generic parameter knobs and recording of its output to a pad.

### Sequencer view

- **Song**: arranger with bank tracks and audio tracks, plus loop markers in the ruler. Draw, move, resize, split, duplicate and repeat clips. Drag the pattern chip onto the timeline. Each track can follow the song or play its own pattern. There are track automation lanes, and you can drop audio files onto the timeline.
- **Scenes**: a pattern grid per track; launches quantised to 1 beat, or 1/2/4/8 bars. Capture, duplicate, rename, reorder, and paste a scene to the end of the song.
- **Pattern**: piano roll for pads and for keys-mode pads (128 keys). Tools: select, draw and paint. Quantize with swing, a velocity lane, copy/paste, double the pattern, and pattern automation lanes.

### Mixer view

- Channel strips for the main out, banks, audio tracks and AUX returns (up to 8). **Show pads** switches to per-pad channels.
- Meters with peak hold, faders, pan, and pre/post-fader sends to every AUX.
- Unlimited insert effects (built-in or plugin), with plugin latency compensation.
- I/O view: audio-track inputs (stereo/mono hardware or internal resampling), input gain, noise gate, arm/monitor, and outputs to any hardware pair.

### Built-in effects

6-band EQ, Compressor (compress / limit / expand), Delay, Stereo Delay, Reverb, Chorus / Flanger (with vibrato), Overdrive, Bit Crusher, Filter, Auto-Pan, Stereo Widener, Slicer and Noise Gate.

### Plugin scanning

Settings → Plugins → **Scan for plugins**. Each plugin is loaded in a separate helper process (the app starts a copy of itself in the background). If a plugin crashes or stops responding for 90 seconds, it's marked **Failed** with the reason, and the scan moves on to the next one. The app itself never goes down. You can stop a scan part-way, and **Retry failed plugins** clears the failed list so the next scan tries them again.

### MIDI

- MIDI learn on any knob: right-click it. You can also assign any knob to one of the 16 macros or add an automation lane for it.
- **MIDI focus actions**: map a controller's pads to the on-screen pads (or notes in keys mode), and its buttons and knobs to modes, banks, bank groups, octave, note repeat, macros and transport.
- OMNI mode, or per-bank and per-pad MIDI input and output setups (single-channel or multi-channel).
- MIDI out per bank or pad, MIDI clock out, and pitch bend / mod / aftertouch / sustain.

### Files

Sessions (`.crsession`), bank presets (`.crbank`, with their samples), packs (`.crpack`), MIDI import (note-number or General MIDI drum mapping) and MIDI export.

Audio export covers the main mix or stems (one file per track), song / loop / custom range, 16/24/32-bit float or 12-bit vintage, normalize, and effect tails. Edited samples are saved with the session.

Everything lives in `Documents/Crate XL` (a `Documents/Crate` folder from an earlier version is renamed automatically) (Samples, Banks, Sessions, Packs, Recordings, Exports). The session auto-saves every two minutes. To get it back, use the menu → Restore auto-saved session.

---

## Keyboard shortcuts

| Key | Action |
|---|---|
| Space | Play / stop |
| R | Record |
| L | Loop on/off |
| C | Metronome |
| T | Tap tempo |
| F1–F4, Tab | Switch views |
| B / X / M / K | Browser / Effects / Macros / Keyboard panels |
| Ctrl/Cmd + Z, Y | Undo, redo |
| Ctrl/Cmd + S, O, N, E | Save, open, new, export |

---

## Engine tests

```
cmake -S . -B build -DCRATE_BUILD_TESTS=ON
cmake --build build --target CrateTests
build/CrateTests_artefacts/Release/CrateTests
```

The tests run the engine headless: voices, pattern and song playback, scenes, recording, note repeat, automation, macros, effects, sample editing, slicing, streaming, stretching, undo, and file round-trips.
