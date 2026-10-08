#!/bin/bash
# JobSubmit-entry snapshot live capture (r338): reload the bridge =2, run one real
# copy tq-perf under a GDB file script (catch-load + python-computed
# absolute breakpoints): snapshot regs + pointer-chain at TQ_BlitInit,
# TQ_CheckFences, TQ_LookUpEOT and +0x3320 entries, correlate pointer
# values offline to name the producer stage; abort-site registers;
# restore default + L3. Evidence lands in build/traces/r338/.
# Desktop is NOT stopped (stop only if held).
export LC_ALL=C
set -u
REPO=/opt/ydyun-vdesktop-linux-driver
GUEST=$REPO/mt-vgpu-guest
: "${WINID:=r338}"
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
js = base + 0x5ff00
b0 = base + 0x656c0
b1 = base + 0x65240
b2 = base + 0x65200
cf = base + 0x889c0
ax = base + 0x2c8cc
gdb.execute('set logging file ' + '@TR@/r338-gdb.txt')
gdb.execute('set logging enabled on')
helper = 'python\nimport gdb, struct\nf = gdb.selected_frame()\ninf = gdb.selected_inferior()\ndef u64(a):\n    try:\n        return struct.unpack("<Q", inf.read_memory(a, 8))[0]\n    except Exception:\n        return None\ndef snap(tag):\n    parts = [tag]\n    for r in ("rdi", "rsi", "rdx"):\n        v = int(f.read_register(r))\n        c0 = u64(v)\n        c58 = u64(v + 0x58) if v else None\n        w20 = u64(c58 + 0x20) if c58 else None\n        cnt = None\n        try:\n            cnt = struct.unpack("<i", inf.read_memory(w20 + 12, 4))[0] if w20 else None\n        except Exception:\n            cnt = None\n        parts.append(r + "=" + hex(v) + " [0]=" + (hex(c0) if c0 is not None else "-") + " [+58]=" + (hex(c58) if c58 is not None else "-") + " [w20]=" + (hex(w20) if w20 is not None else "-") + " cnt=" + (str(cnt) if cnt is not None else "-"))\n    gdb.execute("printf \\"" + " | ".join(parts) + "\\\\n\\"")\n'
js_bp = gdb.Breakpoint('*' + str(js))
b0_bp = gdb.Breakpoint('*' + str(b0))
b1_bp = gdb.Breakpoint('*' + str(b1))
b2_bp = gdb.Breakpoint('*' + str(b2))
cf_bp = gdb.Breakpoint('*' + str(cf))
ax_bp = gdb.Breakpoint('*' + str(ax))
gdb.execute('commands ' + str(js_bp.number) + '\nsilent\n' + helper + 'snap("JOBSUBMIT")\nend\ncontinue\nend')
gdb.execute('commands ' + str(b0_bp.number) + '\nsilent\n' + helper + 'snap("BLITINIT")\nend\ncontinue\nend')
gdb.execute('commands ' + str(b1_bp.number) + '\nsilent\n' + helper + 'snap("CHECKF")\nend\ncontinue\nend')
gdb.execute('commands ' + str(b2_bp.number) + '\nsilent\n' + helper + 'snap("LOOKUPEOT")\nend\ncontinue\nend')
gdb.execute('commands ' + str(cf_bp.number) + '\nsilent\n' + helper + 'snap("REL3320")\nend\ncontinue\nend')
gdb.execute('commands ' + str(ax_bp.number) + '\nsilent\nprintf "ABORTSITE\\n"\ninfo registers rbx rdx rax rdi rsi rcx\nkill\nquit\nend')
gdb.execute('printf "ARMED jobsubmit=' + hex(js) + ' blit=' + hex(b0) + ' cf=' + hex(b1) + ' eot=' + hex(b2) + ' rel=' + hex(cf) + ' abort=' + hex(ax) + '\\n"')
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
