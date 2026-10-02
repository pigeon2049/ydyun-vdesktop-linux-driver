/* LD_PRELOAD shim: log legacy MUSA UMD bridge traffic without hardware.
 *
 * Intercepts opens under /dev/dri (hands back dup()s of /dev/null) and the two
 * PVR Services DRM ioctls (0xc0206440 bridge packages, 0x400c6445 init).
 * Bridge outputs are zeroed (eError=0) so the UMD walks as far into init as
 * its own checks allow; every command, direction and payload byte is appended
 * as JSON lines to $UMD_TRACE. All other ioctls pass through via syscall().
 * Nothing touches PCI, BARs or kernel modules.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SRVKM_CMD 0xc0206440UL
#define SRVKM_INIT 0x40046445UL
#define DRM_VERSION 0xc0406400UL
#define MAX_BYTES 512

struct drm_version {
	int32_t major;
	int32_t minor;
	int32_t patch;
	uint64_t name_len;
	char *name;
	uint64_t date_len;
	char *date;
	uint64_t desc_len;
	char *desc;
};

struct srvkm_cmd {
	uint32_t bridge_id;
	uint32_t bridge_func_id;
	uint64_t in_ptr;
	uint64_t out_ptr;
	uint32_t in_size;
	uint32_t out_size;
};

static FILE *logf;
static unsigned long seq;
static int nullfd = -1;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Two modes, both useful.
 *
 * Default: fabricate. /dev/dri opens become /dev/null dup()s, ioctls are
 * answered from the canned table, and mmap of the render node is a private
 * anonymous mapping. This is how every offline result so far was produced
 * (bA1-bA14) and it must keep working unchanged.
 *
 * UMD_SHIM_PASSTHROUGH=1: record only. Opens, ioctls and mmap go to the real
 * kernel, and nothing is answered on the driver's behalf. This is how the
 * Stage B bridge is validated: the same UMD, the same recipe, but the ioctls
 * land on the real node instead of this file.
 */
static int passthrough;

static void ensure_log(void);

static int resolving_stat_flag;
static int resolving_stat(void) { return resolving_stat_flag; }
static void set_resolving_stat(int v) { resolving_stat_flag = v; }

static int umd_trace_would_exceed(size_t line);

/* Addresses of file-backed mappings this shim recorded, so munmap() can tell a
 * UMD mapping (worth a log line) from the process's own anonymous ones (noise).
 * A small fixed table: the UMD maps a handful of PMRs per session. */
#define UMD_MAP_TRACK_MAX 64
static void *umd_map_addrs[UMD_MAP_TRACK_MAX];
static unsigned umd_map_count;

static void umd_map_remember(void *addr)
{
	if (!addr || addr == MAP_FAILED)
		return;
	if (umd_map_count < UMD_MAP_TRACK_MAX)
		umd_map_addrs[umd_map_count++] = addr;
}

static int umd_mmap_is_interesting(const void *addr)
{
	unsigned i;

	for (i = 0; i < umd_map_count; i++)
		if (umd_map_addrs[i] == addr)
			return 1;
	return 0;
}

/* Record path probes the UMD makes without opening.
 *
 * The device-connection failure happens with no ioctl at all, so anything the
 * UMD learns by stat()ing a device node is invisible in the ioctl and open
 * traces. PVR has always shipped a /dev/dri/controlD* node and the UMD's own
 * node-enumeration function handles "controlD" explicitly, so whether it even
 * looks for one is a cheap question to answer before building a control node.
 * Only paths under /dev/dri are logged, so volume stays negligible.
 */
static void log_path_probe(const char *op, const char *path, int ret)
{
	if (!path || !strstr(path, "/dev/dri"))
		return;
	ensure_log();
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf,
			"{\"seq\":%lu,\"op\":\"%s\",\"path\":\"%s\",\"ret\":%d}\n",
			++seq, op, path, ret);
}

/* Record every open the UMD makes, whichever libc entry point it used.
 *
 * The UMD reaches DRM nodes through open64()/openat() as often as open(), and
 * those paths logged nothing, so "which node did the UMD actually pick?" had no
 * answer. In record-only mode the file name is the only evidence of whether the
 * device connection landed on our node or on the unrelated QXL card0.
 */
static void log_open(const char *op, const char *path, int fd)
{
	ensure_log();
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"%s\",\"path\":\"%s\","
			"\"fd\":%d}\n", ++seq, op, path, fd);
}

static int pvr_passthrough(void)
{
	const char *mode = getenv("UMD_SHIM_PASSTHROUGH");

	if (mode && *mode && strcmp(mode, "0"))
		passthrough = 1;
	return passthrough;
}

/* Raw syscall that bypasses this file's own syscall() interposition. */
static long S_(long n, long a, long b, long c, long d, long e, long f)
{
	long ret;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(n), "D"(a), "S"(b), "d"(c),
			   "r"(r10), "r"(r8), "r"(r9)
			 : "rcx", "r11", "memory");
	return ret;
}

/* FDs handed to the UMD for /dev/dri nodes (backed by /dev/null). */
#define MAX_UMD_FDS 16
static int umd_fds[MAX_UMD_FDS];
static int numd_fds;

static void track_umd_fd(int fd)
{
	if (fd >= 0 && numd_fds < MAX_UMD_FDS)
		umd_fds[numd_fds++] = fd;
}

static int is_umd_fd(int fd)
{
	int i;
	for (i = 0; i < numd_fds; i++)
		if (umd_fds[i] == fd)
			return 1;
	return 0;
}

/* Canned outputs that carry the UMD past early init checks. Each entry is
 * matched on (bridge_id, func_id); bytes are written to the start of the
 * output buffer (which was zeroed first). Grown iteratively from traces. */
