#!/bin/bash
# Dispatch+watch live capture (r336): reload the bridge =2, run one real
# copy tq-perf under a GDB file script (catch-load + python-computed
# absolute breakpoints): +0x3320 entry anchor (rdi/rsi) with a hardware
# write-watch armed on the count slot in-path, 0x88a15 dispatch flags,
# abort-site registers; restore default + L3. Evidence lands in
# build/traces/r336/. Desktop is NOT stopped (stop only if held).
export LC_ALL=C
set -u
REPO=/opt/ydyun-vdesktop-linux-driver
GUEST=$REPO/mt-vgpu-guest
: "${WINID:=r336}"
TR=$GUEST/build/traces/$WINID
mkdir -p "$TR"
KO=$GUEST/kernel/recovery/mt_pvr_bridge.ko
ROOTFS=$GUEST/build/legacy-umd-pvr-connect-candidate/rootfs
LOG=$TR/window.log
STAGE=none
BRIDGE_RELOADED=0

exec > >(tee -a "$LOG") 2>&1
say() { echo "[window] $*"; }
refs() { lsmod | awk '/^mt_pvr_bridge/{print $3}'; }

restore() {
	say "restore: stage=$STAGE"
	if [ "$BRIDGE_RELOADED" = 1 ]; then
		if [ "$(refs)" = 0 ] && ! fuser /dev/dri/renderD128 2>&1 | grep -q .; then
			sudo -n rmmod mt_pvr_bridge 2>/dev/null && say "=2 bridge unloaded" \
				|| say "=2 rmmod FAILED (state kept for inspection)"
			if ! lsmod | grep -q '^mt_pvr_bridge'; then
				sudo -n insmod "$KO" 2>/dev/null && say "default bridge reloaded" \
					|| say "default insmod FAILED"
			fi
		else
			say "bridge held (ref=$(refs)); NOT touching, needs user window"
		fi
		BRIDGE_RELOADED=0
	fi
	say "final refs: bridge=$(refs) nodes=$(ls /dev/dri | tr '\n' ' ')"
}
trap restore EXIT HUP TERM INT

# 1. precheck: refs + no holder (touch nothing if held)
say "refs: bridge=$(refs) probe=$(lsmod | awk '/^mt_guest_probe/{print $3}')"
fuser /dev/dri/renderD128 2>&1 | grep -q . \
	&& { say "FATAL: renderD128 held; aborting untouched"; exit 14; }
say "renderD128 free"

# 2. reload =2
STAGE=reloading
sudo -n rmmod mt_pvr_bridge || { say "FATAL: default rmmod failed"; exit 15; }
sudo -n insmod "$KO" drm_major=2 || { say "FATAL: =2 insmod failed"; exit 16; }
sleep 1
BRIDGE_RELOADED=1
NODE=$(ls /dev/dri/renderD* | head -1)
say "node=$NODE"

# 3. GDB file script (python computes ASLR base; no % formatting anywhere)
STAGE=gdbrun
cat > "$TR/rel.gdb" <<'EOF'
set confirm off
set pagination off
set height 0
set logging overwrite on
catch load libsrv_um
commands
silent
python
import gdb, re
out = gdb.execute('info proc mappings', to_string=True)
base = None
for line in out.splitlines():
    if 'libsrv_um' in line:
        base = int(line.split()[0], 16)
        break
cf = base + 0x889c0
ab = base + 0x88a15
ax = base + 0x2c8cc
gdb.execute('set logging file ' + '@TR@/r336-gdb.txt')
gdb.execute('set logging enabled on')
cf_bp = gdb.Breakpoint('*' + str(cf))
ab_bp = gdb.Breakpoint('*' + str(ab))
ax_bp = gdb.Breakpoint('*' + str(ax))
gdb.execute('commands ' + str(cf_bp.number) + '\nsilent\nprintf "ENTRY rdi=%p rsi=%p rdx=%p\\n", $rdi, $rsi, $rdx\npython\nimport gdb, struct\nf = gdb.selected_frame()\ntry:\n    r13 = int(f.read_register("rsi"))\n    inf = gdb.selected_inferior()\n    def u64(a):\n        return struct.unpack("<Q", inf.read_memory(a, 8))[0]\n    slot = u64(u64(r13 + 0x58) + 0x20) + 12\n    gdb.execute("set $cslot = " + str(slot))\n    w = gdb.Breakpoint("*(int*)$cslot", gdb.BP_WATCHPOINT)\n    gdb.execute("commands " + str(w.number) + "\\nsilent\\nprintf \\"WATCH val=%d\\\\n\\", *(int*)$cslot\\ncontinue\\nend")\n    gdb.execute("printf \\"CSLOT armed\\\\n\\"")\nexcept Exception:\n    gdb.execute("printf \\"CSLOT skipped\\\\n\\"")\nend\ncontinue\nend')
gdb.execute('commands ' + str(ab_bp.number) + '\nsilent\nprintf "DISP m0=%llx m8=%d ma0=%d\\n", *(long long*)$r14, *(int*)($r14+8), *(int*)($r14+0xa0)\ncontinue\nend')
gdb.execute('commands ' + str(ax_bp.number) + '\nsilent\nprintf "ABORTSITE\\n"\ninfo registers rbx rdx rax rdi rsi rcx\nkill\nquit\nend')
gdb.execute('printf "ARMED entry=' + hex(cf) + ' disp=' + hex(ab) + ' abort=' + hex(ax) + '\\n"')
end
continue
end
run -device 0 -n 1
EOF
sed -i "s|@TR@|$TR|" "$TR/rel.gdb"
export UMD_SHIM_PASSTHROUGH=1 LD_PRELOAD=$GUEST/build/probe/umd_bridge_shim.so
export UMD_TRACE=$TR/tqperf.jsonl
export LD_LIBRARY_PATH=$ROOTFS/usr/lib/x86_64-linux-gnu
timeout -s KILL 120 gdb -batch -x "$TR/rel.gdb" --args \
	$ROOTFS/usr/local/bin/musa_tq_performance_test -device 0 -n 1 \
	> "$TR/gdb.stdout" 2>&1
say "gdb exit=$? trace_lines=$(wc -l < "$UMD_TRACE" 2>/dev/null)"
unset UMD_SHIM_PASSTHROUGH LD_PRELOAD UMD_TRACE
export LD_LIBRARY_PATH=

# 4. restore bridge + L3 (before trap fires; disarm by clearing flag after)
STAGE=restoring
sudo -n rmmod mt_pvr_bridge || { say "=2 rmmod blocked; leaving for inspection"; exit 17; }
sudo -n insmod "$KO" || { say "FATAL: default insmod failed"; exit 18; }
BRIDGE_RELOADED=0
sleep 1
$GUEST/build/probe/pvr_node_probe /dev/dri/renderD128 > "$TR/l3-node.log" 2>&1
say "node probe exit=$?"
$GUEST/build/probe/pvr_dma_smoke /dev/dri/renderD128 > "$TR/l3-smoke.log" 2>&1
say "dma smoke exit=$?"
say "window done"
