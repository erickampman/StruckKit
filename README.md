# StruckKit

A cross-platform **CLAP modal-synthesis drum kit** from Unlikelyware, built on the
PolygraphModular voice engine. Forked from [StruckSynth](https://github.com/erickampman/StruckSynth):
it carries the same engine, `ModalBank` resonator core and DPF/NanoVG UI kit, and plays a whole
kit from one instance.

## Status

**A physically modelled kit.** The voice is **DrumKit → Final**; the routing and hi-hat choke come
from PolygraphModularUnified's `DrumKit`, the drums are physical models voiced for an authentic
acoustic sound rather than to match PMU.

**Key map** (Master panel): **PMU** — the note's pitch class picks the drum, repeating every octave —
or **General MIDI** note numbers.

| PMU key | Drum | GM notes |
|---|---|---|
| C / D | Kick / Kick 2 | 36 / 35 |
| E / F | Snare / rimshot | 38, 40 / 37 |
| G / A / B | Toms: 16" floor / 12" / 10" | 41, 43 / 45, 47 / 48, 50 |
| C# / D# | Crash 1 / Crash 2 | 49, 52 / 57, 55 |
| F# / G# / A# | Hi-hat open / half / closed | 46 / — / 42, 44 |
| — | Ride / ride bell (GM only) | 51, 59 / 53 |

Unlisted GM percussion (clap, cowbell, Latin) is silent. The hi-hats share a choke group across
voices (a `ChokeGroupState` shared through `ModuleSharedState`), so a closed hat cuts a ringing open
one. Fixed 16-voice polyphony, oldest stolen; every drum rings out past note-off.

| Drum | Model |
|---|---|
| Snare | Ideal-membrane modes (Bessel zeros) driven by their shapes at the strike radius; batter and snare-side heads coupled through the enclosed air; monopole radiation; the wires as eight pre-loaded strands in contact with the bottom head (`MembraneModes.hpp`, `SnareWires.hpp`). |
| Toms | The same two-headed drum without wires, at real sizes; the resonant head's tuning (Reso) and its lighter damping set sustain; tension-modulation pitch bend on hard strokes. |
| Crashes, ride | A free-edge bronze plate: ~1000 modes, low ones exact (`tools/gen_plate_modes.py`), the rest semiclassical; dome stiffening, strike-radius weighting, 1/f material loss, size-dependent radiation; a nonlinear, energy-conserving cascade whose strength follows the rim's swing against the thickness, so hard strokes on thin cymbals bloom into a wash (`Cymbal.hpp`, `ResonatorBank.hpp`). |
| Hi-hats | The same plate with the bottom cymbal as a contact at the rim: open, chattering half-open, clamped closed. |
| Kicks | PMU's additive kick, still to be modelled. |

Each control is a physical quantity: Crash 1 / 2 Size (diameter), Thin, Decay, Stroke (shoulder on
the edge ↔ tip on the bow); Snare Tune, Ring, Position, Tension (wires), Wires; Tom Tune, Ring,
Position, Reso; Hat Size, Thin, Sizzle. Constants that are set by ear rather than derived are marked
as such in the source.

`pm-drumkit-render [out-dir]` is the measuring bench: decay, partials, pitch bend, band energies and
cymbal bloom over time for each drum, and listening WAVs when given a directory.

MIDI notes are sample-accurate: `run()` splits rendering at each event's frame offset.
Parameter changes are applied per host block (DPF applies them before `run()`).

The wrapper is built with the [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF),
which generates the CLAP around our DSP + UI subclasses.

## Layout

The engine (`src/`) mirrors PolygraphModular's structure; the DPF plugin assembly lives in
`plugin/` + `src/UI/`:

```
plugin/               the DPF plugin definition (the CLAP wrapper is generated from these)
                        DistrhoPluginInfo.h (plugin flags), StruckKitParams.h (shared param
                        table), StruckKitPlugin.cpp (DSP: MIDI + params → VoiceManager)
src/UI/               the plugin GUI (reusable NanoVG widgets + the panel)
                        Knob, Slider, Toggle, Dropdown, PopupMenu, TabBar, GroupBox, TitleText,
                        Logo; PresetKeys / PresetDump (preset menu + dev-only JSON dumper);
                        StruckKitUI.cpp — the kit map + one knob panel per drum family
src/Common/           shim + MIDI + params + utilities
                        pm_compat.h (Apple→portable), pm_midi.* (spec UMP + MIDI-1 decode),
                        clap_ump.h (CLAP↔UMP), pm_params.h / pm_param_data.* / pm_addresses.h,
                        ParamAddressMacros.h, CPPTypes.hpp, CPPProcs.hpp, KPBuffPairHelper.hpp
src/Module/           Module core + modules
                        DrumKitModule.* (the kit) + ChokeGroupState / ModuleSharedState
                        (cross-voice state); the physics: MembraneModes, SnareWires, PlateModes
                        (+ generated PlateModesTable), Cymbal, ResonatorBank, ModalBank; Final.
                        StrikeModule / ModalBankModule and the IFS / Sine / ADSR / LPF / CVMixer /
                        Level / Combine modules inherited via StruckSynth stay instantiated but
                        unconnected
src/Voice/            voice engine
                        Voice.*, VoiceStateManager.*, VoiceManager.*, ModuleMap.*, ModuleGraph.hpp,
                        ModuleInitSpecs.hpp, pm_module_specs.cpp, ConnectionCommandQueue.hpp
Presets/              factory presets (JSON), compiled into GeneratedPresets.h by
                        tools/gen_presets.py
Resources/            font + logo, embedded into the binary at build time
test/                 in-process self-tests (see TESTING.md)
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — generates the .clap
external/clap         CLAP headers (submodule, pinned 1.2.9) — used by the framework self-tests
```

Vendored PolygraphModular files keep their original `#include "X.hpp"` form; every framework
folder is on the include path so they resolve across `Common/` `Module/` `Voice/`. The DSP
wrapper links the `pm_framework` static lib; DPF owns the plugin/UI lifecycle.

## Build

DPF's OpenGL/NanoVG UI needs the usual X11 + GL dev packages (Linux):

```sh
sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
```

Then:

```sh
git clone --recurse-submodules https://github.com/erickampman/StruckKit.git
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure     # see TESTING.md for the full suite
```

Produces `build/bin/StruckKit.clap`. A post-build step symlinks it into `~/.clap` (Linux) or
`~/Library/Audio/Plug-Ins/CLAP/` (macOS), so any CLAP host picks up the freshest build after a
rescan. Validate with [`clap-validator`](https://github.com/free-audio/clap-validator). Test
details in [TESTING.md](TESTING.md).

Configure options: `-DSTRUCKKIT_WERROR=ON` (warnings as errors), `-DSTRUCKKIT_PRESET_DUMP=ON`
(dev-only DUMP button that writes the current patch as JSON to `$HOME`).

## License

MIT.
