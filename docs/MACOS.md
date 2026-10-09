# Building StruckKit on macOS

Checked 2026-10-08 on Apple Silicon (macOS 26, Xcode 26.3), branch `uw-core-switch`: the
native and universal builds both build, all 13 ctest suites pass, and REAPER loads the plugin.

## Prerequisites

- The Xcode Command Line Tools (`xcode-select --install`) or Xcode: clang, the macOS SDK and
  Python 3.
- CMake 3.17 or later (`brew install cmake`).
- Read access to the private `erickampman/uw-core` repo on GitHub. It's a submodule, and
  without access the clone fails.

An Xcode installed under a name with a space (for example `/Applications/Xcode 26.3.app`) is
fine. CMake detects it and picks a `make` whose path has no space.

## Clone

```sh
git clone --recurse-submodules https://github.com/erickampman/StruckKit.git
```

In an existing clone, after a pull that moves a submodule (dpf or `external/uw-core`):

```sh
git submodule update --init --recursive
```

## Build (native architecture)

```sh
cd ~/Dev-Audio/StruckKit
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

This produces `build/bin/StruckKit.clap`. Two steps run after every build:
- the bundle is ad-hoc signed with `codesign`;
- `~/Library/Audio/Plug-Ins/CLAP/StruckKit.clap` becomes a symlink to it (not `~/.clap`, the
  Linux location). Rescan plugins in the host to pick up the new build.

The installed symlink points at whichever build ran most recently. If you build in a
second folder (a fresh clone, a universal build), build in your main folder again afterwards
to point the host back at it.

## Universal build (Apple Silicon + Intel)

Use its own build folder:

```sh
cmake -B build-universal -S . -DCMAKE_BUILD_TYPE=Release -DSTRUCKKIT_MACOS_UNIVERSAL=ON
cmake --build build-universal -j
```

Check it:

```sh
lipo -archs build-universal/bin/StruckKit.clap/Contents/MacOS/StruckKit
```

```sh
codesign --verify --verbose build-universal/bin/StruckKit.clap
```

`lipo` should print `x86_64 arm64`. Use a separate folder because turning
`STRUCKKIT_MACOS_UNIVERSAL` back to `OFF` in the same folder still builds universal: the
option writes `CMAKE_OSX_ARCHITECTURES` into the cache and turning it off doesn't remove it. To
go back in place, also pass `-UCMAKE_OSX_ARCHITECTURES`, or delete the folder.

## Options

- Minimum macOS: 15.0 by default. `-DCMAKE_OSX_DEPLOYMENT_TARGET=...` changes it.
- `-DSTRUCKKIT_WERROR=ON`: warnings as errors in the framework and tests (CI uses it).
- `-DSTRUCKKIT_PRESET_DUMP=ON`: the dev-only DUMP button, which writes the current patch as JSON
  to `$HOME`.

`nproc` isn't on macOS. Plain `-j` is enough; for an explicit count, use
`-j"$(sysctl -n hw.ncpu)"`.

## Clean

```sh
cmake --build build -j --clean-first
```

If CMake's cache is stale (for example after a submodule is added or removed), delete `build/`
and configure again.
