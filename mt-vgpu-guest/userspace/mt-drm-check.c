// SPDX-License-Identifier: GPL-2.0
#include "mt-drm-check-common.h"
int main(int argc,char **argv)
{
	REQUIRE(argc==3 && (!strcmp(argv[2],"query")||!strcmp(argv[2],"smoke")||!strcmp(argv[2],"exercise")));
	int fd=open(argv[1],O_RDWR|O_CLOEXEC);
	REQUIRE(fd>=0);
	char name[32]={0};
	struct drm_version version={.name_len=sizeof(name)-1,.name=name};
	REQUIRE(ioctl(fd,DRM_IOCTL_VERSION,&version)==0 && !strcmp(name,"mtvgpu"));
	struct drm_get_cap cap={.capability=DRM_CAP_SYNCOBJ};
	REQUIRE(ioctl(fd,DRM_IOCTL_GET_CAP,&cap)==0 && cap.value==1);
	struct drm_mt_query before,after;
	query(fd,&before);
	if(!strcmp(argv[2],"query")) { show_query(&before); close(fd); return 0; }
	unsigned int handles[8];
	for(unsigned int i=0;i<8;i++) handles[i]=create(fd);
	struct drm_mt_create extra={.bytes=4096};
	errno=0; REQUIRE(ioctl(fd,DRM_IOCTL_MT_CREATE,&extra)==-1 && errno==ENOSPC);
	buffer_io(fd,handles[0],observed,0);
	for(unsigned int i=0;i<65536;i++) REQUIRE(observed[i]==0);
	int stranger=open(argv[1],O_RDWR|O_CLOEXEC);
	REQUIRE(stranger>=0);
	struct drm_mt_rw bad={.handle=handles[0],.bytes=1};
	errno=0; REQUIRE(ioctl(stranger,DRM_IOCTL_MT_READ,&bad)==-1 && errno==ENOENT);
	close(stranger);
	bad.offset=UINT64_MAX;
	errno=0; REQUIRE(ioctl(fd,DRM_IOCTL_MT_READ,&bad)==-1 && errno==EINVAL);
	struct drm_syncobj_create sync={0};
	REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_CREATE,&sync)==0 && sync.handle);
	struct drm_mt_copy invalid={.source=handles[0],.destination=handles[1],.bytes=1,.out_syncobj=sync.handle,.source_offset=UINT64_MAX};
	errno=0; REQUIRE(ioctl(fd,DRM_IOCTL_MT_COPY,&invalid)==-1 && errno==EINVAL);
	invalid.source_offset=0; invalid.out_syncobj=0xffffffffU;
	errno=0; REQUIRE(ioctl(fd,DRM_IOCTL_MT_COPY,&invalid)==-1 && errno==ENOENT);
	query(fd,&after);
	REQUIRE(after.submitted==before.submitted && after.completed==before.completed);
	unsigned int count=0;
	one_copy(fd,handles[0],handles[1],sync.handle,256,0,0,1); count++;
	if(!strcmp(argv[2],"exercise")) {
		one_copy(fd,handles[0],handles[1],sync.handle,65536,0,0,2); count++;
		one_copy(fd,handles[0],handles[1],sync.handle,32768,0,0,3); count++;
		one_copy(fd,handles[0],handles[1],sync.handle,8192,4096,8192,4); count++;
		one_copy(fd,handles[0],handles[1],sync.handle,8207,3,17,5); count++;
		one_copy(fd,handles[0],handles[1],sync.handle,4097,4095,12289,6); count++;
		one_copy(fd,handles[6],handles[7],sync.handle,31,65505,65505,7); count++;
	}
	/* Reuse a closed physical slot, verify it was fully cleared. */
	close_handle(fd,handles[0]);
	handles[0]=create(fd);
	buffer_io(fd,handles[0],observed,0);
	for(unsigned int i=0;i<65536;i++) REQUIRE(observed[i]==0);
	for(unsigned int i=0;i<8;i++) close_handle(fd,handles[i]);
	struct drm_syncobj_destroy destroy={.handle=sync.handle};
	REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)==0);
	query(fd,&after);
	REQUIRE(after.leased==before.leased && after.submitted==before.submitted+count && after.completed==before.completed+count);
	pid_t child=fork(); REQUIRE(child>=0);
	if(!child) {
		close(fd);
		fd=open(argv[1],O_RDWR|O_CLOEXEC); REQUIRE(fd>=0);
		(void)create(fd); (void)create(fd);
		_exit(0); /* File close must reclaim GEM leases without explicit close. */
	}
	int status;
	REQUIRE(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
	query(fd,&after); REQUIRE(after.leased==before.leased);
	show_query(&after);
	close(fd);
	return 0;
}
