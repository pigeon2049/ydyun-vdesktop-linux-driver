// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* Live mixed-kick proof for the kick translator (r223/r227).
 * Step 0 (r227): preset the check slot with the 0x2:0xa write path.
 * Step 1 (mixed fire, r227): a raw 0x88:0x4 with one check entry for
 * the preset slot+value AND one update entry must translate promptly:
 * the check wait hits, the marker submits, and the update value is
 * written back after completion (r159 order).
 * Step 2 (check probe): a raw 0x88:0x4 with one check entry for the
 * update slot+value must translate PROMPTLY (<4s). Timing is the
 * readback: no PMR mmap needed.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../kernel/mt_pvr_wire.h"

#define PVR_BRIDGE_IOCTL 0xc0206440UL
#define PVR_INIT_IOCTL 0x40046445UL
#define PROOF_VAL 1U
#define CHECK_VAL 7U
#define UPDATE_VAL 9U
#define PROMPT_LIMIT_NS 4000000000ULL

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

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
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
	uint32_t uoff[1] = { 4 };
	uint32_t uval[1] = { UPDATE_VAL };
	uint64_t uufo[1];
	uint32_t coff[1] = { 0 };
	uint32_t cval[1] = { CHECK_VAL };
	uint64_t cufo[1];
	struct mt_pvr_syncprimset_in set_in = { 0 };
	struct mt_pvr_syncprimset_out set_out = { 0 };
	uint64_t t0, dt;
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
	if (bridge_call(fd, 0x2, 0x0, &sync_in, sizeof(sync_in),
			&sync_out, sizeof(sync_out)) || sync_out.error ||
	    !sync_out.sync_pmr) {
		perror("AllocSyncPrimitiveBlock");
		close(fd);
		return 1;
	}
	if (bridge_call(fd, 0x88, 0x0, &ks_in, sizeof(ks_in), &ks_out,
			sizeof(ks_out)) || ks_out.error ||
	    !ks_out.kicksync_context) {
		perror("KickSyncContextCreate");
		close(fd);
		return 1;
	}
	/* Step 0: preset the check slot with the 0x2:0xa write path, so
	 * the mixed fire below exercises a real check wait (r227).
	 * A fresh PMR reads zero, which would also pass and prove
	 * nothing about waiting. */
	set_in.sync = sync_out.sync_pmr;
	set_in.index = 0;
	set_in.value = CHECK_VAL;
	check("check slot preset",
	      !bridge_call(fd, 0x2, 0xa, &set_in, sizeof(set_in),
			   &set_out, sizeof(set_out)) && !set_out.error);
	/* Step 1: mixed fire (check + update in one kick). The bridge
	 * must wait for the preset check value, submit the marker,
	 * then publish UPDATE_VAL into the update slot. */
	uufo[0] = sync_out.sync_pmr;
	cufo[0] = sync_out.sync_pmr;
	kick_in.kicksync_context = ks_out.kicksync_context;
	kick_in.check_devvar_offset = (u64)(uintptr_t)coff;
	kick_in.check_value = (u64)(uintptr_t)cval;
	kick_in.check_ufo_block = (u64)(uintptr_t)cufo;
	kick_in.client_check_count = 1;
	kick_in.update_devvar_offset = (u64)(uintptr_t)uoff;
	kick_in.update_value = (u64)(uintptr_t)uval;
	kick_in.update_ufo_block = (u64)(uintptr_t)uufo;
	kick_in.client_update_count = 1;
	kick_in.update_fence_name = (u64)(uintptr_t)"pvr-mixed-fire";
	kick_in.check_fence_fd = 0xffffffffU;
	kick_in.timeline_fence_fd = 0xffffffffU;
	t0 = now_ns();
	check("mixed fire accepted",
	      !bridge_call(fd, 0x88, 0x4, &kick_in, sizeof(kick_in),
			   &kick_out, sizeof(kick_out)) &&
	      !kick_out.error && kick_out.update_fence_fd >= 0);
	dt = now_ns() - t0;
	printf("%-34s %llu ns\n", "mixed fire elapsed",
	       (unsigned long long)dt);
	check("mixed fire prompt (check wait hit)",
	      dt < PROMPT_LIMIT_NS);
	close(kick_out.update_fence_fd);
	/* Step 2: check probe for the update slot+value. Prompt
	 * translation proves the writeback landed. */
	memset(&kick_in, 0, sizeof(kick_in));
	memset(&kick_out, 0, sizeof(kick_out));
	cufo[0] = sync_out.sync_pmr;
	kick_in.kicksync_context = ks_out.kicksync_context;
	kick_in.check_devvar_offset = (u64)(uintptr_t)uoff;
	kick_in.check_value = (u64)(uintptr_t)uval;
	kick_in.check_ufo_block = (u64)(uintptr_t)cufo;
	kick_in.client_check_count = 1;
	kick_in.update_fence_name = (u64)(uintptr_t)"pvr-update-probe";
	kick_in.check_fence_fd = 0xffffffffU;
	kick_in.timeline_fence_fd = 0xffffffffU;
	t0 = now_ns();
	check("check probe for written value accepted",
	      !bridge_call(fd, 0x88, 0x4, &kick_in, sizeof(kick_in),
			   &kick_out, sizeof(kick_out)) &&
	      !kick_out.error && kick_out.update_fence_fd >= 0);
	dt = now_ns() - t0;
	printf("%-34s %llu ns\n", "check probe elapsed",
	       (unsigned long long)dt);
	check("check probe prompt (writeback landed)",
	      dt < PROMPT_LIMIT_NS);
	close(kick_out.update_fence_fd);
	ksd_in.kicksync_context = ks_out.kicksync_context;
	check("kicksync destroy",
	      !bridge_call(fd, 0x88, 0x1, &ksd_in, sizeof(ksd_in), &ksd_out,
			   sizeof(ksd_out)) && !ksd_out.error);
	close(fd);
	printf(fails ? "FAIL: %d check(s)\n" :
	       "PASS: mixed kick + writeback verified by timing\n",
	       fails);
	return !!fails;
}