struct canned_out {
	uint32_t bridge_id;
	uint32_t func_id;
	uint8_t bytes[64];
	uint32_t size;
};

static const struct canned_out canned[] = {
	/* SRVCORE:AcquireGlobalEventObject -> nonzero event handle (bA2-E2). */
	{0x1, 0x2,
	 {0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 12},
	/* SRVCORE:Connect -> exact core ID, zero caps/arch (bA4-H2):
	 * nonzero caps misfires the devmem-ctx reuse counter. */
	{0x1, 0x0,
	 {0x17, 0x00, 0x60, 0x06, 0x04, 0x00, 0x23, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00},
	 17},
	/* SRVCORE:ACQUIREINFOPAGE -> nonzero info-page PMR handle. */
	{0x1, 0xf,
	 {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 12},
	/* MM:PMRLOCALIMPORTPMR -> align/size + local PMR handle. */
	{0x6, 0x6,
	 {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 28},
	/* MM:HEAPCFGHEAPCOUNT -> 15 heaps (retest with distinct handles). */
	{0x6, 0x1e,
	 {0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00},
	 8},
	/* MM:DEVMEMINTCTXCREATE -> nonzero server ctx/priv + 64B cache line. */
	{0x6, 0xf,
	 {0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x01, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00},
	 24},
	/* RGXTA3D:RGXCREATERENDERCONTEXT -> nonzero render ctx handle. */
	{0x82, 0x8,
	 {0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 12},
	/* SRVCORE:EVENTOBJECTOPEN -> nonzero OS event handle. */
	{0x1, 0x4,
	 {0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 12},
};

/* Heap 0-14 base/length/reserved from the vendor RGX application heap
 * configuration (S3000 Guest layout). */
struct heap_range { uint64_t base, size, reserved; };
static const struct heap_range heap_ranges[15] = {
	{0x4000000000ULL, 274877906944ULL, 2097152ULL},
	{0x8000000000ULL, 137438953472ULL, 65536ULL},
	{0xb800000000ULL, 34359738368ULL, 0ULL},
	{0xda00000000ULL, 4294967296ULL, 65536ULL},
	{0xe000000000ULL, 4294967296ULL, 65536ULL},
	{0xe900000000ULL, 1073741824ULL, 0ULL},
	{0xea00000000ULL, 65536ULL, 0ULL},
	{0xeb00000000ULL, 4294967296ULL, 0ULL},
	{0xec00000000ULL, 2097152ULL, 0ULL},
	{0xec40000000ULL, 2097152ULL, 0ULL},
	{0xed00000000ULL, 16777216ULL, 0ULL},
	{0xee00000000ULL, 1073741824ULL, 0ULL},
	{0xef00000000ULL, 1073741824ULL, 0ULL},
	{0xf000000000ULL, 4294967296ULL, 0ULL},
	{0xf200000000ULL, 2097152ULL, 0ULL},
};

/* Heap names for FindHeapByName (RGX_*_HEAP_IDENT). Every blueprint is named.
 * BufSz is 160. */
static const char *heap_names[15] = {
	"General SVM", "General", "General NON-4K", "PDS Code and Data",
	"USC Code", "Vulkan Capture Replay", "Signals", "Component Control",
	"FBCDC", "Large FBCDC", "PDS Indirect State", "Compute Mission RMW",
	"Compute Safety RMW", "Texture State", "Visibility Test",
};

static void fabricate_heap_details(const uint8_t *in, uint32_t in_size,
				   uint8_t *out, uint32_t out_size)
{
	/* OUT (44B): base u64@0, length u64@8, reserved u64@16, name@24,
	 * eError u32@32, log2page u32@36, log2align u32@40.
	 * IN (20B): nameptr u64@0, config u32@8, heap u32@12, bufsz u32@16. */
	uint32_t idx = 0;
	uint64_t base = 0xf000000000ULL, size = 0x100000000ULL;
	if (in_size >= 20) {
		uint32_t i, bufsz = 0;
		uint64_t nameptr = 0;
		memcpy(&i, in + 12, 4);
		memcpy(&nameptr, in, 8);
		memcpy(&bufsz, in + 16, 4);
		idx = i;
		if (nameptr && bufsz && idx < 15 && heap_names[idx]) {
			size_t n = strlen(heap_names[idx]) + 1;
			if (n > bufsz)
				n = bufsz;
			memcpy((void *)(uintptr_t)nameptr, heap_names[idx], n);
		}
	}
	if (idx < 15 && heap_ranges[idx].size) {
		base = heap_ranges[idx].base;
		size = heap_ranges[idx].size;
	}
	memset(out, 0, out_size < 44 ? out_size : 44);
	if (out_size >= 24) {
		uint64_t reserved = idx < 15 ? heap_ranges[idx].reserved : 0;
		memcpy(out, &base, 8);
		memcpy(out + 8, &size, 8);
		memcpy(out + 16, &reserved, 8);
	}
	/* The vendor blueprint reports zero log2 page and alignment values. */
}

/* Distinct PMR handles per allocation (OUT 24B: hPMR u64@0).
 * Zero handles alias and break later unref/map steps. */
static uint64_t next_pmr = 0x5000;
static uint64_t next_mapping = 0x8000;
static uint64_t next_reservation = 0x9000;

static void fabricate_handle_out(uint64_t *counter, uint8_t *out,
				 uint32_t out_size, uint32_t body)
{
	uint64_t h;
	memset(out, 0, out_size < body ? out_size : body);
	if (out_size >= 8) {
		h = (*counter)++;
		memcpy(out, &h, 8);
	}
}

static void fabricate_pmr_alloc(const uint8_t *in, uint32_t in_size,
				uint8_t *out, uint32_t out_size)
{
	uint64_t h;
	(void)in;
	(void)in_size;
	memset(out, 0, out_size < 24 ? out_size : 24);
	if (out_size >= 8) {
		h = next_pmr++;
		memcpy(out, &h, 8);
	}
	if (out_size >= 20) {
		/* RAM-backed PMRs live in system memory; echo back a
		 * plausible flag set (IN asks 0x1233-class). */
		uint32_t one = 1, flags = 0x1233;
		memcpy(out + 12, &flags, 4);
		memcpy(out + 16, &one, 4);
	}
}

/* Sync primitive block (0x02:0x00) OUT is 32B: hSyncHandle u64@0,
 * hhSyncPMR u64@8, eError u32@16, BlockSize u32@20, VAddr u64@24.
 * Zero BlockSize kills the follow-up RA_Alloc (entry rejects size 0);
 * zero handles alias later unref/map steps. Fabricate all nonzero. */
static void fabricate_sync_alloc(uint8_t *out, uint32_t out_size)
{
	uint64_t hpmr;
	uint32_t blk = 0x1000;
	memset(out, 0, out_size < 32 ? out_size : 32);
	if (out_size >= 8) {
		uint64_t h = 0x6000 + (next_pmr & 0xff);
		memcpy(out, &h, 8);
	}
	if (out_size >= 16) {
		hpmr = next_pmr++;
		memcpy(out + 8, &hpmr, 8);
	}
	if (out_size >= 24)
		memcpy(out + 20, &blk, 4);
}

/* TDM shared memory (0x89:0x5) OUT is 20B: hMem u64@0, hMem u64@8,
 * eError u32@16. Zero handles stall RGXTDMCreateStaticMem at its first
 * TQPMR map (r87); hand out distinct nonzero fabrications the way the
 * PMR path does. eError stays 0 from the pre-zeroing at the call site.
 * 0x89:0x6 release takes one handle and needs no fabricated output. */
static uint64_t next_tdm = 0xa000;

static void fabricate_tdm_shmem(uint8_t *out, uint32_t out_size)
{
	uint64_t h;

	if (out_size < 16)
		return;
	h = next_tdm++;
	memcpy(out, &h, 8);
	h = next_tdm++;
	memcpy(out + 8, &h, 8);
}

/* Distinct heap handles keyed by base VA (OUT 12B: hHeap u64@0).
 * IN (28B): base u64@0, length u64@8, ctx u64@16, log2page u32@24. */
static void fabricate_heap_create(const uint8_t *in, uint32_t in_size,
				  uint8_t *out, uint32_t out_size)
{
	uint64_t base = 0, h = 1;
	uint32_t i;
	if (in_size >= 8)
		memcpy(&base, in, 8);
	for (i = 0; i < 15; i++) {
		if (heap_ranges[i].base == base) {
			h = (uint64_t)(i + 1);
			break;
		}
	}
	memset(out, 0, out_size < 12 ? out_size : 12);
	if (out_size >= 8)
		memcpy(out, &h, 8);
}

static void apply_canned(uint32_t bridge_id, uint32_t func_id,
			 void *out, uint32_t out_size)
{
	size_t i;
	for (i = 0; i < sizeof(canned) / sizeof(canned[0]); i++) {
		uint32_t n;
		if (canned[i].bridge_id != bridge_id ||
		    canned[i].func_id != func_id)
			continue;
		n = canned[i].size < out_size ? canned[i].size : out_size;
		memcpy(out, canned[i].bytes, n);
	}
}

/* Hard ceiling on the trace, in bytes.
 *
 * This has now bitten twice: a 4.7 GB / 81M-line flood in bA13, and 7.6 GB from
 * a single run under gdb, where the UMD's DRM-node enumeration looped far more
 * than the four commands a normal run issues. Both times the symptom was
 * remote -- /tmp hit 100%, every "cc" in the test suite started failing, and
 * the test count silently dropped from 120 to 91 -- which is easy to misread
 * as a code regression. The trace is a diagnostic, so it must never be able to
 * take the machine down with it.
 */
#define UMD_TRACE_MAX_BYTES (256ULL * 1024ULL * 1024ULL)

static unsigned long long umd_trace_bytes;
static int umd_trace_capped;

static void ensure_log(void)
{
	const char *p;
	if (logf)
		return;
	p = getenv("UMD_TRACE");
	logf = fopen(p && *p ? p : "/tmp/opencode/umda/trace.jsonl", "a");
	if (logf)
		setvbuf(logf, NULL, _IONBF, 0);
}

/* Called before every write. Accounts the line when it is allowed through, and
 * once the cap is hit returns 1 forever after so the trace stops rather than
 * growing. Says so once on stderr, so a truncated trace is never mistaken for a
 * complete one.
 */
static int umd_trace_would_exceed(size_t line)
{
	if (umd_trace_capped)
		return 1;
	if (umd_trace_bytes + line > UMD_TRACE_MAX_BYTES) {
		umd_trace_capped = 1;
		fprintf(stderr,
			"[umd-shim] trace capped at %llu bytes; UMD appears to be "
			"looping. Remaining records dropped.\n",
			UMD_TRACE_MAX_BYTES);
		return 1;
	}
	umd_trace_bytes += line;
	return 0;
}

static int is_dri(const char *path)
{
	return path && strncmp(path, "/dev/dri/", 9) == 0;
}

/* Env-gated bridge traffic dump for the passthrough (real-driver) path,
 * e.g. UMD_DUMP_BRIDGE="0x88:0x4" or "0x6:0x9,0x6:0x13,0x6:0x15". Runs
 * in-process, so reading cmd.in_ptr/out_ptr is as safe as the existing
 * fabricated-path log_hex on the same pointers.
 */
static int dump_bridge_match(unsigned tb, unsigned tf)
{
	const char *spec = getenv("UMD_DUMP_BRIDGE");
	char entry[32];
	size_t len, off = 0;
	if (!spec || !*spec)
		return 0;
	while (spec[off]) {
		unsigned b = 0, f = 0;
		int used = 0;
		while (spec[off] == ' ' || spec[off] == ',')
			off++;
		len = 0;
		while (spec[off + len] && spec[off + len] != ',')
			len++;
		if (!len || len >= sizeof(entry))
			return 0;
		memcpy(entry, spec + off, len);
		entry[len] = 0;
		off += len;
		if (sscanf(entry, "%i:%i%n", &b, &f, &used) != 2 ||
		    !used || entry[used])
			return 0;
		if (b == tb && f == tf)
			return 1;
	}
	return 0;
}

/* UMD-side annotation string carried by 0x6:0x9 (pointer + length inside
 * the IN buffer). Same shape as the fabricated path's annotation reader.
 */
static void log_pmr_annotation(const uint8_t *inb, uint32_t in_size)
{
	uint64_t ap = 0;
	uint32_t alen = 0, k;
	if (in_size < 36)
		return;
	memcpy(&ap, inb + 24, 8);
	memcpy(&alen, inb + 32, 4);
	fprintf(logf, "\"annotation\":\"");
	if (alen > 128)
		alen = 128;
	for (k = 0; k < alen; k++) {
		char ch = ((const char *)(uintptr_t)ap)[k];
		if (!ch)
			break;
		if (ch == '"' || ch == '\\')
			fputc('\\', logf);
		fputc(ch < 32 || ch > 126 ? '.' : ch, logf);
	}
	fprintf(logf, "\"");
}

static int devnull(void)
{
	if (nullfd < 0)
		nullfd = (int)S_(SYS_openat, AT_FDCWD, (long)"/dev/null",
				      O_RDWR | O_CLOEXEC, 0, 0, 0);
	return nullfd;
}

static void log_hex_bytes(const void *ptr, uint32_t size)
{
	const unsigned char *b = ptr;
	uint32_t n = size > MAX_BYTES ? MAX_BYTES : size;
	uint32_t i;
	for (i = 0; i < n; i++)
		fprintf(logf, "%02x", b[i]);
}

static void log_hex(const char *tag, const void *ptr, uint32_t size)
{
	if (!logf)
		return;
	fprintf(logf, "\"%s\":\"", tag);
	log_hex_bytes(ptr, size);
	fprintf(logf, "\"");
	if (size > MAX_BYTES)
		fprintf(logf, ",\"%s_truncated\":%u", tag, size);
}

static int dri_open(const char *path)
{
	int fd;
	ensure_log();
	fd = (int)S_(SYS_dup, devnull(), 0, 0, 0, 0, 0);
	track_umd_fd(fd);
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"open\",\"path\":\"%s\",\"fd\":%d}\n",
			++seq, path ? path : "?", fd);
	return fd;
}

int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (is_dri(path) && !pvr_passthrough())
		return dri_open(path);
	{
		int fd = (int)S_(SYS_openat, AT_FDCWD, (long)path, flags, mode, 0, 0);
		ensure_log();
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, "{\"seq\":%lu,\"op\":\"open_other\",\"path\":\"%s\","
				"\"fd\":%d}\n", ++seq, path, fd);
		return fd;
	}
}

int open64(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (is_dri(path) && !pvr_passthrough())
		return dri_open(path);
	{
		int fd = (int)S_(SYS_openat, AT_FDCWD, (long)path, flags, mode, 0, 0);

		if (pvr_passthrough())
			log_open("open_real", path, fd);
		return fd;
	}
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (path[0] == '/' && is_dri(path) && !pvr_passthrough())
		return dri_open(path);
	{
		int fd = (int)S_(SYS_openat, dirfd, (long)path, flags, mode, 0, 0);

		if (pvr_passthrough() && path[0] == '/')
			log_open("openat_real", path, fd);
		else if (pvr_passthrough() && strstr(path, "/sys/"))
			/* The UMD's DRM-device discovery walks sysfs
			 * (/sys/dev/char/.../device/drm and udev property
			 * files) and makes no ioctls while doing it, so a
			 * failure there is invisible in the ioctl trace.
			 */
			log_open("openat_sys", path, fd);
		return fd;
	}
}

int openat64(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (path[0] == '/' && is_dri(path) && !pvr_passthrough())
		return dri_open(path);
	return (int)S_(SYS_openat, dirfd, (long)path, flags, mode, 0, 0);
}

int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	long ret;
	struct srvkm_cmd cmd;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	ensure_log();

	/* Record-only mode: answer nothing, forward everything. The log line
	 * keeps the bridge id visible so the real trace can be diffed against
	 * the fabricated one.
	 */
	if (pvr_passthrough()) {
		ret = S_(SYS_ioctl, fd, req, (long)arg, 0, 0, 0);
		if (logf) {
			if (req == SRVKM_CMD && arg) {
				struct srvkm_cmd cmd;

				memcpy(&cmd, arg, sizeof(cmd));
				fprintf(logf,
					"{\"seq\":%lu,\"op\":\"ioctl_real\","
					"\"fd\":%d,\"bridge\":\"0x%x:0x%x\","
					"\"in_size\":%u,\"out_size\":%u,",
					++seq, fd, cmd.bridge_id,
					cmd.bridge_func_id, cmd.in_size,
					cmd.out_size);
				if (dump_bridge_match(cmd.bridge_id,
						      cmd.bridge_func_id)) {
					if (cmd.in_ptr && cmd.in_size) {
						log_hex("in_hex",
							(const void *)(uintptr_t)cmd.in_ptr,
							cmd.in_size);
						if (cmd.bridge_id == 0x6 &&
						    cmd.bridge_func_id == 0x9) {
							fprintf(logf, ",");
							log_pmr_annotation(
								(const uint8_t *)(uintptr_t)cmd.in_ptr,
								cmd.in_size);
						}
						fprintf(logf, ",");
					}
					if (cmd.out_ptr && cmd.out_size) {
						log_hex("out_hex",
							(const void *)(uintptr_t)cmd.out_ptr,
							cmd.out_size);
						fprintf(logf, ",");
					}
				}
				fprintf(logf, "\"ret\":%ld}\n", ret);
			} else {
				fprintf(logf,
					"{\"seq\":%lu,\"op\":\"ioctl_real\","
					"\"fd\":%d,\"req\":\"0x%lx\",\"ret\":%ld}\n",
					++seq, fd, req, ret);
			}
		}
		return (int)ret;
	}

	if (req == DRM_VERSION && arg) {
		/* libdrm-style probe: first with name==NULL to learn the
		 * length, then with a buffer. Claim to be the mtgpu node so
		 * the UMD proceeds to the Services handshake. */
		struct drm_version *v = arg;
		if (!v->name || !v->name_len) {
			v->major = 1;
			v->minor = 0;
			v->patch = 0;
			v->name_len = 4;
			v->date_len = 1;
			v->desc_len = 1;
			if (logf && !umd_trace_would_exceed(512))
				fprintf(logf, "{\"seq\":%lu,\"op\":\"version_probe\","
					"\"fd\":%d,\"ret\":0}\n", ++seq, fd);
			return 0;
		}
		if (v->name_len >= 4)
			memcpy(v->name, "pvr", 4);
		v->name_len = 4;
		if (v->date && v->date_len >= 1)
			v->date[0] = '\0';
		v->date_len = 1;
		if (v->desc && v->desc_len >= 1)
			v->desc[0] = '\0';
		v->desc_len = 1;
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, "{\"seq\":%lu,\"op\":\"version_claim\","
				"\"fd\":%d,\"name\":\"mtgpu\",\"ret\":0}\n",
				++seq, fd);
		return 0;
	}

	if ((req == SRVKM_CMD || req == SRVKM_INIT) && arg) {
		if (req == SRVKM_INIT) {
			if (logf && !umd_trace_would_exceed(512))
				fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl\",\"fd\":%d,"
					"\"req\":\"0x%lx\",\"init_module\":%u,\"ret\":0}\n",
					++seq, fd, req, *(uint32_t *)arg);
			return 0;
		}
		memcpy(&cmd, arg, sizeof(cmd));
		{
			/* UMD_TRAP="bridge:func" raises SIGTRAP when that
			 * bridge fires, so gdb captures the caller stack. */
			const char *trap = getenv("UMD_TRAP");
			unsigned tb = 0, tf = 0;
			if (trap && sscanf(trap, "%u:%u", &tb, &tf) == 2 &&
			    tb == cmd.bridge_id && tf == cmd.bridge_func_id)
				raise(SIGTRAP);
		}
		if (logf) {
			fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl\",\"fd\":%d,"
				"\"bridge\":\"0x%x:0x%x\",\"in_size\":%u,\"out_size\":%u,",
				++seq, fd, cmd.bridge_id, cmd.bridge_func_id,
				cmd.in_size, cmd.out_size);
			if (cmd.in_ptr && cmd.in_size) {
				log_hex("in", (const void *)(uintptr_t)cmd.in_ptr,
					cmd.in_size);
				/* PMR alloc carries a human annotation naming
				 * the allocation purpose. */
				if (cmd.bridge_id == 0x6 &&
				    cmd.bridge_func_id == 0x9 && cmd.in_size >= 36) {
					const uint8_t *inb =
						(const uint8_t *)(uintptr_t)cmd.in_ptr;
					uint64_t ap = 0;
					uint32_t alen = 0;
					uint32_t k;
					memcpy(&ap, inb + 24, 8);
					memcpy(&alen, inb + 32, 4);
					fprintf(logf, ",\"annotation\":\"");
					if (alen > 128)
						alen = 128;
					for (k = 0; k < alen; k++) {
						char ch =
							((const char *)(uintptr_t)ap)[k];
						if (!ch)
							break;
						fputc(ch < 32 || ch > 126 ? '.'
									  : ch,
						      logf);
					}
					fprintf(logf, "\"");
				}
			} else
				fprintf(logf, "\"in\":null");
			fprintf(logf, ",\"fabricated_zero_out\":%u",
				cmd.out_ptr && cmd.out_size ? cmd.out_size : 0);
		}
		if (cmd.out_ptr && cmd.out_size) {
			uint32_t n = cmd.out_size > 4096 ? 4096 : cmd.out_size;
			memset((void *)(uintptr_t)cmd.out_ptr, 0, n);
			if (cmd.bridge_id == 0x6 && cmd.bridge_func_id == 0x20 &&
			    cmd.in_ptr && cmd.in_size)
				fabricate_heap_details(
					(const uint8_t *)(uintptr_t)cmd.in_ptr,
					cmd.in_size,
					(uint8_t *)(uintptr_t)cmd.out_ptr, n);
			else if (cmd.bridge_id == 0x6 && cmd.bridge_func_id == 0x11 &&
				 cmd.in_ptr && cmd.in_size)
				fabricate_heap_create(
					(const uint8_t *)(uintptr_t)cmd.in_ptr,
					cmd.in_size,
					(uint8_t *)(uintptr_t)cmd.out_ptr, n);
			else if (cmd.bridge_id == 0x6 && cmd.bridge_func_id == 0x9)
				fabricate_pmr_alloc(
					(const uint8_t *)(uintptr_t)cmd.in_ptr,
					cmd.in_size,
					(uint8_t *)(uintptr_t)cmd.out_ptr, n);
			else if (cmd.bridge_id == 0x2 && cmd.bridge_func_id == 0x0)
				fabricate_sync_alloc(
					(uint8_t *)(uintptr_t)cmd.out_ptr, n);
			else if (cmd.bridge_id == 0x6 && cmd.bridge_func_id == 0x13)
				fabricate_handle_out(&next_mapping,
						     (uint8_t *)(uintptr_t)cmd.out_ptr,
						     n, 12);
			else if (cmd.bridge_id == 0x6 && cmd.bridge_func_id == 0x15)
				fabricate_handle_out(&next_reservation,
						     (uint8_t *)(uintptr_t)cmd.out_ptr,
						     n, 12);
			else if (cmd.bridge_id == 0x89 && cmd.bridge_func_id == 0x5)
				fabricate_tdm_shmem(
					(uint8_t *)(uintptr_t)cmd.out_ptr, n);
			else
				apply_canned(cmd.bridge_id, cmd.bridge_func_id,
					     (void *)(uintptr_t)cmd.out_ptr, n);
			if (logf) {
				fprintf(logf, ",\"out_written\":\"");
				log_hex_bytes((const void *)(uintptr_t)cmd.out_ptr,
					      n > 32 ? 32 : n);
				fprintf(logf, "\"");
			}
		}
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, ",\"ret\":0}\n");
		return 0;
	}

	/* PVR sync ioctl family on DRM nodes (SYNC_RENAME 0x41 and
	 * siblings): succeed silently so sync-timeline setup proceeds. */
	if (arg && ((req >> 8) & 0xff) == 0x64) {
		unsigned nr = req & 0xff;
		if (nr >= 0x40 && nr <= 0x45 && req != SRVKM_CMD) {
			ensure_log();
			if (logf && !umd_trace_would_exceed(512))
				fprintf(logf, "{\"seq\":%lu,\"op\":\"pvr_sync_ioctl\","
					"\"fd\":%d,\"req\":\"0x%lx\",\"ret\":0}\n",
					++seq, fd, req);
			return 0;
		}
	}

	ret = S_(SYS_ioctl, fd, req, (long)arg, 0, 0, 0);
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl_passthrough\",\"fd\":%d,"
			"\"req\":\"0x%lx\",\"ret\":%ld}\n", ++seq, fd, req, ret);
	return (int)ret;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *ret = (void *)S_(SYS_mmap, (long)addr, len, prot, flags, fd, off);
	/* Only file-backed mappings are of interest. An anonymous mapping
	 * (fd < 0) is the process's own heap and library bookkeeping, and
	 * logging those produced 79 million records and 8 GB of trace in one
	 * run: the shim is also preloaded into gdb and its Python interpreter,
	 * which map constantly. Nothing about the UMD shows up in them.
	 */
	if (fd >= 0) {
		umd_map_remember(ret);
		ensure_log();
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap\",\"fd\":%d,"
				"\"len\":%zu,\"off\":\"0x%lx\",\"ret\":\"%p\"}\n",
				++seq, fd, len, (unsigned long)off, ret);
	}
	return ret;
}

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off64_t off)
{
	void *ret = (void *)S_(SYS_mmap, (long)addr, len, prot, flags, fd, (long)off);
	if (fd >= 0) {
		umd_map_remember(ret);
		ensure_log();
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap\",\"fd\":%d,"
				"\"len\":%zu,\"off\":\"0x%llx\",\"ret\":\"%p\"}\n",
				++seq, fd, len, (unsigned long long)off, ret);
	}
	return ret;
}

