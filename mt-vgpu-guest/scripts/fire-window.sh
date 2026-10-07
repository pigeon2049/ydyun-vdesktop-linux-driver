#!/bin/bash
export LC_ALL=C
# UMD-driven fire window (r290): stop the session desktop (releases
# renderD128), reload the bridge tri-open (=2 + tqx_ctx + fire), run one
# real DDK2 blit, restore default + L3, restart the desktop. Every step
# is bounded; restore runs on ANY exit path (trap). Evidence + full log
# land in build/traces/r290/; the desktop UI is expected back at the end.
set -u
REPO=/opt/ydyun-vdesktop-linux-driver
GUEST=$REPO/mt-vgpu-guest
: "${WINID:=r290}"
TR=$GUEST/build/traces/$WINID
mkdir -p "$TR"
KO=$GUEST/kernel/recovery/mt_pvr_bridge.ko
ROOTFS=$GUEST/build/legacy-umd-pvr-connect-candidate/rootfs
LOG=$TR/window.log
STAGE=none
UI_STOPPED=0
BRIDGE_RELOADED=0
BRIDGE_WAS_REMOVED=0
UNIT=$(systemctl --user list-units --type=service --state=running 2>/dev/null \
	| grep -oE 'app-ai\.opencode\.desktop@[^ ]+\.service' | head -1)

exec > >(tee -a "$LOG") 2>&1
say() { echo "[window] $*"; }

refs() { lsmod | awk '/^mt_pvr_bridge/{print $3}'; }

restore() {
	say "restore: stage=$STAGE"
	if [ "$BRIDGE_RELOADED" = 1 ]; then
		sudo -n rmmod mt_pvr_bridge 2>/dev/null && say "bridge unloaded" \
			|| say "bridge rmmod FAILED (state kept for inspection)"
		sudo -n insmod "$KO" 2>/dev/null && say "default bridge reloaded" \
			|| say "default insmod FAILED"
		BRIDGE_RELOADED=0
		BRIDGE_WAS_REMOVED=0
	elif [ "$BRIDGE_WAS_REMOVED" = 1 ] && ! lsmod | grep -q '^mt_pvr_bridge'; then
		sudo -n insmod "$KO" 2>/dev/null && say "default bridge restored (abort path)" \
			|| say "default insmod FAILED (abort path)"
		BRIDGE_WAS_REMOVED=0
	fi
	if lsmod | grep -q '^mt_live_tqx_fire'; then
		sudo -n rmmod mt_live_tqx_fire 2>/dev/null || true
	fi
	if [ "$UI_STOPPED" = 1 ]; then
		if [ -n "$UNIT" ]; then
			systemctl --user start "$UNIT" 2>/dev/null \
				&& say "desktop start issued" \
				|| say "desktop start FAILED"
		else
			say "no recorded unit; desktop NOT restarted"
		fi
	fi
	say "final refs: bridge=$(refs) nodes=$(ls /dev/dri | tr '\n' ' ')"
}
trap restore EXIT HUP TERM INT

# 0. binary sanity (offline-checkable, fail before touching anything)
strings "$KO" | grep -q 'scheduled chunks' \
	|| { say "FATAL: on-disk bridge lacks serialized fire"; exit 10; }
strings "$KO" | grep -q "vermagic=$(uname -r)" \
	|| { say "FATAL: vermagic mismatch"; exit 11; }
say "bridge binary OK: $(lsmod | awk '/^mt_pvr_bridge/{print $3}' | head -1) refs held, unit=$UNIT"

# 1. stop desktop, wait for ref 0 (30s budget)
[ -n "$UNIT" ] || { say "FATAL: desktop unit not found"; exit 12; }
systemctl --user stop "$UNIT" && UI_STOPPED=1 && say "desktop stopped"
for i in $(seq 1 30); do
	[ "$(refs)" = "0" ] && break
	sleep 1
done
[ "$(refs)" = "0" ] || { say "FATAL: ref still $(refs) after 30s"; exit 13; }
fuser /dev/dri/renderD128 2>&1 | grep -q . \
	&& { say "FATAL: renderD128 still held"; exit 14; }
say "renderD128 free"

# 2. reload tri-open
STAGE=reloaded
sudo -n rmmod mt_pvr_bridge || { say "FATAL: rmmod refused"; exit 15; }
BRIDGE_WAS_REMOVED=1
sudo -n insmod "$KO" drm_major=2 translate_tqx_ctx=1 translate_tqx_fire=1 \
	|| { say "FATAL: tri-open insmod failed"; exit 16; }
BRIDGE_RELOADED=1
sleep 1
NODE=$(ls /dev/dri/renderD* | head -1)
say "node=$NODE"
sudo -n sh -c 'echo "[fire-window] umd-fire-start" > /dev/kmsg'

# 3-4. real DDK2 blit(s) + fire verdicts (60s hard caps: submit3 lands
# in seconds, the rest is post-submit hang; r290 fired 1s after
# schedule). BLITS>1 re-fires in the SAME translator lifetime to prove
# the single-flight reset (fire seq=N back to back, r292).
STAGE=blit
: "${BLITS:=1}"
for ROUND in $(seq 1 "$BLITS"); do
export UMD_SHIM_PASSTHROUGH=1 LD_PRELOAD=$GUEST/build/probe/umd_bridge_shim.so
export UMD_TRACE=$TR/window-blit-$ROUND.jsonl
export LD_LIBRARY_PATH=$ROOTFS/usr/lib/x86_64-linux-gnu
DMESG_MARK=$(sudo -n dmesg 2>/dev/null | wc -l)
timeout -s KILL 60 $ROOTFS/usr/local/bin/musa_blit_test -device 0 -f -o
say "round $ROUND blit exit=$? trace_lines=$(wc -l < "$UMD_TRACE" 2>/dev/null)"
# Unset the shim env immediately: later children (the fire poll below)
# would otherwise inherit LD_PRELOAD and append their own records into
# the blit trace (r290 lesson: 741 junk lines).
unset UMD_SHIM_PASSTHROUGH LD_PRELOAD UMD_TRACE
export LD_LIBRARY_PATH=
FIRE_LINE=""
for i in $(seq 1 60); do
	FIRE_LINE=$(sudo -n dmesg 2>/dev/null | tail -n +$DMESG_MARK \
		| grep -a -E "mt_pvr_bridge: fire seq=$ROUND: fired=" | tail -1)
	[ -n "$FIRE_LINE" ] && break
	sleep 1
done
say "round $ROUND fire verdict: ${FIRE_LINE:-TIMEOUT-no-fired-line}"
done
sudo -n dmesg --ctime > "$TR/dmesg-window.txt" 2>/dev/null

# 5. restore default + L3
STAGE=restore
sudo -n rmmod mt_pvr_bridge && say "tri-open unloaded cleanly" \
	|| say "tri-open rmmod FAILED"
sudo -n insmod "$KO" && say "default bridge back" \
	|| { say "FATAL: default insmod failed"; exit 17; }
BRIDGE_RELOADED=0
NODE=$(ls /dev/dri/renderD* | head -1)
$GUEST/build/probe/pvr_node_probe "$NODE" 1 > "$TR/l3-node.log" 2>&1
say "node probe rc=$? tail: $(tail -2 "$TR/l3-node.log" | tr '\n' '|')"
$GUEST/build/probe/pvr_dma_smoke /dev/dri/renderD128 > "$TR/l3-smoke.log" 2>&1
say "dma smoke rc=$? tail: $(tail -2 "$TR/l3-smoke.log" | tr '\n' '|')"
STAGE=done
say "window complete"
