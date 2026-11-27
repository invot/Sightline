# SightLine

A VST3 plugin and standalone Windows app that turns a webcam into a MIDI controller:
move a hand onto one of the translucent targets drawn down the sides of the picture and
the note (or drum) is played by whatever instrument the track is hosting.

The plugin never makes sound in a DAW: it sends MIDI out on the host's plugin MIDI port.
The standalone build adds a small built-in instrument (poly synth + drum kit) so it makes
noise on its own.

## Building

Everything is built with the portable toolchain in `.tools` (MinGW-w64 GCC 16.2, CMake
3.31.6, JUCE 8.0.6), so no Visual Studio install is required.

```bash
./build.sh            # configure (first run) and build everything
./build.sh clean      # wipe build/ first
./build.sh tests      # build and run the JUCE-free core tests only
```

Outputs:

```
build/SightLine_artefacts/Release/VST3/SightLine.vst3
build/SightLine_artefacts/Release/Standalone/SightLine.exe
build/SightLineTests.exe       # zone map / dwell gate / detector / synth checks
build/SightLineSmoke.exe       # offscreen end-to-end harness (SIGHTLINE_NO_CAMERA=1)
```

The smoke harness can also render the UI pages to PNG:

```bash
SIGHTLINE_SNAPSHOT_DIR=build-logs/snap ./build/SightLineSmoke.exe
```

`.tools/JUCE-8.0.6` carries a handful of small patches so JUCE 8 builds under MinGW:
the platform check, a few DirectShow/Windows Media gaps, Direct2D GUIDs, and the manifest
handling (see the comments at each patch site). A fresh JUCE checkout would need them
re-applied.

## Using it in a DAW

1. Copy `SightLine.vst3` into `C:\Program Files\Common Files\VST3`, or add the
   `build/SightLine_artefacts/Release/VST3` folder to the plugin paths.
2. Add SightLine to a track **in front of** the instrument you want it to play, and route
   SightLine's MIDI output to that instrument (in most DAWs a MIDI-generating plugin feeds
   the next plugin on the track automatically).
3. Open the editor, go to **Setup** and pick the camera. The video appears on the **Play**
   page with the overlay rails.

Notes go out on the melody channel (default 1); drum pads go out on channel 10 by default,
with General MIDI note numbers, so a GM kit answers out of the box.

## Overlays

| Overlay | What is on the rails |
| --- | --- |
| **KEYS** | A chromatic keyboard standing on its side, one octave per rail. The short black keys sit against the inside edge, as on a piano. Left rail starts at C3, right rail an octave above (the Octave control moves both). |
| **SCALE** | Only the notes of the selected key, one octave per rail, so a hand can land anywhere on the rail and still be in key. |
| **DRUMS** | Kick, snare, closed hat, open hat, clap, low tom, crash and ride on both rails. |

Both rails are live at the same time: one hand per rail, so two notes can sound together.

## Playing

* Hold a hand over a target for the **dwell** time (default 90 ms) and the note starts;
  it keeps sounding while the hand stays there.
* Move off the target and the note stops after the **release** window (default 150 ms), so
  a hand crossing a gap does not chop the note.
* The key/scale/octave, dwell, release, velocity, sensitivity, overlay opacity, mirroring,
  channels and the built-in instrument's level are on the Play (overlay switch) and Setup
  pages. **Send a test note** confirms MIDI is reaching the instrument.

The tracker is a heuristic, not machine learning: it looks for skin tones and frame
motion inside the two side regions only (the face in the middle is outside the search
area). That keeps it fast and dependency-free, and it copes with a resting hand, but it
is worth knowing:

* Strongly coloured walls, warm lamps or a sleeve in frame can be picked up as a hand.
  Lower **Sensitivity** or move the light if a target fires on its own.
* A hand fully outside the side regions is ignored by design.
* A dark or gloved hand may only be found while it moves.
* Mirroring is on by default, matching how people expect a webcam to behave.

## Layout of the source

```
Source/core/       JUCE-free: zones, scales, hand tracking, dwell gating, built-in synth
Source/            processor (parameters, MIDI queue), editor (camera, pages), standalone bridge
Source/ui/         native-drawn theme, stage with the rails, key page, setup page
Source/platform/   Windows manifest for the exe and for the DLL, MinGW UIA shim
Tests/             core tests and the offscreen smoke harness
```
