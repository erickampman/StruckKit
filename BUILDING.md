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

Of the VST® 3 SDK's nested submodules it takes only the four the build needs. The patch makes the
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

This produces `build/StruckKit.clap`, `build/StruckKit.vst3` (on Linux
`build/Release/StruckKit.vst3`) and, on macOS, `build/StruckKit.component` (AUv2 instrument,
`aumu StKt nLKL`). On macOS the bundles are ad-hoc signed after linking, which is all a local build
needs.

## Installing the plugin for hosts

A plain `cmake --build build` writes nothing outside `build/`. These targets put the plugin where
hosts look for it:

```bash
cmake --build build --target install-clap
cmake --build build --target install-vst3
cmake --build build --target install-auv2     # macOS
```

| | Linux | macOS |
|---|---|---|
| CLAP | `~/.clap` | `~/Library/Audio/Plug-Ins/CLAP` |
| VST3 | `~/.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| AUv2 | — | `~/Library/Audio/Plug-Ins/Components` |

`install-clap` and `install-vst3` symlink the plugin in `build/`, so a host picks up a rebuild
after a rescan. The link points into this clone: delete the clone and the host loses the plugin,
and running the target in another build folder points the host there instead. `install-auv2`
copies the component (AU hosts don't reliably follow symlinks), so run it again after a rebuild.

To keep the plugin without the clone, copy it instead (the `rm` clears a link the targets left):

```bash
# Linux
mkdir -p ~/.clap ~/.vst3
rm -rf ~/.clap/StruckKit.clap ~/.vst3/StruckKit.vst3
cp build/StruckKit.clap ~/.clap/
cp -R build/Release/StruckKit.vst3 ~/.vst3/
```

```bash
# macOS
mkdir -p ~/Library/Audio/Plug-Ins/CLAP ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/CLAP/StruckKit.clap ~/Library/Audio/Plug-Ins/VST3/StruckKit.vst3
cp -R build/StruckKit.clap ~/Library/Audio/Plug-Ins/CLAP/
cp -R build/StruckKit.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

For every user instead, the same folders without `~` (`/usr/lib/clap` and `/usr/lib/vst3` on Linux,
`/Library/Audio/Plug-Ins/...` on macOS), with `sudo`. Then rescan in the host. A host that is
already running keeps the plugin it loaded first: quit and restart it after a rebuild. A plugin you
built yourself has no quarantine flag, so macOS loads it without the steps a downloaded one needs.

## Options

Pass these to the first command (`cmake -B build ... -DNAME=VALUE`).

| Option | Default | Effect |
|---|---|---|
| `STRUCKKIT_BUILD_VST3` | `ON` | Build `StruckKit.vst3` through clap-wrapper (with the vendored `external/vst3sdk`). |
| `STRUCKKIT_BUILD_AUV2` | `ON` on macOS, `OFF` elsewhere | Build `StruckKit.component` through clap-wrapper. **macOS only**. The first configure downloads Apple's AudioUnitSDK; `-DAUDIOUNIT_SDK_ROOT=...` uses a local copy. |
| `STRUCKKIT_WERROR` | `OFF` | Warnings as errors (CI turns it on). |
| `STRUCKKIT_PRESET_DUMP` | `OFF` | A dev-only DUMP button that writes the current patch as JSON to `$HOME`. |
| `STRUCKKIT_UW_CORE_DIR` | `external/uw-core` | Build against another uw-core clone (below). |

## Checking a build

Validate the CLAP with [clap-validator](https://github.com/free-audio/clap-validator)
(`clap-validator validate build/StruckKit.clap`), and the AU with `auval -v aumu StKt nLKL`.

An Xcode selected from a path with a space in it (say `/Applications/Xcode 26.3.app`) can't drive
GNU Make's recursive rules. Use Ninja, as above; with a Makefile configure the build falls back to
the Command Line Tools' make and says so.

If CMake's cache is stale (for example after a submodule is added or removed), delete `build/` and
configure again.

## Building against a local uw-core

To try a uw-core change that isn't pushed and pinned yet, point the build at another clone:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTRUCKKIT_UW_CORE_DIR=../uw-core
```

When the change is ready, push uw-core first, then move StruckKit's submodule pin
(uw-core's README, "Pushing: uw-core first").

## Making a release package

For the maintainer only: a release package is signed with the Developer ID certificate and
published to the [downloads repo](https://github.com/erickampman/unlikelyware), so these steps
need that keychain setup and write access there. Building from source, as above, needs neither.

Once per machine, point `UW_DOWNLOADS_DIR` at a checkout of the downloads repo, in your shell's
startup file (`~/.zprofile` or `~/.zshrc` with zsh, `~/.bashrc` with bash):

```bash
export UW_DOWNLOADS_DIR=~/path/to/unlikelyware
```

Then, in this checkout, after building the release tag as above:

```bash
"$UW_DOWNLOADS_DIR/scripts/package.sh"
```

```bash
"$UW_DOWNLOADS_DIR/scripts/publish.sh" v0.9.0
```

The first makes this platform's package (on macOS, signed and notarized); the second uploads it
to the release. The details are in the downloads repo's
[README](https://github.com/erickampman/unlikelyware#making-a-release-package).

## How this differs from the other projects

Every Unlikelyware project builds the same way: `git clone`, `tools/fetch_deps.sh`, the three
commands under "Build and test", and for a plugin `cmake --build build --target install-clap`. The
same package list, the same `build/` folder, the same option names (`<PROJECT>_BUILD_VST3`,
`<PROJECT>_UW_CORE_DIR` and so on). What's different here, and why:

- One of the seven synths (PlainOldSynth, StruckSynth, StruckRoom, StruckGaussian, StruckKit, IFSynth, ModicumFM), which share the PolygraphModular framework, vendored, and DPF for the UI. `tools/fetch_deps.sh` also applies `patches/` to clap-wrapper (its AUv2 view), so `external/clap-wrapper` shows a local change: that is the patch, not uncommitted work.
- The only synth that uses uw-core (for UMP parsing), so it has `external/uw-core` and `STRUCKKIT_UW_CORE_DIR`.
- Its CMake target is `StruckKit-clap` (from its DPF days); the plugin is `build/StruckKit.clap` like the others' (until 2026-10-25 it was `build/bin/StruckKit.clap`: run `install-clap` again after updating).

VST is a registered trademark of Steinberg Media Technologies GmbH.
