# Changelog

## Unreleased

- **XY pad for the spectral sends:** on the XY screen, the FX1 pad is
  labelled SPECTRAL in Stasis, Flux and Spiral, and moves the mode's own
  controls: Stasis Tone (X) / Wandr (Y), Flux
  Blur / Wandr, Spiral Shift / Cutoff. The other FX1 modes keep the stock
  Cutoff / Width, and the REVERB pad is unchanged.

- **Spiral level fix:** with Shift near the top and FEEDB close to full, the
  repeats could jump in level and clip. The loop now has a soft limit (unity
  to -3 dBFS, never over 0 dBFS) and a very small loss per repeat, so full
  FEEDB fades slowly instead of building up.

- **Boot screen:** the version in the bottom-right corner of the start-up
  screen reads `1.5.2 Cobalt 1.3`, so you can see at power-on that Cobalt is
  installed. TOOLS is unchanged.

## 1.3.0-dev.3 (dev, 2026-11-26)

- **Spectral FX1 sends:** FX1 → B → A opens a second page, Spectral, with
  three new FX1 modes. **Stasis** is a spectral freeze (Snap or Chop on every
  Time step from 1/64 to 1 bar, or a hand-held Freeze), with Fade, Wandr,
  Tone and Level. **Flux** is a spectral blur (Blur 20 ms to 10 s; full Blur
  holds the wash). **Spiral** is a frequency-shift delay: the SPEC
  frequency shifter inside the delay loop, Shift ±2.5 kHz (default about
  +77 Hz), SPRD, Drift and the stock filter. Tone on Stasis and Flux is
  level-neutral, and a soft limit keeps their output under 0 dBFS. All
  controls are saved in the project and can be MIDI-learned.

## 1.3.0-dev.2 (dev, 2026-11-22)

