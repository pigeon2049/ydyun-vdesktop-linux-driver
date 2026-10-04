// SPDX-License-Identifier: GPL-2.0
#include "mt-drm-check-common.h"
#include <stddef.h>
_Static_assert(sizeof(struct drm_mt_fill)==56,"fill ABI");
struct rectangle { unsigned int width,height,x,y,w,h,offset,color; };
static unsigned int count;
static void fill(int fd,unsigned int handle,unsigned int sync,const struct rectangle *c)
{
	struct drm_mt_fill r={.destination=handle,.out_syncobj=sync,.offset=c->offset,
		.width=c->width,.height=c->height,.x=c->x,.y=c->y,.rect_width=c->w,.rect_height=c->h,.color=c->color};
	REQUIRE(ioctl(fd,DRM_IOCTL_MT_FILL,&r)==0 && r.sequence);
	verify_fence(fd,sync);
	for(unsigned int y=c->y;y<c->y+c->h;y++)
		for(unsigned int x=c->x;x<c->x+c->w;x++)
			memcpy(expected+c->offset+(y*c->width+x)*4,&c->color,4);
	buffer_io(fd,handle,observed,0);
	if(memcmp(expected,observed,65536)) {
		for(unsigned int i=0;i<65536;i++) if(expected[i]!=observed[i]) {
			fprintf(stderr,"fill mismatch byte %u expected=%02x observed=%02x\n",i,expected[i],observed[i]); break;
		}
		REQUIRE(0);
	}
	count++;
	printf("{\"native_fill\":true,\"width\":%u,\"height\":%u,\"x\":%u,\"y\":%u,\"rect_width\":%u,\"rect_height\":%u,\"offset\":%u,\"color\":%u,\"sequence\":%"PRIu64",\"data_and_guards\":true,\"native_gpu_sync_file\":true}\n",
		c->width,c->height,c->x,c->y,c->w,c->h,c->offset,c->color,(uint64_t)r.sequence);
	fflush(stdout);
}
static void optional_syncobj_fill(int fd,unsigned int handle)
{
	/* Positive counterpart of the removed bad[3]: 0-syncobj must execute,
	 * return a nonzero fence sequence, advance completed by exactly 1,
	 * and leave byte-exact pixels. Keeps expected[]/count bookkeeping
	 * so the later full-surface and fill-to-copy assertions still hold. */
	struct drm_mt_query before,after;
	query(fd,&before);
	const struct rectangle c={128,128,64,64,16,16,0,0xff0a0b0c};
	struct drm_mt_fill r={.destination=handle,.out_syncobj=0,.offset=c.offset,
		.width=c.width,.height=c.height,.x=c.x,.y=c.y,.rect_width=c.w,.rect_height=c.h,.color=c.color};
	REQUIRE(ioctl(fd,DRM_IOCTL_MT_FILL,&r)==0 && r.sequence);
	query(fd,&after);
	REQUIRE(after.completed==before.completed+1 && after.submitted==before.submitted+1 && !after.faulted);
	for(unsigned int y=c.y;y<c.y+c.h;y++)
		for(unsigned int x=c.x;x<c.x+c.w;x++)
			memcpy(expected+c.offset+(y*c.width+x)*4,&c.color,4);
	buffer_io(fd,handle,observed,0);
	REQUIRE(!memcmp(expected,observed,65536));
	count++;
	printf("{\"optional_syncobj_fill\":true,\"sequence\":%"PRIu64"}\n",(uint64_t)r.sequence);
	fflush(stdout);
}
static unsigned int invalid_cases(int fd,unsigned int handle,unsigned int sync)
{
	struct drm_mt_fill base={.destination=handle,.out_syncobj=sync,.width=16,.height=16,.rect_width=16,.rect_height=16};
	struct { size_t offset; uint32_t value; int error; } bad[]={
		{offsetof(struct drm_mt_fill,flags),1,EINVAL},
		{offsetof(struct drm_mt_fill,destination),0xffffffffU,ENOENT},
		{offsetof(struct drm_mt_fill,out_syncobj),0xffffffffU,ENOENT},
		/* NOTE: out_syncobj=0 is VALID (optional, executes without syncobj;
		 * r129/r130: driver `if (r->out_syncobj)` since r40, uapi documents
		 * Optional. Covered positively by optional_syncobj_fill() below,
		 * not by this reject-table. */
		{offsetof(struct drm_mt_fill,width),0,EINVAL},
		{offsetof(struct drm_mt_fill,height),32769,EINVAL},
		{offsetof(struct drm_mt_fill,x),16,EINVAL},
		{offsetof(struct drm_mt_fill,y),16,EINVAL},
		{offsetof(struct drm_mt_fill,rect_width),0,EINVAL},
		{offsetof(struct drm_mt_fill,rect_height),0xffffffffU,EINVAL},
		{offsetof(struct drm_mt_fill,offset),1,EINVAL},
		{offsetof(struct drm_mt_fill,offset),65532,EINVAL},
		{offsetof(struct drm_mt_fill,sequence),1,EINVAL},
	};
	for(unsigned int i=0;i<sizeof(bad)/sizeof(*bad);i++) {
		struct drm_mt_fill r=base;
		memcpy((unsigned char *)&r+bad[i].offset,&bad[i].value,4);
		errno=0; REQUIRE(ioctl(fd,DRM_IOCTL_MT_FILL,&r)==-1 && errno==bad[i].error);
	}
	return sizeof(bad)/sizeof(*bad);
}
int main(int argc,char **argv)
{
	REQUIRE(argc>=3 && (!strcmp(argv[2],"smoke")||!strcmp(argv[2],"exercise")||(!strcmp(argv[2],"demo")&&argc==4)));
	int fd=open(argv[1],O_RDWR|O_CLOEXEC); REQUIRE(fd>=0);
	struct drm_mt_query before,after;
	query(fd,&before); REQUIRE(before.capabilities&MT_DRM_CAP_FILL);
	unsigned int handle=create(fd),second=create(fd);
	struct drm_syncobj_create sync={0}; REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_CREATE,&sync)==0);
	unsigned int invalid=invalid_cases(fd,handle,sync.handle);
	query(fd,&after); REQUIRE(after.submitted==before.submitted && after.completed==before.completed);
	optional_syncobj_fill(fd,handle);
	if(strcmp(argv[2],"demo")) {
		const struct rectangle cases[]={
			{16,16,0,0,16,16,0,0xff123456},
			{128,128,0,0,128,128,0,0xffc01877},
			{128,128,3,5,17,19,0,0xff35ca92},
			{128,128,127,127,1,1,0,0x01020304},
			{67,109,1,3,65,103,4,0x89abcdef},
			{128,128,0,127,128,1,0,0x00000000},
			{128,128,127,0,1,128,0,0xffffffff},
			{1,1,0,0,1,1,65532,0xdeadbeef},
			{16384,1,8191,0,2,1,0,0x55aa33cc},
			{1,16384,0,8191,1,2,0,0x80818283},
		};
		unsigned int runs=!strcmp(argv[2],"smoke")?1:40;
		for(unsigned int i=0;i<runs;i++) {
			for(unsigned int j=0;j<65536;j++) expected[j]=(unsigned char)((j*73+i*29)^(j>>5));
			buffer_io(fd,handle,expected,1); /* Guard initialization only. */
			fill(fd,handle,sync.handle,&cases[i%(sizeof(cases)/sizeof(*cases))]);
		}
	} else {
		/* Created GEM is zero. Every visible pixel is generated by GPU fills. */
		memset(expected,0,sizeof(expected));
		struct rectangle r={128,128,0,0,128,128,0,0xff101b32};
		fill(fd,handle,sync.handle,&r);
		r=(struct rectangle){128,128,8,8,112,8,0,0xffe6edf7}; fill(fd,handle,sync.handle,&r);
		const unsigned int colors[]={0xff49dca5,0xff5794ff,0xffffbd59};
		for(unsigned int i=0;i<3;i++) {
			r=(struct rectangle){128,128,8+i*39,25,34,48,0,colors[i]}; fill(fd,handle,sync.handle,&r);
		}
		for(unsigned int i=0;i<8;i++) {
			r=(struct rectangle){128,128,8+i*14,84,12,12,0,i&1?0xffe6edf7:0xff5794ff}; fill(fd,handle,sync.handle,&r);
		}
		for(unsigned int i=0;i<8;i++) {
			r=(struct rectangle){128,128,8+i*14,104,12,16,0,0xff49dca5U+i*0x000a0000U}; fill(fd,handle,sync.handle,&r);
		}
	}
	/* Copy the actual GPU-filled surface into a distinct GEM with the same
	 * context, proving transitions from clear back to textured copy. */
	struct drm_mt_copy copy={.source=handle,.destination=second,.bytes=65536,.out_syncobj=sync.handle};
	REQUIRE(ioctl(fd,DRM_IOCTL_MT_COPY,&copy)==0 && copy.sequence); verify_fence(fd,sync.handle);
	buffer_io(fd,second,observed,0); REQUIRE(!memcmp(expected,observed,65536));
	if(!strcmp(argv[2],"demo")) {
		int output=open(argv[3],O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600); REQUIRE(output>=0);
		FILE *f=fdopen(output,"wb"); REQUIRE(f!=NULL);
		REQUIRE(fprintf(f,"P6\n128 128\n255\n")>0);
		for(unsigned int i=0;i<16384;i++) {
			unsigned char rgb[]={observed[i*4+2],observed[i*4+1],observed[i*4]};
			REQUIRE(fwrite(rgb,1,3,f)==3);
		}
		REQUIRE(fclose(f)==0);
	}
	close_handle(fd,handle); close_handle(fd,second);
	struct drm_syncobj_destroy destroy={.handle=sync.handle}; REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)==0);
	query(fd,&after);
	REQUIRE(after.leased==before.leased && after.completed==before.completed+count+1 && after.submitted==after.completed);
	printf("{\"fill_cases\":%u,\"invalid_cases\":%u,\"fill_to_copy\":true}\n",count,invalid);
	show_query(&after); close(fd);
	/* Keep the shared copy checker compiled into this tool as a callable
	 * diagnostic; no additional GPU job is performed here. */
	(void)one_copy;
	return 0;
}