int munmap(void *addr, size_t len)
{
	int ret = (int)S_(SYS_munmap, (long)addr, len, 0, 0, 0, 0);
	/* Same reasoning as mmap(): unmapping the process's own anonymous
	 * allocations carries no UMD signal and floods the trace. Only record
	 * a munmap whose address was handed out by a file-backed mapping we
	 * logged, which is the info-page and PMR case.
	 */
	if (umd_mmap_is_interesting(addr)) {
		ensure_log();
		if (logf && !umd_trace_would_exceed(512))
			fprintf(logf, "{\"seq\":%lu,\"op\":\"munmap\",\"addr\":\"%p\","
				"\"len\":%zu,\"ret\":%d}\n", ++seq, addr, len, ret);
	}
	return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
	ssize_t ret = S_(SYS_read, fd, (long)buf, count, 0, 0, 0);
	ensure_log();
	/* Worker threads poll fds in tight loops; logging every read floods
	 * the trace (tens of millions of lines) and fills /tmp. Log reads
	 * only when explicitly asked. Bridge/ioctl/mmap logging is unaffected. */
	if (logf && getenv("UMD_TRACE_READ") && (fd > 2 || ret > 0))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"read\",\"fd\":%d,"
			"\"count\":%zu,\"ret\":%zd}\n",
			++seq, fd, count, ret);
	return ret;
}