- **SAT insert type** in the DRV group (CLIP, SAT, FOLD, WRAP): the per-track
  saturation from
  [pull request #7](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/7) by
  [Alex Bonhomme](https://github.com/alexbonhomme). Drive is the saturation
  drive (up to 36 dB, level-compensated), Tone reads CURVE and morphs the
  Soft curve into an overdriven hard clip, Mix and Output as on the other
  drives. Saved per track, MIDI-learnable and in scenes.

## 1.3.0-dev.1 (dev, 2026-11-21)

First development build of v1.3 on `dev`. TOOLS shows `Cblt 1.3d`. Includes
the DJ EQ from
[pull request #2](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/2) by
[Milan Wulf](https://github.com/milanwulf). User guide: README "New in v1.3"
and the patcher page's "New in v1.3" section.

- **Synth tracks:** mono synth per track (VA from Mutable Instruments Plaits,
  Cobalt Cluster), filter with Reso, volume and modulation envelopes, two
  LFOs, Drone, Input None support, MIDI-learnable.
- **Track inserts:** DRV (CLIP, FOLD, WRAP), FLTR (LP, BP, HP, DJ with RES
  and ENV) and SPEC (SHFT, SPRD, RING frequency shifter; HARM, CHRD, BELL
  resonator bank with chords). Inserts are saved per track and in scenes.
  SPEC stops processing once a tail has decayed below −120 dB and the input
  is silent.
- **DJ EQ and filter** (PR 2): 4/3-band ISO or EQ curve, kills, LP/HP
  filter, DJ view, presets, in scenes. 4 bands cost about 3 % CPU per stereo
  track on hardware, a neutral track nothing.
- **Synth Seed knob** reads SEED at rest and its number while turning.
- **Out 2 clock:** Clk Out L / R (PROJ → B), 1–24 PPQ pulses on Out 2.
- **MIDI Learn** for every Cobalt control: Cobalt's FX1 delay modes, Gazer,
  master saturation and the Gate threshold now move with their CCs (the
  value used to stay at 0). A mapped CC on an FX control shows its value
  with units for three seconds, and the Gate status on TRACK no longer
  blinks `--` while the threshold moves.
- **User Settings** (Dim No Input, Track Knob 8, Hide DEFAULT) and the
  `DEFAULT` new-project template.
- **Bank Select fix:** a project load that finished between two UI passes
  is no longer missed, so a second Bank Select straight after a load works
  (before, it could wait for a 15 s timeout).

## v1.2.0

Phones AUTO cue, for bluebox desktop firmware 1.5.2. Contributed by
[Milan Wulf](https://github.com/milanwulf) in
[pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1).

- **Phones Src AUTO** (MAIN → B): the phones play the cue mix while any track
  has its cue on or turned up, and the main mix otherwise, like PFL on a DJ
  mixer. Works with CUE Style Fader and Toggle and both CUE Send settings;
  stopped tracks and tracks without an input count when cued. Back to Main
  0.1 s after the last cue goes off. CUE and MAIN are unchanged.
- **Cue icons:** a headphone in the top-right corner of every cued track
  header, in the theme's active button fill with a background outline.
- **Unconfigured tracks:** tracks without an input are greyed out on the
  track headers.
- TOOLS shows `Cblt 1.2`.

## v1.1.0

Scenes and MIDI, for bluebox desktop firmware 1.5.2.

- **Scenes:** up to 16 snapshots per project of the mixer (track level,
  pregain, pan, sends, mute, solo, compressor, EQ, gate), master and aux
  buses, every FX1/FX2 parameter including the effect type, and the BPM.
  Saved in the project file. Recall replays only the values that differ,
  without loading anything, so playback and recording carry on.
- **Scene page** (A on PROJ): Load Scene, Save Scene and Bank # rows with a
  Select button; A leaves. The project name shows at the top.
- **Quick save:** hold A for 2 s on MAIN to save the next empty scene. A new
  project starts at scene 1. Scene saves also save the project.
- **MIDI recall:** Program Change recalls a scene: program 0 (PC 1 on gear
  that counts from 1) is scene 1. Bank Select (CC 0, or CC 32, value 0 =
  Bank 1) loads the project with that Bank #.
- **MIDI Settings** in TOOLS (tap the row, or B on it; B goes back): Bank CC,
  PC Ch (All / None / 1–16), MIDI Out, Map CC Sync and Clock Src.
- **Map CC Sync** (Off / USB / USB+MIDI Out / MIDI Out): sends the mapped CC
  values to the controller on plug-in, project load and scene recall, so LED
  rings and motor faders follow the bluebox.
- **MIDI Out** (Default / USB Thru / All Thru): notes and CC from USB, or USB
  and MIDI in, are passed to the MIDI out with their channel. Clock and
  transport are never passed on.
- **Clock Src** (MIDI In / USB): with clock on more than one input, only one
  is followed; the preferred one takes over at once, another after 300 ms of
  silence. Changing it shows the order on the band.
- **Bank #:** None or 1–128 per project; a number another project already
  has is never taken. PROJ Save applies the chosen number.
- **PROJ list:** the loaded project is marked with `* `; long selected names
  scroll.
- **MIXER sends with OUT3:** the seven buttons read `O 2`, `O 3` and `NG`
  (gate) so they fit; the six-button layout is unchanged.
- Patcher page: a "Using scenes and MIDI" guide.
- TOOLS shows `Cblt 1.1`.

## v1.0.0

First public release, for bluebox desktop firmware 1.5.2.

- FX1 delay modes: Delay (stock), Warp delay (pitch, drift and send to
  reverb), Granulr, Tape, Ducked and Reverse.
- FX2 Gazer reverb.
- Per-track noise gate on TRACK and MIXER, with Gate meters in MIXER.
- Master saturation: Soft and Tape, with Tape Tone and Sat Out.
- CPU load bar and compressor gain-reduction meters.
- Overdub recording fix: faster, bit-identical PCM conversion so long
  overdubs no longer fall behind.
- Theme editor with 18 presets and 22 colours, and browser and Python
  patchers with exact restore of the original firmware.
- TOOLS shows `Cblt 1.0` in the middle header field.
