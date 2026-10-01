/* SPDX-License-Identifier: GPL-2.0 */
/* Direct probe for the Stage B bridge node.
 *
 * The UMD is a useful end-to-end oracle but a poor first debugger: when it
 * fails, the question is which of the 19 commands went wrong. This tool drives
 * the node directly so each step's errno is attributable.
 *
 * Usage: pvr_node_probe <node> [init_module]
 *   node          e.g. /dev/dri/renderD130
 *   init_module   1 = generic connection, 2 = device connection (default 1)
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../kernel/mt_pvr_wire.h"

struct drm_version {
	int32_t major, minor, patch;
	uint64_t name_len;
	char *name;
	uint64_t date_len;
	char *date;
	uint64_t desc_len;
	char *desc;
};

/* Spell the numbers out, exactly as the vendor UMD does.
 *
 * Do NOT derive these with _IOWR()/_IOW() from a glibc program: _IOWR('d',
 * 0x40, 32) assembles to 0xc0046440 here, because glibc's _IOC_SIZEBITS
 * disagrees with the kernel's on x86 and the size field lands in a different
 * bit position. The number that reaches the driver is what the UMD compiled
 * against, and only the literal reproduces it.
 */
#define BRIDGE_IOCTL 0xc0206440UL	/* _IOWR('d', 0x40, srvkm_cmd)   */
#define INIT_IOCTL 0x40046445UL		/* _IOW ('d', 0x45, init_data)  */

static int failures;
static int mismatches;

/* ioctl() reports failure as -1 and puts the reason in errno. Printing
 * strerror(-ret) instead shows strerror(1) for every step, which hides the
 * real error entirely.
 */
static void step(const char *what, long ret)
{
	if (ret < 0) {
		printf("%-28s ret=-1 errno=%d (%s)\n", what, errno,
		       strerror(errno));
		failures++;
	} else {
		printf("%-28s ret=%ld\n", what, ret);
	}
}

/* Some steps exist to prove the driver *refuses* something. Counting those as
 * failures made a correct -ENOTTY/-EINVAL look like a bug, which is how the
 * real bug (an EFAULT on every command) stayed hidden for a whole round.
 */
static void step_expecting(const char *what, long ret, int expected_errno)
{
	/* Capture errno before anything else can touch it: printf() and
	 * strerror() are both allowed to clobber it, which is how the first
	 * version of this helper reported errno=0 for calls that had in fact
	 * failed correctly.
	 */
	int saved_errno = ret < 0 ? errno : 0;

	if (ret != -1 || saved_errno != expected_errno) {
		printf("%-28s got ret=%ld errno=%d, expected %s\n", what, ret,
		       saved_errno, strerror(expected_errno));
		failures++;
		return;
	}
	printf("%-28s refused with %s, as required\n", what,
	       strerror(expected_errno));
}

