// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../include/mt_copy_uapi.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s (errno=%d)\n", __LINE__, #x, errno); exit(1); } } while (0)
static void query(int fd, struct mt_copy_query *q)
{
	memset(q, 0, sizeof(*q));
	REQUIRE(ioctl(fd, MT_COPY_QUERY, q) == 0);
	REQUIRE(q->abi == MT_COPY_ABI && q->faulted == 0);
}
static void init_request(struct mt_copy_request *r, unsigned int seed)
{
	memset(r, 0, sizeof(*r));
	r->abi = MT_COPY_ABI;
	r->bytes = 257;
	r->source_offset = 3;
	r->destination_offset = 17;
	for (size_t i = 0; i < sizeof(r->source); i++) {
		r->source[i] = (unsigned char)((i * 73 + seed * 37) ^ (i >> 3));
		r->destination[i] = (unsigned char)(0xa5 ^ (seed * 13));
	}
}
static void expect_error(int fd, unsigned long cmd, void *ptr, int want)
{
	errno = 0;
	REQUIRE(ioctl(fd, cmd, ptr) == -1 && errno == want);
}
int main(void)
{
	struct mt_copy_request r;
	struct mt_copy_query before, after;
	uint64_t sequences[32];
	int fd = open("/dev/mt-vgpu-copy", O_RDWR | O_CLOEXEC);
	int output[2], gate[2];
	pid_t kids[4];
	unsigned int rejected = 0;
	REQUIRE(fd >= 0);
	query(fd, &before);
	for (unsigned int i = 0; i < 10; i++) {
		init_request(&r, 1);
		switch (i) {
		case 0: r.abi++; break;
		case 1: r.flags = 1; break;
		case 2: r.reserved[0] = 1; break;
		case 3: r.sequence = 1; break;
		case 4: r.bytes = 0; break;
		case 5: r.bytes = 4097; break;
		case 6: r.source_offset = 4096; break;
		case 7: r.destination_offset = 0xffffffffU; break;
		case 8: r.bytes = 0xffffffffU; break;
		case 9: r.source_offset = 4095; r.bytes = 2; break;
		}
		expect_error(fd, MT_COPY_EXEC, &r, EINVAL);
		rejected++;
	}
	expect_error(fd, MT_COPY_EXEC ^ (1UL << _IOC_SIZESHIFT), &r, ENOTTY); rejected++;
	expect_error(fd, MT_COPY_EXEC, (void *)1, EFAULT); rejected++;
	query(fd, &after);
	REQUIRE(!memcmp(&before, &after, sizeof(before)));
	/* A transferred/inherited descriptor does not bypass current credentials. */
	pid_t child = fork();
	REQUIRE(child >= 0);
	if (!child) {
		REQUIRE(setgid(65534) == 0 && setuid(65534) == 0);
		expect_error(fd, MT_COPY_QUERY, &after, EPERM);
		init_request(&r, 2);
		expect_error(fd, MT_COPY_EXEC, &r, EPERM);
		_exit(0);
	}
	int status;
	REQUIRE(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	rejected += 2;
	REQUIRE(pipe(output) == 0 && pipe(gate) == 0);
	for (unsigned int c = 0; c < 4; c++) {
		kids[c] = fork();
		REQUIRE(kids[c] >= 0);
		if (!kids[c]) {
			uint64_t receipt[8];
			unsigned char expected[4096], source[4096];
			char token;
			close(output[0]); close(gate[1]); close(fd);
			fd = open("/dev/mt-vgpu-copy", O_RDWR | O_CLOEXEC);
			REQUIRE(fd >= 0 && read(gate[0], &token, 1) == 1);
			for (unsigned int i = 0; i < 8; i++) {
				init_request(&r, 100 + c * 8 + i);
				if (i == 0) { r.bytes = 4096; r.source_offset = 0; r.destination_offset = 0; }
				if (i == 1) { r.bytes = 7; r.source_offset = 4089; r.destination_offset = 4089; }
				memcpy(source, r.source, sizeof(source));
				memcpy(expected, r.destination, sizeof(expected));
				memcpy(expected + r.destination_offset, source + r.source_offset, r.bytes);
				REQUIRE(ioctl(fd, MT_COPY_EXEC, &r) == 0);
				REQUIRE(r.sequence && !memcmp(r.source, source, sizeof(source)) &&
					!memcmp(r.destination, expected, sizeof(expected)));
				receipt[i] = r.sequence;
			}
			REQUIRE(write(output[1], receipt, sizeof(receipt)) == (ssize_t)sizeof(receipt));
			close(fd);
			_exit(0);
		}
	}
	close(gate[0]); close(output[1]);
	REQUIRE(write(gate[1], "1234", 4) == 4);
	close(gate[1]);
	size_t read_bytes = 0;
	while (read_bytes < sizeof(sequences)) {
		ssize_t n = read(output[0], (unsigned char *)sequences + read_bytes, sizeof(sequences) - read_bytes);
		REQUIRE(n > 0);
		read_bytes += (size_t)n;
	}
	close(output[0]);
	for (unsigned int c = 0; c < 4; c++)
		REQUIRE(waitpid(kids[c], &status, 0) == kids[c] && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	query(fd, &after);
	REQUIRE(after.submitted == before.submitted + 32 && after.completed == before.completed + 32);
	for (unsigned int i = 0; i < 32; i++) {
		REQUIRE(sequences[i] > after.last_sequence - 32 && sequences[i] <= after.last_sequence);
		for (unsigned int j = 0; j < i; j++) REQUIRE(sequences[i] != sequences[j]);
	}
	/* Readable but non-writable output: job completes, copyout reports EFAULT.
	 * The query must retain the completed count; no automatic retry occurs. */
	void *readonly = mmap(NULL, 12288, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	REQUIRE(readonly != MAP_FAILED);
	init_request(readonly, 999);
	REQUIRE(mprotect(readonly, 12288, PROT_READ) == 0);
	expect_error(fd, MT_COPY_EXEC, readonly, EFAULT);
	REQUIRE(munmap(readonly, 12288) == 0);
	query(fd, &after);
	REQUIRE(after.submitted == before.submitted + 33 && after.completed == before.completed + 33);
	close(fd);
	printf("{\"invalid_or_unauthorized_rejected\":%u,\"concurrent_processes\":4,"
	       "\"verified_copies\":32,\"copyout_fault_after_gpu_completion\":true,"
	       "\"submitted_delta\":33,\"completed_delta\":33,\"last_sequence\":%" PRIu64 "}\n",
	       rejected, (uint64_t)after.last_sequence);
	return 0;
}
