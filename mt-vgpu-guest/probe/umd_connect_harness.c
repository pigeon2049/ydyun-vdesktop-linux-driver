/* Offline harness: drive legacy MUSA UMD entry points in-process.
 *
 * Usage: umd_connect_harness <lib_path> <symbol> [u32_arg]
 * dlopens the library, resolves <symbol> as int (*)(void **, uint32_t),
 * calls it and prints the return code. Bridge traffic is observed via
 * LD_PRELOAD=umd_bridge_shim.so with $UMD_TRACE set. No hardware touched:
 * the shim redirects /dev/dri opens and fabricates ioctl responses.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int32_t (*entry_fn)(void **, uint32_t);

int main(int argc, char **argv)
{
	void *handle;
	entry_fn entry;
	void *conn = NULL;
	uint32_t arg = 0;
	const char *err;
	int32_t ret;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <lib> <symbol> [u32_arg]\n", argv[0]);
		return 2;
	}
	if (argc > 3)
		arg = (uint32_t)strtoul(argv[3], NULL, 0);

	handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "dlopen failed: %s\n", dlerror());
		return 1;
	}
	dlerror();
	entry = (entry_fn)dlsym(handle, argv[2]);
	err = dlerror();
	if (err) {
		fprintf(stderr, "dlsym(%s) failed: %s\n", argv[2], err);
		return 1;
	}
	ret = entry(&conn, arg);
	printf("SYMBOL %s(%u) -> %d conn=%p\n", argv[2], arg, ret, conn);
	fflush(stdout);
	return 0;
}
