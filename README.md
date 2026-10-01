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

## The knobs

| Knob | What it does |
|---|---|
| **Wand** | Centre is off (and the effect passes audio untouched). **Left:** a clock-synced echo; the further left, the shorter the interval, in TEMPO's steps (2, 1, 3/4, 1/2, 3/8, 1/3, 1/4, 1/6, 1/8 of its delay cycle). **Right:** the same intervals, increasingly blended into diffusion reverb |
| **Random** | Probability of a random event on each eighth note. Left side: the echo jumps an octave up or down, reverses, or retriggers. Right side: octave-up echoes panned at random, close to a granular smear |
| **Feedback** | Repeats. Past about 60 % the input starts ducking the repeats: play and they step aside, stop and they swell back |
| **Mix** | Dry to wet. Centre is both at full |
| **Freeze** | Locks what is in the buffer and loops it; the Wand still moves it through the intervals. The delay must be on |
| **Clock** | Runs the effect at half, normal or double Move's tempo |
| **Thaw** | What happens on unfreeze: **Now**, the repeats come straight back; **Gap**, they drop out for one interval and fade back in (TEMPO's "Delay Buffer Unfreeze Mute") |

**The clock.** The intervals follow Move's tempo. While Move's transport runs,
the random events land on its beat; while it is stopped the effect runs free at
the set tempo. Below 40 BPM the effect holds 40 (its buffer is 10 seconds).

**Try this:**
- Turn **Random** up on the left side with a long interval: an echo that
  occasionally answers an octave down or backwards, like a drum fill.
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
