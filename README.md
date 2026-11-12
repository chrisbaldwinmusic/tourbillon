# Tourbillon

A boutique modulation plugin: chorus to rotary speaker in one unit. AU, VST3 and Standalone, built with JUCE. A sibling of Atelier (the channel strip), Reverie (delay and reverb) and Lutherie (pedals and amps), built the same way.

Seven circuits, three voices each:

- **Chorus**: Classic (one bucket-brigade line per side, as in the compact pedals), Dimension (two lines from the mono sum swept in opposition, the subtle studio widener) and Ensemble (three lines per side on interlocking sines plus a fast shimmer, the string machine).
- **Flanger**: BBD, Through-Zero (a swept copy against a fixed one, cancelling completely as it passes) and Clean, with positive or negative feedback.
- **Phaser**: 4, 8 or 12 all-pass stages on a JFET-style sweep, with Regen and drive.
- **Vibe**: four deliberately mismatched phase stages (15 nF, 220 nF, 470 pF, 4.7 nF) lit by one lamp onto photocells, with the lamp's and the cells' lag.
- **Tremolo**: Bias (valve), Opto (lamp and photocell) and Harmonic (two bands in opposite phase).
- **Vibrato**: BBD, Clean and Tape (wow and flutter); Depth is the swing in cents at any rate.
- **Rotary**: horn and drum behind an 800 Hz crossover, Doppler and level from each rotor at two microphones, rotors that take their time to change speed. Its voices are Slow, Fast and Brake.

## What's in it

- **LFO**: Sine, Triangle, Square, Ramps, S&H and Drift, free or synced to the tempo (following the song position while the transport runs), with stereo phase between the sides.
- **Envelope**: pushes the sweep with the playing, from this track or the sidechain key.
- **Wet filters and Width**: shape and widen the effect only; the dry signal is never filtered.
- **Mix**: equal-sum, so 50 % is the classic dry-plus-wet sound and the level holds across the range.
- **Motion**: the LFO's path for each side, and live what it does: the frequency response, the level, the pitch, or the rotary cabinet from above.
- **Guide**: explains whichever control is under the pointer (the shared knobs change job and caption with the type), gives tips and a starting point per source and style, and comments live on what the circuit is doing.
- **Workflow**: click-free switching of everything, zero latency, Wet Only for use on sends, A/B, undo/redo, presets.

## Building (macOS)

Needs the Xcode Command Line Tools, plus CMake and Ninja:

```sh
pip3 install --user cmake ninja
./build.sh
```

`build.sh` finds or fetches JUCE (it uses Atelier's copy in `../Channel/JUCE` when there is one), runs the offline test suite, installs the AU and VST3 into `~/Library/Audio/Plug-Ins`, then validates them with pluginval at strictness 10 and with Apple's `auval`. `./build.sh --no-install` builds and tests without installing; `--quick` skips validation. The build directory is `~/Library/Caches/Tourbillon-build`.

After installing, quit your DAW, run `killall -9 AudioComponentRegistrar`, and reopen it. The plugin appears under the manufacturer "Channel Audio".

## Tests

`Tests/Main.cpp` runs audio through the processor offline and checks bypass and the dry path, LFO rate against Hz and tempo, stereo phase, through-zero cancellation, the phaser's notch positions, vibrato depth in cents at different rates, chorus width, the rotors' acceleration and brake, the bucket-brigade filters after a sample-rate change, click-free switching between every type and voice, a stress matrix over every type, voice, sample rate, block size and layout, the Guide's starting points, saved state and CPU use, then renders screenshots of the interface.

## Not yet

The circuits are modelled, but unlike Atelier's valves and Lutherie's pedals they are not yet checked against ngspice. The phaser's JFET stages, the vibe's lamp and photocells, and the opto tremolo are the candidates for that.

Built on [JUCE](https://juce.com), which has its own licence terms (AGPLv3 or commercial).
