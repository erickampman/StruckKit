# Building StruckKit

StruckKit builds the same way on Linux and macOS: CMake + Ninja, git submodules, C++20. The build
makes the plugin `StruckKit.clap`, the same plugin wrapped as `StruckKit.vst3` and, on macOS,
`StruckKit.component` (AUv2), and the tests.

## What you need

**macOS:** macOS 26 or later on Apple Silicon (arm64). Intel Macs aren't supported.

- The Xcode Command Line Tools: `xcode-select --install`. They bring clang, the macOS SDK, git and
  Python 3.
- [Homebrew](https://brew.sh), then `brew install cmake ninja`.
- Full Xcode isn't needed.

**Linux:** a C++20 compiler (GCC 11+ or Clang 14+), CMake 3.21+, Ninja, Python 3 and git. On Debian
or Ubuntu this installs them and every library the build needs:

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 git \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libxcb1-dev libx11-xcb-dev libxcb-util-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libfontconfig1-dev libasound2-dev libjack-jackd2-dev \
  libxfixes-dev libxi-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libdecor-0-dev libudev-dev \
  libdbus-1-dev libibus-1.0-dev
```

It's one list for every Unlikelyware project (the same one CI installs), so it covers more than
StruckKit needs. On other distros, install the same libraries under their own names.

## Get the source

```bash
git clone https://github.com/erickampman/StruckKit.git
cd StruckKit
tools/fetch_deps.sh
```

`tools/fetch_deps.sh` checks out the git submodules at their pinned commits, then applies the
clap-wrapper patch in `patches/`. Run it again after a `git pull` that moves, adds or re-points a
submodule: a plain pull leaves them where they were.

Of the VST3 SDK's nested submodules it takes only the four the build needs. The patch makes the
AUv2 editor draw: clap-wrapper's AUv2 view never calls `gui->show()` without it, so the window stays
blank.

uw-core (`external/uw-core`) is built UMP-only (`UW_CORE_UMP_ONLY`), so no JACK, ALSA, CoreAudio or
CoreMIDI is needed. While the repos are private, cloning needs access to `erickampman/uw-core`.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The same three commands on both platforms. Everything goes in `build/` (ignored by git).

The full test suite is described in [TESTING.md](TESTING.md).

This produces `build/bin/StruckKit.clap`, `build/StruckKit.vst3` (on Linux
`build/Release/StruckKit.vst3`) and, on macOS, `build/StruckKit.component` (AUv2 instrument,
`aumu StKt nLKL`). On macOS the bundles are ad-hoc signed after linking, which is all a local build
needs.

## Installing the plugin for hosts

A post-build step symlinks the CLAP into `~/.clap` (Linux) or `~/Library/Audio/Plug-Ins/CLAP`
(macOS), so a host picks up the freshest build after a rescan. The link points at whichever build
ran most recently: after building in a second folder (a fresh clone, say), build in your main folder
again to point the host back at it. The other formats are installed only when you ask:

```bash
cmake --build build --target install-vst3
cmake --build build --target install-auv2     # macOS
```

`install-vst3` symlinks the VST3 into `~/.vst3` or `~/Library/Audio/Plug-Ins/VST3`; `install-auv2`
copies the component into `~/Library/Audio/Plug-Ins/Components` (AU hosts don't reliably follow
symlinks).

## Options

- `-DSTRUCKKIT_BUILD_VST3=OFF`, `-DSTRUCKKIT_BUILD_AUV2=OFF`: skip a format (both on by default;
  AUv2 exists only on macOS). The first configure with AUv2 on downloads Apple's AudioUnitSDK;
  `-DAUDIOUNIT_SDK_ROOT=...` uses a local copy.
- `-DSTRUCKKIT_WERROR=ON`: warnings as errors.
- `-DSTRUCKKIT_PRESET_DUMP=ON`: a dev-only DUMP button that writes the current patch as JSON to
  `$HOME`.

## Checking a build

Validate the CLAP with [clap-validator](https://github.com/free-audio/clap-validator)
(`clap-validator validate build/bin/StruckKit.clap`), and the AU with `auval -v aumu StKt nLKL`.

An Xcode selected from a path with a space in it (say `/Applications/Xcode 26.3.app`) can't drive
GNU Make's recursive rules. Use Ninja, as above; with a Makefile configure the build falls back to
the Command Line Tools' make and says so.

If CMake's cache is stale (for example after a submodule is added or removed), delete `build/` and
configure again.