ssize_t pread(int fd, void *buf, size_t count, off_t off)
{
	ssize_t ret = S_(SYS_pread64, fd, (long)buf, count, (long)off, 0, 0);
	ensure_log();
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"pread\",\"fd\":%d,"
			"\"count\":%zu,\"off\":\"0x%lx\",\"ret\":%zd}\n",
			++seq, fd, count, (unsigned long)off, ret);
	return ret;
}

ssize_t pread64(int fd, void *buf, size_t count, off64_t off)
{
	ssize_t ret = S_(SYS_pread64, fd, (long)buf, count, (long)off, 0, 0);
	ensure_log();
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"pread\",\"fd\":%d,"
			"\"count\":%zu,\"off\":\"0x%llx\",\"ret\":%zd}\n",
			++seq, fd, count, (unsigned long long)off, ret);
	return ret;
}

off_t lseek(int fd, off_t off, int whence)
{
	off_t ret = (off_t)S_(SYS_lseek, fd, off, whence, 0, 0, 0);
	ensure_log();
	if (logf && fd > 2)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"lseek\",\"fd\":%d,"
			"\"off\":\"0x%lx\",\"whence\":%d,\"ret\":\"0x%lx\"}\n",
			++seq, fd, (unsigned long)off, whence,
			(unsigned long)ret);
	return ret;
}

