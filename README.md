# StruckKit

A cross-platform **CLAP modal-synthesis drum kit** from Unlikelyware, built on the
PolygraphModular voice engine. Forked from [StruckSynth](https://github.com/erickampman/StruckSynth):
it carries the same engine, `ModalBank` resonator core and DPF/NanoVG UI kit, and plays a whole
kit from one instance.

## Status

**Step 3 of the plan, in progress: moving PolygraphModularUnified's `DrumKit` onto `ModalBank`.**
The voice is **DrumKit → Final**. Each note's pitch class picks the drum, so the kit repeats every octave:

| Key | Drum | Key | Drum |
|---|---|---|---|
| C | Kick | C# | Crash 1 |
| D | Kick 2 | D# | Crash 2 |
| E | Snare | F# | Open hi-hat |
| F | Snare 2 | G# | Half hi-hat |
| G / A / B | Low / mid / high tom | A# | Closed hi-hat |

- **Hi-hat choke**: the three hats share a choke group across voices (a `ChokeGroupState`
  shared by every voice's copy of the module, via `ModuleSharedState`), so a closed hat cuts a
  ringing open one. Everything else rings out.
- **Controls**: Master level plus per-family knobs: Crash (swell, tone, decay, bright), Snare
  (tune, body, buzz, snares), Toms (tune, decay, bright, thump), Hats (tune, balance, bright).
- **Voices**: fixed 16-voice polyphony, oldest stolen. Each drum renders its own decay and a
  voice frees once it has rung out, so hits survive note-off.

- **Body (Additive / Modal)**: a temporary A/B switch in the Master panel. Additive is PMU's
  sines; Modal rings each drum's body on a per-voice `ModalBank` excited by a strike burst whose
  contact time follows velocity (soft hits roll off the upper modes). A modal head that is
  struck again while ringing gains energy rather than restarting.

| Family | Body |
|---|---|
| Toms | **Modal**. Matches the additive voicing (see `pm-drumkit-compare`) |
| Snare, hats, crashes | Additive (next) |
| Kicks | Additive: a swept sine, which modes would not improve |

`pm-drumkit-compare [out-dir]` checks the two models stay matched and, given a directory, writes
`toms_additive.wav` / `toms_modal.wav` playing the same sequence for listening.

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
                        DrumKitModule.* + ChokeGroupState / ModuleSharedState (cross-voice
                        state), Final; StrikeModule.*, ModalBank.* / ModalBankModule.* (+
                        ModalMaterials, ModalGenerators, ModalDiscretizer) are instantiated for
                        the modal port; the IFS / Sine / ADSR / LPF / CVMixer / Level / Combine
                        modules inherited via StruckSynth stay instantiated but unconnected
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
