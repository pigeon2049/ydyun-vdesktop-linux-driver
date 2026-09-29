#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/mt_runtime_context.h"

struct log {
	struct mt_runtime_context *runtime;
	u32 offset[2];
	u64 value[2];
	unsigned count;
};

static void record(void *opaque, u32 offset, u64 value)
{
	struct log *l = opaque;
	/* Retention must precede the first externally visible pointer. */
	assert(mt_runtime_context_can_release(l->runtime) == -EBUSY);
	assert(l->count < 2);
	l->offset[l->count] = offset;
	l->value[l->count++] = value;
}

int main(int argc, char **argv)
{
	u8 info[4096], desc[4096], windows[4096], expected[4096];
	struct mt_runtime_context r = {0}, fresh;
	struct mt_fw_context_addresses a = {
		.root_pa = 0x605800000, .firmware_gpa = 0x83f000000,
		.firmware_va = 0xe1c0000000, .info_gpa = 0x100000, .aperture_gpa = 0x101000,
	};
	struct mt_guest_window_inputs inputs = {
		.system_memory_bytes = 16ULL << 30, .platform_9d8 = 0x8000000000ULL,
	};
	struct log log = {.runtime = &r};
	FILE *f;
	unsigned bits, i;
	assert(argc == 2);
	f = fopen(argv[1], "rb");
	assert(f && fread(info, 1, sizeof(info), f) == sizeof(info));
	fclose(f);
	memset(desc, 0xa5, sizeof(desc));
	memset(windows, 0xa5, sizeof(windows));
	a.root_pa++;
	assert(mt_runtime_context_build(&r, desc, windows, 0x102000, &a, info,
		sizeof(info), &inputs, 0x800000000, 0x400000000) == -EINVAL);
	memset(expected, 0xa5, sizeof(expected));
	assert(!memcmp(desc, expected, sizeof(desc)) && !memcmp(windows, expected, sizeof(windows)));
	assert(!r.prepared && !r.published && !r.descriptor && !r.windows);
	a.root_pa--;
	assert(!mt_runtime_context_build(&r, desc, windows, 0x102000, &a, info,
		sizeof(info), &inputs, 0x800000000, 0x400000000));
	memset(expected, 0xa5, sizeof(expected));
	assert(!mt_fw_context_build(expected, MT_FW_CONTEXT_BYTES, &a));
	assert(!memcmp(desc, expected, sizeof(desc)));
	memset(expected, 0xa5, sizeof(expected));
	assert(!mt_guest_windows_build(expected, MT_GUEST_WINDOWS_BYTES, info, sizeof(info),
		&inputs, 0x800000000, 0x400000000));
	assert(!memcmp(windows, expected, sizeof(windows)));
	assert(mt_runtime_context_build(&r, desc, windows, 0x102000, &a, info,
		sizeof(info), &inputs, 0x800000000, 0x400000000) == -EINVAL);
	fresh = r;
	/* All combinations of the five independent publication prerequisites. */
	for (bits = 0; bits < 31; bits++) {
		assert(mt_runtime_context_publish(&r, bits & 1 ? 2 : 1, bits & 2 ? 2 : 1,
			bits & 4, bits & 8, bits & 16, 1, record, &log) == -EAGAIN);
		assert(log.count == 0 && !r.published && !mt_runtime_context_can_release(&r));
	}
	for (i = 0; i < 2; i++) {
		r = fresh;
		log.count = 0;
		assert(!mt_runtime_context_publish(&r, 2, 2, 1, 1, true, i, record, &log));
		assert(log.count == 1 + i && log.offset[0] == 0xf0 && log.value[0] == 0x102000);
		assert(r.notified == (bool)i && mt_runtime_context_can_release(&r) == -EBUSY);
		if (i)
			assert(log.offset[1] == 0x70 && log.value[1] == 1);
		assert(mt_runtime_context_publish(&r, 2, 2, 1, 1, true, i, record, &log) == -EALREADY);
		assert(log.count == 1 + i);
	}
	puts("PASS: atomic preparation, 31 blocked publication states, ordered publication, repeat rejection and retained lifetime");
	return 0;
}