/* Fill a fabricated info page. Layout is refined iteratively from traces;
 * v2: device count at +0, KMD capability mask at +0x44 (must cover 0xb57),
 * KMD build-options magic at +0x48 (must equal 0x688a847 mod bit 16).
 * Sources: UMD checks at file 0x92477/0x9247d (S). */
static void fill_info_page(void *base, size_t len)
{
	uint32_t *u = base;
	if (len >= 4)
		u[0] = 1;
	if (len >= 0x48 + 4) {
		u[0x44 / 4] = 0xb57;
		u[0x48 / 4] = 0x688a847;
	}
}

/* Interpose libc syscall() itself: the UMD issues key syscalls (notably
 * mmap of the render node for the info page) via syscall(), bypassing the
 * mmap@plt wrapper above. Only mmap on UMD DRI fds is fabricated;
 * everything else is forwarded untouched. */
long syscall(long n, ...)
{
	va_list ap;
	if (n == SYS_mmap) {
		void *addr;
		size_t len;
		int prot, flags, fd;
		off_t off;
		void *p;
		va_start(ap, n);
		addr = va_arg(ap, void *);
		len = va_arg(ap, size_t);
		prot = va_arg(ap, int);
		flags = va_arg(ap, int);
		fd = va_arg(ap, int);
		off = va_arg(ap, off_t);
		va_end(ap);
		ensure_log();
		if (pvr_passthrough()) {
			/* Let the real driver map. This is the path the Stage B
			 * bridge is judged on: the driver maps its own PMR at
			 * handle << 12, not an anonymous page.
			 */
			p = (void *)S_(SYS_mmap, (long)addr, (long)len, prot,
				       flags, fd, (long)off);
			if (logf && !umd_trace_would_exceed(512))
				fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap_real\","
					"\"fd\":%d,\"len\":%zu,\"off\":\"0x%lx\","
					"\"ret\":\"%p\"}\n",
					++seq, fd, len, (unsigned long)off, p);
			return (long)p;
		}
		if (is_umd_fd(fd)) {
			p = (void *)S_(SYS_mmap, 0, (long)len,
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (p != MAP_FAILED)
				fill_info_page(p, len);
			if (logf && !umd_trace_would_exceed(512))
				fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap_fabricated\","
					"\"fd\":%d,\"len\":%zu,\"off\":\"0x%lx\","
					"\"ret\":\"%p\"}\n",
					++seq, fd, len, (unsigned long)off, p);
			return (long)p;
		}
		return S_(SYS_mmap, (long)addr, (long)len, prot, flags, fd,
			  (long)off);
	}
	{
		long a, b, c, d, e, f;
		va_start(ap, n);
		a = va_arg(ap, long);
		b = va_arg(ap, long);
		c = va_arg(ap, long);
		d = va_arg(ap, long);
		e = va_arg(ap, long);
		f = va_arg(ap, long);
		va_end(ap);
		return S_(n, a, b, c, d, e, f);
	}
}

