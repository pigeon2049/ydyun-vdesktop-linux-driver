// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* Live kick-inventory probe: issue a raw 0x88:0x4 with nonzero check/update
 * counts referencing real file objects, proving the bridge's inspect-only
 * path (copy arrays, resolve UFO handles, log inventory) without changing
 * the accept-and-inspect wire result. No GPU work, no page tables, no MMIO.
 *
 * Success is judged on the wire (mapping-agnostic); the "kick sync
 * inventory" dmesg line is checked separately and must show the two real
 * handles known and the bogus one unknown.
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
#define BOGUS_HANDLE 0xdeadULL

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
	struct mt_pvr_sync_block_in sync_in = {
		.mem_type = MT_PVR_SYNC_MEM_TYPE,
	};
	struct mt_pvr_sync_block_out sync_out = { 0 };
	struct mt_pvr_kicksync_create_in ks_in = { 0 };
	struct mt_pvr_kicksync_create_out ks_out = { 0 };
	struct mt_pvr_kicksync_destroy_in ksd_in = { 0 };
	struct mt_pvr_kicksync_destroy_out ksd_out = { 0 };
	struct mt_pvr_kicksync3_in kick_in = { 0 };
	struct mt_pvr_kicksync3_out kick_out = { 0 };
	uint32_t check_off[2] = { 0, 4 };
	uint32_t check_val[2] = { 1, 2 };
	uint64_t check_ufo[2];
	uint32_t update_off[1] = { 8 };
	uint32_t update_val[1] = { 3 };
	uint64_t update_ufo[1];
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
	check("sync block", !bridge_call(fd, 0x2, 0x0, &sync_in, sizeof(sync_in),
					  &sync_out, sizeof(sync_out)) &&
		!sync_out.error && sync_out.sync_pmr);
	check("kicksync create",
	      !bridge_call(fd, 0x88, 0x0, &ks_in, sizeof(ks_in), &ks_out,
			   sizeof(ks_out)) && !ks_out.error &&
	      ks_out.kicksync_context);
	/* Nonzero counts over readable memory: one PMR handle, one object
	 * handle, one bogus handle. The bridge must accept regardless. */
	check_ufo[0] = sync_out.sync_pmr;
	check_ufo[1] = BOGUS_HANDLE;
	update_ufo[0] = sync_out.sync_handle;
	kick_in.kicksync_context = ks_out.kicksync_context;
	kick_in.check_devvar_offset = (u64)(uintptr_t)check_off;
	kick_in.check_value = (u64)(uintptr_t)check_val;
	kick_in.check_ufo_block = (u64)(uintptr_t)check_ufo;
	kick_in.client_check_count = 2;
	kick_in.update_devvar_offset = (u64)(uintptr_t)update_off;
	kick_in.update_value = (u64)(uintptr_t)update_val;
	kick_in.update_ufo_block = (u64)(uintptr_t)update_ufo;
	kick_in.client_update_count = 1;
	kick_in.update_fence_name = (u64)(uintptr_t)"pvr-kick-probe";
	kick_in.check_fence_fd = 0xffffffffU;
	kick_in.timeline_fence_fd = 0xffffffffU;
	check("nonzero-count kick accepted",
	      !bridge_call(fd, 0x88, 0x4, &kick_in, sizeof(kick_in), &kick_out,
			   sizeof(kick_out)) && !kick_out.error &&
	      kick_out.update_fence_fd >= 0);
	close(kick_out.update_fence_fd);
	ksd_in.kicksync_context = ks_out.kicksync_context;
	check("kicksync destroy",
	      !bridge_call(fd, 0x88, 0x1, &ksd_in, sizeof(ksd_in), &ksd_out,
			   sizeof(ksd_out)) && !ksd_out.error);
	close(fd);
	printf(fails ? "FAIL: %d check(s)\n" :
	       "PASS: nonzero kick accepted; see dmesg for ufo_known=2/3\n",
	       fails);
	return !!fails;
}
