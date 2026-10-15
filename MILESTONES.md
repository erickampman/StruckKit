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
- [ ] Kick drum: physical model (currently PMU's additive kick)

## M2: Later
- [ ] Raw CLAP entry point (DPF as GUI only) for MIDI 2.0 velocity and note-expression support
- [ ] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE
