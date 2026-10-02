// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* Bounded live test for the PVR bridge's opportunistic system-PMR DMA map.
 *
 * This creates one 16 KiB CPU-backed PMR spanning four vmalloc pages,
 * reserves/maps it through the bridge, and verifies that the session module
 * reference is held for the PMR lifetime.
 * It does not submit GPU work, program a page table, or touch MMIO. The module
 * reference is the observable success signal because failed/absent sessions
 * degrade MapPMR to system-memory semantics and immediately drop the ref.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../kernel/mt_pvr_wire.h"

#define PVR_BRIDGE_IOCTL 0xc0206440UL
#define PVR_INIT_IOCTL 0x40046445UL
#define SESSION_REFCOUNT "/sys/module/mt_guest_probe/refcnt"
#define TEST_VA 0x5000000000ULL
#define TEST_BYTES (4 * 4096)

struct drm_version {
	int32_t major, minor, patch;
	uint64_t name_len;
	char *name;
	uint64_t date_len;
	char *date;
	uint64_t desc_len;
	char *desc;
};

static int read_session_refcount(void)
{
	FILE *file = fopen(SESSION_REFCOUNT, "r");
	int refs;

	if (!file)
		return -errno;
	if (fscanf(file, "%d", &refs) != 1) {
		fclose(file);
		return -EIO;
	}
	fclose(file);
	return refs;
}

static int bridge_call(int fd, uint32_t bridge, uint32_t function,
		       const void *in, uint32_t in_size,
		       void *out, uint32_t out_size)
{
	struct mt_pvr_cmd cmd = {
		.bridge_id = bridge,
		.function_id = function,
		.in_ptr = (uint64_t)(uintptr_t)in,
		.out_ptr = (uint64_t)(uintptr_t)out,
		.in_size = in_size,
		.out_size = out_size,
	};

	return ioctl(fd, PVR_BRIDGE_IOCTL, &cmd);
}

static int cleanup(int fd, uint64_t pmr, uint64_t reservation,
		   int mapped, int reserved, int allocated)
{
	struct mt_pvr_unmap_pmr_in unmap_in = { .mapping = pmr };
	struct mt_pvr_unreserve_in unreserve_in = { .reservation = reservation };
	struct mt_pvr_unmap_out out = { 0 };
	struct mt_pvr_hwperf_release_in put_in = { .pmr = pmr };
	struct mt_pvr_hwperf_release_out put_out = { 0 };
	int errors = 0;

	if (mapped && bridge_call(fd, 0x6, 0x14, &unmap_in,
				  sizeof(unmap_in), &out, sizeof(out))) {
		perror("UnmapPMR");
		errors++;
	}
	if (reserved && bridge_call(fd, 0x6, 0x16, &unreserve_in,
				    sizeof(unreserve_in), &out, sizeof(out))) {
		perror("UnreserveRange");
		errors++;
	}
	if (allocated && bridge_call(fd, 0x6, 0x7, &put_in, sizeof(put_in),
				     &put_out, sizeof(put_out))) {
		perror("PmrUnrefPmr");
		errors++;
	}
	return errors;
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	struct drm_version version = { 0 };
	char name[64] = { 0 };
	uint32_t init_module = 1;
	struct mt_pvr_connect_in connect_in = {
		.client_build_options = 0x80000850,
		.client_ddk_version = 0x10000,
		.flags = 0x20,
	};
	struct mt_pvr_connect_out connect_out = { 0 };
	struct mt_pvr_pmr_in pmr_in = {
		.size = TEST_BYTES,
		.log2_page_size = 12,
	};
	struct mt_pvr_pmr_out pmr_out = { 0 };
	struct mt_pvr_reserve_in reserve_in = {
		.address = TEST_VA,
		.length = TEST_BYTES,
	};
	struct mt_pvr_reserve_out reserve_out = { 0 };
	struct mt_pvr_map_in map_in = { 0 };
	struct mt_pvr_map_out map_out = { 0 };
	int fd, refs_before, refs_mapped, refs_released, ret = 1;
	int allocated = 0, reserved = 0, mapped = 0;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [render-node]\n", argv[0]);
		return 2;
	}
	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open PVR render node");
		return 1;
	}
	version.name = name;
	version.name_len = sizeof(name) - 1;
	if (ioctl(fd, 0xc0406400UL, &version) || strcmp(name, "pvr")) {
		fprintf(stderr, "%s is not the PVR bridge node (name='%s')\n",
			node, name);
		goto out;
	}
	if (ioctl(fd, PVR_INIT_IOCTL, &init_module)) {
		perror("PVR INIT");
		goto out;
	}
	if (bridge_call(fd, 0x1, 0x0, &connect_in, sizeof(connect_in),
			&connect_out, sizeof(connect_out)) || connect_out.error) {
		perror("PVR Connect");
		goto out;
	}
	refs_before = read_session_refcount();
	if (refs_before < 0) {
		fprintf(stderr, "cannot read %s: %s\n", SESSION_REFCOUNT,
			strerror(-refs_before));
		goto out;
	}
	if (bridge_call(fd, 0x6, 0x9, &pmr_in, sizeof(pmr_in), &pmr_out,
			sizeof(pmr_out)) || pmr_out.error || !pmr_out.pmr) {
		perror("PhysMemNewRamBackedPmr");
		goto out;
	}
	allocated = 1;
	reserve_in.server_heap = 0;
	if (bridge_call(fd, 0x6, 0x15, &reserve_in, sizeof(reserve_in),
			&reserve_out, sizeof(reserve_out)) || reserve_out.error ||
	    !reserve_out.reservation) {
		perror("DevmemIntReserveRange");
		goto out;
	}
	reserved = 1;
	map_in.pmr = pmr_out.pmr;
	map_in.reservation = reserve_out.reservation;
	if (bridge_call(fd, 0x6, 0x13, &map_in, sizeof(map_in), &map_out,
			sizeof(map_out)) || map_out.error || !map_out.mapping) {
		perror("DevmemIntMapPmr");
		goto out;
	}
	mapped = 1;
	refs_mapped = read_session_refcount();
	printf("map returned mapping=%#" PRIx64 ", session refs %d -> %d\n",
	       map_out.mapping, refs_before, refs_mapped);
	if (refs_mapped != refs_before + 1) {
		fprintf(stderr, "DMA registration was not retained for this PMR\n");
		goto out;
	}
	if (cleanup(fd, pmr_out.pmr, reserve_out.reservation, mapped, reserved,
		    allocated)) {
		mapped = reserved = allocated = 0;
		goto out;
	}
	mapped = reserved = allocated = 0;
	refs_released = read_session_refcount();
	printf("PMR released, session refs now %d (expected %d)\n",
	       refs_released, refs_before);
	if (refs_released != refs_before) {
		fprintf(stderr, "session module reference leaked after PMR release\n");
		goto out;
	}
	puts("PASS: four-page PMR DMA-mapped and unmapped; no GPU work submitted");
	ret = 0;
out:
	if (allocated && cleanup(fd, pmr_out.pmr, reserve_out.reservation,
				 mapped, reserved, allocated))
		ret = 1;
	close(fd);
	return ret;
}
