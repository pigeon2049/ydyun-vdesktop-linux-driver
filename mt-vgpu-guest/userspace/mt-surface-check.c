// SPDX-License-Identifier: GPL-2.0
#include "mt-drm-check-common.h"
#define FRAME_BYTES (8U*1024U*1024U)
#define TILE_BYTES (4U*1024U*1024U)
static unsigned char *frame_expected,*frame_read;
static unsigned int fills,copies;
static unsigned int sized_create(int fd,unsigned int n)
{
 struct drm_mt_create r={.bytes=n};
 REQUIRE(ioctl(fd,DRM_IOCTL_MT_CREATE,&r)==0 && r.bytes==n && r.handle);
 return r.handle;
}
static void read_bytes(int fd,unsigned int h,unsigned char *data,unsigned int bytes)
{
 for(unsigned int off=0;off<bytes;off+=4096) {
  struct drm_mt_rw r={.handle=h,.offset=off,.bytes=bytes-off<4096?bytes-off:4096};
  REQUIRE(ioctl(fd,DRM_IOCTL_MT_READ,&r)==0);memcpy(data+off,r.data,r.bytes);
 }
}
static void check_equal(const void *expected_data,const void *observed_data,unsigned int n)
{
 if(memcmp(expected_data,observed_data,n)) {
  const unsigned char *a=expected_data,*b=observed_data;
  for(unsigned int i=0;i<n;i++) if(a[i]!=b[i]) {
   fprintf(stderr,"pixel/guard mismatch offset=%u expected=%02x got=%02x\n",i,a[i],b[i]);break;
  }
  REQUIRE(0);
 }
}
static void rectangle(int fd,unsigned int h,unsigned int sync,unsigned int width,unsigned int height,
 unsigned int x,unsigned int y,unsigned int w,unsigned int rh,unsigned int off,unsigned int color)
{
 struct drm_mt_fill r={.destination=h,.out_syncobj=sync,.width=width,.height=height,
  .x=x,.y=y,.rect_width=w,.rect_height=rh,.offset=off,.color=color};
 REQUIRE(ioctl(fd,DRM_IOCTL_MT_FILL,&r)==0 && r.sequence);verify_fence(fd,sync);
 for(unsigned int row=y;row<y+rh;row++)
  for(unsigned int col=x;col<x+w;col++) memcpy(frame_expected+off+(row*width+col)*4,&color,4);
 read_bytes(fd,h,frame_read,FRAME_BYTES);check_equal(frame_expected,frame_read,FRAME_BYTES);fills++;
 printf("{\"native_fill\":true,\"width\":%u,\"height\":%u,\"x\":%u,\"y\":%u,\"rect_width\":%u,\"rect_height\":%u,\"offset\":%u,\"color\":%u,\"sequence\":%"PRIu64",\"verified_bytes\":%u,\"native_gpu_sync_file\":true}\n",
  width,height,x,y,w,rh,off,color,(uint64_t)r.sequence,FRAME_BYTES);fflush(stdout);
}
static void copy_tile(int fd,unsigned int frame,unsigned int tile,unsigned int sync,
 unsigned int offset,unsigned int n)
{
 struct drm_mt_copy c={.source=frame,.destination=tile,.source_offset=offset,.bytes=n,.out_syncobj=sync};
 REQUIRE(ioctl(fd,DRM_IOCTL_MT_COPY,&c)==0 && c.sequence);verify_fence(fd,sync);
 read_bytes(fd,tile,frame_read,TILE_BYTES);check_equal(frame_expected+offset,frame_read,n);
 /* A freshly leased tile is zero outside the copied interval. */
 for(unsigned int i=n;i<TILE_BYTES;i++) REQUIRE(frame_read[i]==0);
 read_bytes(fd,frame,frame_read,FRAME_BYTES);check_equal(frame_expected,frame_read,FRAME_BYTES);copies++;
 printf("{\"copy_bytes\":%u,\"source_offset\":%u,\"sequence\":%"PRIu64",\"source_and_guards\":true,\"native_gpu_sync_file\":true}\n",n,offset,(uint64_t)c.sequence);fflush(stdout);
}
int main(int argc,char **argv)
{
 REQUIRE(argc>=3 && (!strcmp(argv[2],"smoke") || (!strcmp(argv[2],"exercise") && argc==4)));
 int fd=open(argv[1],O_RDWR|O_CLOEXEC);REQUIRE(fd>=0);
 struct drm_mt_query before,after;query(fd,&before);REQUIRE(before.slot_bytes>=FRAME_BYTES && (before.capabilities&MT_DRM_CAP_FILL));
 frame_expected=calloc(1,FRAME_BYTES);frame_read=malloc(FRAME_BYTES);REQUIRE(frame_expected && frame_read);
 /* Reserve the largest objects, then prove all six small slots remain usable.
  * Exact-fit selection protects large slots when small requests arrive first. */
 unsigned int small[6];for(unsigned int i=0;i<6;i++) small[i]=create(fd);
 unsigned int h=sized_create(fd,FRAME_BYTES),tile=sized_create(fd,TILE_BYTES);
 struct drm_mt_create full={.bytes=4096};errno=0;
 REQUIRE(ioctl(fd,DRM_IOCTL_MT_CREATE,&full)==-1 && errno==ENOSPC);
 for(unsigned int i=0;i<6;i++) close_handle(fd,small[i]);
 struct drm_syncobj_create s={0};REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_CREATE,&s)==0);
 read_bytes(fd,h,frame_read,FRAME_BYTES);check_equal(frame_expected,frame_read,FRAME_BYTES);
 struct drm_mt_fill invalid={.destination=h,.out_syncobj=s.handle,.width=1920,.height=1080,
  .rect_width=1920,.rect_height=1080,.offset=FRAME_BYTES-8294400+4};errno=0;
 REQUIRE(ioctl(fd,DRM_IOCTL_MT_FILL,&invalid)==-1 && errno==EINVAL);
 query(fd,&after);REQUIRE(after.submitted==before.submitted);
 if(!strcmp(argv[2],"smoke")) {
  /* Cross a 2 MiB PTE-table boundary, then the final physical page. */
  rectangle(fd,h,s.handle,1024,2048,1020,510,4,4,0,0xff13579b);
  rectangle(fd,h,s.handle,1,1,0,0,1,1,FRAME_BYTES-4,0xffabcdef);
  copy_tile(fd,h,tile,s.handle,0,TILE_BYTES);
 } else {
  rectangle(fd,h,s.handle,1920,1080,0,0,1920,1080,0,0xff101b32);
  rectangle(fd,h,s.handle,1920,1080,96,72,1728,48,0,0xffe6edf7);
  const unsigned int colors[]={0xff49dca5,0xff5794ff,0xffffbd59};
  for(unsigned int i=0;i<3;i++) rectangle(fd,h,s.handle,1920,1080,96+i*600,210,528,420,0,colors[i]);
  for(unsigned int i=0;i<16;i++) rectangle(fd,h,s.handle,1920,1080,96+i*108,735,84,84,0,i&1?0xffe6edf7:0xff5794ff);
  rectangle(fd,h,s.handle,1920,1080,96,924,1728,60,0,0xff49dca5);
  rectangle(fd,h,s.handle,1920,1080,1919,1079,1,1,0,0xfff05070);
  copy_tile(fd,h,tile,s.handle,0,TILE_BYTES);
  close_handle(fd,tile);tile=sized_create(fd,TILE_BYTES);
  copy_tile(fd,h,tile,s.handle,TILE_BYTES,8294400-TILE_BYTES);
  int out=open(argv[3],O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);REQUIRE(out>=0);
  FILE *f=fdopen(out,"wb");REQUIRE(f!=NULL && fprintf(f,"P6\n1920 1080\n255\n")>0);
  /* frame_read is actual VRAM readback after the final copy, never a CPU
   * raster uploaded to the device. No WRITE ioctl is used by this program. */
  for(unsigned int i=0;i<1920*1080;i++) {
   unsigned char rgb[]={frame_read[i*4+2],frame_read[i*4+1],frame_read[i*4]};
   REQUIRE(fwrite(rgb,1,3,f)==3);
  }
  REQUIRE(fclose(f)==0);
 }
 close_handle(fd,h);close_handle(fd,tile);
 /* All physical bytes are cleared when the 8 MiB slot is leased again. */
 h=sized_create(fd,FRAME_BYTES);read_bytes(fd,h,frame_read,FRAME_BYTES);
 memset(frame_expected,0,FRAME_BYTES);check_equal(frame_expected,frame_read,FRAME_BYTES);close_handle(fd,h);
 struct drm_syncobj_destroy destroy={.handle=s.handle};REQUIRE(ioctl(fd,DRM_IOCTL_SYNCOBJ_DESTROY,&destroy)==0);
 query(fd,&after);REQUIRE(after.leased==before.leased && after.completed==before.completed+fills+copies && after.submitted==after.completed);
 printf("{\"fill_cases\":%u,\"copy_cases\":%u,\"invalid_cases\":2,\"lease_clear_bytes\":%u}\n",fills,copies,FRAME_BYTES);show_query(&after);
 free(frame_expected);free(frame_read);close(fd);
 (void)one_copy;(void)buffer_io;(void)source;(void)expected;(void)observed;
 return 0;
}
