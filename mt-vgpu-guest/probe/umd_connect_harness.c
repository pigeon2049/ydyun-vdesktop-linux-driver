/* Offline harness, session mode: drive legacy MUSA UMD entry points in one
 * process so connection state persists across calls.
 *
 * Usage: umd_connect_harness <lib> <op> [args...] ...
 * Ops (processed in order):
 *   connect FLAGS            PVRSRVConnect(&conn, FLAGS); remembers conn
 *   buf ID SIZE              calloc(SIZE,1); remembers ID -> pointer
 *   u32 ID OFF VAL           poke u32 into buffer ID at OFF
 *   u64 ID OFF VAL           poke u64 into buffer ID at OFF
 *   call SYM A...            call SYM(up to 6 args); each arg is one of:
 *                              conn          remembered connection
 *                              bID           buffer ID base pointer
 *                              bID+OFF       buffer ID plus byte offset
 *                              uVAL          integer (dec/0xhex)
 *   dump ID OFF LEN          hexdump buffer region
 *   ret SYM ...              alias for call (prints SYMBOL ... -> ret)
 *
 * Bridge traffic is observed via LD_PRELOAD=umd_bridge_shim.so with
 * $UMD_TRACE set. No hardware touched.
 */
#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t (*generic_fn)(uint64_t, uint64_t, uint64_t,
			       uint64_t, uint64_t, uint64_t,
			       uint64_t, uint64_t);

#define MAX_BUFS 32

static void *handle;
static uint64_t conn;
static int have_conn;
static void *bufs[MAX_BUFS];

static void *resolve(const char *sym)
{
	void *fn;
	const char *err;
	dlerror();
	fn = dlsym(handle, sym);
	err = dlerror();
	if (err) {
		fprintf(stderr, "dlsym(%s) failed: %s\n", sym, err);
		exit(1);
	}
	return fn;
}

static uint64_t parse_arg(const char *s)
{
	unsigned long off;
	char *end;
	uint64_t base, val;
	if (s[0] == '*') {
		memcpy(&val, (void *)(uintptr_t)parse_arg(s + 1), 8);
		return val;
	}
	if (strcmp(s, "conn") == 0) {
		if (!have_conn) {
			fprintf(stderr, "no connection yet\n");
			exit(1);
		}
		return conn;
	}
	if (strncmp(s, "conn+", 5) == 0) {
		if (!have_conn) {
			fprintf(stderr, "no connection yet\n");
			exit(1);
		}
		return conn + strtoul(s + 5, NULL, 0);
	}
	if (s[0] == 'b') {
		unsigned id = (unsigned)strtoul(s + 1, &end, 10);
		if (id >= MAX_BUFS || !bufs[id]) {
			fprintf(stderr, "bad buffer ref %s\n", s);
			exit(1);
		}
		base = (uint64_t)(uintptr_t)bufs[id];
		if (*end == '*') {
			memcpy(&val, (void *)(uintptr_t)base, 8);
			if (*(end + 1) == '+')
				val += strtoul(end + 2, NULL, 0);
			return val;
		}
		if (*end == '@') {
			if (*(end + 1) == '@') {
				char *tail;
				off = strtoul(end + 2, &tail, 0);
				memcpy(&val, (void *)(uintptr_t)base, 8);
				memcpy(&val,
				       (void *)(uintptr_t)(val + off), 8);
				if (*tail == '+')
					val += strtoul(tail + 1, NULL, 0);
				return val;
			}
			memcpy(&val, (void *)(uintptr_t)(base + strtoul(end + 1, NULL, 0)), 8);
			return val;
		}
		off = 0;
		if (*end == '+')
			off = strtoul(end + 1, NULL, 0);
		return base + off;
	}
	if (s[0] == 'u' || s[0] == 'i')
		return strtoull(s + 1, NULL, 0);
	return strtoull(s, NULL, 0);
}

