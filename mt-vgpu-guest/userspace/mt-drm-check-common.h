// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/sync_file.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../include/mt_drm_uapi.h"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "check failed line %d: %s errno=%d\n", __LINE__, #x, errno); exit(1); } } while (0)
_Static_assert(sizeof(struct drm_mt_query)==56, "query ABI");
_Static_assert(sizeof(struct drm_mt_copy)==48, "copy ABI");
_Static_assert(sizeof(struct drm_mt_rw)==4120, "rw ABI");
static unsigned char source[65536] __attribute__((unused)), expected[65536] __attribute__((unused)), observed[65536] __attribute__((unused));
static __attribute__((unused)) void query(int fd, struct drm_mt_query *q)
{
	memset(q, 0, sizeof(*q));
	REQUIRE(ioctl(fd, DRM_IOCTL_MT_QUERY, q)==0 && q->abi==MT_DRM_ABI && !q->faulted);
}
static __attribute__((unused)) void show_query(struct drm_mt_query *q)
{
	printf("{\"abi\":%u,\"slots\":%u,\"slot_bytes\":%u,\"leased\":%u,\"retained\":%u,\"faulted\":%u,\"submitted\":%"PRIu64",\"completed\":%"PRIu64",\"sequence\":%"PRIu64"}\n",
		q->abi,q->slot_count,q->slot_bytes,q->leased,q->retained,q->faulted,
		(uint64_t)q->submitted,(uint64_t)q->completed,(uint64_t)q->last_sequence);
}
static __attribute__((unused)) unsigned int create(int fd)
{
	struct drm_mt_create r={.bytes=65536};
	REQUIRE(ioctl(fd,DRM_IOCTL_MT_CREATE,&r)==0 && r.bytes==65536 && r.handle);
	return r.handle;
}
static __attribute__((unused)) void close_handle(int fd,unsigned int handle)
{
	struct drm_gem_close r={.handle=handle};
	REQUIRE(ioctl(fd,DRM_IOCTL_GEM_CLOSE,&r)==0);
}
static __attribute__((unused)) void buffer_io(int fd,unsigned int handle,unsigned char *bytes,int write)
{
	for(unsigned int off=0;off<65536;off+=4096) {
		struct drm_mt_rw r={.handle=handle,.offset=off,.bytes=4096};
		if(write) memcpy(r.data,bytes+off,4096);
		REQUIRE(ioctl(fd,write?DRM_IOCTL_MT_WRITE:DRM_IOCTL_MT_READ,&r)==0);
		if(!write) memcpy(bytes+off,r.data,4096);
	}
}
static __attribute__((unused)) void verify_fence(int fd,unsigned int handle)
{
	struct drm_syncobj_wait wait={.handles=(uintptr_t)&handle,.count_handles=1,.timeout_nsec=0};
	struct drm_syncobj_handle out={.handle=handle,.flags=DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE};
	struct sync_fence_info detail={0};
	struct sync_file_info info={.num_fences=1,.sync_fence_info=(uintptr_t)&detail};
	REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_WAIT,&wait)==0);
	REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD,&out)==0 && out.fd>=0);
	struct pollfd p={.fd=out.fd,.events=POLLIN};
	REQUIRE(poll(&p,1,0)==1 && (p.revents&POLLIN));
	REQUIRE(ioctl(out.fd,SYNC_IOC_FILE_INFO,&info)==0);
	REQUIRE(info.status==1 && info.num_fences==1 && detail.status==1 && detail.timestamp_ns);
	REQUIRE(!strcmp(detail.driver_name,"mt-vgpu-guest") && !strcmp(detail.obj_name,"firmware-submit"));
	close(out.fd);
}
static __attribute__((unused)) void one_copy(int fd,unsigned int src,unsigned int dst,unsigned int sync,unsigned int bytes,unsigned int so,unsigned int doff,unsigned int seed)
{
	for(unsigned int i=0;i<65536;i++) {
		source[i]=(unsigned char)((i*73+seed*37)^(i>>3)^(i>>11));
		expected[i]=(unsigned char)(0xa5^(seed*13));
	}
	buffer_io(fd,src,source,1);
	buffer_io(fd,dst,expected,1);
	memcpy(expected+doff,source+so,bytes);
	struct drm_mt_copy r={.source=src,.destination=dst,.bytes=bytes,
		.source_offset=so,.destination_offset=doff,.out_syncobj=sync};
	REQUIRE(ioctl(fd,DRM_IOCTL_MT_COPY,&r)==0 && r.sequence);
	verify_fence(fd,sync);
	buffer_io(fd,src,observed,0);
	REQUIRE(!memcmp(source,observed,65536));
	buffer_io(fd,dst,observed,0);
	REQUIRE(!memcmp(expected,observed,65536));
	printf("{\"copy_bytes\":%u,\"source_offset\":%u,\"destination_offset\":%u,\"sequence\":%"PRIu64",\"data_and_guards\":true,\"native_gpu_sync_file\":true}\n",bytes,so,doff,(uint64_t)r.sequence);
	fflush(stdout);
}
