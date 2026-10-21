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

## Build

**Linux:** DPF's OpenGL/NanoVG UI needs the usual X11 + GL dev packages:

```sh
sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
```

**macOS:** the Xcode Command Line Tools (`xcode-select --install`; clang, the SDK's Cocoa and
OpenGL frameworks, and Python 3) plus CMake ≥ 3.21 and Ninja (`brew install cmake ninja`). Nothing else: the UI uses
the system frameworks.

Then, on either:

```sh
git clone https://github.com/erickampman/StruckKit.git && cd StruckKit
tools/fetch_deps.sh                            # submodules at their pins + patches/
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
ctest --test-dir build --output-on-failure     # see TESTING.md for the full suite
ninja -C build install-vst3 install-auv2       # optional; install-auv2 is macOS-only
```

`tools/fetch_deps.sh` rather than `--recurse-submodules`: it fetches only the four VST3 SDK
submodules the build needs, and applies the clap-wrapper patch in `patches/` (the AUv2 view never
calls `gui->show()` without it, so the window stays blank).

uw-core is a private repo: cloning needs access to `erickampman/uw-core`, and CI needs the
`SUBMODULES_TOKEN` secret (as StageHand's). A change StruckKit needs from uw-core is pushed there
first, then the pin (`external/uw-core`) is moved here; `git config push.recurseSubmodules check`
makes git refuse a pin uw-core's remote doesn't have yet.

Produces `build/bin/StruckKit.clap`. A post-build step symlinks it into `~/.clap` (Linux) or
`~/Library/Audio/Plug-Ins/CLAP/` (macOS), so any CLAP host picks up the freshest build after a
rescan. Validate with [`clap-validator`](https://github.com/free-audio/clap-validator). Test
details in [TESTING.md](TESTING.md).

The same plugin is also wrapped by [clap-wrapper](https://github.com/free-audio/clap-wrapper) as a
VST3 (`build/StruckKit.vst3` on macOS, `build/Release/StruckKit.vst3` on Linux) and, on macOS, an
AUv2 instrument (`build/StruckKit.component`, `aumu StKt nLKL`; check with
`auval -v aumu StKt nLKL`). These are not installed on every build: `install-vst3` symlinks the
VST3 into `~/Library/Audio/Plug-Ins/VST3` or `~/.vst3`, and `install-auv2` copies the component
into `~/Library/Audio/Plug-Ins/Components` (AU hosts don't reliably follow symlinks). The first
configure with AUv2 on downloads Apple's AudioUnitSDK (`-DAUDIOUNIT_SDK_ROOT=...` to use a local
copy).

Configure options: `-DSTRUCKKIT_WERROR=ON` (warnings as errors), `-DSTRUCKKIT_PRESET_DUMP=ON`
(dev-only DUMP button that writes the current patch as JSON to `$HOME`), `-DSTRUCKKIT_BUILD_VST3=OFF`
and `-DSTRUCKKIT_BUILD_AUV2=OFF` (on by default; AUv2 only exists on macOS).

macOS options: the build targets macOS 15.0 and later (`-DCMAKE_OSX_DEPLOYMENT_TARGET=...` to
change it) and, by default, only the machine's own architecture. `-DSTRUCKKIT_MACOS_UNIVERSAL=ON`
builds one universal bundle for Apple Silicon and Intel; switching it on an existing build directory
needs a fresh one (`rm -rf build`). The bundles are ad-hoc signed after linking, which is all a local
build needs; giving it to other people needs a Developer ID signature and notarization (not set up
here). An Xcode selected from a path containing a space (say `/Applications/Xcode 26.3.app`) cannot
be driven by GNU Make's recursive rules, so a Makefile configure falls back to the Command Line Tools' make and
says so; `sudo xcode-select -s /Applications/Xcode.app` removes the need for that.

## License

MIT.
