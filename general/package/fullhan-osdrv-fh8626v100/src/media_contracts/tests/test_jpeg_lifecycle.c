#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../jpeg/fh8626_jpeg_lifecycle.h"
#include "../jpeg/fh8626_jpeg_owned_image.h"
struct f {int fail_stop;int submits;};
static int ok(void*p){(void)p;return 0;} static int cfg(void*p,uint32_t m){(void)p;return m==2?0:-EINVAL;} static int sub(void*p,const struct fh_jpeg_yuv_submit_wire*w){struct f*f=p;f->submits++;return w->effective_w==w->width?0:-EINVAL;} static int stop(void*p){struct f*f=p;return f->fail_stop?-EIO:0;}
static void* rr(void*o,void*p,size_t n){int*fail=o;if(*fail)return NULL;return realloc(p,n);}
int main(void){struct f f={0};struct fh_jpeg_lifecycle_ops ops={ok,ok,cfg,ok,ok,stop,sub};struct fh_jpeg_lifecycle lc;struct fh_jpeg_yuv_submit_wire w;memset(&w,0,sizeof(w));w.mode=2;w.width=640;w.height=360;w.y_phys=0x1000;w.c_phys=0x2000;w.pts_lo=7;w.pts_hi=1;w.input_selector=1;
 fh_jpeg_lifecycle_init(&lc,&ops,&f);assert(fh_jpeg_lifecycle_mem_init(&lc)==0);assert(fh_jpeg_lifecycle_configure(&lc,2,0)==0);assert(fh_jpeg_lifecycle_apply_drop(&lc)==0);assert(fh_jpeg_lifecycle_start(&lc)==0);assert(fh_jpeg_lifecycle_submit(&lc,&w,NULL)==0&&lc.input_held);assert(fh_jpeg_lifecycle_stop(&lc)==0&&lc.input_held);assert(fh_jpeg_lifecycle_mem_uninit(&lc)==0&&!lc.input_held&&lc.vmm_release_ready);
 fh_jpeg_lifecycle_init(&lc,&ops,&f);assert(fh_jpeg_lifecycle_mem_init(&lc)==0);assert(fh_jpeg_lifecycle_configure(&lc,2,0)==0);assert(fh_jpeg_lifecycle_start(&lc)==0);f.fail_stop=1;assert(fh_jpeg_lifecycle_stop(&lc)==-EIO&&lc.state==FH_JPEG_LC_QUARANTINED&&!lc.vmm_release_ready);
 struct fh_jpeg_owned_image d={0};uint8_t b[4]={1,2,3,4};struct fh_jpeg_image_snapshot s={b,4,4,1,1,1,2,3,4};int fail=1;assert(fh_jpeg_owned_image_assign(&d,&s,rr,&fail)==-ENOMEM&&d.bytes==NULL);fail=0;assert(fh_jpeg_owned_image_assign(&d,&s,rr,&fail)==0&&d.size==4);free(d.bytes);
 puts("test_jpeg_lifecycle: PASS");return 0;}
