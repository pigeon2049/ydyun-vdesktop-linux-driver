#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${YDYUN_BUILD_DIR:-"$root/build"}
patch_file="$root/wayland/patches/0001-kde-wayland-kscreen.patch"
source_dir=${1:-"$build_dir/spice-vdagent-0.22.1"}

mkdir -p "$build_dir"

if [ ! -d "$source_dir" ]; then
    (cd "$build_dir" && apt-get source spice-vdagent=0.22.1-4.1)
fi

if [ ! -f "$source_dir/configure.ac" ]; then
    echo "source tree not found: $source_dir" >&2
    exit 2
fi

python3 "$root/wayland/scripts/prepare-debian.py" "$source_dir"

if ! grep -q 'KDE Plasma Wayland does not provide Mutter' \
    "$source_dir/src/vdagent/display.c"; then
    (cd "$source_dir" && patch -p1 < "$patch_file")
fi

(cd "$source_dir" && dpkg-buildpackage -us -uc -b)
printf '%s\n' "patched spice-vdagent build complete; packages are in $(dirname "$source_dir")"