/* Log free() arguments with a short backtrace. Used to locate UMD-side
 * double-free/abort sites during bring-up; quiet by default unless
 * UMD_LOG_FREE=1. Forwarded via the real libc free looked up once. */
static void (*real_free)(void *);
static unsigned long umd_slide;

static void resolve_umd_slide(void)
{
	char line[512];
	FILE *maps;
	if (umd_slide)
		return;
	maps = fopen("/proc/self/maps", "r");
	if (!maps)
		return;
	while (fgets(line, sizeof(line), maps)) {
		unsigned long start, end, off;
		if (!strstr(line, "libsrv_um_MUSA") || !strstr(line, "r-xp"))
			continue;
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &off) == 3) {
			umd_slide = start - off;
			break;
		}
	}
	fclose(maps);
}

/* Allocation accounting: match frees against mallocs to pinpoint invalid
 * or double frees. Active only with UMD_LOG_ALLOC=1.
 * Reentrancy-safe: dlsym itself allocates, so resolve under a guard with
 * a bump fallback. */
static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);
static int resolving;
static char tmpbuf[16384];
static size_t tmpused;

static void *tmp_alloc(size_t s)
{
	size_t a = (s + 15) & ~(size_t)15;
	void *p;
	if (!a || tmpused + a > sizeof(tmpbuf))
		return NULL;
	p = tmpbuf + tmpused;
	tmpused += a;
	return p;
}

