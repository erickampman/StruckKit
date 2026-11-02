# StruckKit milestones

## Tests
- all: cmake --build build -j && ctest --test-dir build --output-on-failure

## M1: Kit and physics
- [x] Snare: ideal-membrane Bessel modes, two coupled heads, wires (8 pre-loaded strands)
- [x] Toms: two-headed drum without wires, tension-modulation pitch bend on hard strokes
- [x] Crashes / Ride: free-edge bronze plate (~1000 modes), nonlinear energy-conserving cascade
- [x] Hi-hats: plate model with bottom-cymbal contact (open / half-open / closed choke)
- [x] Congas, bongos, timbales: single-headed open shells, hand contact, rawhide damping
- [x] Idiophones: cowbell, agogo, claves, wood blocks, triangle (bar/bell/rod modal series)
- [x] StruckKit and General MIDI key maps; choke groups (hi-hats, triangle)
- [x] uw-core UMP layer for MIDI decode; factory presets; DPF/NanoVG UI
- [ ] Kick drum: physical model (today's is additive, from PolygraphModular; it ships in 0.9, decided 2026-11-01)

## M2: Plugin formats
- [x] VST3 / AUv2 via clap-wrapper (v0.16.0, vst3sdk 9fad977): VST3 on macOS + Linux, AUv2 `aumu StKt nLKL`
  on macOS; clap-wrapper AUv2 GUI patch carried in patches/. Validated 2026-10-21 on macOS (thin
  arm64 Release): ctest 13/13, clap-validator 34 pass / 0 fail / 10 skipped, Steinberg validator
  47 pass / 0 fail, `auval -v aumu StKt nLKL` succeeds, `codesign --verify --strict` on all three
  bundles
- [ ] By hand: Logic (AUv2), REAPER (VST3 + AUv2), Bitwig (VST3): GUI draws, pads play, automation
  and state recall (Logic done 2026-11-01)
- [ ] Linux VST3 build checked on the Linux box

## Save and Load (2026-10-27)
- [x] Save / Load in the header write the patch as the factory presets' JSON and read it back (src/UI/PresetFile.hpp),
      through DPF's file dialog; loaded values clamped to each param's range.
      Ported from StruckGaussian. Fixed on the way:
      the dumper wrote no HandDrums or Percussion section.
      sk-presetfile checks the round trip, every factory preset, and non-presets
- [x] AUv2: clap-wrapper gives no timers and (unpatched) never calls gui show(), so the UI's idle never ran and DPF would never
      collect the dialog's result. The GUI now runs its own main-run-loop timer when the host has none, started at
      set_parent; the home folder comes from the account, not the sandboxed $HOME; failures show in the Preset menu
      (StruckGaussian 3f0c97c)
- [x] By hand on Linux (2026-11-01): Save and Load (DPF's own dialog)
- [x] By hand on the Mac (2026-11-01): Save and Load in Logic Pro (AUv2) and StageHand (CLAP)

## M3: Later
- [x] Raw CLAP entry point (DPF as GUI only) for MIDI 2.0 velocity and note-expression support (`src/plugin.cpp`)
- [x] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE (2026-10-23)
