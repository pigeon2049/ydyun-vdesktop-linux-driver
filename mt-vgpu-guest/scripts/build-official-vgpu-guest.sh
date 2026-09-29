#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SOURCE="$ROOT/src/official-vgpu-2.3.0/usr/src/mtgpu-1.0.0"
FIRMWARE_SOURCE="$ROOT/src/official-vgpu-2.3.0/usr/lib/firmware/mthreads"
PATCH_FILE="$ROOT/patches/official-vgpu-2.3.0-linux-6.12-guest-only.patch"
VPU_SHARE_PATCH="$ROOT/patches/mtgpu-v2-guest-vpu-share-map.patch"
VPU_HEAP_ALIAS_SOURCE="$ROOT/patches/mtgpu_guest_vpu_heap_alias.c"
if [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" = 1 ]; then
	VPU_HEAP_ALIAS_SOURCE="$ROOT/patches/mtgpu_guest_heap_audit.c"
fi
KERNELVER=$(uname -r)
BUILD_DIR=${MTGPU_GUEST_BUILD_DIR:-"$ROOT/build/official-vgpu-2.3.0-guest-$KERNELVER"}
JOBS=${MTGPU_GUEST_JOBS:-2}

case "$(uname -m)" in
	x86_64) ARCH=x86_64 ;;
	aarch64) ARCH=arm64 ;;
	riscv64) ARCH=riscv ;;
	loongarch64) ARCH=loongarch ;;
	*) printf 'Unsupported build architecture: %s\n' "$(uname -m)" >&2; exit 2 ;;
esac

if [ ! -f "$SOURCE/Makefile" ] || [ ! -d "$FIRMWARE_SOURCE" ] || [ ! -f "$PATCH_FILE" ]; then
	printf 'Missing source package or compatibility patch.\n' >&2
	exit 2
