#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../mux/fh8626_mp4_sample.h"
static size_t add(uint8_t*b,size_t off,uint8_t type,uint8_t val){b[off]=0;b[off+1]=0;b[off+2]=0;b[off+3]=1;b[off+4]=(uint8_t)(0x60|type);b[off+5]=val;return 6;}
int main(void){uint8_t au_buf[256]={0},out_buf[512]={0};struct fh_h264_au_snapshot au;memset(&au,0,sizeof(au));au.bytes=au_buf;au.capacity=sizeof(au_buf);au.pts=123;au.generation=9;size_t off=0;size_t i;
 au.entry_count=14;au.span[0].offset=off;au.span[0].len=add(au_buf,off,7,1);off+=au.span[0].len;au.span[1].offset=off;au.span[1].len=add(au_buf,off,8,2);off+=au.span[1].len;
 for(i=2;i<14;i++){au.span[i].offset=off;au.span[i].len=add(au_buf,off,(i==2)?5:1,(uint8_t)i);off+=au.span[i].len;}au.size=off;
 struct fh_mp4_sample_snapshot s;memset(&s,0,sizeof(s));s.bytes=out_buf;s.capacity=sizeof(out_buf);assert(fh_mp4_sample_from_au(&au,&s)==1&&s.valid&&s.key&&s.source_generation==9&&s.sps_len==2&&s.pps_len==2);
 uint8_t keep=out_buf[0];au_buf[au.span[13].offset+0]=9;au_buf[au.span[13].offset+2]=9;assert(fh_mp4_sample_from_au(&au,&s)==-EPROTO);assert(out_buf[0]==keep);
 au.entry_count=2;au.size=au.span[1].offset+au.span[1].len;assert(fh_mp4_sample_from_au(&au,&s)==0&&!s.valid);
 puts("test_mp4_sample: PASS");return 0;}
