# Third-party work in Munchi Delay

Munchi Delay is a port of the delay and reverb effect from **CHOMPI TEMPO 1.0**
to Ableton Move. Everything it is built from is MIT-licensed; the notices below
travel with every copy.

| Component | Copyright | License | Where |
|---|---|---|---|
| CHOMPI TEMPO 1.0 firmware (granular delay, compressor, crossfades, effect wiring) | © CHOMPI Club | MIT | `src/dsp/tempo/` (originals of edited files in `src/dsp/tempo/upstream/`) |
| `reverb.h`, `fx_engine.h` | © Émilie Gillet (Mutable Instruments) | MIT | `src/dsp/tempo/` — original notices kept in the files |
| DaisySP (`dsp.h` utilities) | © Electrosmith, Corp. | MIT | `src/dsp/daisysp/` (its `LICENSE` beside it) |
| `plugin_api_v1.h`, `audio_fx_api_v2.h`, `resampler.h` | Schwung | MIT | `src/dsp/` |

Authorship of the firmware, per the CHOMPI repository's own `THIRD_PARTY.md`
(copied to `docs/CHOMPI-THIRD_PARTY.md`): Electrosmith engineered the original
platform; TAPE 2.0, TEMPO and WAVE were written at Chase Bliss after CHOMPI Club
became part of Chase Bliss. CHOMPI Club releases them under the MIT license.

## Trademarks

The CHOMPI name, logo, character and related marks are trademarks of CHOMPI
Club and are **not** covered by the MIT license (`docs/CHOMPI-TRADEMARKS.md`).
Munchi Delay is an independent port, not an official CHOMPI Club release; it is
named differently for that reason and uses no CHOMPI artwork.
