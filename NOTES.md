# Chordism on the FM-1: notes

- The engine (app/dsp/chd.c) is schwung-chordism's src/dsp/chordism_plugin.cpp (MIT, vendored in tests/ref/ for the
  side-by-side test) in C. tests/host/chd_test.cpp plays every preset on both: the dry path matches to 65 dB SNR.
- FM-1 changes: the delay at half rate in 16 bits; AMB-1's plate (half rate) for the original's (it sounded small);
  the voices a voice at a time over a block (state in registers), half of them on the second core; a guard that
  fades the oldest tail when a block takes over 80 %. Output at the original's level, peaks bent softly.
- Measured 2026-11-20 (360 MHz, MASTER 0): two Lush Pad chords overlapping 59 %; fast chords on 13 presets up to
  79 %, 0 xruns, the guard active. CHD_PROF (chd.h) times each pass, read on GLO > Setup through the screen reader.
- The look: LOOM-8's bands and grammar, an aurora palette (night sky, green / teal / violet / pink, a voice each).
