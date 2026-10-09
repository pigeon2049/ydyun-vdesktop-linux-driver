// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
/* r416: T2 readback verification tool.
 *
 * Submits a real TA via the 0x82:0xFD debug ioctl (requires a bridge build
 * with MT_TA_READBACK_DEBUG=1; the default build returns -ENOTTY), reads
 * back the 64x64 RGBA8 render target, writes a PPM (P6) file, and verifies
 * pixel content (non-zero pixels => firmware actually rendered).
 *
 * Flow: open renderD128 -> INIT -> Connect (0x1:0x0) ->
 *       CreateRenderContext2 (0x82:0x12) -> DebugTAReadback (0x82:0xFD) ->
 *       write PPM -> verify.
 *
 * Usage: mt-ta-readback /dev/dri/renderD128 <output.ppm>
 * The output file must not exist (O_EXCL).
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

#define REQUIRE(x) do { \
		if (!(x)) { \
			fprintf(stderr, "check failed line %d: %s errno=%d\n", \
				__LINE__, #x, errno); \
			exit(1); \
		} \
	} while (0)

#define DRM_IOCTL_PVR_INIT	_IOW('d', 0x45, uint32_t)  /* 0x40046445 */
#define DRM_IOCTL_PVR_BRIDGE	_IOWR('d', 0x40, struct pvr_cmd)  /* 0xc0206440 */

#define T2_WIDTH  64U
#define T2_HEIGHT 64U
#define T2_BYTES  (T2_WIDTH * T2_HEIGHT * 4U)

struct pvr_cmd {
	uint32_t bridge_id;
	uint32_t function_id;
	uint64_t in_ptr;
	uint64_t out_ptr;
	uint32_t in_size;
	uint32_t out_size;
} __attribute__((packed));
_Static_assert(sizeof(struct pvr_cmd) == 32, "pvr_cmd");

/* 0x1:0x0 Connect: 16-byte IN, 17-byte OUT (mt_pvr_wire.h). */
struct connect_in {
	uint32_t client_build_options;
	uint32_t client_ddk_build;
	uint32_t client_ddk_version;
	uint32_t flags;
} __attribute__((packed));
struct connect_out {
	uint64_t packed_bvnc;
	uint32_t error;
	uint32_t capability_flags;
	uint8_t kernel_arch;
} __attribute__((packed));

/* 0x82:0x12 CreateRenderContext2: 12-byte IN, 12-byte OUT. */
struct create_in {
	uint64_t priv_data;
	uint32_t priority;
} __attribute__((packed));
struct create_out {
	uint64_t handle;
	uint32_t error;
} __attribute__((packed));

/* 0x82:0xFD DebugTAReadback (r416, MT_TA_READBACK_DEBUG gated). */
struct readback_in {
	uint64_t h_render_context;
	uint32_t width;
	uint32_t height;
	uint32_t n_entries;
} __attribute__((packed));
struct readback_out {
	uint32_t status;
	uint32_t completion_code;
	uint8_t pixels[T2_BYTES];
} __attribute__((packed));

static int bridge_call(int fd, uint32_t bridge, uint32_t func,
		       const void *in, uint32_t in_size,
		       void *out, uint32_t out_size)
{
	struct pvr_cmd cmd = {
		.bridge_id = bridge,
		.function_id = func,
		.in_ptr = (uint64_t)(uintptr_t)in,
		.out_ptr = (uint64_t)(uintptr_t)out,
		.in_size = in_size,
		.out_size = out_size,
	};
	return ioctl(fd, DRM_IOCTL_PVR_BRIDGE, &cmd);
}

int main(int argc, char **argv)
{
	int fd, out_fd;
	uint32_t init_mod = 2;
	struct connect_in cin = { 0 };
	struct connect_out cout;
	struct create_in crin = { 0 };
	struct create_out crout;
	struct readback_in rbin;
	struct readback_out *rbout;
	FILE *f;
	unsigned int i, nonzero = 0;
	uint32_t distinct[16];
	unsigned int n_distinct = 0;

	REQUIRE(argc == 3);
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	REQUIRE(fd >= 0);

	/* INIT(2) — matches r414 live procedure. */
	REQUIRE(ioctl(fd, DRM_IOCTL_PVR_INIT, &init_mod) == 0);

	/* Connect (0x1:0x0). */
	memset(&cout, 0, sizeof(cout));
	REQUIRE(bridge_call(fd, 0x1, 0x0, &cin, sizeof(cin),
			    &cout, sizeof(cout)) == 0);
	REQUIRE(cout.error == 0);
	printf("[*] connected bvnc=%#" PRIx64 "\n", cout.packed_bvnc);

	/* Create render context (0x82:0x12). */
	memset(&crout, 0, sizeof(crout));
	REQUIRE(bridge_call(fd, 0x82, 0x12, &crin, sizeof(crin),
			    &crout, sizeof(crout)) == 0);
	REQUIRE(crout.error == 0 && crout.handle != 0);
	printf("[*] render context handle=%#" PRIx64 "\n", crout.handle);

	/* DebugTAReadback (0x82:0xFD). Requires MT_TA_READBACK_DEBUG=1 build. */
	rbin.h_render_context = crout.handle;
	rbin.width = T2_WIDTH;
	rbin.height = T2_HEIGHT;
	rbin.n_entries = 1;
	rbout = calloc(1, sizeof(*rbout));
	REQUIRE(rbout != NULL);
	errno = 0;
	if (bridge_call(fd, 0x82, 0xFD, &rbin, sizeof(rbin),
			rbout, sizeof(*rbout)) != 0) {
		if (errno == ENOTTY) {
			fprintf(stderr,
				"0x82:0xFD not implemented: rebuild bridge "
				"with MT_TA_READBACK_DEBUG=1 (r416)\n");
			free(rbout);
			close(fd);
			return 2;
		}
		REQUIRE(0);
	}
	printf("[*] readback status=%u completion_code=%#x\n",
	       rbout->status, rbout->completion_code);
	REQUIRE(rbout->status == 0);

	/* Write PPM (P6). Output must not exist. */
	out_fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	REQUIRE(out_fd >= 0);
	f = fdopen(out_fd, "wb");
	REQUIRE(f != NULL && fprintf(f, "P6\n%u %u\n255\n",
				     T2_WIDTH, T2_HEIGHT) > 0);
	for (i = 0; i < T2_WIDTH * T2_HEIGHT; i++) {
		uint8_t rgb[3] = {
			rbout->pixels[i * 4 + 0],
			rbout->pixels[i * 4 + 1],
			rbout->pixels[i * 4 + 2],
		};
		uint32_t px;
		unsigned int j;
		int seen = 0;
		REQUIRE(fwrite(rgb, 1, 3, f) == 3);
		px = ((uint32_t)rgb[0] << 16) |
		     ((uint32_t)rgb[1] << 8) | rgb[2];
		if (px)
			nonzero++;
		for (j = 0; j < n_distinct; j++)
			if (distinct[j] == px) {
				seen = 1;
				break;
			}
		if (!seen && n_distinct < 16)
			distinct[n_distinct++] = px;
	}
	REQUIRE(fclose(f) == 0);
	printf("[*] wrote %s: nonzero_pixels=%u distinct_colors=%u\n",
	       argv[2], nonzero, n_distinct);
	if (nonzero == 0)
		printf("[!] all pixels zero: firmware completed but "
		       "drew nothing (expected for dummy TA entries)\n");
	free(rbout);
	close(fd);
	return nonzero ? 0 : 1;
}
