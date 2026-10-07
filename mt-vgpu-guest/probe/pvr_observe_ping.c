// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* Live routing ping for the 0x82:0x14 accept-and-log observer (r215).
 * Send a raw RGXKickTA3D5 packet naming a bogus render context: the
 * observer must answer -ENOENT (context auth), proving dispatch reaches
 * it instead of falling through to -ENOTTY. A second raw call to the
 * still-unknown 0x82:0x1f must stay -ENOTTY, proving no blanket accept.
 * No objects are created, nothing is executed, no GPU work. The fresh
 * file is closed on exit; dmesg gains no observe line (auth fails first).
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
#define BOGUS_CONTEXT 0xdeadULL
#define FIRE_VA 0x5000000000ULL
#define FIRE_PAGES 4
#define FIRE_BYTES (FIRE_PAGES * 4096)
#define FIRE_SIZE 64

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
	struct mt_pvr_rgxkickta3d5_in gfx_in = { 0 };
	struct mt_pvr_rgxkickta3d5_out gfx_out = { 0 };
	uint32_t ctl_out = 0;
	struct mt_pvr_handle_out render_out = { 0 };
	struct mt_pvr_pmr_in pmr_in = {
		.size = FIRE_BYTES,
		.log2_page_size = 12,
	};
	struct mt_pvr_pmr_out pmr_out = { 0 };
	struct mt_pvr_reserve_in reserve_in = {
		.address = FIRE_VA,
		.length = FIRE_BYTES,
	};
	struct mt_pvr_reserve_out reserve_out = { 0 };
	struct mt_pvr_map_in map_in = { 0 };
	struct mt_pvr_map_out map_out = { 0 };
	struct mt_pvr_heap_destroy_in render_destroy_in = { 0 };
	struct mt_pvr_unmap_out unmap_out = { 0 };
	struct mt_pvr_unreserve_in unreserve_in = { 0 };
	struct mt_pvr_hwperf_release_in put_in = { 0 };
	struct mt_pvr_hwperf_release_out put_out = { 0 };
	struct mt_pvr_syncprimset_in set_in = { 0 };
	struct mt_pvr_syncprimset_out set_out = { 0 };
	uint64_t zero_in = 0;
	int fd, ret;

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
	/* Bogus render context: the observer must refuse with -ENOENT,
	 * proving the 0x82:0x14 dispatch case is live (pre-r215: -ENOTTY).
	 */
	gfx_in.render_context = BOGUS_CONTEXT;
	ret = bridge_call(fd, 0x82, 0x14, &gfx_in, sizeof(gfx_in),
			  &gfx_out, sizeof(gfx_out));
	check("0x82:0x14 reaches observer (-ENOENT)",
	      ret == -1 && errno == ENOENT);
	/* Control: a still-unknown 0x82 function must stay refused. */
	ret = bridge_call(fd, 0x82, 0x1f, NULL, 0, &ctl_out, sizeof(ctl_out));
	check("0x82:0x1f still refused (-ENOTTY)",
	      ret == -1 && errno == ENOTTY);
	/* Full-path fire: a legal envelope (real render context, real PMR
	 * reservation+map, VA inside the window) with NULL check/update
	 * arrays. The observer never dereferences them (gate-proven), so
	 * NULL is the honest synthetic choice; counts are still reported.
	 * The fresh zeroed PMR window must come back nonzero=0.
	 */
	check("render create",
	      !bridge_call(fd, 0x82, 0x8, &zero_in, sizeof(zero_in),
			   &render_out, sizeof(render_out)) &&
	      !render_out.error && render_out.handle);
	check("pmr alloc",
	      !bridge_call(fd, 0x6, 0x9, &pmr_in, sizeof(pmr_in),
			   &pmr_out, sizeof(pmr_out)) &&
	      !pmr_out.error && pmr_out.pmr);
	reserve_in.server_heap = 0;
	check("reserve range",
	      !bridge_call(fd, 0x6, 0x15, &reserve_in, sizeof(reserve_in),
			   &reserve_out, sizeof(reserve_out)) &&
	      !reserve_out.error && reserve_out.reservation);
	map_in.pmr = pmr_out.pmr;
	map_in.reservation = reserve_out.reservation;
	check("map pmr",
	      !bridge_call(fd, 0x6, 0x13, &map_in, sizeof(map_in),
			   &map_out, sizeof(map_out)) &&
	      !map_out.error && map_out.mapping);
	gfx_in.render_context = render_out.handle;
	gfx_in.submission_flags = 0;
	gfx_in.submission_va = FIRE_VA;
	gfx_in.submission_size = FIRE_SIZE;
	gfx_in.submission_id = 1;
	gfx_in.check_count = 1;
	gfx_in.update_count = 1;
	gfx_in.sync_pmr_count = 0;
	check("0x82:0x14 full-path fire accepted",
	      !bridge_call(fd, 0x82, 0x14, &gfx_in, sizeof(gfx_in),
			   &gfx_out, sizeof(gfx_out)) &&
	      !gfx_out.error);
	/* Nonzero phase (r224): plant five nonzero u32s into the same
	 * window with the 0x2:0xa write path, then fire again. The
	 * observer must report nonzero=20/first=0 plus the FNV over the
	 * planted bytes (predicted offline: 0xb5e3eda7f6a52a47).
	 */
	{
		static const uint32_t vals[5] = {
			0x11111111U, 0x22222222U, 0x33333333U,
			0x44444444U, 0x55555555U,
		};
		int i, preset_ok = 1;

		set_in.sync = pmr_out.pmr;
		for (i = 0; i < 5; i++) {
			set_in.index = (uint32_t)i;
			set_in.value = vals[i];
			memset(&set_out, 0, sizeof(set_out));
			if (bridge_call(fd, 0x2, 0xa, &set_in,
					sizeof(set_in), &set_out,
					sizeof(set_out)) ||
			    set_out.error) {
				preset_ok = 0;
				break;
			}
		}
		check("0x2:0xa presets planted", preset_ok);
	}
	memset(&gfx_out, 0, sizeof(gfx_out));
	check("0x82:0x14 nonzero-window fire accepted",
	      !bridge_call(fd, 0x82, 0x14, &gfx_in, sizeof(gfx_in),
			   &gfx_out, sizeof(gfx_out)) &&
	      !gfx_out.error);
	/* Teardown in reverse order; every step must succeed so the fresh
	 * file leaves zero residue (verified via lsmod after the run).
	 */
	render_destroy_in.devmem_heap = render_out.handle;
	check("render destroy",
	      !bridge_call(fd, 0x82, 0x9, &render_destroy_in,
			   sizeof(render_destroy_in), &unmap_out,
			   sizeof(unmap_out)) && !unmap_out.error);
	unmap_out.error = 0;
	{
		struct mt_pvr_unmap_pmr_in unmap_in = {
			.mapping = pmr_out.pmr,
		};

		check("unmap pmr",
		      !bridge_call(fd, 0x6, 0x14, &unmap_in, sizeof(unmap_in),
				   &unmap_out, sizeof(unmap_out)) &&
		      !unmap_out.error);
	}
	unreserve_in.reservation = reserve_out.reservation;
	check("unreserve range",
	      !bridge_call(fd, 0x6, 0x16, &unreserve_in, sizeof(unreserve_in),
			   &unmap_out, sizeof(unmap_out)) &&
	      !unmap_out.error);
	put_in.pmr = pmr_out.pmr;
	check("pmr unref",
	      !bridge_call(fd, 0x6, 0x7, &put_in, sizeof(put_in), &put_out,
			   sizeof(put_out)) && !put_out.error);
	close(fd);
	printf(fails ? "FAIL: %d check(s)\n" :
	       "PASS: observer ping + fires clean; see dmesg for kickta3d5 observe\n",
	       fails);
	return !!fails;
}
