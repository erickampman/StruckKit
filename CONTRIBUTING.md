# Contributing to StruckKit

Bug reports and pull requests are welcome. Building and testing: [BUILDING.md](BUILDING.md) and
[TESTING.md](TESTING.md); each drum's model: [docs/how-it-works.md](docs/how-it-works.md). This page
is the rules the code depends on.

## Before each commit

```sh
ctest --test-dir build --output-on-failure
clap-validator validate build/StruckKit.clap
```

The tests are the `pm-*` engine and drum tests and `sk-presetfile`. They are silent and need no MIDI
hardware. On macOS, if you touched the AUv2 build, also run `ninja -C build install-auv2` and
`auval -v aumu StKt nLKL`. After rebuilding, quit and restart the host before listening: a running
host keeps the plugin binary it loaded first.

## Rules

- **Parameters: append, never insert.** The CLAP parameter id is the parameter's index, and hosts
  save sessions and automation by id: a new parameter goes at the end of the list, and none is
  reordered or removed. A parameter that shapes the sound gets a preset key
  (`src/UI/PresetKeys.hpp`) and a line in every `Presets/*.json`, since the loader leaves a key a
  preset doesn't mention alone.
- **Threads.** `state_load` runs on the main thread while `process()` may be running: it stores the
  loaded values, and the audio thread applies them at the start of the next block. Never rebuild the
  voice graph from the main thread.
- **Physical controls stay physical.** A control is a physical quantity (a size, a tension, a
  position). A constant set by ear rather than derived is marked as such in the source.
- **Panels before their controls.** Declare each `GroupBox` member before the controls it parents,
  or closing the editor crashes; only a real host catches this.

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
external/uw-core      uw-core (submodule): uw::ump, the UMP layer shared with StageHand and the
                        other plugins; built UMP-only (UW_CORE_UMP_ONLY)
```

Vendored PolygraphModular files keep their original `#include "X.hpp"` form; every framework
folder is on the include path so they resolve across `Common/` `Module/` `Voice/`. The
plugin links the `pm_framework` static lib.

## Third-party code and licenses

StruckKit is MIT licensed, and contributions are taken under the same license: a pull request is
offered as MIT.

- **What ships.** [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every third-party
  component in the builds, with its version, license and full license text, and every binary
  package includes it. A change that adds a component, or moves one to a new version, updates its
  entry in the same commit.
- **Upstream code stays as it came.** Submodules aren't edited in place: a fix goes upstream, to
  Unlikelyware's DPF or pugl fork, or into a patch file that `tools/fetch_deps.sh` applies.
  Copied-in files keep their copyright and license headers. The PolygraphModular engine in `src/` is Unlikelyware's own, MIT like the rest.
- **A new dependency** must allow MIT-licensed source and binary distribution with nothing more
  than keeping its notice: MIT, BSD, ISC, zlib, Apache-2.0 (with its NOTICE file, if it has one) or
  public domain, and the SIL Open Font License for fonts. No GPL or LGPL code.
- **Trademarks.** VST® is a registered trademark of Steinberg: the first "VST" in any document
  carries the ®, and a document that names VST ends with Steinberg's attribution line. Showing the
  VST Compatible Logo brings in the rest of [Steinberg's VST Usage Guidelines](https://github.com/steinbergmedia/vst3sdk/blob/master/VST3_Usage_Guidelines.pdf). Name
  hosts and other products plainly, to say what works with what, never so it reads as an
  endorsement.

VST is a registered trademark of Steinberg Media Technologies GmbH.