static int bridge(int fd, uint32_t bridge_id, uint32_t function,
		  const void *in, uint32_t in_size, void *out, uint32_t out_size)
{
	struct mt_pvr_cmd cmd = {
		.bridge_id = bridge_id,
		.function_id = function,
		.in_ptr = (uint64_t)(uintptr_t)in,
		.out_ptr = (uint64_t)(uintptr_t)out,
		.in_size = in_size,
		.out_size = out_size,
	};
	return ioctl(fd, BRIDGE_IOCTL, &cmd);
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD130";
	uint32_t init_module = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1;
	struct drm_version version;
	struct mt_pvr_connect_in connect_in = {
		.client_build_options = 0x80000850,
		.client_ddk_version = 0x10000,
		.flags = 0x20,
	};
	struct mt_pvr_connect_out connect_out;
	struct mt_pvr_heap_details_in heap_in;
	struct mt_pvr_heap_details_out heap_out;
	struct mt_pvr_sync_block_in sync_in = { .mem_type = MT_PVR_SYNC_MEM_TYPE };
	struct mt_pvr_sync_block_out sync_out;
	struct mt_pvr_handle_out handle_out;
	struct mt_pvr_heap_count_out heap_count_out;
	uint8_t name_buffer[160];
	int fd;

	fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("open(%s): %s\n", node, strerror(errno));
		return 1;
	}
	memset(&version, 0, sizeof(version));
	{
		char n[64] = {0}, d[64] = {0}, s[256] = {0};

		version.name = n;
		version.name_len = sizeof(n) - 1;
		version.date = d;
		version.date_len = sizeof(d) - 1;
		version.desc = s;
		version.desc_len = sizeof(s) - 1;
		step("DRM_IOCTL_VERSION", ioctl(fd, 0xc0406400, &version));
		printf("%-28s name='%s' ver=%d.%d.%d\n", "version", n,
		       version.major, version.minor, version.patch);
		if (strncmp(n, "pvr", 3))
			printf("%-28s the driver only accepts 'pvr'\n",
			       "WARNING:");
	}

	/* The driver passes a pointer to a u32, not the value. */
	{
		uint32_t value = init_module;

		step("INIT", ioctl(fd, INIT_IOCTL, &value));
	}

	memset(&connect_out, 0, sizeof(connect_out));
	step("0x1:0x0 Connect",
	     bridge(fd, 0x1, 0x0, &connect_in, sizeof(connect_in), &connect_out,
		    sizeof(connect_out)));
	printf("%-28s bvnc=0x%llx error=%u caps=0x%x arch=%u\n", "",
	       (unsigned long long)connect_out.packed_bvnc, connect_out.error,
	       connect_out.capability_flags, connect_out.kernel_arch);

	memset(&handle_out, 0, sizeof(handle_out));
	step("0x1:0x2 AcquireGlobalEventObject",
	     bridge(fd, 0x1, 0x2, NULL, 0, &handle_out, sizeof(handle_out)));
	printf("%-28s handle=0x%llx\n", "",
	       (unsigned long long)handle_out.handle);

	memset(&handle_out, 0, sizeof(handle_out));
	step("0x1:0xf AcquireInfoPage",
	     bridge(fd, 0x1, 0xf, NULL, 0, &handle_out, sizeof(handle_out)));
	printf("%-28s info-pmr=0x%llx (offset 0x%llx)\n", "",
	       (unsigned long long)handle_out.handle,
	       (unsigned long long)handle_out.handle);

	/* heap_name_out must be armed on *every* call: the driver copies the
	 * name into it, and a NULL pointer there is correctly refused with
	 * EFAULT. That is a probe bug, not a driver bug.
	 */
	memset(&heap_in, 0, sizeof(heap_in));
	memset(&heap_out, 0, sizeof(heap_out));
	heap_in.heap_name_out = (uint64_t)(uintptr_t)name_buffer;
	/* ui32HeapConfigIndex selects a heap *configuration* and is always 0
	 * for the single config this driver publishes; ui32HeapIndex is the
	 * entry within it. The real UMD sends cfg_index=0 with heap_index
	 * counting 0..10, so the probe must do the same or it stops
	 * representing what the UMD does.
	 */
	heap_in.heap_config_index = 0;
	heap_in.heap_index = 0;
	heap_in.heap_name_buf_size = sizeof(name_buffer);
	memset(name_buffer, 0, sizeof(name_buffer));
	step("0x6:0x20 HeapCfgHeapDetails[0]",
	     bridge(fd, 0x6, 0x20, &heap_in, sizeof(heap_in), &heap_out,
		    sizeof(heap_out)));
	printf("%-28s base=0x%llx size=0x%llx log2page=%u name='%s'\n", "",
	       (unsigned long long)heap_out.base,
	       (unsigned long long)heap_out.length, heap_out.log2_data_page_size,
	       name_buffer);

	/* "USC Code" is the heap whose absence bA5 showed to block
	 * device-memory-context creation, so it is the one that has to come
	 * back. Look it up by name the way the UMD does, rather than pinning an
	 * index: the table now compacts away its two empty slots, so hardcoded
	 * indices silently drift whenever a slot is added or removed. That is
	 * exactly the class of bug this probe exists to catch.
	 *
	 * heap_name_out must be re-armed on every call: leaving it NULL makes the
	 * driver's copy_to_user() fail with EFAULT, which is correct behaviour
	 * and a probe bug, not a driver bug.
	 */
	{
		uint32_t i;
		int found = -1;

		for (i = 0; i < heap_count_out.num_heaps; i++) {
			memset(&heap_in, 0, sizeof(heap_in));
			heap_in.heap_name_out = (uint64_t)(uintptr_t)name_buffer;
			heap_in.heap_name_buf_size = sizeof(name_buffer);
			heap_in.heap_config_index = 0;
			heap_in.heap_index = i;
			memset(name_buffer, 0, sizeof(name_buffer));
			heap_out = (struct mt_pvr_heap_details_out){ 0 };
			if (bridge(fd, 0x6, 0x20, &heap_in, sizeof(heap_in),
				   &heap_out, sizeof(heap_out)))
				break;
			if (!strcmp((const char *)name_buffer, "USC Code")) {
				found = (int)i;
				break;
			}
		}
		if (found < 0) {
			printf("%-28s 'USC Code' absent from %u heaps\n",
			       "MISMATCH:", heap_count_out.num_heaps);
			mismatches++;
		} else {
			printf("0x6:0x20 found 'USC Code'  %-8s index=%u base=0x%llx "
			       "size=0x%llx log2=%u\n", "",
			       found, (unsigned long long)heap_out.base,
			       (unsigned long long)heap_out.length,
			       heap_out.log2_data_page_size);
		}
	}

	/* Every published heap must be usable: non-zero base and size. A
	 * zero-length entry cannot host an arena, which is what made the UMD
	 * fail with 82 = MTSRV_ERROR_DEVICEMEM_UNABLE_TO_CREATE_ARENA.
	 */
	{
		uint32_t i;

		for (i = 0; i < heap_count_out.num_heaps; i++) {
			memset(&heap_in, 0, sizeof(heap_in));
			heap_in.heap_name_out = (uint64_t)(uintptr_t)name_buffer;
			heap_in.heap_name_buf_size = sizeof(name_buffer);
			heap_in.heap_config_index = 0;
			heap_in.heap_index = i;
			memset(name_buffer, 0, sizeof(name_buffer));
			heap_out = (struct mt_pvr_heap_details_out){ 0 };
			if (bridge(fd, 0x6, 0x20, &heap_in, sizeof(heap_in),
				   &heap_out, sizeof(heap_out)))
				continue;
			if (!heap_out.base || !heap_out.length) {
				printf("%-28s heap[%u] name='%s' base=0x%llx "
				       "size=0x%llx\n", "MISMATCH:", i,
				       name_buffer,
				       (unsigned long long)heap_out.base,
				       (unsigned long long)heap_out.length);
				mismatches++;
			}
		}
		printf("%-28s all %u heaps have a non-zero base and size\n",
		       "", heap_count_out.num_heaps);
	}

	/* An unnamed slot must come back as an empty string, not a fault. */
	memset(&heap_in, 0, sizeof(heap_in));
	heap_in.heap_name_out = (uint64_t)(uintptr_t)name_buffer;
	heap_in.heap_name_buf_size = sizeof(name_buffer);
	heap_in.heap_config_index = 0;
	heap_in.heap_index = 1;
	memset(name_buffer, 0, sizeof(name_buffer));
	step("0x6:0x20 HeapCfgHeapDetails[1] unnamed",
	     bridge(fd, 0x6, 0x20, &heap_in, sizeof(heap_in), &heap_out,
		    sizeof(heap_out)));
	printf("%-28s name='%s' (expect empty)\n", "", name_buffer);
	if (name_buffer[0]) {
		printf("%-28s unnamed slot returned a name\n", "MISMATCH:");
		mismatches++;
	}

	/* Out-of-range index must be refused. */
	memset(&heap_in, 0, sizeof(heap_in));
	heap_in.heap_name_out = (uint64_t)(uintptr_t)name_buffer;
	heap_in.heap_name_buf_size = sizeof(name_buffer);
	heap_in.heap_config_index = 0;
	heap_in.heap_index = 0xffff;
	memset(name_buffer, 0, sizeof(name_buffer));
	step_expecting("0x6:0x20 out of range",
		       bridge(fd, 0x6, 0x20, &heap_in, sizeof(heap_in),
			      &heap_out, sizeof(heap_out)),
		       EINVAL);

	/* Read the info page the way the UMD does: acquire, import, then mmap
	 * at handle << 12 and actually touch it. bA15 measured that offset
	 * form across 28 mmaps, and the UMD's own failing address in the S1 run
	 * was exactly info_base + 0x48, so a plain read here separates "the
	 * mapping is unusable" from "the contents are wrong".
	 */
	/* eError first, then the count -- see mt_pvr_heap_count_out in the wire
	 * header. Reading a bare u64 here reported the count because the driver
	 * used to write it at offset 0; the UMD read that as eError and cached
	 * zero heaps, so the probe must read it where the UMD reads it.
	 */
	memset(&heap_count_out, 0, sizeof(heap_count_out));
	step("0x6:0x1e HeapCfgHeapCount",
	     bridge(fd, 0x6, 0x1e, NULL, 0, &heap_count_out,
		    sizeof(heap_count_out)));
	printf("%-28s eError=%u num_heaps=%u\n", "", heap_count_out.error,
	       heap_count_out.num_heaps);
	if (heap_count_out.error != 0) {
		printf("%-28s driver reported error %u\n", "MISMATCH:",
		       heap_count_out.error);
		mismatches++;
	}
	if (heap_count_out.num_heaps == 0) {
		printf("%-28s UMD would cache zero heaps here\n", "MISMATCH:");
		mismatches++;
	}

	{
		struct mt_pvr_handle_out info = { 0 };
		struct mt_pvr_import_in imp_in;
		struct mt_pvr_import_out imp_out;
		uint64_t info_offset;
		volatile uint32_t *page;
		void *mapped;
		size_t map_len = 0x10000;

		memset(&info, 0, sizeof(info));
		step("0x1:0xf AcquireInfoPage (for mmap)",
		     bridge(fd, 0x1, 0xf, NULL, 0, &info, sizeof(info)));
		info_offset = (uint64_t)info.handle << 12;
		printf("%-28s handle=0x%llx mmap offset=0x%llx\n", "",
		       (unsigned long long)info.handle,
		       (unsigned long long)info_offset);

		memset(&imp_in, 0, sizeof(imp_in));
		memset(&imp_out, 0, sizeof(imp_out));
		imp_in.ext_handle = info.handle;
		step("0x6:0x6 PmrLocalImportPmr (info)",
		     bridge(fd, 0x6, 0x6, &imp_in, sizeof(imp_in), &imp_out,
			    sizeof(imp_out)));
		printf("%-28s align=0x%llx size=0x%llx pmr=0x%llx\n", "",
		       (unsigned long long)imp_out.align,
		       (unsigned long long)imp_out.size,
		       (unsigned long long)imp_out.pmr);

		mapped = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd,
			      (off_t)info_offset);
		if (mapped == MAP_FAILED) {
			printf("%-28s mmap failed: %s\n", "",
			       strerror(errno));
			failures++;
		} else {
			page = (volatile uint32_t *)mapped;
			printf("%-28s mapped at %p, first word 0x%08x "
			       "word@0x48 0x%08x\n", "info page", mapped,
			       page[0], page[0x48 / 4]);
			munmap(mapped, map_len);
		}
	}

	memset(&sync_out, 0, sizeof(sync_out));
	step("0x2:0x0 AllocSyncPrimitiveBlock",
	     bridge(fd, 0x2, 0x0, &sync_in, sizeof(sync_in), &sync_out,
		    sizeof(sync_out)));
	printf("%-28s handle=0x%llx pmr=0x%llx block=0x%x vaddr=0x%llx\n", "",
	       (unsigned long long)sync_out.sync_handle,
	       (unsigned long long)sync_out.sync_pmr, sync_out.block_size,
	       (unsigned long long)sync_out.vaddr);

	/* An unknown command must be refused, not silently accepted. */
	{
		uint32_t out = 0;

		step_expecting("0xff:0xff unknown",
			       bridge(fd, 0xff, 0xff, NULL, 0, &out, sizeof(out)),
			       ENOTTY);
	}

	close(fd);
	printf("\n%s: %d failing step(s), %d value mismatch(es)\n",
	       (failures || mismatches) ? "FAIL" : "OK", failures, mismatches);
	return (failures || mismatches) ? 1 : 0;
}
