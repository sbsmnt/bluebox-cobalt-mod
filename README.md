# Cobalt for 1010music bluebox desktop

**v1.1.0-alpha.3 (alpha) - for bluebox desktop firmware 1.5.2**

Cobalt is an unofficial firmware mod for the bluebox desktop. It adds delay
modes, a new reverb, a per-track noise gate, master saturation, compressor
meters, an overdub recording fix, mixer scenes with MIDI recall and custom
colour themes. The patcher runs locally: it takes your copy of the official
1.5.2 firmware and writes a new `BLUEBOX.BIN`. No firmware is distributed
here.

**Open the patcher: [sbsmnt.github.io/bluebox-cobalt-mod](https://sbsmnt.github.io/bluebox-cobalt-mod/)**

> Cobalt is not made, endorsed or supported by 1010music. Keep a copy of your
> working firmware and back up your projects before installing.

## New in v1.1 (alpha)

- **Scenes:** up to 16 snapshots per project of the mixer, master and aux
  buses, FX1/FX2 (including the effect type) and BPM. Recall is instant,
  with no project load, so playback and recording carry on.
- **Scene page:** select a project in PROJ and press A to load or save a
  scene and set the project's Bank #.
- **Quick save:** hold A for 2 s on MAIN to save the next empty scene.
- **MIDI recall:** Program Change recalls a scene; Bank Select loads the
  project with that Bank #. Channel and Bank CC in TOOLS > MIDI Settings.
- **PROJ list:** the loaded project is marked with `* `, and long names
  scroll.
- **MIXER sends with OUT3:** buttons read `O 2`, `O 3` and `NG` (gate) so
  all seven fit.

Details in [Scenes (alpha)](#scenes-alpha) and the [CHANGELOG](CHANGELOG.md).
The alpha's patcher is `patch_bluebox.html` on this branch; the hosted page
still serves v1.0.0.

## Install

1. Open the patcher at
   [sbsmnt.github.io/bluebox-cobalt-mod](https://sbsmnt.github.io/bluebox-cobalt-mod/),
   or open `patch_bluebox.html` from this repository in any modern browser.
   Nothing is uploaded; patching happens in the page.
2. Drop the **original official desktop 1.5.2 `BLUEBOX.BIN`** on the drop
   zone (or click to browse). Other versions and already-patched files are
   refused.
3. Pick a theme preset or build your own, then **Verify and prepare
   download**.
4. Install the new `BLUEBOX.BIN` with the usual SD-card procedure.
5. Check that the middle TOOLS field shows **Cblt 1.0**. The official version
   field still shows 1.5.2.

To go back, tick **Restore the original** and choose your Cobalt
`BLUEBOX.BIN`: the page rebuilds the exact official 1.5.2 file.

The same patcher runs from the command line:

```sh
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --check
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --output /path/to/new/BLUEBOX.BIN
python3 patch_bluebox.py /path/to/BLUEBOX.BIN --theme themes/synthwave.json --output /path/to/new/BLUEBOX.BIN
python3 patch_bluebox.py /path/to/cobalt/BLUEBOX.BIN --restore --output /path/to/original/BLUEBOX.BIN
```

## Features

### FX1 delay modes

Open FX1 and press **B** to choose the mode. Each mode shows its own
controls on the FX1 panel; switching modes keeps every mode's settings.

#### Delay

The stock 1.5.2 delay, unchanged and bit-identical. New projects start here.

| Control | What it does |
|---|---|
| Time | Delay time in ms, or a note division (such as 1/4T) when BEAT is on |
| FEEDB | How much of each repeat is fed back (more repeats) |
| FILT | Turns the delay's filter on |
| CUTOF / WIDTH | Filter frequency and width |
| BEAT | Syncs Time to the project tempo |
| PING | Ping-pong repeats between left and right |
| QUAD | The stock quad delay layout |

#### Warp delay (panel title: Warp)

The stock delay with three extra controls:

| Control | What it does |
|---|---|
| Pitch | Shifts every repeat by −12 to +12 semitones (the toggle turns it on), so repeats climb or fall in pitch as they feed back |
| Drift | Tape-style wow: a slow 0.6 Hz wobble plus a random wander, up to about ±2.5 % pitch at full |
| To Rev | Sends the repeats into FX2, so the echoes are reverberated |

#### Granulr (Granular)

Records the last 2 seconds into a buffer and replays it as up to four
overlapping grains. When fully wet, the stock delay engine is paused to save
CPU.

| Control | What it does |
|---|---|
| Position | How far back in the 2-second buffer grains start: 0 % is the most recent audio, 100 % the oldest |
| Size ms | Grain length, 20–250 ms |
| Density | How many grains overlap, 1–4 (more is smoother and denser) |
| Pitch | Grain pitch, −12 to +12 semitones |
| Jitter | Randomises each grain's start position around Position |
| Feedback | Feeds grains back into the buffer (up to 95 %) for evolving clouds |
| To Rev | Send to the FX2 reverb |
| Freeze | Stops recording: grains keep playing from what is already in the buffer |
| Reverse | Grains play backwards |

With jitter or pitch the grains are mixed by power, so jittered settings stay
as loud as plain ones.

#### Tape

A tape echo: every repeat passes through saturation and filters inside the
feedback loop, so repeats get darker, thinner and more driven as they decay.

| Control | What it does |
|---|---|
| Time / FEEDB | Delay time and feedback, as on Delay |
| Drift | Wow and flutter, as on Warp |
| Drive | Saturation of each repeat (takes WIDTH's place). Quiet tails are not boosted, so drive adds grit without runaway feedback |
| Lo C | Low cut inside the loop: each repeat loses a little more low end. 20 Hz is Off |
| Hi C | High cut inside the loop, 500 Hz–16 kHz: each repeat gets a little darker |
| To Rev | Send to the FX2 reverb |
| BEAT / PING / QUAD | As on Delay |
| WILD | Adds pre-drive damping and extra regeneration in the loop, for a less polite, more unstable echo |

Tape bypasses the stock filter (no FILT), and your FILT setting is kept for
the other modes.

#### Ducked

A delay whose repeats duck while you play and swell back in the gaps.

| Control | What it does |
|---|---|
| Duck | How much the repeats are turned down while the FX1 input is loud (takes WIDTH's place) |
| Release ms | How quickly the repeats come back after the input stops, 20–2000 ms (takes Drift's place) |
| Pitch / To Rev | As on Warp |
| Time / FEEDB / FILT / CUTOF / BEAT / PING / QUAD | As on Delay |

Only what you hear is ducked: the feedback loop keeps running underneath, so
the echoes are all there when they swell back.

#### Reverse

Plays the input backwards in overlapping segments.

| Control | What it does |
|---|---|
| Segment ms | Length of each reversed slice, 40–600 ms (a note division when BEAT is on) |
| Feedback | Feeds reversed audio back for repeated reverse echoes |
| Tone | Low-pass on the reversed sound, from dark (200 Hz) to open (10 kHz) |
| Pitch | −12 to +12 semitones |
| To Rev | Send to the FX2 reverb |
| BEAT | Syncs Segment to the project tempo |

### FX2 Gazer reverb

FX2 → B → **Gazer**: a large, modulated shoegaze reverb with a fuzz stage in
front.

| Control | What it does |
|---|---|
| Saturate | Fuzz before the reverb: clean at 0, about 7 dB of drive at 35 %, pedal-style fuzz at the top. Output level stays constant |
| Size | Room size and decay, from about 3 s to about 40 s |
| Diffusion | How much the sound is smeared before it enters the reverb; higher is smoother |
| Mod Depth | Pitch modulation of the reverb tail (chorus-like shimmer) |
| Rate mHz | Modulation speed, 10–2000 mHz (0.01–2 Hz) |
| Hi C / Lo C | High and low cut on the sound going into the reverb |

### Noise gate

A gate on every track. Set it on TRACK (GATE knob) or in MIXER → Gate (`NG`
when OUT3 is present). The
threshold runs from −96 dB to −12 dB; the lowest setting is Off. It takes fine
steps when turned slowly and speeds up when spun. MIXER → Gate meters show
the level the gate listens to on the Gate fader's scale, so the meter crosses
the fader exactly when the gate opens.

### Master saturation

MAIN → B adds four rows after the compressor:

| Control | What it does |
|---|---|
| Saturate | Amount of saturation on the master. 0 is a true bypass |
| Sat Mode | **Soft** or **Tape** |
| Tape Tone | Tape mode only: the treble roll-off, from darker (about 5 kHz) to brighter (about 18 kHz). Default 50 % is about 11.5 kHz |
| Sat Out | Output level after saturation, −12 to +6 dB, to match levels when comparing. Has no effect while Saturate is 0 |

- **Soft** is a smooth, symmetric saturation (adapted from BlueLex). Drive is
  level-compensated, so turning it up adds harmonics rather than volume.
- **Tape** is a tape-style voicing, not a model of a specific machine. Like
  tape it boosts treble going in and cuts it coming out, so highs saturate
  first. Loud passages also lose a little more treble (the treble roll-off is
  linked across both channels). More Saturate adds a gentle low-end bump. Very
  low frequencies and DC are removed. **Tape Tone** sets where the treble
  starts to roll off. Turn it down for warm, darker tape; turn it up to keep
  the top end open.

### Meters

**CPU load bar.** The thin bar right of the clock shows how busy the audio
engine is. It is green up to 70 %, amber to 90 % and red above. A white mark
shows the recent peak.

**Compressor gain reduction.** When the master compressor is on, Cobalt
shows how much it is turning the level down:

- **Header bar:** the bar just left of the CPU bar. Half height is 10 dB of
  reduction; full height is 40 dB.
- **Thresh row (MAIN → B):** a strip along the bottom of the Thresh row. It
  fills from the left as gain reduction rises. Full width is 40 dB. The three
  scale markers are at **5 dB, 10 dB and 20 dB**.

Both use a square-root scale, so small amounts of compression are easy to
see. They show the larger of the two channels, held over about 20 ms. The
fill colour is set in the theme editor (Compressor GR meter).

### Overdub recording fix

Stock 1.5.2 can stop with an error while overdubbing long takes because the
storage task falls behind. Cobalt converts recorded and played-back audio in
word-sized bursts instead of byte by byte. The audio is bit-identical.

### Scenes (alpha)

A scene is a snapshot of the mixer (track level, pregain, pan, sends, mute,
solo, compressor, EQ and gate), the master and aux buses, every FX1/FX2
parameter including the effect type, and the BPM. Each project holds up to
16, saved in the project file. Recalling a scene changes only the values
that differ, without loading anything, so playback and recording carry on.

- **Scene page:** select a project in PROJ and press A. Turn to pick a row
  and a number, then press Select:
  - **Load Scene:** recall that scene.
  - **Save Scene:** save the current state to that scene.
  - **Bank #:** give the project a number (None or 1–128) for MIDI Bank
    Select. A number another project has is refused ("BANK n IN USE").
  Press A to leave. PROJ Save also applies the Bank # chosen there.
- **Quick save:** hold A for 2 s on MAIN. A band at the top shows progress,
  then saves the next empty scene. A new project starts at scene 1.
- **MIDI:** Program Change recalls a scene: program 0 (PC 1 on gear that
  counts from 1) is scene 1. Bank Select loads the project with that Bank #
  (value 0 = Bank 1); a Program Change after it waits for the load. TOOLS > **MIDI Settings** (tap it, or press B with it selected;
  B goes back) sets **Bank CC** (MSB - CC 0 or LSB - CC 32) and **PC Ch** (All, None or
  1–16).
- **PROJ list:** the loaded project is marked with `* `, and a long selected
  name scrolls.

### Themes

The patcher page has a theme editor with 18 presets and 22 colours, a live
preview of 27 device screens, hold-to-compare with stock and an RGB565
preview. Themes export and import as JSON (`themes/` holds the presets).

## Building from source

Requirements: Arm GNU Toolchain **13.2.Rel1** (other GCC versions produce
different payloads), Python 3 and the original firmware.

```sh
ARM_CC=/path/to/arm-none-eabi-gcc ARM_OBJCOPY=/path/to/arm-none-eabi-objcopy \
ARM_NM=/path/to/arm-none-eabi-nm sh build.sh /path/to/BLUEBOX.BIN
```

`build.sh` compiles the payloads into `build/` and regenerates
`patch_bluebox.html`, `patch_bluebox.py`, `patch_manifest.json`, `themes/`
and `docs/index.html`.

### Tests

The tests run the real patched firmware routines in the Unicorn emulator,
compile host-side DSP checks with sanitizers, and drive both patchers.

```sh
python3 -m pip install -r tests/requirements.txt
python3 tests/run_checks.py /path/to/BLUEBOX.BIN
```

They need Node.js and a host C compiler; add `--browser` to also run the
Playwright end-to-end test. Regression tests compare against earlier
development builds kept in `tests/references/` (patch data only, no
firmware). The tests cover correctness, not sound or CPU load on the device.

### Continuous integration and GitHub Pages

`.github/workflows/pages.yml` runs on every push and pull request. It checks
that no firmware image is committed, that the Python sources compile, that
the theme presets load, and that `docs/index.html` matches
`patch_bluebox.html`. It also runs the patcher page tests and the sanitized
DSP host tests. The emulator suite needs the original firmware, so run
`tests/run_checks.py` locally before pushing.

On `main`, the workflow then publishes `docs/` (the self-contained patcher
page) to GitHub Pages at [https://sbsmnt.github.io/bluebox-cobalt-mod/](https://sbsmnt.github.io/bluebox-cobalt-mod/).
Enable it once under **Settings → Pages → Build and
deployment → Source: GitHub Actions**.

## Layout

| Path | Contents |
|---|---|
| `src/` | Payload sources (M7 audio core, M4 UI core) and linker scripts |
| `tools/` | Release builder and patcher templates |
| `tests/` | Emulator, host and patcher tests |
| `themes/` | Theme presets as JSON |
| `docs/` | GitHub Pages copy of the patcher |
| `.github/workflows/` | Checks and Pages deployment |
| `patch_bluebox.html`, `patch_bluebox.py` | Ready-to-use patchers |

## Credits

Cobalt builds on **BlueLex**, the modded firmware for the 1010music Bluebox
Euro by lexnels: https://github.com/lexnels/bluelex-bluebox-euro. The CPU
load indicator, the MAIN → B compressor level and meter approach, the Soft
master saturation, and the delay pitch and send to reverb were adapted from
BlueLex, with every desktop address and hook traced independently.
[NOTICE.md](NOTICE.md) has the full attribution and disclaimer.

See [CHANGELOG.md](CHANGELOG.md) for release notes.
