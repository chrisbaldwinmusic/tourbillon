> **This repository has moved.** Tourbillon is now developed in the Channel Audio monorepo, [chrisbaldwinmusic/channelaudio](https://github.com/chrisbaldwinmusic/channelaudio), in the `Tourbillon/` folder, with its full history. This repository is archived and kept for reference only.

# Tourbillon

A boutique modulation plugin: chorus to rotary speaker in one unit. AU, VST3 and Standalone, built with JUCE. A sibling of Atelier (the channel strip), Reverie (delay and reverb) and Lutherie (pedals and amps), built the same way.

Seven circuits, three voices each:

- **Chorus**: Classic (one bucket-brigade line per side, as in the compact pedals), Dimension (two lines from the mono sum swept in opposition, the subtle studio widener) and Ensemble (three lines per side on interlocking sines plus a fast shimmer, the string machine).
- **Flanger**: BBD, Through-Zero (a swept copy against a fixed one, cancelling completely as it passes) and Clean, with positive or negative feedback.
- **Phaser**: 4, 8 or 12 stages of the classic one-knob pedal, each solved as its circuit (47 nF, 24k in parallel with a linearised JFET, its gate swept over -2.8 to -1.5 V) and checked against ngspice, with Regen and drive.
- **Vibe**: the four phase stages of the original, each solved as its circuit (a phase splitter with unequal legs driving its own capacitor, 15 nF, 220 nF, 470 pF or 4.7 nF, and a photocell behind a 1 uF DC block) with the gains and photocell ranges measured on an original unit by Darabundit et al. (DAFx-19), checked against ngspice. One lamp lights all four photocells: a tungsten filament heated by its voltage and cooled by radiation, quick to light and slow to fade, and cadmium-sulphide cells that answer light quickly and darkness slowly.
- **Tremolo**: Bias (valve), Opto (the same lamp and photocell, shunting the signal after a 100k resistor) and Harmonic (two bands in opposite phase).
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

`Tests/Main.cpp` runs audio through the processor offline and checks bypass and the dry path, LFO rate against Hz and tempo, stereo phase, through-zero cancellation, the phaser's notch positions, vibrato depth in cents at different rates, chorus width, the rotors' acceleration and brake, the bucket-brigade filters after a sample-rate change, the phaser and vibe stages and the lamp and photocell against ngspice, click-free switching between every type and voice, a stress matrix over every type, voice, sample rate, block size and layout, the Guide's starting points, saved state and CPU use, then renders screenshots of the interface.

If ngspice is installed (at `~/Library/Caches/ngspice-44.2`, or wherever `NGSPICE` points), the phaser's and the vibe's four stages are simulated in ngspice, part for part (the phaser with ngspice's own JFET model), while their sweep moves, and compared sample for sample: stepped as finely as ngspice they match to about -100 dB, and at 192 kHz the trapezoidal rule's own error is about -55 dB (phaser) and -62 dB (vibe). The lamp and photocell are checked the same way (-136 dB). Otherwise that check is skipped.

`Tests/spice/check_bbd.py` checks the bucket-brigade chip (shared with Reverie) against ngspice with Tourbillon's chips: filters, clock timing, sample-and-hold, aliasing and the charge-transfer smear, for the chorus and the flanger (about -55 dB). It takes a few minutes, so it is run by hand: `python3 Tests/spice/check_bbd.py`.

## Not yet

- The lamp and photocell are physical models, but their numbers (filament and cell speeds) are this project's own, chosen for the speeds vibes and opto tremolos are known for; ngspice checks the solving, not the numbers.
- The phaser's JFET numbers are chosen, not measured, and the JFETs are modelled in their linear range only: driven very hard, a real one starts to distort, which Drive stands in for.

Built on [JUCE](https://juce.com), which has its own licence terms (AGPLv3 or commercial).
