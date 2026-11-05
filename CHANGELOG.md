# Changelog

## Unreleased

- **Phones Src AUTO** (MAIN → B): the phones play the cue mix while any track
  has its cue on or turned up, and the main mix otherwise, like PFL on a DJ
  mixer. Works with CUE Style Fader and Toggle and both CUE Send settings;
  stopped tracks and tracks without an input count when cued. Back to Main
  0.1 s after the last cue goes off. CUE and MAIN are unchanged.
- **Cue icons:** a headphone in the top-right corner of every cued track
  header, in the theme's active button fill with a background outline.
- **Unconfigured tracks:** tracks without an input are greyed out on the
  track headers.

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
