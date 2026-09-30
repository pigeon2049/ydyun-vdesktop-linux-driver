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
			.reserved = 0,
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

	close(fd);
	printf("=== Test Passed Successfully ===\n");
	return 0;
}
