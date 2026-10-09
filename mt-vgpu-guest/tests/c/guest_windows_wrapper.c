#include "../../kernel/mt_guest_windows.h"

int refresh(void *out, u32 bytes, const void *info, u32 length,
		const struct mt_guest_window_inputs *inputs)
{
	return mt_guest_windows_refresh(out, bytes, info, length, inputs);
}

int build(void *out, u32 bytes, const void *info, u32 length,
		const struct mt_guest_window_inputs *inputs, u64 bar2_gpa, u64 bar2_bytes)
{
	return mt_guest_windows_build(out, bytes, info, length, inputs, bar2_gpa, bar2_bytes);
}
