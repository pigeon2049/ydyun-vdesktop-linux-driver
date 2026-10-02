# Top-level entry point for the mt-vgpu-guest Stage-B bridge work.
#
# Timeout policy (bA38 lesson): the harness timeout is 300s, never tight.
# A SIGKILL landing inside a bridge ioctl leaks file->lock with the dying
# task (kernel mutexes have no owner-death release), wedging that file, its
# FDs and the module refcount until reboot. A slow run must time out on its
# own terms, not by being killed mid-ioctl.
#
# Layered gates:
#   L1  check-offline  pure userspace: python suite + bridge core RAM test.
#                      Never touches hardware, modules, PCI or /dev/dri.
#   L2  check          L1 + kernel W=1 builds + shared-ABI gate
#                      (scripts/verify-runtime-integration.py). Still no
#                      module loads and no PCI access -- that script states
#                      this in its own docstring and this Makefile holds it
#                      to the promise.
#   L3  probe          loads mt_pvr_bridge.ko, runs pvr_node_probe, unloads.
#                      Needs root (sudo) and a render node. rmmod is enforced
#                      by trap so a failing probe cannot wedge the module.
#   L4  umd            L3 setup plus the real-vendor-UMD ladder, rung by rung.
#                      Needs the 5.2.0 UMD (restored from the in-tree archive
#                      when /tmp was cleared) and sudo. Stops at kick-submit
#                      shaped traffic (S4-1); real GPU execution (S4-2) is NOT
#                      part of any target here.
#
# NOTE: L2 currently depends on build/recovery-channel/loaded-6f259*.ko,
# which lives under gitignored build/. On a fresh clone without that file
# L2 fails; see PROGRESS-SNAPSHOT.md section 7 for the pending decision.

ROOT := $(CURDIR)
BUILD := $(ROOT)/build
UMD_LIB := /tmp/mtt-linux-umd-5.2.0/root/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0
UMD_ARCHIVE := $(ROOT)/build/legacy-umd-pvr-connect-candidate/rootfs/usr/lib/x86_64-linux-gnu/libsrv_um_MUSA.so.1.0.0

.PHONY: help check check-offline kernel probe umd clean

help:
	@echo 'make check          L1+L2, default gate; never loads modules or touches PCI'
	@echo 'make check-offline  L1 only: python suite + bridge core RAM test'
	@echo 'make kernel         build all kernel modules (W=1, no loading)'
	@echo 'make probe          L3: load bridge module, run node probe, unload (needs sudo)'
	@echo 'make umd            L4: L3 setup + real-UMD ladder rung by rung (needs sudo)'
	@echo 'make clean          remove in-tree kernel build products'

check-offline:
	python3 -m unittest discover -s tests
	cc -std=gnu11 -Wall -Wextra -Werror -O2 -I$(BUILD)/runtime-test \
		tests/pvr_bridge_core_test.c -o $(BUILD)/pvr_bridge_core_test
	$(BUILD)/pvr_bridge_core_test

check: check-offline
	python3 scripts/verify-runtime-integration.py

kernel:
	$(MAKE) -C kernel W=1 -j2
	$(MAKE) -C kernel/recovery W=1 -j2
	$(MAKE) -C kernel/selftest W=1 -j2
	$(MAKE) -C probe
	$(MAKE) -C userspace

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C kernel/recovery clean
	$(MAKE) -C kernel/selftest clean
	$(MAKE) -C probe clean
	rm -f $(BUILD)/pvr_bridge_core_test

# L3/L4 share: build everything, load the bridge, guarantee unload on exit.
define WITH_BRIDGE
set -e; \
trap 'sudo -n rmmod mt_pvr_bridge 2>/dev/null || true' EXIT; \
sudo -n rmmod mt_pvr_bridge 2>/dev/null || true; \
sudo -n insmod kernel/recovery/mt_pvr_bridge.ko; \
sleep 1; \
NODE=$$(ls /dev/dri/renderD* | head -1); \
echo "node=$$NODE"
endef

probe: kernel
	@$(WITH_BRIDGE); \
	./build/probe/pvr_node_probe $$NODE 1

umd: kernel
	@$(WITH_BRIDGE); \
	if [ ! -f "$(UMD_LIB)" ]; then \
		mkdir -p $$(dirname "$(UMD_LIB)"); \
		cp -p "$(UMD_ARCHIVE)" "$(UMD_LIB)"; \
	fi; \
	mkdir -p /tmp/opencode/umda; \
	IDX=$$(basename $$NODE | sed 's/renderD//'); \
	export UMD_SHIM_PASSTHROUGH=1 LD_PRELOAD=$(BUILD)/probe/umd_bridge_shim.so; \
	PREFIX="connect 0 buf 7 64 call PVRSRVConnectionCreateDevice b7 u$$IDX u0"; \
	MEMCTX="$$PREFIX buf 5 16 call RGXCreateDeviceMemContext b7* b5 b5+8"; \
	RENDER="$$MEMCTX buf 6 256 u64 6 16 'b5*+0' u32 6 48 u1 u32 6 52 u1 buf 9 8 call RGXCreateRenderContext b7* b6 b9"; \
	SYNC="$$RENDER buf 14 8 u64 14 0 '*b7*+176' buf 10 64 buf 11 32 call CreateSyncPrim 'b14*' b10 b11"; \
	run() { name="$$1"; shift; echo "=== $$name ==="; \
		UMD_TRACE=/tmp/opencode/umda/l4-$$name.jsonl \
		timeout 300 ./build/probe/umd_connect_harness "$(UMD_LIB)" $$@; \
		echo "-> exit=$$?"; }; \
	go() { eval "run $$1 $$2"; }; \
	go rung1-connect "connect 0"; \
	go rung2-device "$$PREFIX"; \
	go rung3-devmemctx "$$MEMCTX"; \
	go rung4-render "$$RENDER"; \
	go rung5-syncprim "$$SYNC"; \
	go rung6-kicksync "$$SYNC buf 12 8 call RGXCreateKickSyncContextCCB b7* b5 u0 u0 u0 u0 b12 call RGXDestroyKickSyncContext b7* b12*"; \
	go rung7-compute "$$MEMCTX buf 15 64 u64 15 32 u1000 u64 15 16 'b5*+0' u32 15 24 u0 u32 15 40 u0 u64 15 48 'b5*+0' buf 16 8 call RGXCreateComputeContext b7* b15 b16 call RGXDestroyComputeContext b7* b16*"; \
	go rung8-kicksubmit "$$SYNC buf 12 8 call RGXCreateKickSyncContextCCB b7* b5 u0 u0 u0 u0 b12 buf 26 224 call RGXKickSync b7* b12* b26 u0 u0 u0";