fi
if [ ! -f "/lib/modules/$KERNELVER/build/Makefile" ]; then
	printf 'Kernel build tree is missing: /lib/modules/%s/build\n' "$KERNELVER" >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" = 1 ] &&
	[ "${MTGPU_GUEST_PATCH_FW_HEAP_BASE:-0}" = 1 ]; then
	printf 'The v1-only and v1/v2 info-page patches are mutually exclusive.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" = 1 ] &&
	[ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" != 1 ]; then
	printf 'The v1/v2 GDPA translator requires the matching v1/v2 info-page patch.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_REQUEST_INFO_V2:-0}" = 1 ] &&
	{ [ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" != 1 ] ||
	  [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" != 1 ] ||
	  [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" != 1 ]; }; then
	printf 'V2 negotiation currently requires the enlarged buffer and diagnostic heap stop.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VPU_SHARE_MAP:-0}" = 1 ] &&
	[ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" != 1 ]; then
	printf 'The v2 VPU shared-memory mapper requires the matching v1/v2 info-page patch.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" = 1 ] &&
	[ "$ARCH" != x86_64 ]; then
	printf 'The audited GDPA wrapper currently supports only the x86_64 core object.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VPU_HEAP_ALIAS:-0}" = 1 ] &&
	[ "$ARCH" != x86_64 ]; then
	printf 'The audited Guest VPU heap-alias patch currently supports only the x86_64 core object.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VPU_HEAP_ALIAS:-0}" = 1 ] &&
	[ "${MTGPU_GUEST_PATCH_VPU_HEAP_GROUP:-0}" = 1 ]; then
	printf 'The Guest VPU heap-alias and single-group experiments are mutually exclusive.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_PATCH_VZ_MMU_BAR2_FALLBACK:-0}" = 1 ]; then
	printf 'BAR2 MMU fallback withdrawn: backing and address translation are unproven.\n' >&2
	exit 2
fi
if [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" = 1 ] &&
	{ [ "${MTGPU_GUEST_PATCH_VPU_HEAP_ALIAS:-0}" != 1 ] ||
	  [ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" != 1 ]; }; then
	printf 'Heap audit requires the call wrapper and enlarged info-page allocation.\n' >&2
	exit 2
fi
if [ -e "$BUILD_DIR" ]; then
	printf 'Build directory already exists; set MTGPU_GUEST_BUILD_DIR to a new path:\n%s\n' "$BUILD_DIR" >&2
	exit 2
fi
if [ "${MTGPU_GUEST_MMU_CONTEXT_AUDIT:-0}" = 1 ] &&
	{ [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" != 1 ] ||
	  [ "${MTGPU_GUEST_REQUEST_INFO_V2:-0}" != 1 ]; }; then
	printf 'MMU context audit requires heap audit and V2 negotiation.\n' >&2
	exit 2
fi

mkdir -p "$BUILD_DIR"
cp -a "$SOURCE"/. "$BUILD_DIR"/
cp "$ROOT/patches/mtgpu_vgpu_info_compat.c" \
	"$BUILD_DIR/src/mtgpu/vgpu/mtgpu_vgpu_info_compat.c"
patch -d "$BUILD_DIR" -p1 < "$PATCH_FILE"
if [ "${MTGPU_GUEST_PATCH_VPU_SHARE_MAP:-0}" = 1 ]; then
	patch -d "$BUILD_DIR" -p1 < "$VPU_SHARE_PATCH"
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/prepare-guest-vgpu-addr-compat.py" \
		"$BUILD_DIR" "$ROOT/patches/mtgpu_vgpu_addr_compat.c"
fi
if [ "${MTGPU_GUEST_PATCH_VPU_HEAP_ALIAS:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/prepare-guest-vpu-heap-alias.py" \
		"$BUILD_DIR" "$VPU_HEAP_ALIAS_SOURCE"
fi
if [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" = 1 ]; then
	cp "$ROOT/kernel/mt_memory_layout.h" "$ROOT/kernel/mt_mmu.h" "$BUILD_DIR/src/pvr/"
	cp "$ROOT/patches/mtgpu_guest_mmu_smoke.h" "$BUILD_DIR/src/pvr/"
fi
if [ "${MTGPU_GUEST_MMU_CONTEXT_AUDIT:-0}" = 1 ]; then
	cp "$ROOT/kernel/mt_pvr_heap_layout.h" "$ROOT/patches/mtgpu_guest_mmu_context_audit.h" "$BUILD_DIR/src/pvr/"
	cp "$ROOT/patches/mtgpu_guest_mmu_mapping_audit.h" "$BUILD_DIR/src/pvr/"
	printf '\nccflags-y += -DMTGPU_GUEST_MMU_CONTEXT_AUDIT\n' >> "$BUILD_DIR/Makefile"
fi
(cd "$BUILD_DIR" && make -j"$JOBS" ARCH="$ARCH" KERNELVER="$KERNELVER")
if [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/verify-guest-vgpu-addr-compat.py" "$BUILD_DIR"
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" = 1 ]; then
	cp "$BUILD_DIR/mtgpu.ko" "$BUILD_DIR/mtgpu.prepostlink.ko"
fi
if [ "${MTGPU_GUEST_PATCH_PHYSHEAP_COUNT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-physheap-count.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_FW_HEAP_BASE:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-fw-heap-base.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_INFO_COMPAT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-vgpu-info-compat.py" "$BUILD_DIR/mtgpu.ko"
	python3 "$SCRIPT_DIR/verify-vgpu-info-compat-ftrace.py" \
		"$BUILD_DIR/mtgpu.prepostlink.ko" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_VPU_HEAP_GROUP:-0}" = 1 ]; then
	if [ "$ARCH" != x86_64 ]; then
		printf 'The audited Guest VPU heap-group patch currently supports only the x86_64 core object.\n' >&2
		exit 2
	fi
	python3 "$SCRIPT_DIR/patch-guest-vpu-heap-group.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_VPU_HEAP_ALIAS:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-vpu-heap-alias-call.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_HEAP_AUDIT_ONLY:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-vz-mmu-bar2-fallback.py" "$BUILD_DIR/mtgpu.ko" --audit-only
fi
if [ "${MTGPU_GUEST_REQUEST_INFO_V2:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-info-request-v2.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_VGPU_ADDR_COMPAT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-vgpu-addr-callers.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_MMU_CONTEXT_AUDIT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/patch-guest-mmu-context-audit.py" "$BUILD_DIR/mtgpu.ko"
fi
if [ "${MTGPU_GUEST_PATCH_PHYSHEAP_COUNT:-0}" = 1 ]; then
	python3 "$SCRIPT_DIR/verify-guest-physheap-count-candidate.py" "$BUILD_DIR/mtgpu.ko"
fi
python3 "$SCRIPT_DIR/verify-guest-probe-pci-master.py" "$BUILD_DIR/mtgpu.ko"
mkdir -p "$BUILD_DIR/stage/lib/firmware/mthreads" \
	"$BUILD_DIR/stage/lib/modules/$KERNELVER/extra"
cp -a "$FIRMWARE_SOURCE"/. "$BUILD_DIR/stage/lib/firmware/mthreads/"
cp "$BUILD_DIR/mtgpu.ko" "$BUILD_DIR/stage/lib/modules/$KERNELVER/extra/mtgpu.ko"
printf '\nBuilt module: %s/mtgpu.ko\n' "$BUILD_DIR"
printf 'Staged offline install root: %s/stage\n' "$BUILD_DIR"
printf 'The script did not install or load the module.\n'