static void resolve_alloc(const char *name, void **slot)
{
	if (*slot || resolving)
		return;
	resolving = 1;
	*slot = dlsym(RTLD_NEXT, name);
	resolving = 0;
}

static int in_log;
static int in_free_log;

static void log_alloc(const char *op, void *ptr, size_t size)
{
	if (in_log || in_free_log)
		return;
	in_log = 1;
	ensure_log();
	if (logf && !umd_trace_would_exceed(512))
		fprintf(logf, "{\"seq\":0,\"op\":\"%s\",\"ptr\":\"%p\","
			"\"size\":%zu}\n", op, ptr, size);
	in_log = 0;
}

void free(void *ptr)
{
	static int busy;
	if (!getenv("UMD_ALLOC_WRAP")) {
		if (!real_free)
			real_free = dlsym(RTLD_NEXT, "free");
		if (real_free)
			real_free(ptr);
		return;
	}
	if (!real_free) {
		resolve_alloc("free", (void **)&real_free);
		if (!real_free)
			return;
	}
	if (getenv("UMD_LOG_FREE") && !busy && !in_log) {
		void *bt[6];
		int n, i;
		busy = 1;
		in_free_log = 1;
		pthread_mutex_lock(&log_lock);
		ensure_log();
		resolve_umd_slide();
		if (logf) {
			fprintf(logf, "{\"seq\":0,\"op\":\"free\",\"ptr\":\"%p\","
				"\"bt\":[", ptr);
			n = backtrace(bt, 6);
			for (i = 0; i < n; i++) {
				unsigned long a = (unsigned long)bt[i];
				fprintf(logf, "%s\"0x%lx\"", i ? "," : "",
					umd_slide && a >= umd_slide
						? a - umd_slide : a);
			}
			fprintf(logf, "]}\n");
		}
		pthread_mutex_unlock(&log_lock);
		in_free_log = 0;
		busy = 0;
	}
	real_free(ptr);
}

