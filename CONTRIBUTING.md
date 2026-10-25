# Contributing to StruckKit

Building and testing: [BUILDING.md](BUILDING.md) and [TESTING.md](TESTING.md).

## Changing uw-core

A change StruckKit needs from uw-core is pushed there first, then the pin (`external/uw-core`) is
moved here. `git config push.recurseSubmodules check` makes git refuse a pin uw-core's remote
doesn't have yet.

## Layout

The engine (`src/`) mirrors PolygraphModular's structure; the CLAP plugin is `src/plugin.cpp`,
with its GUI shell in `src/UI/`:

```
plugin/               DistrhoPluginInfo.h (DPF's UI flags; its plugin-identity macros are inert),
                        StruckKitParams.h (the param table shared by DSP and UI)
src/plugin.cpp        the CLAP plugin: entry, params, state, MIDI → VoiceManager, voice-info
src/plugin_gui.h      the C seam between plugin.cpp and the GUI shell
src/UI/               the plugin GUI (reusable NanoVG widgets + the panel)
                        Knob, Slider, Toggle, Dropdown, PopupMenu, TabBar, GroupBox, TitleText,
                        Logo; PresetKeys / PresetDump (preset menu + dev-only JSON dumper);
                        StruckKitUI.cpp — the kit map + one knob panel per drum family, and
                        the CLAP gui extension (BackingScale_mac.mm: live Retina scale)
src/Common/           shim + MIDI + params + utilities
                        pm_compat.h (Apple→portable), pm_midi.* (decoded UMP over uw::ump +
                        MIDI-1 decode), pm_params.h / pm_param_data.* / pm_addresses.h,
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
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — the GUI layer only
external/clap         the CLAP SDK headers (submodule, 1.2.9)
external/uw-core      uw-core (private submodule): uw::ump, the UMP layer shared with StageHand,
                        Umpire and the other plugins; built UMP-only (UW_CORE_UMP_ONLY)
```

Vendored PolygraphModular files keep their original `#include "X.hpp"` form; every framework
folder is on the include path so they resolve across `Common/` `Module/` `Voice/`. The
plugin links the `pm_framework` static lib.
