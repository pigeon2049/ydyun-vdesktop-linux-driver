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
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

/* Canned outputs that carry the UMD past early init checks. Each entry is
 * matched on (bridge_id, func_id); bytes are written to the start of the
 * output buffer (which was zeroed first). Grown iteratively from traces. */
struct canned_out {
	uint32_t bridge_id;
	uint32_t func_id;
	uint8_t bytes[32];
	uint32_t size;
};

static const struct canned_out canned[] = {
	/* SRVCORE:ACQUIREINFOPAGE -> nonzero info-page PMR handle. */
	{0x1, 0xf,
	 {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 12},
	/* MM:PMRLOCALIMPORTPMR -> align/size + local PMR handle. */
	{0x6, 0x6,
	 {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00},
	 28},
};

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

static int is_dri(const char *path)
{
	return path && strncmp(path, "/dev/dri/", 9) == 0;
}

static int devnull(void)
{
	if (nullfd < 0)
		nullfd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/null",
				      O_RDWR | O_CLOEXEC, 0);
	return nullfd;
}

static void log_hex(const char *tag, const void *ptr, uint32_t size)
{
	const unsigned char *b = ptr;
	uint32_t n = size > MAX_BYTES ? MAX_BYTES : size;
	uint32_t i;
	if (!logf)
		return;
	fprintf(logf, "\"%s\":\"", tag);
	for (i = 0; i < n; i++)
		fprintf(logf, "%02x", b[i]);
	fprintf(logf, "\"");
	if (size > MAX_BYTES)
		fprintf(logf, ",\"%s_truncated\":%u", tag, size);
}

static int dri_open(const char *path)
{
	int fd;
	ensure_log();
	fd = (int)syscall(SYS_dup, devnull());
	if (logf)
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
	if (is_dri(path))
		return dri_open(path);
	return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
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
	if (is_dri(path))
		return dri_open(path);
	return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
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
	if (path[0] == '/' && is_dri(path))
		return dri_open(path);
	return (int)syscall(SYS_openat, dirfd, path, flags, mode);
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
	if (path[0] == '/' && is_dri(path))
		return dri_open(path);
	return (int)syscall(SYS_openat, dirfd, path, flags, mode);
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
			if (logf)
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
		if (logf)
			fprintf(logf, "{\"seq\":%lu,\"op\":\"version_claim\","
				"\"fd\":%d,\"name\":\"mtgpu\",\"ret\":0}\n",
				++seq, fd);
		return 0;
	}

	if ((req == SRVKM_CMD || req == SRVKM_INIT) && arg) {
		if (req == SRVKM_INIT) {
			if (logf)
				fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl\",\"fd\":%d,"
					"\"req\":\"0x%lx\",\"init_module\":%u,\"ret\":0}\n",
					++seq, fd, req, *(uint32_t *)arg);
			return 0;
		}
		memcpy(&cmd, arg, sizeof(cmd));
		if (logf) {
			fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl\",\"fd\":%d,"
				"\"bridge\":\"0x%x:0x%x\",\"in_size\":%u,\"out_size\":%u,",
				++seq, fd, cmd.bridge_id, cmd.bridge_func_id,
				cmd.in_size, cmd.out_size);
			if (cmd.in_ptr && cmd.in_size)
				log_hex("in", (const void *)(uintptr_t)cmd.in_ptr,
					cmd.in_size);
			else
				fprintf(logf, "\"in\":null");
			fprintf(logf, ",\"fabricated_zero_out\":%u,\"ret\":0}\n",
				cmd.out_ptr && cmd.out_size ? cmd.out_size : 0);
		}
		if (cmd.out_ptr && cmd.out_size) {
			uint32_t n = cmd.out_size > 4096 ? 4096 : cmd.out_size;
			memset((void *)(uintptr_t)cmd.out_ptr, 0, n);
			apply_canned(cmd.bridge_id, cmd.bridge_func_id,
				     (void *)(uintptr_t)cmd.out_ptr, n);
		}
		return 0;
	}

	ret = syscall(SYS_ioctl, fd, req, arg);
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"ioctl_passthrough\",\"fd\":%d,"
			"\"req\":\"0x%lx\",\"ret\":%ld}\n", ++seq, fd, req, ret);
	return (int)ret;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *ret = (void *)syscall(SYS_mmap, addr, len, prot, flags, fd, off);
	ensure_log();
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap\",\"fd\":%d,"
			"\"len\":%zu,\"off\":\"0x%lx\",\"ret\":\"%p\"}\n",
			++seq, fd, len, (unsigned long)off, ret);
	return ret;
}

void *mmap64(void *addr, size_t len, int prot, int flags, int fd, off64_t off)
{
	void *ret = (void *)syscall(SYS_mmap, addr, len, prot, flags, fd, off);
	ensure_log();
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"mmap\",\"fd\":%d,"
			"\"len\":%zu,\"off\":\"0x%llx\",\"ret\":\"%p\"}\n",
			++seq, fd, len, (unsigned long long)off, ret);
	return ret;
}

int munmap(void *addr, size_t len)
{
	int ret = (int)syscall(SYS_munmap, addr, len);
	ensure_log();
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"munmap\",\"addr\":\"%p\","
			"\"len\":%zu,\"ret\":%d}\n", ++seq, addr, len, ret);
	return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
	ssize_t ret = syscall(SYS_read, fd, buf, count);
	ensure_log();
	if (logf && (fd > 2 || ret > 0))
		fprintf(logf, "{\"seq\":%lu,\"op\":\"read\",\"fd\":%d,"
			"\"count\":%zu,\"ret\":%zd}\n",
			++seq, fd, count, ret);
	return ret;
}

ssize_t pread(int fd, void *buf, size_t count, off_t off)
{
	ssize_t ret = syscall(SYS_pread64, fd, buf, count, off);
	ensure_log();
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"pread\",\"fd\":%d,"
			"\"count\":%zu,\"off\":\"0x%lx\",\"ret\":%zd}\n",
			++seq, fd, count, (unsigned long)off, ret);
	return ret;
}

ssize_t pread64(int fd, void *buf, size_t count, off64_t off)
{
	ssize_t ret = syscall(SYS_pread64, fd, buf, count, off);
	ensure_log();
	if (logf)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"pread\",\"fd\":%d,"
			"\"count\":%zu,\"off\":\"0x%llx\",\"ret\":%zd}\n",
			++seq, fd, count, (unsigned long long)off, ret);
	return ret;
}

off_t lseek(int fd, off_t off, int whence)
{
	off_t ret = (off_t)syscall(SYS_lseek, fd, off, whence);
	ensure_log();
	if (logf && fd > 2)
		fprintf(logf, "{\"seq\":%lu,\"op\":\"lseek\",\"fd\":%d,"
			"\"off\":\"0x%lx\",\"whence\":%d,\"ret\":\"0x%lx\"}\n",
			++seq, fd, (unsigned long)off, whence,
			(unsigned long)ret);
	return ret;
}