int main(int argc, char **argv)
{
	int i = 2;
	if (argc < 3) {
		fprintf(stderr, "usage: %s <lib> <ops...>\n", argv[0]);
		return 2;
	}
	handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "dlopen failed: %s\n", dlerror());
		return 1;
	}
	while (i < argc) {
		if (strcmp(argv[i], "connect") == 0) {
			typedef int32_t (*conn_fn)(void **, uint32_t);
			conn_fn f = (conn_fn)resolve("PVRSRVConnect");
			uint32_t flags = (uint32_t)strtoul(argv[i + 1], NULL, 0);
			void *c = NULL;
			int32_t ret = f(&c, flags);
			conn = (uint64_t)(uintptr_t)c;
			have_conn = 1;
			printf("CONNECT(%u) -> %d conn=%p\n", flags, ret, c);
			i += 2;
		} else if (strcmp(argv[i], "buf") == 0) {
			unsigned id = (unsigned)strtoul(argv[i + 1], NULL, 10);
			size_t size = (size_t)strtoull(argv[i + 2], NULL, 0);
			if (id >= MAX_BUFS || bufs[id]) {
				fprintf(stderr, "bad buf id\n");
				return 1;
			}
			bufs[id] = calloc(1, size ? size : 1);
			if (!bufs[id]) {
				fprintf(stderr, "calloc failed\n");
				return 1;
			}
			printf("BUF %u -> %p (%zu bytes)\n", id, bufs[id], size);
			i += 3;
		} else if (strcmp(argv[i], "u32") == 0) {
			unsigned id = (unsigned)strtoul(argv[i + 1], NULL, 10);
			unsigned long off = strtoul(argv[i + 2], NULL, 0);
			uint32_t val = (uint32_t)parse_arg(argv[i + 3]);
			memcpy((char *)bufs[id] + off, &val, 4);
			i += 4;
		} else if (strcmp(argv[i], "u64") == 0) {
			unsigned id = (unsigned)strtoul(argv[i + 1], NULL, 10);
			unsigned long off = strtoul(argv[i + 2], NULL, 0);
			uint64_t val = parse_arg(argv[i + 3]);
			memcpy((char *)bufs[id] + off, &val, 8);
			i += 4;
		} else if (strcmp(argv[i], "call") == 0 ||
			   strcmp(argv[i], "ret") == 0) {
			generic_fn f = (generic_fn)resolve(argv[i + 1]);
			uint64_t a[8] = {0, 0, 0, 0, 0, 0, 0, 0};
			int n = 0;
			int j = i + 2;
			while (j < argc && n < 8 && strcmp(argv[j], "call") != 0 &&
			       strcmp(argv[j], "ret") != 0 &&
			       strcmp(argv[j], "connect") != 0 &&
			       strcmp(argv[j], "buf") != 0 &&
			       strcmp(argv[j], "u32") != 0 &&
			       strcmp(argv[j], "u64") != 0 &&
			       strcmp(argv[j], "dump") != 0 &&
			       strcmp(argv[j], "dumpat") != 0 &&
			       strcmp(argv[j], "strat") != 0)
				a[n++] = parse_arg(argv[j++]);
			printf("SYMBOL %s(...) -> %" PRId64 "\n", argv[i + 1],
			       (int64_t)f(a[0], a[1], a[2], a[3], a[4], a[5],
					  a[6], a[7]));
			i = j;
		} else if (strcmp(argv[i], "dump") == 0) {
			unsigned id = (unsigned)strtoul(argv[i + 1], NULL, 10);
			unsigned long off = strtoul(argv[i + 2], NULL, 0);
			unsigned long len = strtoul(argv[i + 3], NULL, 0);
			unsigned long k;
			unsigned char *p = (unsigned char *)bufs[id] + off;
			printf("DUMP buf%u+0x%lx:", id, off);
			for (k = 0; k < len; k++)
				printf("%s%02x", k % 16 == 0 ? "\n  " : " ", p[k]);
			printf("\n");
			i += 4;
		} else if (strcmp(argv[i], "dumpat") == 0) {
			unsigned char *p = (unsigned char *)(uintptr_t)
				parse_arg(argv[i + 1]);
			unsigned long len = strtoul(argv[i + 2], NULL, 0);
			unsigned long k;
			printf("DUMPAT %s:", argv[i + 1]);
			for (k = 0; k < len; k++)
				printf("%s%02x", k % 16 == 0 ? "\n  " : " ", p[k]);
			printf("\n");
			i += 3;
		} else if (strcmp(argv[i], "strat") == 0) {
			const char *p = (const char *)(uintptr_t)
				parse_arg(argv[i + 1]);
			unsigned long maxlen = strtoul(argv[i + 2], NULL, 0);
			unsigned long k;
			printf("STRAT %s: \"", argv[i + 1]);
			for (k = 0; k < maxlen; k++) {
				char ch = p[k];
				if (!ch)
					break;
				putchar(ch < 32 || ch > 126 ? '.' : ch);
			}
			printf("\"\n");
			i += 3;
		} else {
			fprintf(stderr, "unknown op %s\n", argv[i]);
			return 2;
		}
		fflush(stdout);
	}
	return 0;
}
