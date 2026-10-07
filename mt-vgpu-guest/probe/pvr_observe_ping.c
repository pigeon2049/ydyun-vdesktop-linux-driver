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
	close(fd);
	printf(fails ? "FAIL: %d check(s)\n" :
	       "PASS: observer dispatch live, unknown still refused\n",
	       fails);
	return !!fails;
}
