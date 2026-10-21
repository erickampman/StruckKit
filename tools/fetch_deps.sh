#!/usr/bin/env bash
#
# Initialise every dependency submodule StruckKit needs, at the pinned
# commits recorded in .gitmodules, then apply the local dependency patches.
#
# Run this once after a fresh `git clone`.
#
# Note on vst3sdk: it declares seven nested submodules (base, cmake,
# pluginterfaces, public.sdk, doc, tutorials, vstgui4). Only the first four are
# needed to build; doc/tutorials/vstgui4 are deliberately left uninitialised so
# a checkout stays small. A bare `git submodule update --init --recursive` would
# pull all seven, so prefer this script.
#
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Top-level submodules"
git submodule update --init dpf external/clap external/uw-core external/clap-wrapper external/vst3sdk

echo "==> dpf: nested pugl (GUI backend)"
git -C dpf submodule update --init dgl/src/pugl-upstream

echo "==> vst3sdk: nested base, cmake, pluginterfaces, public.sdk"
git -C external/vst3sdk submodule update --init --recursive \
    base cmake pluginterfaces public.sdk

echo "==> Dependency patches"
tools/apply_dep_patches.sh

echo "==> Done. Configure with: cmake -B build -G Ninja -S . -DCMAKE_BUILD_TYPE=Release"
