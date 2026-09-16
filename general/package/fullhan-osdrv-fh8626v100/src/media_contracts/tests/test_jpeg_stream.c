#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../jpeg/fh8626_jpeg_stream.h"
struct fake { struct fh_jpeg_query_wire q; int rel_rc, releases; };
static int mq(void *p,unsigned long r,void *a){struct fake*f=p;assert(r==FH_JPEG_MEDIA_QUERY_STREAM);*(struct fh_jpeg_query_wire*)a=f->q;return 0;}
static int jq(void *p,unsigned long r,void *a){struct fake*f=p;assert(r==FH_JPEG_RELEASE_STREAM);assert(*(uint32_t*)a==2);f->releases++;return f->rel_rc;}
int main(void){
 uint8_t ring[64]={0},dst[64]={0}; struct fake f; memset(&f,0,sizeof(f));
 ring[8]=0xff;ring[9]=0xd8;ring[10]=1;ring[11]=2;ring[12]=0xff;ring[13]=0xd9;
 f.q.returned_type=2;f.q.jpeg.mode=2;f.q.jpeg.width=640;f.q.jpeg.height=360;f.q.jpeg.phys=0x1008;f.q.jpeg.user=0x2008;f.q.jpeg.len=6;f.q.jpeg.pts_lo=5;f.q.jpeg.pts_hi=1;f.q.jpeg.qp=20;
 struct fh_jpeg_stream_owner o={mq,jq,&f,2,0,0}; struct fh_jpeg_stream_map m={0x1000,0x2000,sizeof(ring),ring}; struct fh_jpeg_image_snapshot s={dst,sizeof(dst),0,0,0,0,0,0,0};
 assert(fh_jpeg_stream_acquire(&o,&m,&s)==0 && s.size==6 && s.pts==0x100000005ULL);
 f.rel_rc=-EIO;assert(fh_jpeg_stream_release(&o)==-EIO && o.held);f.rel_rc=0;assert(fh_jpeg_stream_release(&o)==0 && !o.held);
 f.q.jpeg.qp=99;assert(fh_jpeg_stream_acquire(&o,&m,&s)<0 && !o.held);
 puts("test_jpeg_stream: PASS");return 0;}
