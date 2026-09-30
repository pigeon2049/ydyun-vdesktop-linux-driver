// SPDX-License-Identifier: GPL-2.0
/* Userspace 3D Workload Submission & Fence Verification
 * Tests MT vGPU 3D capabilities via /dev/dri/renderD128 or /dev/dri/card1.
 * Validates:
 * - DRM_IOCTL_MT_QUERY reports MT_DRM_CAP_3D
 * - DRM_IOCTL_MT_SUBMIT_3D submits Universal 3D workload to hardware DM2
 * - DRM syncobj and sync_file export
 * - Multi-frame userspace 3D execution with latency reporting
 */
#include "mt-drm-check-common.h"

_Static_assert(sizeof(struct drm_mt_submit_3d) == 32, "submit_3d ABI");

static void check_3d_execution(int fd, int frames)
{
	struct drm_mt_query q;
	query(fd, &q);
	printf("[*] DRM Query: capabilities=0x%x (COPY=%d, FILL=%d, 3D=%d)\n",
		q.capabilities,
		!!(q.capabilities & MT_DRM_CAP_COPY),
		!!(q.capabilities & MT_DRM_CAP_FILL),
		!!(q.capabilities & MT_DRM_CAP_3D));
	REQUIRE(q.capabilities & MT_DRM_CAP_3D);
	/* Address-space headroom is reported, and the mapping count must fit the
	 * derived ceiling. Earlier revisions capped this at 24 by driver constant. */
	REQUIRE(q.vm3d_max_mappings > 24);
	REQUIRE(q.vm3d_mappings <= q.vm3d_max_mappings);
	printf("[*] GPU VA mappings: 2D=%"PRIu64"  3D=%"PRIu64"/%"PRIu64" (page-budget derived)\n",
		(uint64_t)q.vm2d_mappings, (uint64_t)q.vm3d_mappings,
		(uint64_t)q.vm3d_max_mappings);

	printf("[*] Submitting %d 3D frames via DRM_IOCTL_MT_SUBMIT_3D...\n", frames);

	for (int f = 0; f < frames; f++) {
		unsigned int sync = 0;
		struct drm_syncobj_create sc = {0};
		REQUIRE(ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sc) == 0);
		sync = sc.handle;

		struct drm_mt_submit_3d sub = {
			.out_syncobj = sync,
			.flags = 0,
			.frame_tag = (uint64_t)(f + 1),
			.sequence = 0,
			.latency_us = 0,
			.target_handle = 0,
		};

		int ret = ioctl(fd, DRM_IOCTL_MT_SUBMIT_3D, &sub);
		if (ret != 0) {
			fprintf(stderr, "[!] DRM_IOCTL_MT_SUBMIT_3D failed on frame %d: ret=%d errno=%d\n", f + 1, ret, errno);
			exit(1);
		}

		/* Verify native fence and sync_file */
		verify_fence(fd, sync);

		struct drm_syncobj_destroy sd = {.handle = sync};
		REQUIRE(ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &sd) == 0);

		printf("    Frame %2d: seq=%"PRIu64" latency=%u us [OK]\n",
			f + 1, (uint64_t)sub.sequence, sub.latency_us);
	}

	query(fd, &q);
	printf("[*] Completed: submitted=%"PRIu64" completed=%"PRIu64" last_sequence=%"PRIu64"\n",
		(uint64_t)q.submitted, (uint64_t)q.completed, (uint64_t)q.last_sequence);
	REQUIRE(q.completed >= (uint64_t)frames);
}

static void check_3d_render_target(int fd)
{
	printf("[*] Testing 3D Render Target binding & VRAM readback...\n");
	unsigned int target = create(fd);
	printf("    Created target GEM handle: %u (64 KiB)\n", target);

	/* Initialize target buffer with clear pattern 0x5a */
	memset(source, 0x5a, 65536);
	buffer_io(fd, target, source, 1);

	/* Verify initial content */
	buffer_io(fd, target, observed, 0);
	REQUIRE(!memcmp(source, observed, 65536));
	printf("    Verified initial target VRAM contents (0x5a filled)\n");

	/* Create DRM syncobj */
	unsigned int sync = 0;
	struct drm_syncobj_create sc = {0};
	REQUIRE(ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sc) == 0);
	sync = sc.handle;

	/* Submit 3D workload bound to this Render Target */
	struct drm_mt_submit_3d sub = {
		.out_syncobj = sync,
		.target_handle = target,
		.flags = 0,
		.frame_tag = 0x3d7001,
		.sequence = 0,
		.latency_us = 0,
	};

	int ret = ioctl(fd, DRM_IOCTL_MT_SUBMIT_3D, &sub);
	if (ret != 0) {
		fprintf(stderr, "[!] DRM_IOCTL_MT_SUBMIT_3D with render target failed: ret=%d errno=%d\n", ret, errno);
		exit(1);
	}

	/* Verify native fence and sync_file */
	verify_fence(fd, sync);
	printf("    Render Target 3D Frame executed: seq=%"PRIu64" latency=%u us [OK]\n",
		(uint64_t)sub.sequence, sub.latency_us);

	/* Read back Render Target from VRAM */
	buffer_io(fd, target, observed, 0);
	printf("    Successfully read back Render Target VRAM (64 KiB) after GPU execution [OK]\n");

	struct drm_syncobj_destroy sd = {.handle = sync};
	REQUIRE(ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &sd) == 0);
	close_handle(fd, target);
	printf("    Released Render Target GEM handle: %u [OK]\n", target);
}

int main(int argc, char **argv)
{
	int frames = 10;
	if (argc > 1) {
		frames = atoi(argv[1]);
		if (frames <= 0) frames = 10;
	}

	const char *node = "/dev/dri/renderD128";
	int fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		node = "/dev/dri/card1";
		fd = open(node, O_RDWR | O_CLOEXEC);
	}
	if (fd < 0) {
		fprintf(stderr, "[!] Failed to open DRM node /dev/dri/renderD128 or /dev/dri/card1: errno=%d\n", errno);
		return 1;
	}

	printf("=== MT vGPU Userspace DRM 3D Execution Test ===\n");
	printf("[*] Opened DRM device node: %s (fd=%d)\n", node, fd);

	check_3d_execution(fd, frames);
	check_3d_render_target(fd);

	close(fd);
	printf("=== Test Passed Successfully ===\n");
	return 0;
}
