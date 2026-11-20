# Chordism

Chordism turns your M-VAVE FM-1 into a chord synth. Each key plays a chord of four voices, and you choose the kind of
chord, how it's spread and where each voice sits in stereo. The voices run through a filter that opens as you press
harder, a delay and a stereo plate reverb, with an arpeggiator on top. It comes with the Chordism module's 57 sounds,
and you can keep 16 of your own.

![Chordism playing C major 9 on Lush Pad: the chord's name, its four notes under curtains of light, and the knobs for chord, spread, width and volume](manual/img/screen-chord.png "Chordism on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). Play it right here, with sound. It runs the same code as the FM-1, so you don't
  need one.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- To learn every page, read the [Manual](manual/).
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/charlesvestal/fm1-chordism) is under MPL-2.0, its engine under MIT. It's built on
  the Chordism module for Schwung, fm1-base and felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton
  Instruments.

M-VAVE and FM-1 are trademarks of their owners. Chordism is not affiliated with M-VAVE.
