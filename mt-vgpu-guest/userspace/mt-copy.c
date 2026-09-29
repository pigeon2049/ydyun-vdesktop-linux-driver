// SPDX-License-Identifier: GPL-2.0
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../include/mt_copy_uapi.h"

_Static_assert(sizeof(struct mt_copy_request) == 8232, "request ABI");
_Static_assert(sizeof(struct mt_copy_query) == 48, "query ABI");

static void die(const char *what)
{
	perror(what);
	exit(1);
}
static void write_all(int fd, const void *data, size_t bytes)
{
	const unsigned char *p = data;
	while (bytes) {
		ssize_t n = write(fd, p, bytes);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			die("write destination");
		p += n;
		bytes -= (size_t)n;
	}
}
int main(int argc, char **argv)
{
	struct mt_copy_query q = {0};
	struct mt_copy_request r;
	uint64_t total = 0, jobs = 0;
	int fd, in, out;
	if (!((argc == 2 && !strcmp(argv[1], "query")) ||
	      (argc == 4 && !strcmp(argv[1], "copy")))) {
		fprintf(stderr, "Usage: %s query | copy INPUT NEW_OUTPUT\n", argv[0]);
		return 2;
	}
	fd = open("/dev/mt-vgpu-copy", O_RDWR | O_CLOEXEC);
	if (fd < 0)
		die("open /dev/mt-vgpu-copy (requires root/CAP_SYS_RAWIO)");
	if (ioctl(fd, MT_COPY_QUERY, &q) < 0)
		die("MT_COPY_QUERY");
	if (q.abi != MT_COPY_ABI || q.max_bytes != MT_COPY_PAGE_BYTES || q.faulted) {
		fprintf(stderr, "Unsupported or faulted copy bridge\n");
		return 1;
	}
	if (argc == 2) {
		printf("{\"abi\":%u,\"max_bytes\":%u,\"capabilities\":%u,\"faulted\":%u,"
		       "\"submitted\":%" PRIu64 ",\"completed\":%" PRIu64 ",\"last_sequence\":%" PRIu64 "}\n",
		       q.abi, q.max_bytes, q.capabilities, q.faulted,
		       (uint64_t)q.submitted, (uint64_t)q.completed, (uint64_t)q.last_sequence);
		close(fd);
		return 0;
	}
	in = open(argv[2], O_RDONLY | O_CLOEXEC);
	if (in < 0)
		die("open input");
	/* Do not truncate or overwrite an existing file. */
	out = open(argv[3], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (out < 0)
		die("create output");
	for (;;) {
		size_t used = 0;
		memset(&r, 0, sizeof(r));
		r.abi = MT_COPY_ABI;
		memset(r.destination, 0xa5, sizeof(r.destination));
		while (used < sizeof(r.source)) {
			ssize_t n = read(in, r.source + used, sizeof(r.source) - used);
			if (n < 0 && errno == EINTR)
				continue;
			if (n < 0)
				die("read input");
			if (!n)
				break;
			used += (size_t)n;
		}
		if (!used)
			break;
		r.bytes = (unsigned int)used;
		if (ioctl(fd, MT_COPY_EXEC, &r) < 0)
			die("GPU copy (output may be partial)");
		if (!r.sequence || memcmp(r.source, r.destination, used)) {
			fprintf(stderr, "GPU copy verification failed; output may be partial\n");
			return 1;
		}
		write_all(out, r.destination, used);
		total += used;
		jobs++;
	}
	if (fsync(out) || close(out))
		die("finish output");
	close(in);
	close(fd);
	printf("{\"bytes\":%" PRIu64 ",\"gpu_jobs\":%" PRIu64 "}\n", total, jobs);
	return 0;
}
