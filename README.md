# Cobalt for 1010music bluebox desktop

**v1.3.0-dev.3 - development build for bluebox desktop firmware 1.5.2**

> This is the `dev` branch: the next version while it is being tested on
> hardware. The current release is
> [v1.2.0 on `main`](https://github.com/sbsmnt/bluebox-cobalt-mod/tree/main).

Cobalt is an unofficial firmware mod for the bluebox desktop. It adds a synth
on every track, track insert effects, a DJ EQ and filter, delay modes, a new
reverb, a per-track noise gate, master saturation, compressor meters, an
analog clock on Out 2, an overdub recording fix, mixer scenes with MIDI
recall, MIDI controller sync and custom colour themes. The patcher runs locally: it takes your copy of the official
1.5.2 firmware and writes a new `BLUEBOX.BIN`. No firmware is distributed
here.

**Open the patcher: [sbsmnt.github.io/bluebox-cobalt-mod](https://sbsmnt.github.io/bluebox-cobalt-mod/)**

> Cobalt is not made, endorsed or supported by 1010music. Keep a copy of your
> working firmware and back up your projects before installing.

## New in v1.3 (in development)

- **Track inserts:** an insert effect on every track, in three groups:
  **DRV** (CLIP, SAT, FOLD, WRAP), **FLTR** (LP, BP, HP, DJ, with resonance and
  an envelope follower) and **SPEC** (frequency shifter SHFT, SPRD, RING and
  resonator bank HARM, CHRD, BELL). Press A on TRACK to reach them.
- **Synth tracks:** any track can play a built-in mono synth from MIDI,
  with two engines (VA, from Mutable Instruments Plaits, and Cobalt's
  Cluster), a resonant filter, an envelope, two LFOs and Drone. TRACK →
  TRACK → TRACK opens it. A synth track needs no input.
- **DJ EQ and filter:** a DJ-style 4- or 3-band EQ with kills and a
  low-/high-pass filter on every track, with its own DJ view (the third EQ
  press). A track with the DJ EQ at its centre costs no CPU.
- **Out 2 clock:** PROJ → B → **Clk Out L / Clk Out R** send analog clock
  pulses (1–24 PPQ) on Out 2 left and right for modular gear.
- **Spectral FX1 sends:** FX1 → B has a second page, **Spectral**, with
  **Stasis** (a spectral freeze: snapshots on every Time step, or held by
  hand with Freeze), **Flux** (a spectral blur that smears the send into a
  moving wash) and **Spiral** (a frequency-shift delay whose repeats climb
  or fall in a barber-pole spiral).
- **Scenes** now also hold each track's insert and DJ EQ settings.
- **MIDI Learn** works on all of Cobalt's controls: the FX1 delay modes,
  Gazer, master saturation, the Gate threshold, the synth, the inserts and
  the DJ knobs. A mapped CC on an FX control shows its value as you move it.
- **User Settings** (TOOLS): Dim No Input, Track Knob 8 (Gate or OUT3) and
  Hide DEFAULT.
- **New-project template:** a project named `DEFAULT` becomes the starting
  point for PROJ → New.

Thanks to [Milan Wulf](https://github.com/milanwulf) for the DJ EQ ([pull request #2](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/2)) and [Alex Bonhomme](https://github.com/alexbonhomme) for the SAT saturation ([pull request #7](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/7)). Details in
[Track inserts](#track-inserts), [Stasis](#stasis), [Flux](#flux), [Spiral](#spiral), [Synth tracks](#synth-tracks),
[DJ EQ and filter](#dj-eq-and-filter), [Out 2 clock](#out-2-clock),
[User Settings](#user-settings-and-the-default-template) and the
[CHANGELOG](CHANGELOG.md).

## New in v1.2

- **Phones Src AUTO:** MAIN → B → Phones Src has a third choice, AUTO. The
  phones play the cue mix while any track is cued and the main mix
  otherwise, like PFL on a DJ mixer.
- **Cue icons:** a small headphone marks every cued track on the track
  headers.
- **Unconfigured tracks:** tracks with no input selected are greyed out on
  the track headers.

Thanks to [Milan Wulf](https://github.com/milanwulf) for this feature ([pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1)). Details in [Phones AUTO cue](#phones-auto-cue).

## New in v1.1

- **Scenes:** up to 16 snapshots per project of the mixer, master and aux
  buses, FX1/FX2 (including the effect type) and BPM. Recall is instant,
  with no project load, so playback and recording carry on.
- **Scene page:** select a project in PROJ and press A to load or save a
  scene and set the project's Bank #.
- **Quick save:** hold A for 2 s on MAIN to save the next empty scene.
- **MIDI recall:** Program Change recalls a scene; Bank Select loads the
  project with that Bank #. Channel and Bank CC in TOOLS > MIDI Settings.
- **Map CC Sync:** the bluebox sends the values of its mapped CCs to your
  controller on plug-in, project load and scene recall, so LED rings and
  motor faders show the bluebox values.
- **MIDI Out thru:** notes and CC from USB (or USB and MIDI in) are passed to
  the MIDI out.
- **Clock Src:** with clock on more than one input, one of them is followed
  (MIDI In first, or USB first).
- **PROJ list:** the loaded project is marked with `* `, and long names
  scroll.
- **MIXER sends with OUT3:** buttons read `O 2`, `O 3` and `NG` (gate) so
  all seven fit.

Details in [Scenes](#scenes), [MIDI](#midi) and the [CHANGELOG](CHANGELOG.md).

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
5. Check that the middle TOOLS field shows **Cblt 1.3d** (Cblt 1.2 for the
   release). The official version
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

Open FX1 and press **B** to choose the mode. The list has two pages: **Delay** (Delay, Warp, Granulr, Tape, Ducked, Reverse) and **Spectral** (Stasis, Flux, Spiral). Press **A** while the list is open to switch pages (the header reads **FX1 / Delay** or **FX1 / Spectral**). The list opens on the page of the current mode. Each mode shows its own
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

#### Stasis

FX1 → B → Spectral → **Stasis**: a spectral freeze. It takes a snapshot on every **Time** step, and **Freeze** holds one for as long as it's on. Each snapshot is the sound from just before, held as a smooth, endless sustain. It isn't a repeating grain, so there's no loop buzz. It covers about 150 Hz to 16 kHz: lower and higher frequencies are left out (−6 dB at about 141 Hz and 16 kHz), which saves CPU and keeps the hold out of the bass.

| Control | What it does |
|---|---|
| Freeze | Captures and holds by hand, as on the delays, whatever Time is set to. Pressing again after Off captures again. The hold starts about 40 ms after the press, with the sound from just before it. While it's on, Snap and Chop pause; letting go returns to them |
| Time | 1/64, 1/32, 1/16, 1/8 (the default), 1/4, 1/2 or 1 bar. Stasis runs on its own like a synced delay: on the transport's beats while it plays, at the project tempo while it's stopped. At 1/64 a step is shorter than a capture, so Snap takes a new snapshot about every second step; Chop still chops on every 1/64 |
| Mode | **SNAP**: a new snapshot every step, so the hold always sounds and changes on each boundary (each step holds the sound from just before it). **CHOP**: the hold sounds on every other step and fades out in between, with a new snapshot for each open step |
| Fade | How fast the hold swells in and dies away, 10 ms to 4 s. In Chop mode, a short Fade gives clean chops |
| Wandr | 0 is an exact, still hold. The first half of the knob makes the pitch wander, like a slow ensemble, up to about ±50 cents at the middle. The top half scatters the partials on top of that, into a washy, grainy cloud |
| Tone | Tilts the hold darker (left) or brighter (right), flat in the middle; starts at 3/4 (bright). It changes the colour, not the overall loudness |
| Level | Level of the hold: about the input level in the middle, up to +6 dB. A soft limit keeps peaks under 0 dBFS |
| To Rev | Send to the FX2 reverb, so the hold can be reverberated |

**Encoders:** Fade / Time, Wandr / Tone, Level, Mode / To Rev, top / bottom from the left; the Freeze toggle on the left.

**How it sounds:**
- Held tones keep their exact pitch.
- A chord holds as steadily as it played.
- Each held note sounds as it did when it was captured: the hold is built from the 43 ms before.

**Silence:** until its first snapshot, Stasis returns silence (the stock delay doesn't play in this mode).

**CPU:** holding costs about 9k emulator instructions per block (about 11.5k with Wandr up). Snap at 1/32, which captures all the time, averages about 16k, peaking at 22k. The work is spread evenly over each block by its measured cost, so there are no spikes.

#### Flux

FX1 → B → Spectral → **Flux**: a spectral blur, the moving counterpart to Stasis. It smears what you send into a moving wash, like a spectral reverb: each part of the spectrum glides towards the new sound over the Blur time, so notes melt into each other. It covers the same 150 Hz to 16 kHz band as Stasis, on the same engine and memory.

| Control | What it does |
|---|---|
| Blur | How long the smear is, 20 ms to 10 s. Short sounds come back quieter and longer. At full, the input is ignored and the blurred sound holds forever: turn it up to full to freeze the wash |
| Wandr | As on Stasis: the first half wanders the pitch (up to about ±50 cents), the top half scatters the partials into a diffuse wash |
| Tone | Tilts the wash darker (left) or brighter (right), flat in the middle; starts at 3/4 (bright). It changes the colour, not the overall loudness |
| Level | Level of the wash: about the input level in the middle (starts at −5 %), up to +6 dB. A soft limit keeps peaks under 0 dBFS |
| To Rev | Send to the FX2 reverb |

**Encoders:** Blur / Wandr, Tone / Level, To Rev, top / bottom from the left.

**CPU:** Flux analyses and resynthesises all the time, so it costs more than a Stasis hold: about 15.5k emulator instructions per block, peaking at 19k (18k / 22k with Wandr up). With nothing sent and the tail gone, it costs almost nothing.

#### Spiral

FX1 → B → Spectral → **Spiral**: a frequency-shift delay. Every repeat is shifted by the same number of Hz again, so the echoes climb (or fall) in an endless barber-pole spiral, turning metallic and bell-like as they go. It's the stock delay with the SPEC insert's frequency shifter inside the feedback loop, so it needs no FFT.

| Control | What it does |
|---|---|
| Time / FEEDB | Delay time and feedback, as on Delay. More feedback, more turns of the spiral |
| Shift | How far each repeat moves, from about 0.1 Hz to 2.5 kHz: right of centre rises, left falls, the centre is no shift (plain repeats). A few Hz gives slow, phasing spirals; tens to hundreds of Hz turn the repeats inharmonic. The value shows in Hz while you turn it. Starts at about +77 Hz |
| SPRD | Stereo spread: the left repeats rise and the right ones fall by the same Shift |
| Drift | Wow and flutter, as on Warp |
| FILT / CUTOF / WIDTH | The stock filter, inside the loop: keeps long spirals from getting harsh |
| To Rev | Send to the FX2 reverb |
| BEAT | Syncs Time to the project tempo, as on Delay |

Spiral has no PING or QUAD: it runs without them, and your PING / QUAD settings come back in the other modes.

**CPU:** about 5k emulator instructions per block more than plain Delay; like Stasis and Flux it skips the Granular history the other delay types keep filling.

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

### Track inserts

Every track has an insert effect after its EQ and gate. On TRACK, **A**
cycles the native left and right controls, then the insert's left and
right controls. **INSRT** switches the insert on (it is off on a new track).
The group button beside it (**DRV**, **FLTR**, **SPEC**) chooses the group,
and the type knob steps through its types. Changing type or switching off
fades through dry, so nothing clicks.

| Group | Types | Knobs |
|---|---|---|
| **DRV** | **CLIP** (hard clip, dirtier over the top of Drive), **SAT** (soft saturation that **CURVE** morphs into an overdriven hard clip, level-matched), **FOLD** (wavefolder), **WRAP** (wrap-around, anti-aliased) | Drive, Tone (**CURVE** on SAT), Mix, Output |
| **FLTR** | **LP**, **BP**, **HP**: resonant filters. **DJ**: one knob, centre off, left low-pass, right high-pass | Cutoff (shown in Hz), **RES** (resonance, up to squelchy), **ENV** (envelope follower: right opens the filter on loud hits, left closes it), Output |
| **SPEC** | **SHFT**, **SPRD**, **RING**: frequency shifter | **Shift** (0 Hz at the centre, up to about ±2.5 kHz), **FDBK** (feedback, up to 95 %), Mix, Output |
| **SPEC** | **HARM**, **CHRD**, **BELL**: resonator bank | **Pitch** (C1–C7), **Decay** (50 ms–12 s), Mix, Output, and **Chord** for CHRD |

**Frequency shifter.** It moves every partial by the same number of Hz.
Harmonic sounds turn metallic, small shifts swirl and phase, and feedback
gives a rising or falling barber-pole effect.
- **SHFT:** shifts the whole track.
- **SPRD:** shifts left and right in opposite directions. A mono track plays
  it as SHFT.
- **RING:** ring modulation, with both sidebands.

**Resonator bank.** Eight tuned resonators ring at the Pitch note, fed by
the track, spread across the stereo field.
- **HARM:** the harmonic series.
- **CHRD:** a chord. Chord picks MIN9, MAJ9, MIN7, MAJ7, SUS, 5THS, 4THS or
  DIM, on the top-right encoder of the right page.
- **BELL:** bell partials.

Higher partials decay sooner, and a gentle limiter keeps loud input in
check. Tails ring out naturally after the input stops. Once a tail has
faded completely, the resonator stops using CPU until sound comes back.

**Captions.** The SPEC knobs show their name at rest and their value while
turning or while a mapped CC moves them.

**Saving, scenes and cost**
- Insert settings are saved per track and captured by scenes, and every
  knob except the type can be MIDI-learned.
- A drive or filter costs about 1–2 % CPU per track, a shifter about 3 %
  (stereo) and a resonator about 2 %.

### Synth tracks

Every track can play a built-in synth instead of its input. Press **TRACK**
on the TRACK page to cycle Normal → Sidechain → **Synth**, then tap **SYN
ON**. The synth replaces the track's input; SYN OFF gives the input back.
A synth track needs no input: with Input None it still plays (and is not
greyed). VOL, Gain, the mixer, the gate, the inserts and the DJ EQ work on
the synth as on any track.

It is monophonic with last-note priority. It plays notes from USB and MIDI
in on its **MIDI Ch**, with velocity. **Drone** plays a held note with no
MIDI at all.

**A** cycles four groups of knobs, two pages of left and right:

| Page | Left | Right |
|---|---|---|
| 1 | SYN ON, **Timbre**, **Harm**, **Morph** | **Attack** (1 ms–2 s), **Cutoff** (20 Hz–20 kHz), **Release** (5 ms–5 s), **Reso** |
| 2 | **LFO S** and **LFO A** (speed and amount), **LFO2 S** and **LFO2 A** | **Env %** (−100 to +100 %), **Env Atk**, **Env Dec**, **Seed** (Cluster only) |

**B** opens the setup menu (B returns): **Engine**, **MIDI Ch**, **Drone**,
**Env Dest**, **LFO Dest**, **LFO Shape**, **LFO Sync**, **LFO2 Dest**,
**LFO2 Shape**, **LFO2 Sync**.

**Engines**

- **VA:** the virtual analog engine of Mutable Instruments Plaits: two
  detunable oscillators morphing between saw, square and sync sounds.
  Timbre, Harm and Morph are Plaits' own controls for it.
- **Cluster:** Cobalt's own engine, four oscillators across 16 band-limited
  waves.
  - **Harm** is the span: left detunes them apart (up to ±12 semitones),
    centre is unison, and right stacks them on scale degrees (thirds,
    fourths, fifths).
  - **Timbre** is the warp: centre is clean, left is phase modulation
    around the four oscillators, right is cross-modulation with ring mod at
    the end.
  - **Morph** scans the waves.
  - **Seed** (1–32) re-rolls the small detunes and phases of that track.

**Shaping**

- **Cutoff** and **Reso** set a low-pass filter with a gentle peak (no
  self-oscillation).
- **Attack** and **Release** shape the volume.
- The modulation envelope (**Env %**, **Env Atk**, **Env Dec**, destination
  **Env Dest**) and the two LFOs (rate 0.01–20 Hz, or a tempo division from
  1/64 to 1 bar with **LFO Sync**) can each move Timbre, Harm, Morph or
  Cutoff. Two sources on the same destination add up.
- LFO shapes are Triangle, Sine, Square and Random.

**Saving and MIDI**

- Every setting is saved with the track in the project, and every knob can
  be MIDI-learned. Cutoff, the LFO rates and the envelope times follow the
  same curve by CC as on the encoder.
- Scenes don't include synth settings.
- **Cost:** each synth track uses roughly 6 % of the audio CPU.

### DJ EQ and filter

Every track has a DJ-style EQ and filter after its own EQ, gate and insert:

| Control | What it does |
|---|---|
| High / Hi Mid / Lo Mid / Low | Four bands, 0 dB at the centre: left down to −26 dB (with the ISO curve the far left kills the band), right up to +6 dB |
| Filter | Left: low-pass down to 40 Hz. Right: high-pass up to 10 kHz. Centre: off |
| Res | Resonance of the filter corner |

**Opening the DJ view**
- Press **EQ** until the third view: EQ → EQ full screen → **DJ**.
- The DJ view is laid out like TRACK: two blocks of four knobs, switched
  with **A**. The first block is High, Hi Mid, Lo Mid and Low; the second
  is Filter and Res.
- Pick the track as on TRACK.
- Turning a knob shows its value (dB, `KILL`, `LP`/`HP`, %) in its label.
- The bands and the filter rest at their centre: turning across it stops
  there, and leaving it takes a few more steps.
- The level meter is hidden in this view.

**Kills and Filter Reset**
- The buttons on the right are **kills**, placed like the band knobs:
  `Kill High`, `Kill Hi Mid`, `Kill Lo Mid` and `Kill Low` (`Kill Mid` with
  3 bands).
- A press kills the band. A second press returns it to the value it had
  before (0 dB if it was killed with the knob).
- While a band is killed, its button lights red and its knob label turns
  red too.
- **Filter Reset**, above the Filter knob, puts the filter back to Off (Res
  stays). It is lit while the filter is on.
- Kills and resets fade over about 10 ms, so they don't click.

**Getting around**
- **B** opens the DJ settings; B there returns to the DJ view.
- Press **EQ** again to return to EQ, or **TRACK** for the TRACK page.
- EQ remembers the DJ view when you come back from another page.

**DJ settings** (B in the DJ view) apply to all tracks:

| Setting | What it does |
|---|---|
| Bands | `4 Bands` (High, Hi Mid, Lo Mid, Low) or `3 Bands` (High, Mid, Low). With 3 bands the knobs keep their places (Mid where Hi Mid is, the Lo Mid place stays empty), and it costs less CPU |
| Preset | Crossovers modelled on known mixers; the list follows the band count. 4 bands: `Default`, `Xone:92`, `Xone:96`, `DJM-V10`. 3 bands: `Default`, `DJM`, `Xone:42`, `Rane`, `Evo4`. A preset sets the ISO curve; changing a crossover by hand switches to `Custom` |
| Curve | `ISO` (default): steep, phase-matched Linkwitz-Riley 4th-order bands that sum back to the input at 0 dB; the far left kills a band. `EQ`: gentler 2nd order, no kill |
| Xover Lo / Mid / Hi | The crossovers (Mid only with 4 bands). Wider spacing gives deeper mid kills |
| Filter Slope | `12 dB` or `24 dB` per octave |
| Res Max | Caps the Res knob, 0–100 % |

Presets use the mixers' published EQ band frequencies. The crossovers sit
between neighbouring bands, so they recreate the band split, not the exact
EQ shapes.

**Saving and scenes.** All values are saved in the project and captured by
scenes.

**CPU**
- A track with every DJ control at its centre is not processed at all and
  costs nothing.
- A track using the EQ costs about 3 % of the CPU with 4 bands (less with
  3), and the filter adds about 0.6 %.
- Turning a control away from the centre, or back, fades in or out over
  10 ms, so there are no clicks.

**MIDI learn** works on the DJ knobs; the kill and reset buttons can't be
learned. Learned CCs follow the full range; the centre detent applies only
to the bluebox encoders.

Thanks to [Milan Wulf](https://github.com/milanwulf) for this feature ([pull request #2](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/2)).

### Noise gate

A gate on every track. Set it on TRACK (GATE knob) or in MIXER → Gate (`NG`
when OUT3 is present). The
threshold runs from −96 dB to −12 dB; the lowest setting is Off. It takes fine
steps when turned slowly and speeds up when spun. MIXER → Gate meters show
the level the gate listens to on the Gate fader's scale, so the meter crosses
the fader exactly when the gate opens. The threshold can be MIDI-learned on
TRACK and MIXER, and TRACK shows the gate's state (OPN / CLS) as it moves.

### Phones AUTO cue

MAIN → B → **Phones Src** has a third choice, **AUTO**, next to CUE and MAIN.
It works like PFL on a DJ mixer: the phones play the cue mix while any track
has its cue on (CUE Style Toggle) or turned up (CUE Style Fader), and the
main mix otherwise. Only the cue setting counts, not whether the track is
playing or has an input, so a cued track that is stopped still gives you its
(silent) cue. After the last cue goes off, the phones return to Main within
0.1 s. CUE Send (Pre/Post Fader) works as before. CUE and MAIN are unchanged.

**Track headers** on every page with the track strip:

- A small headphone icon in the top-right corner marks each track with its
  cue on, in any Phones Src mode. It uses the theme's active button fill
  with a background-colour outline. On narrow headers it can cover the last
  letter of the name.
- Tracks with no input selected are greyed out with a stipple, so
  unconfigured tracks stand out. Tracks with a recorded clip are not greyed.

Both follow the track count, Single/Double Row and renamed tracks.

Thanks to [Milan Wulf](https://github.com/milanwulf) for this feature ([pull request #1](https://github.com/sbsmnt/bluebox-cobalt-mod/pull/1)).

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

### Out 2 clock

PROJ → B has two rows, **Clk Out L** and **Clk Out R** (stock calls them
Clock In and Clock Out). Each is Off or **1, 2, 4, 8, 12 or 24 PPQ**.

**What a clock channel does**
- A channel that is on sends 5 ms positive pulses on Out 2 left or right,
  in place of that channel's audio.
- A channel that is Off keeps Out 2's audio.
- Pulses follow the bluebox tempo and run only while the transport plays,
  including when the bluebox follows external MIDI clock.
- Out 2's level knob sets the pulse height.

Two channels give two clocks at different rates, for example 4 PPQ for a
module's clock input and 1 PPQ for another.

**How the screens show it**
- On MIXER, the send that a clock channel has taken over is greyed:
  - in OUT2 Mode Mono, Clk Out L takes the OUT2 send and Clk Out R the
    OUT3 send;
  - in Stereo, either channel takes the OUT2 send.
- While Clk Out R is on, TRACK treats OUT3 as absent.

**Saving.** The rates are saved with the project. Projects from before v1.3
start with both Off, so no project loses its Out 2 audio.

**Tested gear.** Tested with an O_C and an Endorphin.es Ghost. Out 2 is a
line output, so check that your module accepts its pulse level.

### Overdub recording fix

Stock 1.5.2 can stop with an error while overdubbing long takes because the
storage task falls behind. Cobalt converts recorded and played-back audio in
word-sized bursts instead of byte by byte. The audio is bit-identical.

### Scenes

A scene is a snapshot of the mixer (track level, pregain, pan, sends, mute,
solo, compressor, EQ and gate), each track's insert and DJ EQ settings, the
master and aux buses, every FX1/FX2 parameter including the effect type, and
the BPM. Synth settings are not part of scenes. Each project holds up to
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

### MIDI

Also in TOOLS > **MIDI Settings**:

- **MIDI Out:** what the MIDI out passes on, besides the bluebox's own MIDI
  output. `Default` passes nothing else (stock). `USB Thru` passes the
  messages (notes, CC, …) coming from USB (computer on USB-B, controller on
  USB-A). `All Thru` also passes MIDI in. Channels are kept. Clock and transport are never
  passed on, so a clocked setup never sees a second clock.
- **Map CC Sync:** where the values of the mapped CCs are sent, so a
  controller with LED rings or motor faders (OXI E16, MIDI Fighter Twister,
  Launch Control XL 3, Roto-Control, Faderfox EC4, …) shows them:
  - `Off`: never.
  - `USB` (default): to the controller on USB-A when it is plugged in, after
    a project load and after a scene recall.
  - `USB+MIDI Out`: as USB, plus the MIDI out after a project load or scene
    recall.
  - `MIDI Out`: the MIDI out only, after a project load or scene recall.
  The controller must listen on the channel and CC it sends (the usual
  default). If the values can't get out, a band says `MAP CC SYNC: USB OUT
  STUCK` or `MAP CC SYNC: MIDI OUT STUCK`.
- **Clock Src:** which input is followed when more than one sends clock.
  `MIDI In` (default): MIDI in, then the computer (USB-B), then the USB-A
  controller. `USB`: USB-B, USB-A, then MIDI in. The preferred input takes
  over at once; a lower one only after 300 ms without clock from the current
  one. Start, stop, continue and song position follow the same input. The
  band shows the order when the setting is changed.

**MIDI Learn** (EDIT → B → Learn) works on every Cobalt control: the FX1
delay modes and Gazer, master saturation, the Gate threshold, the synth
knobs and SYN ON, the insert knobs and INSRT, and the DJ knobs. Their CCs
are also sent by Map CC Sync. A mapped CC on an FX1 or Gazer control shows
its value with units (such as `-7 st` on Pitch) for three seconds, as a turn
does.

### User Settings and the DEFAULT template

TOOLS → **User Settings** (tap it, or press B with it selected; B goes back):

| Setting | What it does |
|---|---|
| Dim No Input | `On` (default): tracks with no input selected are greyed on the track headers. `Off`: no greying |
| Track Knob 8 | `Gate` (default): TRACK's eighth knob is the Gate threshold. `OUT3`: it stays the stock OUT3 send. This setting only applies in OUT2 Mode Mono, and there A reaches the other one on a third TRACK page |
| Hide DEFAULT | `On`: the template project (below) is left out of the PROJ list |

**New-project template.** Save a project named `DEFAULT` (any case; a
computer-made `_DEFAULT` works too). From then on, PROJ → File → **New**
starts from it: its settings, routing and effects are kept, and clips and
recordings are cleared. Without a `DEFAULT` project, New works as stock.

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

Pull requests go to the `dev` branch; `main` only receives tested releases
(see [CONTRIBUTING](CONTRIBUTING.md)). On `main`, the workflow then publishes `docs/` (the self-contained patcher
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
| `vendor/mutable/` | Mutable Instruments Plaits and stmlib sources (MIT) for the VA engine |
| `docs/` | GitHub Pages copy of the patcher |
| `.github/workflows/` | Checks and Pages deployment |
| `patch_bluebox.html`, `patch_bluebox.py` | Ready-to-use patchers |

## Credits

Cobalt builds on **BlueLex**, the modded firmware for the 1010music Bluebox
Euro by lexnels: https://github.com/lexnels/bluelex-bluebox-euro. The CPU
load indicator, the MAIN → B compressor level and meter approach, the Soft
master saturation, and the delay pitch and send to reverb were adapted from
BlueLex, with every desktop address and hook traced independently.
The synth's VA engine is from Mutable Instruments **Plaits** by Émilie
Gillet (MIT licence, notices kept in `vendor/mutable/`). The DJ EQ is by
[Milan Wulf](https://github.com/milanwulf), and the SAT insert's saturation
by [Alex Bonhomme](https://github.com/alexbonhomme).
[NOTICE.md](NOTICE.md) has the full attribution and disclaimer.

See [CHANGELOG.md](CHANGELOG.md) for release notes.
