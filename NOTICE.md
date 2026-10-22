# Notice

Cobalt is an unofficial modification of the 1010music bluebox desktop
firmware 1.5.2. It is not made, endorsed or supported by 1010music.

The official firmware is proprietary to 1010music and is not distributed
here. The patchers only work on a copy of the original firmware that you
supply, and they verify it before writing anything. No licence over 1010music
firmware is granted or implied.

## Attribution

Parts of Cobalt were adapted from **BlueLex**, the modded firmware for the
1010music Bluebox Euro by lexnels:
https://github.com/lexnels/bluelex-bluebox-euro (reviewed at commit
`c30be5586ec5e81bdeeebfcf7027697e6cb56619`).

Adapted from BlueLex, with every desktop address, hook and call site traced
independently:

- the CPU load indicator (M7 profiler hook and header meter);
- the MAIN → B compressor level and the compressor meter UI approach;
- master saturation (curve and block ramp);
- delay pitch and the delay send to reverb, including the FX-panel placement
  and native list construction.

The BlueLex repository does not include a licence file. Cobalt keeps this
attribution and does not claim a new licence over upstream code.

Cobalt's own work includes the compressor gain-reduction meters, the FX1
delay modes (Delay, Granular, Tape, Ducked, Reverse), the FX2 Gazer reverb,
the per-track noise gate, tape-style master saturation, the overdub recording
fix, the theme editor and the browser and Python patchers.