void *malloc(size_t size)
{
	void *p;
	if (!getenv("UMD_ALLOC_WRAP")) {
		resolve_alloc("malloc", (void **)&real_malloc);
		return real_malloc ? real_malloc(size) : NULL;
	}
	resolve_alloc("malloc", (void **)&real_malloc);
	if (!real_malloc)
		return tmp_alloc(size);
	p = real_malloc(size);
	if (getenv("UMD_LOG_ALLOC"))
		log_alloc("malloc", p, size);
	return p;
}

void *calloc(size_t n, size_t size)
{
	void *p;
	if (!getenv("UMD_ALLOC_WRAP")) {
		resolve_alloc("calloc", (void **)&real_calloc);
		return real_calloc ? real_calloc(n, size) : NULL;
	}
	resolve_alloc("calloc", (void **)&real_calloc);
	if (!real_calloc) {
		p = tmp_alloc(n * size);
		if (p)
			memset(p, 0, n * size);
		return p;
	}
	p = real_calloc(n, size);
	if (getenv("UMD_LOG_ALLOC"))
		log_alloc("calloc", p, n * size);
	return p;
}

void *realloc(void *ptr, size_t size)
{
	void *p;
	if (!getenv("UMD_ALLOC_WRAP")) {
		resolve_alloc("realloc", (void **)&real_realloc);
		return real_realloc ? real_realloc(ptr, size) : NULL;
	}
	resolve_alloc("realloc", (void **)&real_realloc);
	if (!real_realloc) {
		p = tmp_alloc(size);
		if (p && ptr)
			memcpy(p, ptr, size);
		return p;
	}
	p = real_realloc(ptr, size);
	if (getenv("UMD_LOG_ALLOC"))
		log_alloc("realloc", p, size);
	return p;
}

/* Path probes (record-only mode). Each forwards unchanged and, in
 * passthrough mode, logs /dev/dri paths. glibc 2.41 exports stat/stat64 directly
 * and keeps __xstat/__xstat64 as compat symbols, and the decompiled UMD calls
 * __xstat64, so all four spellings are covered.
 */
static int probe_log_path(const char *op, const char *path, int ret)
{
	if (pvr_passthrough())
		log_path_probe(op, path, ret);
	return ret;
}

int stat(const char *path, struct stat *st)
{
	static int (*real)(const char *, struct stat *);
	int r;

	if (!real) {
		if (resolving_stat())
			return -1;
		set_resolving_stat(1);
		real = dlsym(RTLD_NEXT, "stat");
		set_resolving_stat(0);
		if (!real)
			return -1;
	}
	r = real(path, st);
	return probe_log_path("stat", path, r);
}

int stat64(const char *path, struct stat64 *st)
{
	static int (*real)(const char *, struct stat64 *);
	int r;

	if (!real) {
		if (resolving_stat())
			return -1;
		set_resolving_stat(1);
		real = dlsym(RTLD_NEXT, "stat64");
		set_resolving_stat(0);
		if (!real)
			return -1;
	}
	r = real(path, st);
	return probe_log_path("stat64", path, r);
}

int statx(int dirfd, const char *path, int flags, unsigned int mask,
	  struct statx *stx)
{
	static int (*real)(int, const char *, int, unsigned int,
			   struct statx *);
	int r;

	if (!real) {
		if (resolving_stat())
			return -1;
		set_resolving_stat(1);
		real = dlsym(RTLD_NEXT, "statx");
		set_resolving_stat(0);
		if (!real)
			return -1;
	}
	r = real(dirfd, path, flags, mask, stx);
	return probe_log_path("statx", path, r);
}

int access(const char *path, int mode)
{
	static int (*real)(const char *, int);
	int r;

	if (!real) {
		if (resolving_stat())
			return -1;
		set_resolving_stat(1);
		real = dlsym(RTLD_NEXT, "access");
		set_resolving_stat(0);
		if (!real)
			return -1;
	}
	r = real(path, mode);
	return probe_log_path("access", path, r);
}
