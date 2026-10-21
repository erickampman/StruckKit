#!/usr/bin/env bash
#
# Apply the local patches that StruckKit carries against its vendored
# dependency submodules. Idempotent: patches already applied are skipped.
#
# Patch file naming convention:
#   patches/<target>--<slug>.patch
# where <target> is either a top-level submodule ("dpf") or a path under
# external/ ("clap-wrapper"). The patch is applied with `git apply -p1` from
# inside that submodule, so it must use standard a/ b/ prefixes relative to the
# submodule root.
#
set -euo pipefail
cd "$(dirname "$0")/.."

repo_root=$PWD
patch_dir="$repo_root/patches"
status=0

if [ ! -d "$patch_dir" ]; then
    echo "apply_dep_patches: no patches/ directory, nothing to do"
    exit 0
fi

shopt -s nullglob
for patch in "$patch_dir"/*.patch; do
    base=$(basename "$patch" .patch)
    target=${base%%--*}

    # Resolve target submodule directory.
    if [ -d "$repo_root/$target/.git" ] || [ -f "$repo_root/$target/.git" ]; then
        sub="$repo_root/$target"
    elif [ -d "$repo_root/external/$target/.git" ] || [ -f "$repo_root/external/$target/.git" ]; then
        sub="$repo_root/external/$target"
    else
        echo "apply_dep_patches: SKIP $(basename "$patch") — submodule '$target' not initialised"
        status=1
        continue
    fi

    if git -C "$sub" apply --reverse --check "$patch" >/dev/null 2>&1; then
        if [ "${STRUCKKIT_PATCH_VERBOSE:-0}" = "1" ]; then
            echo "apply_dep_patches: already applied  $(basename "$patch")"
        fi
        continue
    fi

    if git -C "$sub" apply --check "$patch" >/dev/null 2>&1; then
        git -C "$sub" apply "$patch"
        echo "apply_dep_patches: applied         $(basename "$patch")  ->  ${sub#$repo_root/}"
        continue
    fi

    echo "apply_dep_patches: FAILED          $(basename "$patch") — does not apply cleanly to ${sub#$repo_root/}" >&2
    status=1
done

exit $status
