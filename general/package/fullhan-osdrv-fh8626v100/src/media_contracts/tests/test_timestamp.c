#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "../timing/fh8626_timestamp.h"
int main(void){struct fh_timestamp_origin o;struct fh_rtmp_timestamp t;struct fh_timebase ms={1,1000};assert(fh_timestamp_origin_set(&o,7,100,ms)==0);assert(fh_timestamp_rtmp(&o,7,100+0x00fffffeULL,&t)==0&&!t.extended);assert(fh_timestamp_rtmp(&o,7,100+0x00ffffffULL,&t)==0&&t.extended);assert(fh_timestamp_rtmp(&o,7,100+0x7fffffffULL,&t)==0&&t.extended);assert(fh_timestamp_rtmp(&o,7,100+0x80000000ULL,&t)==0&&t.ms==0x80000000U&&t.extended);assert(fh_timestamp_rtmp(&o,7,100+0xffffffffULL,&t)==0&&t.ms==0xffffffffU);assert(fh_timestamp_rtmp(&o,7,100+0x100000000ULL,&t)==-ERANGE);assert(fh_timestamp_rtmp(&o,8,101,&t)<0);uint64_t out;assert(fh_timestamp_rescale(90000,(struct fh_timebase){1,90000},1000,&out)==0&&out==1000);puts("test_timestamp: PASS");return 0;}
