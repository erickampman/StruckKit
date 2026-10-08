# Testing StruckKit

The project ships a suite of in-process self-tests (via CTest) plus the CLAP conformance
validator. Everything runs headless on Linux/macOS — no DAW or audio device required.

## Prerequisites

- CMake ≥ 3.17 and a C++20 compiler (gcc or clang).
- Submodules (uw-core's UMP layer; DPF + pugl for the plugin build):

  ```sh
  git submodule update --init --recursive
  ```

- To build the plugin itself (DPF's OpenGL/NanoVG UI), the X11 + GL dev packages:

  ```sh
  sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
  ```

## Build and run all tests

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Add `-DSTRUCKKIT_WERROR=ON` at configure time to treat warnings as errors for the framework
and the `pm-selftest` glue (this is what CI keys on). The tests that pull in vendored
PolygraphModular headers stay on that code's own warning style.

## What each test covers

The CTest cases exercise the `pm_framework` engine end-to-end (the DPF plugin wrapper
itself is covered by `clap-validator`, below):

| CTest name       | Executable               | Verifies |
|------------------|--------------------------|----------|
| `pm`             | `pm-selftest`            | UMP MIDI layer: predicates, raw-word decode, microtonal pitch attribute, CLAP→UMP decoders |
| `pm-module`      | `pm-module-test`         | `Module` + `ModuleMap` core: param read/write, process proc, map add/get |
| `pm-sinosc`      | `pm-sinosc-test`         | The vendored sine oscillator produces an ~A440 sine |
| `pm-adsr`        | `pm-adsr-test`           | ADSR envelope: note-on drives attack, note-off drives release to ~0 |
| `pm-modules`     | `pm-modules-test`        | Combine / Final / Slew compile+run; PitchConverter maps note→pitch |
| `pm-factory`     | `pm-factory-test`        | ModuleFactory + the trimmed init-specs build all six v1 modules |
| `pm-voice`       | `pm-voice-test`          | One full voice renders a note end-to-end and releases |
| `pm-voicemanager`| `pm-voicemanager-test`   | Polyphony: a 3-note chord allocates 3 voices and frees them on release |
| `pm-snare`       | `pm-snare-test`          | The snare's physics: Bessel accuracy and the mode table's zeros; a centre strike drives only m = 0 modes; the two-head coupling solves its eigenproblem; wires are silent on a still head and below lift-off, rattle harder with harder hits, and stop sooner when tight |
| `pm-modalbank`   | `pm-modalbank-test`      | The `ModalBank` core: impulse amplitude, pitch and T60; retuning while ringing keeps amplitude and phase; position weighting can be turned off |
| `pm-drumkit`     | `pm-drumkit-test`        | The kit patch: all 24 keys of the StruckKit map's two octaves sound, stay finite and free their voice; the map repeats every 24 notes; a closed hat chokes an open hat but not a crash; a muted triangle chokes an open one; the GM map plays its notes (kit and percussion), leaves unmodelled ones silent and keeps the hat choke |

`pm-drumkit-render [out-dir]` is a measuring bench, not a CTest: for each drum it prints decay
times, partials, pitch bend, wire duration, band energies and cymbal bloom over time, and the
percussion octave's levels, decays and spectra, and writes listening WAVs when given a directory.

## Running a single test

```sh
ctest --test-dir build -R pm-voice --output-on-failure   # by name (regex)
./build/pm-voice-test                                     # or run the binary directly
```

Each test binary prints diagnostic values (e.g. `Voice: peak=0.87 tail=0.0000`) and ends with
`ALL PASS`; CTest keys success on that string.

## CLAP conformance

Validate the built plugin against the CLAP spec with
[`clap-validator`](https://github.com/free-audio/clap-validator):

```sh
clap-validator validate build/bin/StruckKit.clap
```

## Continuous integration

`.github/workflows/ci.yml` runs the configure + build + `ctest` on every push across
Linux (gcc/clang) and macOS (clang) with `-DSTRUCKKIT_WERROR=ON`.
