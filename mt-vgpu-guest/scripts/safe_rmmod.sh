#!/bin/bash
# mt-vgpu-guest/scripts/safe_rmmod.sh -- unload a kernel module only when
# its refcount is 0.
#
# r394: after the `rmmod -f mt_guest_probe` incident (forced unload of a
# ref=1 module whose trial state was inconsistent -> kernel hang -> user
# cold reboot), `rmmod -f`/`--force` is banned repo-wide (enforced by
# tests/test_pre_live_safety.py). Use this helper in live rounds instead:
#
#   mt-vgpu-guest/scripts/safe_rmmod.sh mt_pvr_bridge
#
# Exit codes: 0 = unloaded (or was not loaded); 3 = held by named holders;
# 4 = numeric refcount != 0. On refusal, teardown the holder first; if the
# holder is unknown, stop the round and report -- never force.
set -u

mod="${1:?usage: safe_rmmod.sh <module>}"

line="$(lsmod | awk -v m="$mod" '$1 == m {print $0}')"
if [ -z "$line" ]; then
    echo "safe_rmmod: $mod not loaded, nothing to do"
    exit 0
fi

ref="$(printf '%s\n' "$line" | awk '{print $3}')"
case "$ref" in
    ''|*[!0-9]*)
        echo "safe_rmmod: $mod is held by: $ref -- refusing (investigate holders, never -f)" >&2
        exit 3
        ;;
esac
if [ "$ref" -ne 0 ]; then
    echo "safe_rmmod: $mod refcount=$ref != 0 -- refusing (teardown holders first, never -f)" >&2
    exit 4
fi

exec sudo -n rmmod "$mod"
