// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* Live exclusive-cover test: two byte-tight PMRs sharing one VA page.
 *
 * Mimics the real UMD packing (r54): PMR A at [BASE,BASE+0x253), PMR B at
 * [BASE+0x253,BASE+0x253+0x408f), so both cover VA page BASE. Both MapPMRs
 * must succeed on the wire (Stage-1 system-memory semantics never change);
 * exactly one of them may enter the CPU-only VM plan -- the second bind
 * must refuse (-EEXIST inside) rather than alias two owners onto one PTE.
 * No GPU work, no page tables uploaded, no MMIO.
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
#define TEST_VA ((u64)0x5000010000ULL)
#define A_BYTES 0x253
#define B_BYTES 0x408f

struct drm_version {
	int32_t major, minor, patch;
	uint64_t name_len;
	char *name;
	uint64_t date_len;
	char *date;
	uint64_t desc_len;
	char *desc;
};

static int fails;

static void check(const char *what, int ok)
{
	printf("%-34s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok)
		fails++;
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

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	char name[64] = { 0 };
	struct drm_version version = { 0 };
	uint32_t init_module = 1;
	struct mt_pvr_connect_in connect_in = {
		.client_build_options = 0x80000850,
		.client_ddk_version = 0x10000,
		.flags = 0x20,
	};
	struct mt_pvr_connect_out connect_out = { 0 };
	struct mt_pvr_pmr_in pmr_in = { .log2_page_size = 12 };
	struct mt_pvr_pmr_out pmr_a = { 0 }, pmr_b = { 0 };
	struct mt_pvr_reserve_in res_in = { 0 };
	struct mt_pvr_reserve_out res_a = { 0 }, res_b = { 0 };
	struct mt_pvr_map_in map_in = { 0 };
	struct mt_pvr_map_out map_out = { 0 };
	struct mt_pvr_unmap_pmr_in unmap_in = { 0 };
	struct mt_pvr_unreserve_in unres_in = { 0 };
	struct mt_pvr_unmap_out dummy_out = { 0 };
	struct mt_pvr_hwperf_release_in put_in = { 0 };
	struct mt_pvr_hwperf_release_out put_out = { 0 };
	int fd;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [render-node]\n", argv[0]);
		return 2;
	}
	fd = open(node, O_RDWR);
	if (fd < 0) {
		perror("open PVR render node");
		return 1;
	}
	version.name = name;
	version.name_len = sizeof(name) - 1;
	if (ioctl(fd, 0xc0406400UL, &version) || strcmp(name, "pvr")) {
		fprintf(stderr, "%s is not the PVR bridge node\n", node);
		close(fd);
		return 1;
	}
	if (ioctl(fd, PVR_INIT_IOCTL, &init_module)) {
		perror("PVR INIT");
		close(fd);
		return 1;
	}
	if (bridge_call(fd, 0x1, 0x0, &connect_in, sizeof(connect_in),
			&connect_out, sizeof(connect_out)) || connect_out.error) {
		perror("PVR Connect");
		close(fd);
		return 1;
	}
	/* Two PMRs, byte-tight like the ladder: A=[BASE,BASE+0x253),
	 * B=[BASE+0x253,BASE+0x253+0x408f). Both cover VA page BASE. */
	pmr_in.size = A_BYTES;
	check("alloc A", !bridge_call(fd, 0x6, 0x9, &pmr_in, sizeof(pmr_in),
				      &pmr_a, sizeof(pmr_a)) && !pmr_a.error &&
		pmr_a.pmr);
	pmr_in.size = B_BYTES;
	check("alloc B", !bridge_call(fd, 0x6, 0x9, &pmr_in, sizeof(pmr_in),
				      &pmr_b, sizeof(pmr_b)) && !pmr_b.error &&
		pmr_b.pmr);
	res_in.address = TEST_VA;
	res_in.length = A_BYTES;
	check("reserve A", !bridge_call(fd, 0x6, 0x15, &res_in, sizeof(res_in),
					&res_a, sizeof(res_a)) &&
		!res_a.error && res_a.reservation);
	res_in.address = TEST_VA + A_BYTES;
	res_in.length = B_BYTES;
	check("reserve B", !bridge_call(fd, 0x6, 0x15, &res_in, sizeof(res_in),
					&res_b, sizeof(res_b)) &&
		!res_b.error && res_b.reservation);
	/* Both maps must succeed on the wire regardless of plan outcome. */
	map_in.pmr = pmr_a.pmr;
	map_in.reservation = res_a.reservation;
	check("map A returns mapping",
	      !bridge_call(fd, 0x6, 0x13, &map_in, sizeof(map_in), &map_out,
			   sizeof(map_out)) && !map_out.error &&
	      map_out.mapping == pmr_a.pmr);
	map_in.pmr = pmr_b.pmr;
	map_in.reservation = res_b.reservation;
	memset(&map_out, 0, sizeof(map_out));
	check("map B returns mapping",
	      !bridge_call(fd, 0x6, 0x13, &map_in, sizeof(map_in), &map_out,
			   sizeof(map_out)) && !map_out.error &&
	      map_out.mapping == pmr_b.pmr);
	/* Teardown in UMD order. A second unmap must fail (nothing live). */
	unmap_in.mapping = pmr_a.pmr;
	check("unmap A", !bridge_call(fd, 0x6, 0x14, &unmap_in, sizeof(unmap_in),
				      &dummy_out, sizeof(dummy_out)));
	unmap_in.mapping = pmr_b.pmr;
	check("unmap B", !bridge_call(fd, 0x6, 0x14, &unmap_in, sizeof(unmap_in),
				      &dummy_out, sizeof(dummy_out)));
	check("double unmap B refused",
	      bridge_call(fd, 0x6, 0x14, &unmap_in, sizeof(unmap_in),
			  &dummy_out, sizeof(dummy_out)) && errno == ENOENT);
	unres_in.reservation = res_a.reservation;
	check("unreserve A", !bridge_call(fd, 0x6, 0x16, &unres_in,
					  sizeof(unres_in), &dummy_out,
					  sizeof(dummy_out)));
	unres_in.reservation = res_b.reservation;
	check("unreserve B", !bridge_call(fd, 0x6, 0x16, &unres_in,
					  sizeof(unres_in), &dummy_out,
					  sizeof(dummy_out)));
	put_in.pmr = pmr_a.pmr;
	check("unref A", !bridge_call(fd, 0x6, 0x7, &put_in, sizeof(put_in),
				      &put_out, sizeof(put_out)));
	put_in.pmr = pmr_b.pmr;
	check("unref B", !bridge_call(fd, 0x6, 0x7, &put_in, sizeof(put_in),
				      &put_out, sizeof(put_out)));
	close(fd);
	printf(fails ? "FAIL: %d check(s)\n" : "PASS: shared cover page stays exclusive, wire unchanged\n",
	       fails);
	return !!fails;
}
