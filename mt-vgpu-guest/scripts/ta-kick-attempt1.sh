#!/bin/bash
# Fabricated TA kick attempt 1 (r348): real render ctx + hand-shaped psKickTA,
# RGXKickTA under the fabricated shim (no PASSTHROUGH, no hardware), two rdi
# mappings (A: render object, B: conn). Fully scripted for reproducibility
# (answers r259). Traces land in build/traces/r348/.
export LC_ALL=C
set -u
REPO=/opt/ydyun-vdesktop-linux-driver
GUEST=$REPO/mt-vgpu-guest
: "${WINID:=r348}"
TR=$GUEST/build/traces/$WINID
mkdir -p "$TR"
WORK=$GUEST/build/r348-replay
mkdir -p "$WORK"
printf '[default]\nPerfCountStartCbID=0\nPerfCountEndCbID=0\n' > "$WORK/musa.ini"
ROOTFS=$GUEST/build/legacy-umd-pvr-connect-candidate/rootfs
LIB=$ROOTFS/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so
HARNESS=$GUEST/build/probe/umd_connect_harness
LOG=$TR/attempt1.log
say() { echo "[ta1] $*"; }

exec > >(tee -a "$LOG") 2>&1

sha=$(sha256sum "$LIB" | cut -d' ' -f1)
say "umd sha=$sha"
case "$sha" in
b3058c02bbd7c879f076c48ee1faf78e0e764beeab09da7cbd053f3ec34237b0) say "sha matches r210" ;;
*) say "SHA MISMATCH, aborting"; exit 10 ;;
esac

PREFIX="connect 0 buf 7 64 call PVRSRVConnectionCreateDevice b7 u1 u0"
MEMCTX="$PREFIX buf 5 16 call RGXCreateDeviceMemContext b7* b5 b5+8"
RENDER="$MEMCTX buf 6 256 u64 6 16 'b5*+0' u32 6 48 u1 u32 6 52 u1 buf 9 8 call RGXCreateRenderContext b7* b6 b9"
TA_SHAPE="buf 20 12288 buf 21 4096 buf 22 4096 buf 23 4096 u64 20 0x30 b21 u64 20 0x2d8 b22 u64 20 0x2e0 b22 u64 20 0x2e8 b23 buf 24 1040 buf 25 1040 buf 26 1040 buf 27 1040 buf 30 64 buf 31 64 buf 28 64 buf 29 64 buf 19 4096 u64 20 0x28 b19"

run() {
	name="$1"; shift
	export UMD_TRACE=$TR/ta-$name.jsonl
	export UMD_CCB_DUMP_DIR=$TR/ccb-$name
	export LD_LIBRARY_PATH=$ROOTFS/usr/lib/x86_64-linux-gnu
	mkdir -p "$UMD_CCB_DUMP_DIR"
	chmod 700 "$UMD_CCB_DUMP_DIR"
	# shellcheck disable=SC2086
	(cd "$WORK" && LD_PRELOAD=$GUEST/build/probe/umd_bridge_shim.so \
		timeout -s KILL 60 $HARNESS "$LIB" "$@") \
		> "$TR/out-$name.txt" 2>&1
	say "$name exit=$? trace=$(wc -l < "$UMD_TRACE" 2>/dev/null) ccb=$(ls "$UMD_CCB_DUMP_DIR" 2>/dev/null | wc -l)"
	unset UMD_TRACE UMD_CCB_DUMP_DIR LD_LIBRARY_PATH
}

eval "run mapA $RENDER $TA_SHAPE poke b9*+80 b30 poke b9*+40 b28 call RGXKickTA b9* b20 b24 b25 b26 b27"
eval "run mapB $RENDER $TA_SHAPE poke conn+80 b31 poke conn+40 b29 call RGXKickTA conn b20 b24 b25 b26 b27"
say done
