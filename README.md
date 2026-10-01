# Munchi Delay

**CHOMPI TEMPO's Magic Wand effect, as a Schwung audio effect.** On the CHOMPI
running TEMPO, the Magic Wand knob drives a **dual delay**: turned left it is an
echo locked to the clock; turned right the same echo melts into a diffusion
reverb. It throws in random octave, reverse and pan events, can freeze its
buffer into a loop, and once the feedback is high new material ducks the repeats
out of its way. This is that effect, on the firmware's own code, locked to
Move's tempo.

It is a [Schwung](https://github.com/charlesvestal/schwung) audio FX: put it in
any slot's FX chain, a bus, or Master FX. Its siblings are the Munchi ports of
the CHOMPI firmwares: [Tape](https://github.com/charlesvestal/schwung-munchi-tape),
[Wave](https://github.com/charlesvestal/schwung-munchi-wave) and
[Tempo](https://github.com/charlesvestal/schwung-munchi-tempo). It is a port of
code CHOMPI Club released as open source
([CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI), MIT), not an
official CHOMPI Club release; see [Credits](#credits).

## What it is

A **clock-synced glitch delay**. Chase Bliss's guide calls it TEMPO's "Dual
Delay" and the firmware file is `granularDelay.h`, but it is not a cloud of tiny
grains:

- **The base** is one stereo delay line whose interval is a clock division of
  Move's tempo. Left and right read about 20 ms and 10 ms apart, for width.
  With Random at 0 it is a plain tempo-synced echo (plus reverb on the up side).
- **The "granular" part** is two read heads that crossfade. On every ⅛-note
  clock step, Random is the chance that the delay jumps to a fresh head playing
  the buffer back differently: retriggered, reversed, an octave up (double
  speed) or an octave down (half speed). Each "grain" is a whole echo long,
  with ~20 ms crossfades: closer to a stutter delay than to Clouds.
- **Shimmer, sort of.** The repeats are fed back into the buffer, so an
  octave-up repeat is pitched up again on the next pass, and on the up side the
  reverb smears it. With Random and Feedback up, that is a clock-synced shimmer.

## The Wand

**Centre is off**, and the effect passes audio untouched. Off centre the echo
fades in at its longest interval; the further you turn, the shorter it gets.

| Wand | Interval (at Move's tempo) |
|---|---|
| just off centre | 1 bar |
| | ½ note |
| | dotted ¼ |
| | ¼ |
| | dotted ⅛ |
| | ¼ triplet |
| | ⅛ |
| | ⅛ triplet |
| fully down / up | 1/16 |

- **Down (left): a clean delay.** Random here makes the echo jump an octave up
  or down, play backwards, or retrigger: a fill.
- **Up (right): the same intervals melting into reverb.** Just past centre the
  diffusion reverb comes in; the further up, the more the echo becomes wash.
  Random here gives octave-up echoes panned at random: a shimmer or smear.

## The other knobs

| Knob | What it does |
|---|---|
| **Random** | The chance of a random event on each ⅛ note (see above) |
| **Feedback** | Repeats. Past about 60 % the input starts ducking the repeats: play and they step aside, stop and they swell back |
| **Mix** | Dry to wet. Centre is both at full |
| **Freeze** | Stops recording and loops what is in the buffer, cut to the current interval; the Wand still moves it through the intervals. Needs the Wand off centre. Down: a crisp loop. Up: a blurred one |
| **Clock** | Runs the intervals at half, normal or double Move's tempo |

**The clock.** The intervals follow Move's tempo. While Move's transport runs,
the random events land on its beat; while it is stopped the effect runs free at
the set tempo. Below 40 BPM the effect holds 40 (its buffer is 10 seconds).

**One quirk, kept from the CHOMPI:** the delay keeps recording even with the
Wand at centre, so turning it on brings back the last few seconds of what went
through rather than starting from silence.

**Try this:**
- Turn **Random** up on the down side with a long interval: an echo that
  occasionally answers an octave down or backwards, like a drum fill.
- Up side, **Random** and **Feedback** both high on a sustained sound: the
  octave-up repeats climb and smear.
- Find a loop you like, **Freeze** it, then sweep the **Wand** through the
  intervals: the frozen buffer restarts at each one.
- **Feedback** past 60 % on a pad: the repeats only come up in the gaps.

## Install

Download `munchi-delay-module.tar.gz` from the
[latest release](https://github.com/charlesvestal/schwung-munchi-delay/releases/latest/download/munchi-delay-module.tar.gz)
and install it with the Schwung web manager's custom-module upload, or paste
this repository's URL into its custom install. From a build:

```bash
./scripts/build.sh      # cross-compiles in Docker
./scripts/install.sh    # copies dist/munchi-delay to the Move
```

## How it is built

| Path | What |
|---|---|
| `src/dsp/tempo/` | TEMPO's `granularDelay.h`, `reverb.h`, `fx_engine.h`, `SimpleCompressor.h`, `SimpleCrossfade.h`. `tempo/upstream/` has the firmware's `FxEngine.h` (the wiring) and the original `granularDelay.h`; `tempo/shim/` answers what the code asked of the CHOMPI's clock |
| `src/dsp/munchi_delay.cpp` | the audio FX: FxEngine's signal path for one input, the clock from Move's tempo, parameters and state |
| `tests/delay_test.cpp` | offline: transparent at centre, echo at the clocked interval, freeze, state |

The effect runs at its native 48 kHz. Only the wet path is resampled; the dry
signal passes at 44.1 kHz untouched.

```bash
c++ -std=c++17 -O2 -Isrc/dsp -Isrc/dsp/tempo/shim -Isrc/dsp/tempo \
    tests/delay_test.cpp -o build-host/delay_test && ./build-host/delay_test out.wav
```

## Credits

- **CHOMPI TEMPO 1.0:** CHOMPI Club / Chase Bliss. MIT. The CHOMPI name, logo and
  character are CHOMPI Club's trademarks and are not licensed; this port is named
  Munchi Delay for that reason.
- **Reverb, FX engine:** Émilie Gillet, Mutable Instruments. MIT.
- **DaisySP** (dsp utilities): Electrosmith. MIT.

See [THIRD_PARTY.md](THIRD_PARTY.md). Munchi Delay itself is MIT ([LICENSE](LICENSE)).
