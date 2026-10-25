# StruckKit

A cross-platform **CLAP modal-synthesis drum kit** from Unlikelyware, built on the
PolygraphModular voice engine. Forked from [StruckSynth](https://github.com/erickampman/StruckSynth):
it carries the same engine, `ModalBank` resonator core and DPF/NanoVG UI kit, and plays a whole
kit from one instance.

## Status

**A physically modelled kit.** The voice is **DrumKit → Final**; the routing and hi-hat choke come
from PolygraphModularUnified's `DrumKit`, the drums are physical models voiced for an authentic
acoustic sound rather than to match PMU.

**Key map** (Master panel): **StruckKit** — two octaves repeating every 24 notes: the kit on 36–47,
60–71, …, the percussion on 48–59, 72–83, … — or **General MIDI** note numbers.

| StruckKit key | Kit octave | Percussion octave | GM notes (kit / percussion) |
|---|---|---|---|
| C | Kick | Tumba (low conga) | 36 / 64 |
| C# | Crash 1 | Cowbell | 49, 52 / 56 |
| D | Kick 2 | Conga | 35 / 63 |
| D# | Crash 2 | Claves | 57, 55 / 75 |
| E | Snare | Conga, muted | 38, 40 / 62 |
| F | Rimshot | Bongo, hembra (low) | 37 / 61 |
| F# | Hi-hat open | Wood block | 46 / 76 |
| G | Floor tom (16") | Bongo, macho (high) | 41, 43 / 60 |
| G# | Hi-hat half | Triangle, muted | — / 80 |
| A | Rack tom (12") | Timbale, low | 45, 47 / 66 |
| A# | Hi-hat closed | Triangle, open | 42, 44 / 81 |
| B | Rack tom (10") | Timbale, high | 48, 50 / 65 |
| — | Ride / ride bell, agogo high / low, low wood block (GM only) | | 51, 59 / 53, 67 / 68, 77 |

Unlisted GM percussion (clap, tambourine, maracas, cabasa, guiro, whistles, cuica, vibraslap) is
silent. The hi-hats share a choke group across voices (a `ChokeGroupState` shared through
`ModuleSharedState`), so a closed hat cuts a ringing open one; the two triangles share another, so a
muted triangle cuts an open one. Polyphony is 10, 16 (default), 20 or 32 voices (the Voices dropdown under Map), oldest stolen; every drum rings out past note-off.

| Drum | Model |
|---|---|
| Snare | Ideal-membrane modes (Bessel zeros) driven by their shapes at the strike radius; batter and snare-side heads coupled through the enclosed air; monopole radiation; the wires as eight pre-loaded strands in contact with the bottom head (`MembraneModes.hpp`, `SnareWires.hpp`). |
| Toms | The same two-headed drum without wires, at real sizes; the resonant head's tuning (Reso) and its lighter damping set sustain; tension-modulation pitch bend on hard strokes. |
| Crashes, ride | A free-edge bronze plate: ~1000 modes, low ones exact (`tools/gen_plate_modes.py`), the rest semiclassical; dome stiffening, strike-radius weighting, 1/f material loss, size-dependent radiation; a nonlinear, energy-conserving cascade whose strength follows the rim's swing against the thickness, so hard strokes on thin cymbals bloom into a wash (`Cymbal.hpp`, `ResonatorBank.hpp`). |
| Hi-hats | The same plate with the bottom cymbal as a contact at the rim: open, chattering half-open, clamped closed. |
| Kicks | PMU's additive kick, still to be modelled. |
| Congas, bongos, timbales | Single-headed drums open at the bottom: the membrane's own modes (no air-coupled pair), the (0,1) mode losing its energy down the shell so the (1,1) carries the pitch; hand contact times for congas and bongos, a steep rawhide-like damping tilt, and a muted conga as the open one with the hand left on the head; timbales are stick-played with a ringing metal shell. |
| Cowbell, agogo, claves, wood blocks, triangle | Struck idiophones on the modal bank: bell, bar and block mode series (some approximate, marked in the source); the triangle is a bent steel rod's free-free modes split into close in-/out-of-plane pairs. |

Toms and hand drums also render the head's modes above the mode table (beyond ~7.5 f0, too dense
for the bank) as band-split noise whose level follows from the membrane's mode density.

Each control is a physical quantity: Crash 1 / 2 Size (diameter), Thin, Decay, Stroke (shoulder on
the edge ↔ tip on the bow); Snare Tune, Ring, Position, Tension (wires), Wires; Tom Tune, Ring,
Position, Reso; Hat Size, Thin, Sizzle; Conga / Bongo / Timbale Tune, Hand Ring; Perc Tune (all the
small percussion, ±5 semitones), Perc Decay. Constants that are set by ear rather than derived are marked
as such in the source.

`pm-drumkit-render [out-dir]` is the measuring bench: decay, partials, pitch bend, band energies and
cymbal bloom over time for each drum, and listening WAVs when given a directory.

MIDI notes and parameter changes are sample-accurate: `process()` splits rendering at each
event's frame offset. MIDI 1.0 and MIDI 2.0 (UMP) note input are both accepted.

The CLAP plugin is hand-written (`src/plugin.cpp`), as in StruckSynth and PlainOldSynth. The
[DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) is used only for the GUI
(UIExporter + NanoVG); `src/UI/StruckKitUI.cpp` implements the CLAP `gui` extension around it.
DPF's own generated CLAP wrapper mis-sized the editor (aspect-ratio lock forced on the host's
resizes, a plugin-initiated resize on open, and a stale Retina scale on macOS), which is why it
is no longer used.

## Installing

The download holds `StruckKit.clap`. Put it in your CLAP folder (create the folder if it isn't there),
then rescan plugins in your host:

| | Linux | macOS |
|---|---|---|
| For you | `~/.clap` | `~/Library/Audio/Plug-Ins/CLAP` |
| For every user | `/usr/lib/clap` | `/Library/Audio/Plug-Ins/CLAP` |

On macOS, `~/Library` is hidden in Finder: Go ▸ Go to Folder opens it. The 0.9 previews aren't
notarized, so macOS won't load a downloaded plugin until its quarantine flag is cleared:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/StruckKit.clap
```

A host that is already running keeps the plugins it loaded: quit and restart it. To build StruckKit
yourself instead, see below.

## Building from source

```bash
git clone https://github.com/erickampman/StruckKit.git
cd StruckKit
tools/fetch_deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

[BUILDING.md](BUILDING.md) has what to install first on Linux and macOS, the tests, installing
the plugin for hosts, and the build options.

## License

MIT.
